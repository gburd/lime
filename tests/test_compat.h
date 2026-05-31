/*
** tests/test_compat.h -- portability shims for lime tests on Windows.
**
** Lime tests historically used POSIX-only APIs (mkdtemp, realpath,
** setenv, fork/exec) and hard-coded /tmp/ paths.  This header
** provides a single portable surface so each test can drop
**
**   #include "test_compat.h"
**
** and use:
**
**   test_compat_tmpdir(prefix, out_buf, out_buf_size)
**       Create a unique temporary directory and write its path
**       into out_buf.  Returns 0 on success, -1 on failure.
**       On POSIX uses mkdtemp("/tmp/<prefix>_XXXXXX"); on Windows
**       uses %TMP% + mkdir + GetTempFileNameA-style suffix.
**       out_buf must be >= 256 bytes to hold deeper Windows paths.
**
**   test_compat_rmdir_recursive(path)
**       Remove a directory and its contents.
**
**   test_compat_setenv(name, value, overwrite)
**       Maps to setenv on POSIX, _putenv_s on Windows.
**
**   test_compat_unsetenv(name)
**       Maps to unsetenv on POSIX, _putenv_s(name, "") on Windows.
**
**   test_compat_realpath(path, resolved_buf, resolved_buf_size)
**       Resolve to absolute canonical path.  Returns 0/-1.
**
**   test_compat_run_cmd_capture_stderr(argv, stderr_buf, ...)
**       Spawn a subprocess, capture its stderr to a buffer, wait
**       for exit, return its exit code.  POSIX: fork+execvp+pipe+
**       waitpid.  Windows: CreateProcessA + anonymous pipe +
**       WaitForSingleObject.
*/
#ifndef LIME_TEST_COMPAT_H
#define LIME_TEST_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#  include <direct.h>
#  include <sys/stat.h>
#  include <process.h>
#  include <fcntl.h>
#else
#  include <unistd.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <fcntl.h>
#  include <dirent.h>
#endif

/* PATH_MAX is POSIX (<limits.h>); Windows MSVC ABI uses MAX_PATH
** in <windows.h> (which we already pulled above on _WIN32).  Tests
** that use PATH_MAX directly need a portable definition. */
#if !defined(PATH_MAX)
#  if defined(_WIN32) && defined(MAX_PATH)
#    define PATH_MAX MAX_PATH
#  else
#    define PATH_MAX 4096
#  endif
#endif

/* popen / pclose: POSIX names are unavailable on Windows MSVC ABI
** (MinGW provides them via the POSIX layer; clang -target msvc and
** clang-cl don't).  MSVC supplies _popen / _pclose with identical
** semantics. */
#if defined(_WIN32) && !defined(__MINGW32__)
#  define popen  _popen
#  define pclose _pclose
#endif

/* S_ISDIR is POSIX (in <sys/stat.h>); Windows MSVC ABI doesn't
** define it.  Use _S_IFMT mask manually. */
#if defined(_WIN32) && !defined(__MINGW32__) && !defined(S_ISDIR)
#  define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#  define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

#define TEST_COMPAT_PATH_MAX 1024

/* --------------------------------------------------------------- */
/*  tmpdir                                                          */
/* --------------------------------------------------------------- */

