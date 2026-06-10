/*
** tests/test_codegen_strict_warnings.c -- regression guard ensuring
** Lime-generated C compiles cleanly under the strict warning flags
** downstream projects use but Lime's own build does not (notably
** PostgreSQL: -Wdeclaration-after-statement, -Wshadow=compatible-local,
** -Wmisleading-indentation).  Reported in v1.5.0: the generated
** *_lex.c and parser .c emitted ~107 warnings under these flags while
** Lime's own warning_level=2 build stayed silent.  Fixed in v1.5.1 by
** hoisting declarations to block top, grouping (void) casts, and
** renaming internal LexFeedBytes parameters (bytes/n -> yybytes/yyn)
** so user action-body locals can't shadow them.
**
** The test compiles each generated TU with the exact flagset and
** -Werror, so any future codegen regression in these categories
** fails the build rather than silently shipping.
*/
#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    ** the work dir first to make both land in the same place. */
    char cmd[2600];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" %s -T\"%s\" \"%s\" >/dev/null 2>&1",
             workdir, lime, extra ? extra : "", tmpl, grammar);
    return system(cmd);
}

/* Compile a generated TU under the strict flagset with -Werror for the
** three reported categories.  Returns 0 iff it compiled with zero of
** those warnings.  We scope -Werror to exactly the reported flags so
** unrelated nits in the C compiler's defaults don't fail the test. */
static int compile_strict(const char *workdir, const char *file) {
    char cmd[3072];
    snprintf(cmd, sizeof(cmd),
             "cc -c -std=c11 -O2 -I\"%s\" "
             "-Werror=declaration-after-statement "
             "-Werror=shadow=compatible-local "
             "-Werror=misleading-indentation "
             "-Wall -Wextra "
             "-Wno-error=unused-parameter -Wno-error=type-limits "
             "-Wno-error=unused-but-set-variable "
             "\"%s/%s\" -o /dev/null 2>\"%s/cc_err.txt\"",
             workdir, workdir, file, workdir);
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
    ** output next to the input).  These are known-valid and exercise
    ** the exact emission paths that regressed:
    **   - a parser grammar with semantic action bodies (the per-rule
    **     reduce preamble that emitted decl-after-statement), and
    **   - the %literal_buffer lexer grammar whose action bodies
    **     declare a local `n` (the LexFeedBytes parameter shadow). */
    struct { const char *src; const char *dst; int is_lexer; const char *out; } cases[] = {
        { "examples/calc/calc.lime", "p.lime", 0, "p.c" },
        { "tests/test_lex_buf_grammar.lex", "lx.lex", 1, "lx_lex.c" },
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
        CHECK(run_lime(lime, tmpl, cases[i].is_lexer ? "-X" : NULL, dir, dst) == 0,
              cases[i].is_lexer ? "generate lexer C" : "generate parser C");
        CHECK(compile_strict(dir, cases[i].out) == 0,
              cases[i].is_lexer
                  ? "lexer C compiles with no strict-flag warnings"
                  : "parser C compiles with no strict-flag warnings");
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
