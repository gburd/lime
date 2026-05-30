/*
** tests/posix_shim.h -- minimal POSIX shims for tests on Windows.
**
** Several lime tests (test_jit, test_jit_fallback, test_dialect,
** test_embed, test_extends, test_formatter_*, test_lint) use
** POSIX functions that aren't in the Windows CRT:
**
**   setenv, unsetenv     -- env-var manipulation
**   mkdtemp              -- temp-dir creation
**   realpath             -- canonical path resolution
**
** This header provides inline shims on _WIN32 that map to the
** Windows _putenv_s / _mktemp_s+_mkdir / _fullpath equivalents.
** On non-Windows it expands to the standard POSIX includes so the
** test source remains identical.
**
** Include AFTER the system <stdio.h>/<stdlib.h>/<unistd.h>/etc.
** Tests may #include "posix_shim.h" wholesale; on POSIX hosts the
** header is a no-op beyond pulling in <unistd.h>+<sys/stat.h>.
*/
#ifndef LIME_TESTS_POSIX_SHIM_H
#define LIME_TESTS_POSIX_SHIM_H

#if defined(_WIN32)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>        /* _mktemp_s */
#include <direct.h>    /* _mkdir */
#include <stdlib.h>    /* _fullpath, _putenv_s */
#include <sys/stat.h>  /* _stat etc. -- pulled in for parity with POSIX */

/* setenv/unsetenv: map to _putenv_s.  Note overwrite is honoured;
** _putenv_s always overwrites, so we check existence first. */
static inline int setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite) {
        size_t len = 0;
        if (getenv_s(&len, NULL, 0, name) == 0 && len > 0) return 0;
    }
    return _putenv_s(name, value) == 0 ? 0 : -1;
}

static inline int unsetenv(const char *name) {
    return _putenv_s(name, "") == 0 ? 0 : -1;
}

/* mkdtemp: same contract as POSIX -- the trailing "XXXXXX" of the
** template is replaced with random characters, the directory is
** created, and the (mutated) template buffer is returned.  On
** failure returns NULL with errno set. */
static inline char *mkdtemp(char *template_) {
    size_t len = strlen(template_);
    /* _mktemp_s mutates template_ in-place. */
    if (_mktemp_s(template_, len + 1) != 0) return NULL;
    if (_mkdir(template_) != 0) return NULL;
    return template_;
}

/* realpath: emulate via _fullpath.  When resolved_path is NULL
** _fullpath malloc's a buffer of _MAX_PATH bytes; caller frees. */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
static inline char *realpath(const char *path, char *resolved_path) {
    return _fullpath(resolved_path, path, resolved_path ? PATH_MAX : 0);
}

#else /* !_WIN32 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#endif /* _WIN32 */

#endif /* LIME_TESTS_POSIX_SHIM_H */
