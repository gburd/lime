/*
** snapshot_create.c -- runtime grammar-file -> snapshot pipeline
** built on lime + cc + dlopen.
**
** Implements lime_snapshot_create("foo.y", &err) by invoking the
** lime parser generator as a subprocess on the grammar file (with
** -n to emit a *_snapshot.c), compiling that file to a shared
** library, dlopen()ing it, and dlsym()ing the generic
** "lime_snapshot_entry" symbol that snapshot files emit.
**
** This is the load-time path for runtime grammar loading -- it lets
** an application say "give me a parser for this grammar text" and
** get back a ParserSnapshot ready for parse_begin() / parse_token().
** The cost is one fork+exec of `lime` plus a cc invocation, then a
** dlopen.  For long-running parsers (e.g. a daemon that loads a
** grammar at startup) this is amortised to nothing.
**
** The full in-process build -- where we'd run the same Build()
** phases lime.c uses, but against an in-memory grammar -- is
** documented in docs/ROADMAP.md as the long-term replacement; it
** requires refactoring lime.c to expose its phases as a library.
** The subprocess path here is the supported interim solution and
** the production path for callers whose hosts have a C compiler
** available.
**
** Environment / preconditions:
**   - The `lime` binary must be on PATH (or set LIME_BIN to its
**     absolute path).
**   - A C compiler must be on PATH (or set LIME_CC, default "cc").
**   - The runtime library (-llime_parser) must be linkable from the
**     C compiler's default search path, or the caller must set
**     LIME_LIBDIR to point at the directory containing
**     liblime_parser.{a,so}.
**
** When any of those is missing, lime_snapshot_create returns NULL
** with an error message naming the missing piece, so the caller
** can either install the prerequisite or fall back to a
** pre-compiled <Prefix>BuildSnapshot() they linked statically.
*/

#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "snapshot.h"
#include "snapshot_build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  String helpers                                                     */
/* ------------------------------------------------------------------ */

static char *xstrdup(const char *s) {
    if (s == NULL) return NULL;
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    if (r != NULL) memcpy(r, s, n);
    return r;
}

static char *fmt_err(const char *fmt, const char *a) {
    if (a == NULL) a = "(null)";
    size_t cap = strlen(fmt) + strlen(a) + 16;
    char *buf = malloc(cap);
    if (buf == NULL) return NULL;
    snprintf(buf, cap, fmt, a);
    return buf;
}

static const char *getenv_or(const char *name, const char *fallback) {
    const char *v = getenv(name);
    return (v != NULL && v[0] != '\0') ? v : fallback;
}

/* ------------------------------------------------------------------ */
/*  Subprocess fallback helpers (POSIX-only)                          */
/* ------------------------------------------------------------------ */

#ifndef _WIN32

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dlfcn.h>

/* ------------------------------------------------------------------ */
/*  Temporary directory                                                */
/* ------------------------------------------------------------------ */

/*
** Create a fresh temporary directory under $TMPDIR (or /tmp) and
** return its absolute path; caller frees.  Returns NULL on failure.
*/
static char *make_tmpdir(char **error) {
    const char *base = getenv_or("TMPDIR", "/tmp");
    /* Strip trailing slash so we don't get "//lime-snap-XXXXXX". */
    size_t blen = strlen(base);
    while (blen > 1 && base[blen - 1] == '/') blen--;

    char tmpl[256];
    int n = snprintf(tmpl, sizeof(tmpl), "%.*s/lime-snap-XXXXXX", (int)blen, base);
    if (n < 0 || n >= (int)sizeof(tmpl)) {
        if (error) *error = xstrdup("make_tmpdir: TMPDIR path too long");
        return NULL;
    }
    if (mkdtemp(tmpl) == NULL) {
        if (error) *error = fmt_err("make_tmpdir: mkdtemp failed (%s)", strerror(errno));
        return NULL;
    }
    return xstrdup(tmpl);
}

static void rm_rf(const char *path) {
    /* Best-effort recursive remove; we don't fail on errors -- the
    ** OS cleans up /tmp eventually anyway. */
    pid_t pid = fork();
    if (pid == 0) {
        execlp("rm", "rm", "-rf", path, (char *)NULL);
        _exit(127);
    } else if (pid > 0) {
        int status;
        (void)waitpid(pid, &status, 0);
    }
}

/* ------------------------------------------------------------------ */
/*  Subprocess runner                                                   */
/* ------------------------------------------------------------------ */

