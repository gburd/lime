/*
** tests/test_codegen_strict_warnings.c -- regression guard ensuring
** Lime-generated C compiles cleanly under the strict warning flags
** downstream projects use but Lime's own build does not.
**
** v1.5.1 (lexer, commit c8ec094): -Wdeclaration-after-statement,
**   -Wshadow=compatible-local, -Wmisleading-indentation.
** v1.5.2 (parser/grammar emitter): -Wmissing-prototypes (public API
**   functions now self-prototyped / the AOT helper too),
**   -Wunused-function, and -Wmaybe-uninitialized / -Wuninitialized
**   (yylhsminor zero-initialised so a copy rule `A = B` never reads
**   it on an undominated path).  These are all on by default in a
**   PostgreSQL build.
**
** The test compiles a generated parser .c (table path AND AOT -j
** path) plus a %literal_buffer lexer .c with -Werror on exactly
** these categories, so any future codegen regression fails the build
** rather than silently shipping.
*/
#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>

#define CHECK(cond, msg) do {                                            \
    if (!(cond)) {                                                       \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);          \
        fail++;                                                          \
    }                                                                    \
} while (0)

static int run_lime(const char *lime, const char *tmpl, const char *extra,
                    const char *workdir, const char *grammar) {
    /* lime writes parser output next to the input file but writes
    ** lexer (-X) output to the current working directory, so cd into
    ** the work dir first to make both land in the same place.  Because
    ** we cd, resolve the lime binary and template to absolute paths
    ** first -- a relative path would be invalid after the cd. */
    char limeabs[1024], tmplabs[1024];
    if (lime[0] == '/') {
        snprintf(limeabs, sizeof(limeabs), "%s", lime);
    } else {
        char cwd[768];
        if (!getcwd(cwd, sizeof(cwd))) return 1;
        snprintf(limeabs, sizeof(limeabs), "%s/%s", cwd, lime);
    }
    if (tmpl[0] == '/') {
        snprintf(tmplabs, sizeof(tmplabs), "%s", tmpl);
    } else {
        char cwd[768];
        if (!getcwd(cwd, sizeof(cwd))) return 1;
        snprintf(tmplabs, sizeof(tmplabs), "%s/%s", cwd, tmpl);
    }
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" %s -T\"%s\" \"%s\" >/dev/null 2>&1",
             workdir, limeabs, extra ? extra : "", tmplabs, grammar);
    return system(cmd);
}

/* Is the C compiler clang?  clang and GCC disagree on a couple of
** warning spellings: clang treats GCC-only options (e.g.
** -Wshadow=compatible-local, -Wmaybe-uninitialized) as unknown and
** GCC rejects clang's -W[no-]unknown-warning-option.  We detect once
** and tailor the flag list so the categories BOTH understand still
** bite (unused-function / missing-prototypes / uninitialized /
** declaration-after-statement). */
static int cc_is_clang(void) {
    /* `cc --version` mentions "clang" iff cc is clang. */
    int rc = system("cc --version 2>/dev/null | grep -qi clang");
    return rc == 0;
}

/* Compile a generated TU under the strict flagset with -Werror for the
** reported categories.  Returns 0 iff it compiled with zero of those
** warnings.  Covers both the v1.5.1 lexer set
** (-Wdeclaration-after-statement / -Wshadow=compatible-local /
** -Wmisleading-indentation) and the v1.5.2 parser set that
** PostgreSQL builds with by default (-Wmissing-prototypes /
** -Wunused-function / -Wmaybe-uninitialized / -Wuninitialized).
** -Werror is scoped to exactly these flags so unrelated compiler nits
** don't fail the test. */
static int compile_strict(const char *workdir, const char *file) {
    /* Flags both compilers accept and that catch the reported bugs. */
    const char *common =
        "-Werror=declaration-after-statement "
        "-Werror=misleading-indentation "
        "-Werror=missing-prototypes "
        "-Werror=unused-function "
        "-Werror=uninitialized ";
    /* GCC-only spellings (clang would emit unknown-option warnings). */
    const char *gcc_only =
        "-Werror=shadow=compatible-local "
        "-Werror=maybe-uninitialized ";
    /* clang's nearest equivalents. */
    const char *clang_only =
        "-Werror=shadow ";
    char cmd[3072];
    snprintf(cmd, sizeof(cmd),
             "cc -c -std=c11 -O2 -I\"%s\" %s%s"
             "-Wall -Wextra "
             "-Wno-error=unused-parameter -Wno-error=type-limits "
             "-Wno-error=unused-but-set-variable "
             "\"%s/%s\" -o /dev/null 2>\"%s/cc_err.txt\"",
             workdir, common, cc_is_clang() ? clang_only : gcc_only,
             workdir, file, workdir);
    int rc = system(cmd);
    return (rc == 0) ? 0 : 1;
}

