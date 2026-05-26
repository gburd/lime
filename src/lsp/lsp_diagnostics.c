/*
 * lsp_diagnostics.c -- run `lime -L`, parse output, build LSP
 * Diagnostic[].
 *
 * The implementation is deliberately POSIX-only (fork, execvp,
 * pipe, waitpid).  Lime's CI matrix lists Windows for the parser
 * generator itself but the LSP server is a developer-tools
 * binary; the editor integrations we ship recipes for (Emacs,
 * Neovim, VS Code) all work fine running an LSP under WSL on
 * Windows.  A native Win32 fork can be added later.
 *
 * Process model per call:
 *
 *   1. write `text` to a temp file under TMPDIR (mkstemp).
 *   2. fork(); child execvp(lime_bin, "lime", "-L", tmpfile).
 *   3. parent reads child's stderr (stdout discarded -- the
 *      "Linting <file>..." banner is not a diagnostic).
 *   4. parent waitpid()s.
 *   5. unlink() temp file.
 *   6. parse captured stderr line by line; build Diagnostic[].
 */

#include "lsp_diagnostics.h"

#include "lsp_json.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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

static const char *get_tmpdir(void) {
    const char *t = getenv("TMPDIR");
    if (t && *t) return t;
    return "/tmp";
}

static int write_temp_file(const char *text, size_t text_len, char *out_path,
                           size_t out_path_cap) {
    int n = snprintf(out_path, out_path_cap, "%s/lime-lsp-XXXXXX.lime",
                     get_tmpdir());
    if (n < 0 || (size_t)n >= out_path_cap) return -1;
    int fd = mkstemps(out_path, 5);
    if (fd < 0) return -1;
    size_t written = 0;
    while (written < text_len) {
        ssize_t w = write(fd, text + written, text_len - written);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(out_path);
            return -1;
        }
        written += (size_t)w;
    }
    close(fd);
    return 0;
}

static char *read_all(int fd, size_t *out_len) {
    size_t cap = 4096;
    size_t len = 0;
    char  *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (len == cap) {
            size_t nc = cap * 2;
            char  *nb = (char *)realloc(buf, nc);
            if (!nb) { free(buf); return NULL; }
            buf = nb; cap = nc;
        }
        ssize_t r = read(fd, buf + len, cap - len);
        if (r < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return NULL;
        }
        if (r == 0) break;
        len += (size_t)r;
    }
    *out_len = len;
    return buf;
}

/* Build an LSP Diagnostic object.  Lines/cols converted from
 * 1-based (compiler convention) to 0-based (LSP).
 */
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

json_value *lsp_diagnostics_run(const char *lime_bin,
                                const char *text, size_t text_len) {
    json_value *arr = json_make_array();
    if (!arr) return NULL;

    char tmp_path[512];
    if (write_temp_file(text, text_len, tmp_path, sizeof(tmp_path)) != 0) {
        return arr;  /* report nothing rather than fail noisily */
    }

    int err_pipe[2];
    if (pipe(err_pipe) != 0) {
        unlink(tmp_path);
        return arr;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        unlink(tmp_path);
        return arr;
    }
    if (pid == 0) {
        /* child */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }
        dup2(err_pipe[1], STDERR_FILENO);
        close(err_pipe[0]);
        close(err_pipe[1]);
        const char *bin = lime_bin && *lime_bin ? lime_bin : "lime";
        execlp(bin, "lime", "-L", tmp_path, (char *)NULL);
        _exit(127);
    }

    close(err_pipe[1]);
    size_t out_len = 0;
    char  *out = read_all(err_pipe[0], &out_len);
    close(err_pipe[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    unlink(tmp_path);

    if (out) {
        parse_output(out, out_len, arr);
        free(out);
    }
    return arr;
}