/*
** fork+exec argv[0] with argv as args, wait for it, return 0 on
** success.  On failure or non-zero exit, sets *error to a
** descriptive message and returns -1.
**
** Captures stderr into a small buffer so build failures surface
** something more useful than "cc exited with status 1".
*/
static int run_cmd(char *const argv[], char **error) {
    int err_pipe[2];
    if (pipe(err_pipe) != 0) {
        if (error) *error = fmt_err("pipe failed (%s)", strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        if (error) *error = fmt_err("fork failed (%s)", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        close(err_pipe[0]);
        /* Redirect stdout to /dev/null and stderr to the pipe so
        ** the parent can read any error message and surface it. */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, 1);
            if (devnull > 2) close(devnull);
        }
        dup2(err_pipe[1], 2);
        close(err_pipe[1]);
        execvp(argv[0], argv);
        fprintf(stderr, "execvp %s failed: %s\n", argv[0], strerror(errno));
        _exit(127);
    }

    close(err_pipe[1]);

    /* Read up to 4 KB of stderr; truncate further. */
    char err_buf[4096];
    size_t err_len = 0;
    for (;;) {
        ssize_t n = read(err_pipe[0], err_buf + err_len, sizeof(err_buf) - 1 - err_len);
        if (n <= 0) break;
        err_len += (size_t)n;
        if (err_len >= sizeof(err_buf) - 1) break;
    }
    err_buf[err_len] = '\0';
    close(err_pipe[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        if (error) *error = fmt_err("waitpid failed (%s)", strerror(errno));
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        char buf[8192];
        snprintf(buf, sizeof(buf), "%s exited with status %d.\nstderr:\n%s", argv[0],
                 WIFEXITED(status) ? WEXITSTATUS(status) : -1, err_buf);
        if (error) *error = xstrdup(buf);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Locate the limpar.c template                                       */
/* ------------------------------------------------------------------ */

/*
** Locate the limpar.c template file.  lime needs -T<path> to find
** it.  We check $LIME_TEMPLATE first, then $LIME_PREFIX/share/lime,
** then a few common install locations.  Return malloc'd path or
** NULL if not found.
*/
static char *find_limpar_template(void) {
    const char *envt = getenv("LIME_TEMPLATE");
    if (envt != NULL && envt[0] != '\0') {
        struct stat st;
        if (stat(envt, &st) == 0) return xstrdup(envt);
    }

    static const char *candidates[] = {
        "/usr/local/share/lime/limpar.c",
        "/usr/share/lime/limpar.c",
        /* Source-tree relative -- useful when running tests
        ** out-of-build-dir during development. */
        "limpar.c",
        "../limpar.c",
        "../../limpar.c",
        NULL,
    };
    for (int i = 0; candidates[i] != NULL; i++) {
        struct stat st;
        if (stat(candidates[i], &st) == 0) {
            return xstrdup(candidates[i]);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Locate the runtime library (for linking the snapshot .so)          */
/* ------------------------------------------------------------------ */

static char *find_runtime_libdir(void) {
    const char *env = getenv("LIME_LIBDIR");
    if (env != NULL && env[0] != '\0') return xstrdup(env);

    static const char *candidates[] = {
        "/usr/local/lib",
        "/usr/lib",
        "builddir/src",  /* source-tree dev */
        "lib",           /* `make` output */
        NULL,
    };
    for (int i = 0; candidates[i] != NULL; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/liblime_parser.a", candidates[i]);
        struct stat st;
        if (stat(path, &st) == 0) return xstrdup(candidates[i]);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Output-file naming                                                 */
/* ------------------------------------------------------------------ */

/*
** lime emits <basename>_snapshot.c -- compute the base from
** grammar_file the same way lime.c does (drop directory + extension).
*/
static void compute_basename(const char *grammar_file, char *out, size_t out_sz) {
    const char *slash = strrchr(grammar_file, '/');
    const char *start = slash ? slash + 1 : grammar_file;
    const char *dot = strrchr(start, '.');
    size_t len = dot ? (size_t)(dot - start) : strlen(start);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, start, len);
    out[len] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Snapshot lifetime: own the dlopen handle                            */
/* ------------------------------------------------------------------ */

/*
** The snapshot tables we get back from <Prefix>BuildSnapshot() were
** copied out of static arrays in the .so, so the snapshot's data is
** independent of the .so once built.  But the .so itself is mapped
** into the address space and consumes a few hundred KB; close it
** when the snapshot is released so long-running processes don't
** accumulate library mappings.
**
** We register an atexit-style hook by storing the dlopen handle in
** snap->jit_ctx (we don't use that field for actual JIT contexts in
** the lime_snapshot_create-built case) -- but jit_ctx is the right
** "opaque finalizer slot" to use.  Actually, we'd be racing with the
** JIT's own user.  Use a side-channel instead.
**
** A cleaner approach is a small parallel registry keyed by
** ParserSnapshot*; on snapshot_release the snapshot's destructor
** calls back here to dlclose.  For v0 we leak the handle (acceptable
** since most processes load grammars during init) and document this
** in the ROADMAP.  TODO entry tracked there.
*/

/* ------------------------------------------------------------------ */
/*  Per-snapshot dlopen-handle registry                                */
/* ------------------------------------------------------------------ */

/*
** Pairs each subprocess-built ParserSnapshot * with the dlopen()
** handle of the .so the action tables were copied out of.  When the
** snapshot's reference count drops to zero, destroy_snapshot in
** src/snapshot.c calls our snapshot_dlopen_release hook below, which
** consults this registry and dlclose()s the matching handle.
**
** Daemon-startup scenarios load a small fixed number of grammars
** (the ROADMAP cited 10s as typical, 100s as upper bound), so a
** linear-search singly-linked list is fine.  A pthread_mutex_t
** serialises register/unregister to make snapshot_release safe from
** any thread.  The dlopen-handle-side has no acquire path (snapshots
** are immutable once built) so we don't need a reader lock.
*/

#include <pthread.h>

typedef struct dlopen_entry {
    ParserSnapshot     *snap;
    void               *handle;
    struct dlopen_entry *next;
} dlopen_entry;

static dlopen_entry  *g_dlopen_registry = NULL;
static pthread_mutex_t g_dlopen_mu = PTHREAD_MUTEX_INITIALIZER;

static void register_dlopen_handle(ParserSnapshot *snap, void *handle) {
    dlopen_entry *e = (dlopen_entry *)malloc(sizeof(*e));
    if (e == NULL) {
        /* Out of memory; we'd rather leak the handle than crash. */
        return;
    }
    e->snap = snap;
    e->handle = handle;
    pthread_mutex_lock(&g_dlopen_mu);
    e->next = g_dlopen_registry;
    g_dlopen_registry = e;
    pthread_mutex_unlock(&g_dlopen_mu);
}

/*
** Strong definition of the weak hook declared in src/snapshot.c.
** Called from destroy_snapshot when the snapshot's last reference
** is dropped.  We unlink the snapshot's entry from the registry,
** dlclose the handle, free the entry.
**
** O(N) over the registry; N is "grammars loaded by this process",
** typically <100, and this only runs at snapshot-destroy time which
** is rare.
*/
void snapshot_dlopen_release(ParserSnapshot *snap) {
    if (snap == NULL) return;
    pthread_mutex_lock(&g_dlopen_mu);
    dlopen_entry **pp = &g_dlopen_registry;
    void *handle = NULL;
    while (*pp != NULL) {
        if ((*pp)->snap == snap) {
            dlopen_entry *e = *pp;
            handle = e->handle;
            *pp = e->next;
            free(e);
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&g_dlopen_mu);
    if (handle != NULL) {
        dlclose(handle);
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/*
** Override the weak default in src/snapshot.c: this strong
** definition wins at link time when liblime_parser is built with
** snapshot_create.c included.
*/
/* ------------------------------------------------------------------ */
/*  Core pipeline: grammar file -> ParserSnapshot                      */
/*                                                                      */
/*  Run lime + cc on grammar_file (which must already exist on disk    */
/*  with a .y / .lime extension), dlopen the result, return the        */
/*  ParserSnapshot.                                                     */
/* ------------------------------------------------------------------ */

static ParserSnapshot *compile_grammar_file_to_snapshot(const char *grammar_file, char **error) {
    if (grammar_file == NULL) {
        if (error) *error = xstrdup("compile_grammar: grammar_file is NULL");
        return NULL;
    }
    if (error) *error = NULL;

    /* 0. Check the grammar file exists. */
    struct stat st;
    if (stat(grammar_file, &st) != 0) {
        if (error)
            *error = fmt_err("compile_grammar: grammar file not found: %s", grammar_file);
        return NULL;
    }

    /* 1. Locate dependencies. */
    const char *lime_bin = getenv_or("LIME_BIN", "lime");
    const char *cc_bin = getenv_or("LIME_CC", "cc");
    char *template_path = find_limpar_template();
    if (template_path == NULL) {
        if (error)
            *error = xstrdup("compile_grammar: limpar.c template not found.  Set LIME_TEMPLATE "
                             "to its absolute path or install lime so "
                             "/usr/local/share/lime/limpar.c exists.");
        return NULL;
    }
    char *libdir = find_runtime_libdir();

    /* 2. Make a temp dir and run lime -n on the grammar. */
    char *tmpdir = make_tmpdir(error);
    if (tmpdir == NULL) {
        free(template_path);
        free(libdir);
        return NULL;
    }

    char base[128];
    compute_basename(grammar_file, base, sizeof(base));

    char tflag[1024];
    snprintf(tflag, sizeof(tflag), "-T%s", template_path);
    char dflag[1024];
    snprintf(dflag, sizeof(dflag), "-d%s", tmpdir);

    char *lime_argv[] = {
        (char *)lime_bin, tflag, "-n", dflag, (char *)grammar_file, NULL,
    };
    if (run_cmd(lime_argv, error) != 0) {
        free(template_path);
        free(libdir);
        rm_rf(tmpdir);
        free(tmpdir);
        return NULL;
    }

    /* 3. Compile the *_snapshot.c (with snapshot_build.c folded in
    ** so snapshot_build_from_tables resolves) into a shared library. */
    char snap_c[1024], so_path[1024];
    snprintf(snap_c, sizeof(snap_c), "%s/%s_snapshot.c", tmpdir, base);
    snprintf(so_path, sizeof(so_path), "%s/%s_snapshot.so", tmpdir, base);

    const char *include_root = getenv_or("LIME_INCLUDE", "include");
    const char *src_root = getenv_or("LIME_SRC_INCLUDE", "src");

    char inc_a[256], inc_b[256];
    snprintf(inc_a, sizeof(inc_a), "-I%s", include_root);
    snprintf(inc_b, sizeof(inc_b), "-I%s", src_root);

    char snapshot_build_c[1024] = {0};
    {
        const char *env = getenv("LIME_SNAPSHOT_BUILD_C");
        if (env && env[0] != '\0') {
            struct stat sst;
            if (stat(env, &sst) == 0) {
                strncpy(snapshot_build_c, env, sizeof(snapshot_build_c) - 1);
            }
        }
        if (snapshot_build_c[0] == '\0') {
            const char *candidates[] = {
                "src/snapshot_build.c",
                "../src/snapshot_build.c",
                "/usr/local/share/lime/snapshot_build.c",
                "/usr/share/lime/snapshot_build.c",
                NULL,
            };
            for (int i = 0; candidates[i] != NULL; i++) {
                struct stat sst;
                if (stat(candidates[i], &sst) == 0) {
                    strncpy(snapshot_build_c, candidates[i], sizeof(snapshot_build_c) - 1);
                    break;
                }
            }
        }
    }
    if (snapshot_build_c[0] == '\0') {
        if (error)
            *error = xstrdup("compile_grammar: snapshot_build.c not found.  Set "
                             "LIME_SNAPSHOT_BUILD_C, install lime to /usr/local/share/lime/, "
                             "or run from the source tree.");
        free(template_path);
        free(libdir);
        rm_rf(tmpdir);
        free(tmpdir);
        return NULL;
    }

    char *cc_argv[24];
    int ai = 0;
    cc_argv[ai++] = (char *)cc_bin;
    cc_argv[ai++] = "-shared";
    cc_argv[ai++] = "-fPIC";
    cc_argv[ai++] = "-O0"; /* fast compile; .so not on the perf path */
    cc_argv[ai++] = "-std=c11";
    /* Feature-test macros for the same reason the project sets them in
    ** meson.build: -std=c11 hides clock_gettime, strdup, pthread_*, etc.
    ** unless one of these is defined.  src/snapshot_build.c uses
    ** clock_gettime(CLOCK_MONOTONIC, ...) which fails to declare under
    ** strict c11 on glibc / FreeBSD / illumos.  Mirrors the project-
    ** wide block in meson.build so the runtime cc invocation matches
    ** the build-time one. */
    cc_argv[ai++] = "-D_GNU_SOURCE";
    cc_argv[ai++] = "-D_POSIX_C_SOURCE=200809L";
    cc_argv[ai++] = "-D__EXTENSIONS__";
    cc_argv[ai++] = "-D_DARWIN_C_SOURCE";
    cc_argv[ai++] = inc_a;
    cc_argv[ai++] = inc_b;
    cc_argv[ai++] = snap_c;
    cc_argv[ai++] = snapshot_build_c;
    cc_argv[ai++] = "-o";
    cc_argv[ai++] = so_path;
#if defined(__APPLE__)
    cc_argv[ai++] = "-Wl,-undefined,dynamic_lookup";
    /* macOS: -Bsymbolic equivalent.  The Mach-O default already
    ** prefers the dylib's own symbols, but be explicit. */
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) \
        || defined(__NetBSD__) || defined(__DragonFly__) || defined(__sun)
    /* On ELF Unix, allow the .so to leave symbols unresolved -- they
    ** will be looked up from the parent executable at dlopen time
    ** (the parent is built with -rdynamic / -Wl,--export-dynamic so
    ** its symbols are visible).  Without this, FreeBSD's default ld
    ** behaviour rejects the .so with "Undefined symbol foo" for any
    ** function it doesn't see in its own object files. */
    cc_argv[ai++] = "-Wl,--unresolved-symbols=ignore-all";
    /* Bind references to symbols defined inside this .so to those
    ** definitions at link time, instead of going through the global
    ** symbol table at dlopen time.  Without this, lime_snapshot_entry()
    ** in the merged-grammar .so resolves <Prefix>BuildSnapshot() to
    ** the host executable's copy (the BASE grammar's, when the host
    ** test was statically linked against bench_arith_grammar_snapshot.c)
    ** instead of the .so's own merged-grammar copy.  -Bsymbolic causes
    ** the .so's internal call to bind to the .so's own definition,
    ** which is what every consumer of the runtime-rebuild path needs.
    ** Without it, test_extension_rebuild and test_snapshot_create both
    ** silently return base-grammar tables and the merge is invisible. */
    cc_argv[ai++] = "-Wl,-Bsymbolic";
#endif
    cc_argv[ai] = NULL;
    if (run_cmd(cc_argv, error) != 0) {
        free(template_path);
        free(libdir);
        rm_rf(tmpdir);
        free(tmpdir);
        return NULL;
    }

    /* 4. dlopen + dlsym the entry point. */
    dlerror();
    void *handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        const char *de = dlerror();
        if (error)
            *error = fmt_err("compile_grammar: dlopen failed: %s", de ? de : "(no error message)");
        free(template_path);
        free(libdir);
        rm_rf(tmpdir);
        free(tmpdir);
        return NULL;
    }
    typedef ParserSnapshot *(*entry_fn)(void);
    union {
        void *ptr;
        entry_fn fn;
    } cast;
    cast.ptr = dlsym(handle, "lime_snapshot_entry");
    if (cast.ptr == NULL) {
        const char *de = dlerror();
        if (error)
            *error = fmt_err("compile_grammar: dlsym(lime_snapshot_entry) failed: %s",
                             de ? de : "(no error message)");
        dlclose(handle);
        free(template_path);
        free(libdir);
        rm_rf(tmpdir);
        free(tmpdir);
        return NULL;
    }

    ParserSnapshot *snap = cast.fn();

    /* 5. Cleanup.  Register the dlopen handle so destroy_snapshot
    ** can dlclose it when the last reference to snap is released
    ** (v0.6.x; prior to this the handle was deliberately leaked at
    ** process scope -- ~100 KB per loaded grammar -- because POSIX
    ** leaves the lifetime of rodata pointers under dlclose loose
    ** for snapshot tables that were memcpy'd out of the .so).
    **
    ** snapshot_build_from_tables in the generated snapshot.c
    ** deep-copies every table out of the .so's static arrays into
    ** heap memory the snapshot owns, so by the time we register
    ** here the snapshot is independent of the .so and the dlclose
    ** during destroy_snapshot is safe.  Verified by valgrind on
    ** test_snapshot_dlopen_cleanup: zero leaks after a 64-cycle
    ** compile/release loop. */
    if (snap != NULL) {
        register_dlopen_handle(snap, handle);
    } else {
        /* Failed entry-fn return: dlclose now since the snapshot
        ** never gets a chance to release. */
        dlclose(handle);
    }
    rm_rf(tmpdir);
    free(tmpdir);
    free(template_path);
    free(libdir);

    if (snap == NULL) {
        if (error && *error == NULL) {
            *error = xstrdup("compile_grammar: lime_snapshot_entry returned NULL");
        }
    }
    return snap;
}

#endif /* !_WIN32 */

/* ------------------------------------------------------------------ */
/*  Compile from in-memory grammar text                                */
/*                                                                      */
/*  Used by lime_snapshot_extend(): take a base grammar's source,     */
/*  concatenate the modification fragment from                          */
/*  lime_modifications_to_grammar_text(), and feed the result through   */
/*  the in-process LALR rebuild library when available, falling back   */
/*  to the subprocess pipeline otherwise.                              */
/*                                                                      */
/*  Dispatch policy (since v0.5.5, ROADMAP item 1 phase 4):             */
/*                                                                      */
/*    1. If LIME_FORCE_SUBPROCESS=1 is set in the environment, skip     */
/*       the in-process attempt entirely.  Useful for debugging        */
/*       compiler-bug regressions and for tests that want to verify    */
/*       the subprocess path still works.                              */
/*                                                                      */
/*    2. Otherwise, if the in-process compiler symbol                  */
/*       (lime_compile_grammar_in_process) is resolved at link time --  */
/*       i.e. the consuming executable links lime_compiler_dep -- try  */
/*       it first.  This costs ~0.04ms per compile, ~2000x faster than */
/*       the subprocess pipeline.                                      */
/*                                                                      */
/*    3. Fall back to the subprocess pipeline (lime + cc + dlopen) on  */
/*       any in-process failure.  The in-process path is new (v0.5.4)  */
/*       and shares lime's globals via lime_active_ctx; it does not    */
/*       yet support every directive (e.g. recursive %extends with     */
/*       file inclusion), and a future caller running concurrent       */
/*       compilations across threads would race on the shared          */
/*       active-context pointer.  Falling back keeps the runtime       */
/*       correct under unanticipated concurrency or unsupported        */
/*       directives.                                                   */
/*                                                                      */
/*    4. If the in-process symbol is not linked in (executable did     */
/*       not include lime_compiler_dep), the weak reference resolves   */
/*       to NULL and dispatch goes straight to the subprocess.  The    */
/*       v0.5.4 behaviour is preserved for callers that have not       */
/*       opted into the new dependency.                                 */
/*                                                                      */
/*  v0.5.6+ Windows support: dispatch now works cross-platform.  The    */
/*  in-process path (step 2) has no Windows-specific code -- it's      */
/*  pure C11 with no fork/exec/dlopen.  On Windows with                */
/*  LIME_FORCE_SUBPROCESS=1 set, lime_compile_grammar_text returns     */
/*  NULL with an explanatory error rather than attempting the          */
/*  subprocess fallback (which is Unix-only).                          */
/* ------------------------------------------------------------------ */

/*
** Weak reference to the in-process compile entry point declared in
** include/lime_compiler.h.  The function lives in lime.c (compiled
** into liblime_compiler.a with -DLIME_HAVE_SNAPSHOT_BUILD) and is
** absent from the runtime parser library to keep it small.  When the
** consumer links lime_compiler_dep this reference resolves to the
** real definition; otherwise it resolves to NULL and we fall through
** to the subprocess fallback below.
*/
extern int lime_compile_grammar_in_process(const char *grammar_text,
                                           size_t len,
                                           ParserSnapshot **out_snapshot,
                                           char **error)
    __attribute__((weak));

ParserSnapshot *lime_compile_grammar_text(const char *grammar_text, size_t len, char **error) {
    if (grammar_text == NULL || len == 0) {
        if (error) *error = xstrdup("lime_compile_grammar_text: grammar_text is empty");
        return NULL;
    }
    if (error) *error = NULL;

    /* In-process fast path (v0.5.5+, ROADMAP-1 phase 4).  Skipped when
    ** LIME_FORCE_SUBPROCESS=1 is set or when the consumer did not link
    ** lime_compiler_dep (weak reference is NULL). */
    const char *force_sub = getenv("LIME_FORCE_SUBPROCESS");
    bool force_subprocess = (force_sub != NULL && force_sub[0] != '\0' && force_sub[0] != '0');
    if (!force_subprocess && lime_compile_grammar_in_process != NULL) {
        ParserSnapshot *snap = NULL;
        char *ip_err = NULL;
        if (lime_compile_grammar_in_process(grammar_text, len, &snap, &ip_err) == 0
                && snap != NULL) {
            free(ip_err);
            return snap;
        }
        /* In-process failed.  Reasons include: grammar uses a
        ** directive the in-process path does not yet support (e.g.
        ** recursive %extends with file resolution), the active
        ** lime_active_ctx is held by another thread, or an internal
        ** assertion in the new compiler.  Discard its diagnostic and
        ** retry via the subprocess pipeline below.  The subprocess
        ** path will surface its own (possibly clearer) error. */
        free(ip_err);
        if (snap != NULL) {
            /* Defensive: in-process returned non-zero but populated a
            ** snapshot.  Release it before falling through so we do
            ** not leak the partial result. */
            snapshot_release(snap);
        }
    }

#ifdef _WIN32
    /* Windows: no subprocess fallback.  The fork/exec/dlopen machinery
    ** below is POSIX-only.  When LIME_FORCE_SUBPROCESS=1 is set on
    ** Windows, return a clear error rather than silently skipping. */
    if (force_subprocess) {
        if (error) {
            *error = xstrdup(
                "lime_compile_grammar_text: LIME_FORCE_SUBPROCESS=1 set, but subprocess "
                "fallback (fork+exec+dlopen) is not available on Windows. "
                "Unset the variable to use the in-process compiler, or link lime_compiler_dep "
                "to enable lime_compile_grammar_in_process().");
        }
        return NULL;
    }
    /* In-process also failed (or wasn't linked).  Surface the reason. */
    if (error) {
        if (lime_compile_grammar_in_process == NULL) {
            *error = xstrdup(
                "lime_compile_grammar_text: in-process compiler unavailable (lime_compiler_dep "
                "not linked). On Windows, the subprocess fallback (fork+exec+dlopen) is not "
                "available. Link lime_compiler_dep to enable lime_compile_grammar_in_process().");
        } else {
            *error = xstrdup(
                "lime_compile_grammar_text: in-process compilation failed, and subprocess "
                "fallback (fork+exec+dlopen) is not available on Windows.");
        }
    }
    return NULL;
#else
    /* Subprocess fallback (the v0.5.3 path).  Write to a temp .y file,
    ** then call the file-based pipeline.  We could in principle hand
    ** a pipe to lime, but lime today is organised around stat()-able
    ** input paths, so a temp file is the simplest correct approach. */
    char *tmpdir = make_tmpdir(error);
    if (tmpdir == NULL) return NULL;

    char path[1024];
    snprintf(path, sizeof(path), "%s/grammar.y", tmpdir);

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        if (error) *error = fmt_err("lime_compile_grammar_text: fopen failed (%s)",
                                    strerror(errno));
        rm_rf(tmpdir);
        free(tmpdir);
        return NULL;
    }
    if (fwrite(grammar_text, 1, len, fp) != len) {
        fclose(fp);
        if (error) *error = xstrdup("lime_compile_grammar_text: fwrite truncated");
        rm_rf(tmpdir);
        free(tmpdir);
        return NULL;
    }
    fclose(fp);

    ParserSnapshot *snap = compile_grammar_file_to_snapshot(path, error);

    rm_rf(tmpdir);
    free(tmpdir);
    return snap;
#endif /* !_WIN32 */
}

/* ------------------------------------------------------------------ */
/*  Public API: create_base_snapshot                                    */
/* ------------------------------------------------------------------ */

ParserSnapshot *create_base_snapshot(const char *grammar_file, char **error) {
#ifdef _WIN32
    /* Windows: create_base_snapshot is file-based, so it requires the
    ** subprocess pipeline (lime + cc + dlopen).  Direct the caller to
    ** use lime_compile_grammar_text with in-process compilation
    ** instead, or fall back to the static-parser shape (Shape A in
    ** docs/INTEGRATING_LIME.md). */
    (void)grammar_file;
    if (error) {
        *error = xstrdup(
            "create_base_snapshot: not available on Windows (requires fork+exec+dlopen). "
            "Use lime_compile_grammar_text() with the in-process compiler (link "
            "lime_compiler_dep), or use the static-parser shape (Shape A in "
            "docs/INTEGRATING_LIME.md).");
    }
    return NULL;
#else
    return compile_grammar_file_to_snapshot(grammar_file, error);
#endif
}
