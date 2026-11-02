/*
 * lsp_diagnostics.c -- lint a grammar buffer in process and build the
 * LSP Diagnostic[] array.
 *
 * lime-lsp always links liblime_compiler, so the lint runs in this
 * process: lime_lint_grammar_fast_in_process() (parse + lint, used by
 * default) or lime_lint_grammar_in_process() (also builds the LALR
 * tables, so conflict warnings appear; LIME_LSP_FULL_LINT=1).  Both
 * return the same human-format text `lime -L` prints, which
 * parse_output() turns into Diagnostic objects.  No temp file, no
 * child process.
 */

#include "lsp_diagnostics.h"

#include "lsp_json.h"
#include "lime_compiler.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* LSP DiagnosticSeverity codes. */
enum {
    SEV_ERROR       = 1,
    SEV_WARNING     = 2,
    SEV_INFORMATION = 3,
    SEV_HINT        = 4
};

static int parse_severity(const char *s, size_t n) {
    if (n == 5 && strncmp(s, "error", 5) == 0)   return SEV_ERROR;
    if (n == 7 && strncmp(s, "warning", 7) == 0) return SEV_WARNING;
    if (n == 4 && strncmp(s, "note", 4) == 0)    return SEV_INFORMATION;
    if (n == 4 && strncmp(s, "info", 4) == 0)    return SEV_INFORMATION;
    if (n == 4 && strncmp(s, "hint", 4) == 0)    return SEV_HINT;
    return SEV_INFORMATION;
}

static json_value *make_diag(long line1, long col1, int severity,
                             const char *message, size_t message_len) {
    if (line1 < 1) line1 = 1;
    if (col1  < 1) col1  = 1;
    long line0 = line1 - 1;
    long col0  = col1  - 1;

    json_value *start = json_make_object();
    json_object_set(start, "line",      json_make_int(line0));
    json_object_set(start, "character", json_make_int(col0));
    json_value *end = json_make_object();
    json_object_set(end, "line",      json_make_int(line0));
    json_object_set(end, "character", json_make_int(col0 + 1));
    json_value *range = json_make_object();
    json_object_set(range, "start", start);
    json_object_set(range, "end",   end);

    json_value *d = json_make_object();
    json_object_set(d, "range",    range);
    json_object_set(d, "severity", json_make_int(severity));
    json_object_set(d, "source",   json_make_string("lime"));
    json_object_set(d, "message",  json_make_string_n(message, message_len));
    return d;
}

/* Try to parse a single diagnostic line.  The expected shapes
 * (matched in order, longest-first):
 *
 *   <path>:<line>:<col>: <severity>: <message>
 *   <path>:<line>:<col>: <message>
 *   <path>:<line>: <message>
 *
 * Returns a Diagnostic or NULL if the line doesn't match.  We
 * accept any path -- callers writing the buffer to a temp file
 * just don't care about the path component because every line in
 * a single-document refresh refers to that document.
 */
