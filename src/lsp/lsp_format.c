/*
 * lsp_format.c -- textDocument/formatting handler.
 *
 * Same fork+exec model as lsp_diagnostics.c, but invokes `lime -F
 * <tmpfile>` which writes formatted output to `<tmpfile>.formatted`.
 * We slurp that file and build a single TextEdit replacing the
 * entire document.
 *
 * Implementation is POSIX-only (fork/exec/pipe/waitpid); same
 * caveat as lsp_diagnostics.c: editor integrations for Windows
 * users currently run lime-lsp under WSL.  Native Win32 spawn
 * can be added later if a real consumer needs it.
 */

#include "lsp_format.h"

#include "lsp_json.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static int errno_was_eintr(void) { return errno == EINTR; }

static int write_temp_file(const char *text, size_t text_len,
                           char *out_path, size_t cap) {
#if defined(_WIN32)
    char tmp_root[MAX_PATH];
    DWORD got = GetTempPathA((DWORD)sizeof(tmp_root), tmp_root);
    if (got == 0 || got >= sizeof(tmp_root))
        strcpy(tmp_root, "C:\\Windows\\Temp\\");
    if (GetTempFileNameA(tmp_root, "lif", 0, out_path) == 0) return -1;
    /* lime -F insists on a .lime suffix; rename in place. */
    char with_ext[MAX_PATH + 8];
    if ((size_t)snprintf(with_ext, sizeof(with_ext), "%s.lime", out_path)
            >= sizeof(with_ext)) {
        DeleteFileA(out_path);
        return -1;
    }
    if (!MoveFileExA(out_path, with_ext, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(out_path);
        return -1;
    }
    if (strlen(with_ext) + 1 > cap) {
        DeleteFileA(with_ext);
        return -1;
    }
    strcpy(out_path, with_ext);
    HANDLE h = CreateFileA(out_path, GENERIC_WRITE, 0, NULL,
        TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD wrote = 0;
    if (!WriteFile(h, text, (DWORD)text_len, &wrote, NULL)
            || wrote != text_len) {
        CloseHandle(h);
        DeleteFileA(out_path);
        return -1;
    }
    CloseHandle(h);
    return 0;
#else
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir) tmpdir = "/tmp";
    int n = snprintf(out_path, cap, "%s/lime_lsp_fmt_XXXXXX.lime", tmpdir);
    if (n < 0 || (size_t)n >= cap) return -1;

    /* mkstemps preserves the .lime suffix that lime -F insists on. */
    int fd = mkstemps(out_path, 5);  /* len(".lime") == 5 */
    if (fd < 0) return -1;

    const char *p = text;
    size_t      remaining = text_len;
    while (remaining > 0) {
        ssize_t w = write(fd, p, remaining);
        if (w < 0) {
            if (errno_was_eintr()) continue;
            close(fd);
            unlink(out_path);
            return -1;
        }
        p         += w;
        remaining -= (size_t)w;
    }
    close(fd);
    return 0;
#endif
}

static char *slurp_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long n = ftell(fp);
    if (n < 0) { fclose(fp); return NULL; }
    rewind(fp);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    if (got != (size_t)n) { free(buf); return NULL; }
    buf[n] = 0;
    if (out_len) *out_len = (size_t)n;
    return buf;
}

/* Compute the LSP `position` (line/character) at the END of `text`.
 * LSP positions are zero-indexed line + UTF-16 character offset
 * within the line.  Lime grammars are ASCII-or-UTF-8; we approximate
 * UTF-16 char offset by the count of bytes since the last newline,
 * which is exact for ASCII and correct enough for UTF-8 in identifier
 * positions (BMP chars are 1 code unit each).  Surrogate-pair
 * grammars don't exist in practice. */