static int test_compat_tmpdir(const char *prefix, char *out_buf,
                              size_t out_buf_size) {
    if (out_buf_size < 64) return -1;
#if defined(_WIN32)
    char tmp_root[256];
    DWORD got = GetTempPathA((DWORD)sizeof(tmp_root), tmp_root);
    if (got == 0 || got >= sizeof(tmp_root)) {
        strcpy(tmp_root, "C:\\Windows\\Temp\\");
    }
    /* GetTempPathA includes a trailing backslash. */
    /* Suffix from time + pid for uniqueness; loop on collision. */
    for (int attempt = 0; attempt < 16; attempt++) {
        unsigned long long u = (unsigned long long)GetTickCount64()
                              ^ ((unsigned long long)GetCurrentProcessId() << 16)
                              ^ ((unsigned long long)attempt << 32);
        int n = snprintf(out_buf, out_buf_size, "%s%s_%llx",
                         tmp_root, prefix, u);
        if (n < 0 || (size_t)n >= out_buf_size) return -1;
        if (CreateDirectoryA(out_buf, NULL)) return 0;
        if (GetLastError() != ERROR_ALREADY_EXISTS) return -1;
    }
    return -1;
#else
    /* POSIX mkdtemp.  Use $TMPDIR if set, else /tmp. */
    const char *tmp_root = getenv("TMPDIR");
    if (tmp_root == NULL || tmp_root[0] == '\0') tmp_root = "/tmp";
    int n = snprintf(out_buf, out_buf_size, "%s/%s_XXXXXX",
                     tmp_root, prefix);
    if (n < 0 || (size_t)n >= out_buf_size) return -1;
    return mkdtemp(out_buf) == NULL ? -1 : 0;
#endif
}

/* --------------------------------------------------------------- */
/*  rmdir_recursive                                                 */
/* --------------------------------------------------------------- */

static int test_compat_rmdir_recursive(const char *path) {
#if defined(_WIN32)
    /* SHFileOperationA is the documented "rm -rf" on Windows but
    ** requires shell32.lib.  Use a manual recursion via FindFirst/
    ** FindNext to avoid the link dependency. */
    char pattern[TEST_COMPAT_PATH_MAX];
    if (snprintf(pattern, sizeof(pattern), "%s\\*", path)
            >= (int)sizeof(pattern)) return -1;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return RemoveDirectoryA(path) ? 0 : -1;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0
                || strcmp(fd.cFileName, "..") == 0) continue;
        char child[TEST_COMPAT_PATH_MAX];
        if (snprintf(child, sizeof(child), "%s\\%s",
                     path, fd.cFileName) >= (int)sizeof(child)) {
            FindClose(h);
            return -1;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            test_compat_rmdir_recursive(child);
        } else {
            DeleteFileA(child);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return RemoveDirectoryA(path) ? 0 : -1;
#else
    /* POSIX: opendir + recurse + unlink + rmdir. */
    DIR *d = opendir(path);
    if (d == NULL) return rmdir(path);
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0
                || strcmp(de->d_name, "..") == 0) continue;
        char child[TEST_COMPAT_PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            test_compat_rmdir_recursive(child);
        } else {
            unlink(child);
        }
    }
    closedir(d);
    return rmdir(path);
#endif
}

/* --------------------------------------------------------------- */
/*  setenv / unsetenv                                               */
/* --------------------------------------------------------------- */

static int test_compat_setenv(const char *name, const char *value,
                              int overwrite) {
#if defined(_WIN32)
    if (!overwrite) {
        size_t len = 0;
        if (getenv_s(&len, NULL, 0, name) == 0 && len > 0) return 0;
    }
    return _putenv_s(name, value) == 0 ? 0 : -1;
#else
    return setenv(name, value, overwrite);
#endif
}

static int test_compat_unsetenv(const char *name) {
#if defined(_WIN32)
    return _putenv_s(name, "") == 0 ? 0 : -1;
#else
    return unsetenv(name);
#endif
}

/* Change the current working directory to the OS temp root
** (POSIX: $TMPDIR or /tmp; Windows: %TEMP%).  Used by tests that
** need to chdir OUT of a directory before deleting it -- on
** Windows you can't remove the cwd. */
/* Recursive mkdir (portable analog of POSIX mkdir -p).  Components
** are split on '/' AND '\\' so it works for paths constructed on
** either platform.  Existing components are tolerated. */
static int test_compat_mkdir_p(const char *path) {
    char buf[TEST_COMPAT_PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return -1;
    memcpy(buf, path, len + 1);
    /* Walk components, mkdir-ing each prefix. */
    for (size_t i = 1; i <= len; i++) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == 0) {
            char saved = buf[i];
            buf[i] = 0;
#if defined(_WIN32)
            if (_mkdir(buf) != 0 && errno != EEXIST) {
                buf[i] = saved;
                /* Drive-letter root like C: produces ENOENT on _mkdir;
                ** ignore and continue. */
                if (errno != ENOENT) return -1;
            }
#else
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
                buf[i] = saved;
                return -1;
            }
#endif
            buf[i] = saved;
        }
    }
    return 0;
}