static json_value *parse_line(const char *line, size_t line_len) {
    /* find first colon after the path.  Path may contain '/' but
     * no colon (Lime's tempfile path has no colon).  We accept
     * the first ':' as the path/line separator.
     */
    if (line_len == 0) return NULL;
    /* Skip the "Linting <file>..." banner and summary lines. */
    if (line_len >= 8 && strncmp(line, "Linting ", 8) == 0) return NULL;
    if (line_len >= 3 && strncmp(line, "OK:", 3) == 0)     return NULL;
    /* The summary line is "<n> errors, <n> warnings". */
    if (isdigit((unsigned char)line[0])) {
        const char *p = line;
        while (p < line + line_len && isdigit((unsigned char)*p)) p++;
        if (p < line + line_len && *p == ' ') return NULL;
    }

    const char *p = line;
    const char *end = line + line_len;
    /* On Windows the file path starts with a drive letter (e.g.,
    ** "C:\Users\...\file.lime:42:7: warning: ...").  The first ':'
    ** is the drive-letter delimiter, not the path/line separator.
    ** Skip past it when the line begins with [A-Za-z]:[\\/]. */
    if (line_len >= 3
            && ((line[0] >= 'A' && line[0] <= 'Z')
                || (line[0] >= 'a' && line[0] <= 'z'))
            && line[1] == ':'
            && (line[2] == '\\' || line[2] == '/')) {
        p = line + 2;  /* points at ':' so memchr(p+1, ...) finds the next */
        p++;
    }
    const char *colon1 = memchr(p, ':', (size_t)(end - p));
    if (!colon1) return NULL;
    /* line number */
    const char *q = colon1 + 1;
    long line1 = 0;
    if (q >= end || !isdigit((unsigned char)*q)) return NULL;
    while (q < end && isdigit((unsigned char)*q)) {
        line1 = line1 * 10 + (*q - '0');
        q++;
    }
    long col1 = 1;
    int  severity = SEV_ERROR;
    const char *msg_start;
    size_t      msg_len;

    if (q < end && *q == ':') {
        /* could be :<col>: or :<sp><msg> -- look for digit */
        const char *r = q + 1;
        if (r < end && isdigit((unsigned char)*r)) {
            col1 = 0;
            while (r < end && isdigit((unsigned char)*r)) {
                col1 = col1 * 10 + (*r - '0');
                r++;
            }
            if (r >= end || *r != ':') return NULL;
            r++; /* skip colon */
            /* Now we may have " severity: message" or " message". */
            while (r < end && *r == ' ') r++;
            /* try to read severity word */
            const char *sev_start = r;
            while (r < end && (isalpha((unsigned char)*r))) r++;
            if (r < end && *r == ':') {
                severity = parse_severity(sev_start,
                                          (size_t)(r - sev_start));
                r++;
                while (r < end && *r == ' ') r++;
                msg_start = r;
            } else {
                msg_start = sev_start;
            }
            msg_len = (size_t)(end - msg_start);
        } else {
            /* ":<sp>message" form (ErrorMsg shape) */
            const char *r2 = q + 1;
            while (r2 < end && *r2 == ' ') r2++;
            msg_start = r2;
            msg_len   = (size_t)(end - msg_start);
        }
    } else {
        return NULL;
    }

    /* trim trailing CR */
    while (msg_len > 0 && (msg_start[msg_len - 1] == '\r' ||
                           msg_start[msg_len - 1] == '\n')) {
        msg_len--;
    }
    if (msg_len == 0) return NULL;
    return make_diag(line1, col1, severity, msg_start, msg_len);
}

static void parse_output(const char *buf, size_t buf_len, json_value *arr) {
    size_t i = 0;
    while (i < buf_len) {
        size_t start = i;
        while (i < buf_len && buf[i] != '\n') i++;
        size_t line_len = i - start;
        if (line_len > 0) {
            json_value *d = parse_line(buf + start, line_len);
            if (d) json_array_push(arr, d);
        }
        if (i < buf_len) i++;
    }
}

json_value *lsp_diagnostics_run(const char *text, size_t text_len) {
    json_value *arr = json_make_array();
    if (!arr) return NULL;
    if (text == NULL || text_len == 0) return arr;

    /* Fast lint (parse + lint, no LALR construction) by default: ~150 ms
    ** on PG's gram.lime against ~2 s for the full lint.  LIME_LSP_FULL_LINT=1
    ** asks for the full one, which also reports conflict warnings. */
    const char *full = getenv("LIME_LSP_FULL_LINT");
    int prefer_full = (full && full[0] && full[0] != '0');
    char *diag_buf = NULL;
    int rc = prefer_full
        ? lime_lint_grammar_in_process(text, text_len, &diag_buf)
        : lime_lint_grammar_fast_in_process(text, text_len, &diag_buf);
    if (rc != -1 && diag_buf) parse_output(diag_buf, strlen(diag_buf), arr);
    /* rc == -1: no context could be set up (OOM); report nothing. */
    free(diag_buf);
    return arr;
}