static void end_position(const char *text, size_t text_len,
                         long long *out_line, long long *out_char) {
    long long line = 0;
    long long col  = 0;
    for (size_t i = 0; i < text_len; i++) {
        if (text[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    *out_line = line;
    *out_char = col;
}

json_value *lsp_format_run(const char *lime_bin,
                           const char *text, size_t text_len) {
    /* Empty document: return empty edits array (no change). */
    if (text == NULL || text_len == 0) {
        return json_make_array();
    }

    char tmp_path[512];
    if (write_temp_file(text, text_len, tmp_path, sizeof(tmp_path)) != 0) {
        /* Couldn't stage input; return empty edits to avoid noisy
         * editor errors over an environmental glitch. */
        return json_make_array();
    }

#if defined(_WIN32)
    /* Win32 port: CreateProcessA + anonymous pipe replacing
    ** fork+execlp+pipe.  Spawn 'lime -F tmp_path', drain its
    ** stderr (we only care about the exit code), then read back
    ** tmp_path.formatted on success. */
    HANDLE pipe_r, pipe_w;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&pipe_r, &pipe_w, &sa, 0)) {
        DeleteFileA(tmp_path);
        return json_make_array();
    }
    SetHandleInformation(pipe_r, HANDLE_FLAG_INHERIT, 0);
    HANDLE devnull = CreateFileA("NUL", GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    char cmdline[2048];
    const char *bin = lime_bin && *lime_bin ? lime_bin : "lime";
    snprintf(cmdline, sizeof(cmdline), "\"%s\" -F \"%s\"", bin, tmp_path);
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
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0,
                             NULL, NULL, &si, &pi);
    if (nul_in != INVALID_HANDLE_VALUE) CloseHandle(nul_in);
    CloseHandle(pipe_w);
    if (devnull != INVALID_HANDLE_VALUE) CloseHandle(devnull);
    if (!ok) {
        CloseHandle(pipe_r);
        DeleteFileA(tmp_path);
        return json_make_array();
    }
    /* Drain child stderr -- we just want a clean exit. */
    char drain[4096];
    DWORD got;
    while (ReadFile(pipe_r, drain, sizeof(drain), &got, NULL) && got > 0) { }
    CloseHandle(pipe_r);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    int status = (int)code;       /* status == 0 means clean exit */
#else
    int err_pipe[2];
    if (pipe(err_pipe) != 0) {
        unlink(tmp_path);
        return json_make_array();
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        unlink(tmp_path);
        return json_make_array();
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
        execlp(bin, "lime", "-F", tmp_path, (char *)NULL);
        _exit(127);
    }

    close(err_pipe[1]);
    /* drain child stderr and discard -- we only care about success. */
    char    drain[4096];
    while (read(err_pipe[0], drain, sizeof(drain)) > 0) { }
    close(err_pipe[0]);
    int status = 0;
    waitpid(pid, &status, 0);
#endif

    /* Build the .formatted path that `lime -F` writes to. */
    char fmt_path[600];
    int  np = snprintf(fmt_path, sizeof(fmt_path), "%s.formatted", tmp_path);
    json_value *result = json_make_array();
#if defined(_WIN32)
    if (np > 0 && (size_t)np < sizeof(fmt_path) && status == 0) {
#else
    if (np > 0 && (size_t)np < sizeof(fmt_path) &&
        WIFEXITED(status) && WEXITSTATUS(status) == 0) {
#endif
        size_t out_len = 0;
        char  *formatted = slurp_file(fmt_path, &out_len);
        if (formatted != NULL) {
            /* Build a single TextEdit replacing 0:0 -> end with the
             * formatted text. */
            long long end_line = 0, end_char = 0;
            end_position(text, text_len, &end_line, &end_char);

            json_value *edit  = json_make_object();
            json_value *range = json_make_object();
            json_value *start = json_make_object();
            json_object_set(start, "line",      json_make_int(0));
            json_object_set(start, "character", json_make_int(0));
            json_value *end_pos = json_make_object();
            json_object_set(end_pos, "line",      json_make_int(end_line));
            json_object_set(end_pos, "character", json_make_int(end_char));
            json_object_set(range, "start", start);
            json_object_set(range, "end",   end_pos);
            json_object_set(edit, "range", range);
            json_object_set(edit, "newText",
                            json_make_string_n(formatted, out_len));
            json_array_push(result, edit);
            free(formatted);
        }
    }
    /* else: lime -F failed (parse error, missing binary, etc.).
     * Returning an empty edit array is the LSP-clean way to say
     * "no change". */

#if defined(_WIN32)
    DeleteFileA(tmp_path);
    DeleteFileA(fmt_path);
#else
    unlink(tmp_path);
    unlink(fmt_path);
#endif
    return result;
}