static int test_compat_chdir_temp(void) {
#if defined(_WIN32)
    char tmp_root[256];
    DWORD got = GetTempPathA((DWORD)sizeof(tmp_root), tmp_root);
    if (got == 0 || got >= sizeof(tmp_root)) {
        strcpy(tmp_root, "C:\\Windows\\Temp\\");
    }
    return SetCurrentDirectoryA(tmp_root) ? 0 : -1;
#else
    const char *tmp = getenv("TMPDIR");
    if (tmp == NULL || tmp[0] == '\0') tmp = "/tmp";
    return chdir(tmp);
#endif
}

/* Create a unique temp file with the given prefix.  Returns an
** open file descriptor on success, -1 on failure.  The path is
** written to out_buf (must be >= 256 bytes).  POSIX: mkstemp.
** Windows: GetTempFileNameA + open. */
static int test_compat_mkstemp(const char *prefix, char *out_buf,
                               size_t out_buf_size) {
#if defined(_WIN32)
    char tmp_root[256];
    DWORD got = GetTempPathA((DWORD)sizeof(tmp_root), tmp_root);
    if (got == 0 || got >= sizeof(tmp_root)) {
        strcpy(tmp_root, "C:\\Windows\\Temp\\");
    }
    if (GetTempFileNameA(tmp_root, prefix, 0, out_buf) == 0) return -1;
    if (strlen(out_buf) + 1 > out_buf_size) return -1;
    /* GetTempFileNameA already creates the file; reopen for write. */
    int fd = _open(out_buf, _O_RDWR | _O_BINARY | _O_TRUNC, _S_IREAD | _S_IWRITE);
    return fd;
#else
    const char *tmp_root = getenv("TMPDIR");
    if (tmp_root == NULL || tmp_root[0] == '\0') tmp_root = "/tmp";
    int n = snprintf(out_buf, out_buf_size, "%s/%s_XXXXXX", tmp_root, prefix);
    if (n < 0 || (size_t)n >= out_buf_size) return -1;
    return mkstemp(out_buf);
#endif
}

/* --------------------------------------------------------------- */
/*  realpath                                                        */
/* --------------------------------------------------------------- */

static int test_compat_realpath(const char *path, char *out_buf,
                                size_t out_buf_size) {
#if defined(_WIN32)
    DWORD got = GetFullPathNameA(path, (DWORD)out_buf_size, out_buf, NULL);
    return (got == 0 || got >= out_buf_size) ? -1 : 0;
#else
    /* glibc's __realpath_chk requires the resolved-buffer to be at
    ** least PATH_MAX bytes when -D_FORTIFY_SOURCE>=2; otherwise
    ** runtime-aborts.  Pass NULL to let glibc malloc its own buffer. */
    char *resolved = realpath(path, NULL);
    if (resolved == NULL) return -1;
    if (strlen(resolved) + 1 > out_buf_size) {
        free(resolved);
        return -1;
    }
    strcpy(out_buf, resolved);
    free(resolved);
    return 0;
#endif
}

/* --------------------------------------------------------------- */
/*  Subprocess: capture stderr (used by test_diff_conflicts)        */
/* --------------------------------------------------------------- */