int main(int argc, char **argv) {
    int fail = 0;
    if (argc < 4) {
        fprintf(stderr,
            "usage: %s <path-to-lime> <path-to-limpar.c> <grammar-dir>\n",
            argv[0]);
        return 2;
    }
    const char *lime = argv[1];
    const char *tmpl = argv[2];
    const char *gdir = argv[3];

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/test_codegen_strict", tmpdir);
    mkdir(dir, 0755);

    /* Copy the real repo grammars into the work dir (lime writes its
    ** parser output next to the input but writes -X lexer output to
    ** the cwd; run_lime cds into the work dir to unify them).  These
    ** are known-valid and exercise the emission paths that regressed:
    **   - a parser grammar with semantic action bodies including the
    **     copy pattern `A = B` (the per-rule reduce preamble:
    **     decl-after-statement in v1.5.1, uninitialised yylhsminor in
    **     v1.5.2), in both the table path and the AOT (-j) path, and
    **   - the %literal_buffer lexer grammar whose action bodies
    **     declare a local `n` (the LexFeedBytes parameter shadow). */
    struct {
        const char *src;    /* repo-relative grammar */
        const char *dst;    /* basename copied into the work dir */
        const char *flags;  /* extra lime flags (NULL = none) */
        const char *out1;   /* first generated TU to compile */
        const char *out2;   /* second TU (e.g. _aot.c), or NULL */
        const char *label;
    } cases[] = {
        { "examples/calc/calc.lime", "p.lime", NULL,
          "p.c", NULL, "parser C (table path)" },
        { "examples/calc/calc.lime", "pa.lime", "-j",
          "pa.c", "pa_aot.c", "parser C (AOT -j path)" },
        { "tests/test_lex_buf_grammar.lex", "lx.lex", "-X",
          "lx_lex.c", NULL, "lexer C" },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char src[1024], dst[700];
        snprintf(src, sizeof(src), "%s/%s", gdir, cases[i].src);
        snprintf(dst, sizeof(dst), "%s/%s", dir, cases[i].dst);
        char copy[2200];
        snprintf(copy, sizeof(copy), "cp \"%s\" \"%s\"", src, dst);
        if (system(copy) != 0) {
            fprintf(stderr, "FAIL: copy %s\n", cases[i].src);
            fail++;
            continue;
        }
        char genmsg[128], cmsg[128];
        snprintf(genmsg, sizeof(genmsg), "generate %s", cases[i].label);
        snprintf(cmsg, sizeof(cmsg),
                 "%s compiles with no strict-flag warnings", cases[i].label);
        CHECK(run_lime(lime, tmpl, cases[i].flags, dir, dst) == 0, genmsg);
        CHECK(compile_strict(dir, cases[i].out1) == 0, cmsg);
        if (cases[i].out2) {
            CHECK(compile_strict(dir, cases[i].out2) == 0, cmsg);
        }
    }

    if (fail) {
        char errpath[600];
        snprintf(errpath, sizeof(errpath), "%s/cc_err.txt", dir);
        fprintf(stderr, "--- last compiler diagnostics (%s) ---\n", errpath);
        FILE *f = fopen(errpath, "rb");
        if (f) {
            char line[1024];
            while (fgets(line, sizeof(line), f)) fputs(line, stderr);
            fclose(f);
        }
    }

    printf("test_codegen_strict_warnings: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
