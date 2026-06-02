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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#  include <direct.h>
#else
#  include <fcntl.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

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
#if defined(_WIN32)
    static char tmp_root[MAX_PATH];
    DWORD got = GetTempPathA((DWORD)sizeof(tmp_root), tmp_root);
    if (got > 0 && got < sizeof(tmp_root)) {
        /* Strip trailing backslash for path concatenation parity. */
        size_t len = strlen(tmp_root);
        if (len > 0 && (tmp_root[len-1] == '\\' || tmp_root[len-1] == '/'))
            tmp_root[len-1] = 0;
        return tmp_root;
    }
    return "C:\\Windows\\Temp";
#else
    const char *t = getenv("TMPDIR");
    if (t && *t) return t;
    return "/tmp";
#endif
}

static int write_temp_file(const char *text, size_t text_len, char *out_path,
                           size_t out_path_cap) {
#if defined(_WIN32)
    /* GetTempFileNameA generates a unique name in the given dir.
    ** It can't append a custom suffix (.lime), but `lime -L` doesn't
    ** care about the file extension. */
    if (GetTempFileNameA(get_tmpdir(), "lim", 0, out_path) == 0) return -1;
    if (strlen(out_path) + 1 > out_path_cap) return -1;
    HANDLE h = CreateFileA(out_path, GENERIC_WRITE, 0, NULL,
                            TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD written = 0;
    if (!WriteFile(h, text, (DWORD)text_len, &written, NULL)
            || written != text_len) {
        CloseHandle(h);
        DeleteFileA(out_path);
        return -1;
    }
    CloseHandle(h);
    return 0;
#else
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
#endif
}

#if defined(_WIN32)
static char *read_all_handle(HANDLE h, size_t *out_len) {
    size_t cap = 4096;
    size_t len = 0;
    char  *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (len == cap) {
            size_t nc = cap * 2;
            char *nb = (char *)realloc(buf, nc);
            if (!nb) { free(buf); return NULL; }
            buf = nb; cap = nc;
        }
        DWORD got = 0;
        BOOL ok = ReadFile(h, buf + len, (DWORD)(cap - len), &got, NULL);
        if (!ok || got == 0) break;
        len += got;
    }
    *out_len = len;
    return buf;
}
#else
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
#endif /* !_WIN32 */

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

/* Forward decl for the in-process lint API.  Weak so the LSP
** binary can be built without linking liblime_compiler.a (a 1.2 MB
** dependency).  When the compiler lib IS linked, the strong
** definition in lime.c wins and we get the in-process diagnostics
** fast path; when it isn't, the function returns -1 and we fall
** through to the subprocess pipeline.  Same pattern as
** src/snapshot_create.c uses for lime_compile_grammar_in_process. */
extern int lime_lint_grammar_in_process(const char *grammar_text,
                                        size_t len,
                                        char **out_diags)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((weak))
#endif
    ;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
int lime_lint_grammar_in_process(const char *grammar_text,
                                 size_t len,
                                 char **out_diags) {
    (void)grammar_text; (void)len;
    if (out_diags) *out_diags = NULL;
    return -1;
}
#endif

json_value *lsp_diagnostics_run(const char *lime_bin,
                                const char *text, size_t text_len) {
    json_value *arr = json_make_array();
    if (!arr) return NULL;

    /* In-process fast path (v0.9.x, Lime-Letter-27).  When this
    ** binary was linked with liblime_compiler.a the in-process
    ** lint API is available and we can run a parse + FindActions
    ** + lint_grammar pipeline WITHOUT the per-request fork+exec+
    ** temp-file + lime+cc cycle.  Big win on large grammars: PG's
    ** gram.lime (~21 KLoC, 0.5 MB) drops from 6-20 s to ~150 ms
    ** per request.
    **
    ** Same diagnostic surface as `lime -L`: emits human-format
    ** stderr that parse_output() understands.  Captures both
    ** errors (from ParseText) and warnings (from lint_grammar).
    **
    ** Falls through to the subprocess path when:
    **   - in-process lint not linked (weak stub returns -1)
    **
    ** Disable via LIME_LSP_FORCE_SUBPROCESS=1 for testing the
    ** subprocess path explicitly.
    */
    {
        const char *force_sub = getenv("LIME_LSP_FORCE_SUBPROCESS");
        int skip_in_process = (force_sub && force_sub[0] && force_sub[0] != '0');
        if (!skip_in_process) {
            char *diag_buf = NULL;
            int rc = lime_lint_grammar_in_process(text, text_len, &diag_buf);
            if (rc != -1) {
                /* In-process pipeline ran (rc 0 = clean, >0 = had
                ** issues).  Either way, parse what it wrote. */
                if (diag_buf) {
                    parse_output(diag_buf, strlen(diag_buf), arr);
                    free(diag_buf);
                }
                return arr;
            }
            /* rc == -1 means the weak stub fired (compiler lib not
            ** linked).  Fall through to the subprocess path below. */
            free(diag_buf);
        }
    }

    char tmp_path[512];
    if (write_temp_file(text, text_len, tmp_path, sizeof(tmp_path)) != 0) {
        return arr;  /* report nothing rather than fail noisily */
    }

#if defined(_WIN32)
    /* Win32 port: CreateProcessA + anonymous pipe instead of
    ** fork+exec+pipe.  Same observable behaviour: spawn lime
    ** with -L and the tmp file, capture its stderr to a buffer,
    ** parse the diagnostic stream. */
    HANDLE pipe_r, pipe_w;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&pipe_r, &pipe_w, &sa, 0)) {
        DeleteFileA(tmp_path);
        return arr;
    }
    SetHandleInformation(pipe_r, HANDLE_FLAG_INHERIT, 0);

    HANDLE devnull = CreateFileA("NUL", GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
        OPEN_EXISTING, 0, NULL);

    char cmdline[2048];
    const char *bin = lime_bin && *lime_bin ? lime_bin : "lime";
    /* Naive quoting -- lime_bin and tmp_path can contain spaces
    ** but no embedded quotes in practice. */
    snprintf(cmdline, sizeof(cmdline), "\"%s\" -L \"%s\"",
             bin, tmp_path);

    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    /* Open NUL for stdin -- under some hosts (meson test runners,
    ** detached editor processes) the parent's stdin can be closed
    ** and GetStdHandle returns INVALID_HANDLE_VALUE; CreateProcessA
    ** with STARTF_USESTDHANDLES then fails because all three handles
    ** must be valid. */
    HANDLE nul_in = CreateFileA("NUL", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, 0, NULL);
    si.hStdInput  = nul_in;
    si.hStdOutput = devnull;
    si.hStdError  = pipe_w;

    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                             0, NULL, NULL, &si, &pi);
    if (nul_in != INVALID_HANDLE_VALUE) CloseHandle(nul_in);
    CloseHandle(pipe_w);
    if (devnull != INVALID_HANDLE_VALUE) CloseHandle(devnull);
    if (!ok) {
        CloseHandle(pipe_r);
        DeleteFileA(tmp_path);
        return arr;
    }

    size_t out_len = 0;
    char *out = read_all_handle(pipe_r, &out_len);
    CloseHandle(pipe_r);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    DeleteFileA(tmp_path);

    if (out) {
        parse_output(out, out_len, arr);
        free(out);
    }
    return arr;
#else
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
#endif /* _WIN32 */
}