#if defined(_WIN32)
/* Quote argv[i] for CommandLine concatenation per the
** CommandLineToArgvW rules.  Returns the number of bytes written
** (excluding terminator) or -1 on overflow. */
static int test_compat_quote_arg(const char *arg, char *out, size_t cap) {
    size_t pos = 0;
    if (pos + 1 >= cap) return -1;
    out[pos++] = '"';
    for (const char *p = arg; *p; p++) {
        if (*p == '"' || *p == '\\') {
            if (pos + 1 >= cap) return -1;
            out[pos++] = '\\';
        }
        if (pos + 1 >= cap) return -1;
        out[pos++] = *p;
    }
    if (pos + 1 >= cap) return -1;
    out[pos++] = '"';
    out[pos] = 0;
    return (int)pos;
}
#endif

/* --------------------------------------------------------------- */
/*  copy_file                                                       */
/* --------------------------------------------------------------- */

static int test_compat_copy_file(const char *src, const char *dst) {
#if defined(_WIN32)
    return CopyFileA(src, dst, FALSE) ? 0 : -1;
#else
    FILE *fin = fopen(src, "rb");
    if (fin == NULL) return -1;
    FILE *fout = fopen(dst, "wb");
    if (fout == NULL) { fclose(fin); return -1; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        if (fwrite(buf, 1, n, fout) != n) {
            fclose(fin); fclose(fout); return -1;
        }
    }
    fclose(fin);
    if (fclose(fout) != 0) return -1;
    return 0;
#endif
}

/* --------------------------------------------------------------- */
/*  Subprocess: run argv[], no output capture                       */
/* --------------------------------------------------------------- */

/* Spawn argv[0] with argv[1..] arguments and wait for it.  No
** output capture (child's stdout/stderr go to /dev/null on POSIX,
** NUL on Windows).  exit_code receives the child's exit status.
** Returns 0 on success, -1 on spawn failure. */
static int test_compat_run(char *const argv[], int *exit_code) {
    if (exit_code) *exit_code = -1;
#if defined(_WIN32)
    char cmdline[4096];
    size_t pos = 0;
    for (int i = 0; argv[i]; i++) {
        if (i > 0) {
            if (pos + 1 >= sizeof(cmdline)) return -1;
            cmdline[pos++] = ' ';
        }
        int n = test_compat_quote_arg(argv[i], cmdline + pos,
                                      sizeof(cmdline) - pos);
        if (n < 0) return -1;
        pos += (size_t)n;
    }
    cmdline[pos] = 0;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE nul = CreateFileA("NUL", GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdError  = nul;
    si.hStdOutput = nul;
    /* Open NUL for stdin -- the parent's stdin may be closed
    ** under meson, in which case GetStdHandle returns
    ** INVALID_HANDLE_VALUE and CreateProcessA fails because
    ** STARTF_USESTDHANDLES requires all three handles valid. */
    HANDLE nul_in = CreateFileA("NUL", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, 0, NULL);
    si.hStdInput  = nul_in;
    SetHandleInformation(nul, HANDLE_FLAG_INHERIT, 1);
    BOOL ok = CreateProcessA(argv[0], cmdline, NULL, NULL, TRUE,
                             0, NULL, NULL, &si, &pi);
    if (nul_in != INVALID_HANDLE_VALUE) CloseHandle(nul_in);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (exit_code) *exit_code = (int)code;
    return 0;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, 1); dup2(dn, 2); close(dn); }
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (exit_code) *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return 0;
#endif
}

/* Spawn argv[0] with argv[1..] arguments; redirect child's
** stdout/stderr to the file `out_path` (created/truncated).
** Returns 0 on success, -1 on spawn failure; exit_code captures
** the child's status. */
static int test_compat_run_to_file(char *const argv[],
                                   const char *out_path,
                                   int *exit_code) {
    if (exit_code) *exit_code = -1;
#if defined(_WIN32)
    char cmdline[4096];
    size_t pos = 0;
    for (int i = 0; argv[i]; i++) {
        if (i > 0) {
            if (pos + 1 >= sizeof(cmdline)) return -1;
            cmdline[pos++] = ' ';
        }
        int n = test_compat_quote_arg(argv[i], cmdline + pos,
                                      sizeof(cmdline) - pos);
        if (n < 0) return -1;
        pos += (size_t)n;
    }
    cmdline[pos] = 0;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hout = CreateFileA(out_path, GENERIC_WRITE,
        FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hout == INVALID_HANDLE_VALUE) return -1;
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdError  = hout;
    si.hStdOutput = hout;
    /* Open NUL for stdin -- the parent's stdin may be closed
    ** under meson, in which case GetStdHandle returns
    ** INVALID_HANDLE_VALUE and CreateProcessA fails because
    ** STARTF_USESTDHANDLES requires all three handles valid. */
    HANDLE nul_in = CreateFileA("NUL", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, 0, NULL);
    si.hStdInput  = nul_in;
    BOOL ok = CreateProcessA(argv[0], cmdline, NULL, NULL, TRUE,
                             0, NULL, NULL, &si, &pi);
    if (nul_in != INVALID_HANDLE_VALUE) CloseHandle(nul_in);
    CloseHandle(hout);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (exit_code) *exit_code = (int)code;
    return 0;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (exit_code) *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return 0;
#endif
}

/* Spawn argv[0] with argv[1..] arguments; capture stderr to
** stderr_buf (NUL-terminated).  exit_code receives the child's
** exit status.  Returns 0 on success, -1 on spawn failure. */
static int test_compat_run_capture_stderr(char *const argv[],
                                          char *stderr_buf,
                                          size_t stderr_buf_size,
                                          int *exit_code) {
    if (stderr_buf_size > 0) stderr_buf[0] = 0;
    if (exit_code) *exit_code = -1;
#if defined(_WIN32)
    /* Build the command line by concatenating quoted argv[]. */
    char cmdline[4096];
    size_t pos = 0;
    for (int i = 0; argv[i]; i++) {
        if (i > 0) {
            if (pos + 1 >= sizeof(cmdline)) return -1;
            cmdline[pos++] = ' ';
        }
        int n = test_compat_quote_arg(argv[i], cmdline + pos,
                                      sizeof(cmdline) - pos);
        if (n < 0) return -1;
        pos += (size_t)n;
    }
    cmdline[pos] = 0;

    HANDLE pipe_r, pipe_w;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&pipe_r, &pipe_w, &sa, 0)) return -1;
    SetHandleInformation(pipe_r, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdError = pipe_w;
    si.hStdOutput = pipe_w;
    /* Open NUL for stdin -- see comments in test_compat_run. */
    HANDLE nul_in = CreateFileA("NUL", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, 0, NULL);
    si.hStdInput = nul_in;

    BOOL ok = CreateProcessA(argv[0], cmdline, NULL, NULL, TRUE,
                             0, NULL, NULL, &si, &pi);
    if (nul_in != INVALID_HANDLE_VALUE) CloseHandle(nul_in);
    CloseHandle(pipe_w);
    if (!ok) {
        CloseHandle(pipe_r);
        return -1;
    }
    /* Drain pipe_r into stderr_buf until EOF. */
    size_t written = 0;
    char chunk[1024];
    DWORD got;
    while (ReadFile(pipe_r, chunk, sizeof(chunk), &got, NULL) && got > 0) {
        size_t to_copy = got;
        if (written + to_copy + 1 > stderr_buf_size) {
            to_copy = stderr_buf_size > written + 1
                    ? stderr_buf_size - written - 1
                    : 0;
        }
        if (to_copy > 0) {
            memcpy(stderr_buf + written, chunk, to_copy);
            written += to_copy;
        }
    }
    if (stderr_buf_size > 0) stderr_buf[written] = 0;
    CloseHandle(pipe_r);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (exit_code) *exit_code = (int)code;
    return 0;
#else
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }
    if (pid == 0) {
        /* Child: redirect stderr to pipe write end. */
        close(pipe_fds[0]);
        dup2(pipe_fds[1], 2);
        dup2(pipe_fds[1], 1);
        close(pipe_fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipe_fds[1]);
    size_t written = 0;
    char chunk[1024];
    ssize_t got;
    while ((got = read(pipe_fds[0], chunk, sizeof(chunk))) > 0) {
        size_t to_copy = (size_t)got;
        if (written + to_copy + 1 > stderr_buf_size) {
            to_copy = stderr_buf_size > written + 1
                    ? stderr_buf_size - written - 1
                    : 0;
        }
        if (to_copy > 0) {
            memcpy(stderr_buf + written, chunk, to_copy);
            written += to_copy;
        }
    }
    if (stderr_buf_size > 0) stderr_buf[written] = 0;
    close(pipe_fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (exit_code) *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return 0;
#endif
}

/* Spawn argv[0] with argv[1..]; redirect child stdout to
** stdout_path, stderr to stderr_path (both created/truncated).
** Either path may be NULL to discard. */
static int test_compat_run_to_files(char *const argv[],
                                    const char *stdout_path,
                                    const char *stderr_path,
                                    int *exit_code) {
    if (exit_code) *exit_code = -1;
#if defined(_WIN32)
    char cmdline[4096];
    size_t pos = 0;
    for (int i = 0; argv[i]; i++) {
        if (i > 0) {
            if (pos + 1 >= sizeof(cmdline)) return -1;
            cmdline[pos++] = ' ';
        }
        int n = test_compat_quote_arg(argv[i], cmdline + pos,
                                      sizeof(cmdline) - pos);
        if (n < 0) return -1;
        pos += (size_t)n;
    }
    cmdline[pos] = 0;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hout = INVALID_HANDLE_VALUE, herr = INVALID_HANDLE_VALUE;
    if (stdout_path) {
        hout = CreateFileA(stdout_path, GENERIC_WRITE, FILE_SHARE_READ,
            &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hout == INVALID_HANDLE_VALUE) return -1;
    } else {
        hout = CreateFileA("NUL", GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &sa, OPEN_EXISTING, 0, NULL);
    }
    if (stderr_path) {
        herr = CreateFileA(stderr_path, GENERIC_WRITE, FILE_SHARE_READ,
            &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (herr == INVALID_HANDLE_VALUE) {
            if (hout != INVALID_HANDLE_VALUE) CloseHandle(hout);
            return -1;
        }
    } else {
        herr = CreateFileA("NUL", GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &sa, OPEN_EXISTING, 0, NULL);
    }
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hout;
    si.hStdError  = herr;
    /* Open NUL for stdin -- the parent's stdin may be closed
    ** under meson, in which case GetStdHandle returns
    ** INVALID_HANDLE_VALUE and CreateProcessA fails because
    ** STARTF_USESTDHANDLES requires all three handles valid. */
    HANDLE nul_in = CreateFileA("NUL", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, 0, NULL);
    si.hStdInput  = nul_in;
    BOOL ok = CreateProcessA(argv[0], cmdline, NULL, NULL, TRUE,
                             0, NULL, NULL, &si, &pi);
    if (nul_in != INVALID_HANDLE_VALUE) CloseHandle(nul_in);
    CloseHandle(hout);
    CloseHandle(herr);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (exit_code) *exit_code = (int)code;
    return 0;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (stdout_path) {
            int fd = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) { dup2(fd, 1); close(fd); }
        } else {
            int fd = open("/dev/null", O_WRONLY);
            if (fd >= 0) { dup2(fd, 1); close(fd); }
        }
        if (stderr_path) {
            int fd = open(stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) { dup2(fd, 2); close(fd); }
        } else {
            int fd = open("/dev/null", O_WRONLY);
            if (fd >= 0) { dup2(fd, 2); close(fd); }
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (exit_code) *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return 0;
#endif
}

#endif /* LIME_TEST_COMPAT_H */
