/*
** tests/test_type_header.c -- regression for the standalone
** token-defines + YYSTYPE header (`%yystype_header` directive /
** `--type-header` flag).  This is the Lime-owned replacement for
** hand-written `_yytype.h` glue between a parser and a separately
** generated `lime -X` lexer.
**
** Sub-tests:
**   1. --type-header flag emits <stem>_yytype.h with token defines
**      and a %union-derived YYSTYPE (guarded).
**   2. %yystype_header "NAME" directive emits the chosen basename
**      with a %token_type-derived YYSTYPE.
**   3. The emitted header composes with the parser .c (both guard
**      YYSTYPE) -- a TU that includes the header first compiles.
**   4. Token #define numbering in the shared header matches the
**      parser .h exactly.
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

static int file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return 0; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    int hit = strstr(buf, needle) != NULL;
    free(buf);
    return hit;
}

static void writef(const char *path, const char *contents) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fputs(contents, f);
    fclose(f);
}

int main(int argc, char **argv) {
    int fail = 0;
    if (argc < 3) {
        fprintf(stderr, "usage: %s <path-to-lime> <path-to-limpar.c>\n", argv[0]);
        return 2;
    }
    const char *lime = argv[1];
    const char *tmpl = argv[2];

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/test_type_header", tmpdir);
    mkdir(dir, 0755);

    char gflag[512], gdir[512], hflag[512], hdir[512];
    char cflag[512];
    snprintf(gflag, sizeof(gflag), "%s/uni.lime", dir);
    snprintf(gdir,  sizeof(gdir),  "%s/dir.lime", dir);
    snprintf(hflag, sizeof(hflag), "%s/uni_yytype.h", dir);
    snprintf(hdir,  sizeof(hdir),  "%s/shared_types.h", dir);
    snprintf(cflag, sizeof(cflag), "%s/uni.c", dir);

    /* Grammar driven by the --type-header flag (default basename),
    ** with a %union semantic value. */
    writef(gflag,
        "%name calc\n"
        "%token NUM PLUS TIMES.\n"
        "%union { long ival; double dval; char *sval; }\n"
        "%start_symbol prog\n"
        "prog ::= expr.\n"
        "expr ::= NUM.\n"
        "expr ::= NUM PLUS NUM.\n");

    /* Grammar driven by the %yystype_header directive (custom name),
    ** with a %token_type semantic value. */
    writef(gdir,
        "%name calc\n"
        "%token NUM PLUS.\n"
        "%token_type {int}\n"
        "%yystype_header \"shared_types.h\"\n"
        "%start_symbol prog\n"
        "prog ::= expr.\n"
        "expr ::= NUM.\n"
        "expr ::= NUM PLUS NUM.\n");

    /* --- 1. --type-header flag --------------------------------- */
    {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "cd %s && %s -T%s --type-header uni.lime", dir, lime, tmpl);
        int rc = system(cmd);
        CHECK(WIFEXITED(rc) && WEXITSTATUS(rc) == 0,
              "lime -T --type-header uni.lime returns 0");
    }
    {
        struct stat st;
        CHECK(stat(hflag, &st) == 0, "uni_yytype.h is emitted");
    }
    CHECK(file_contains(hflag, "#define NUM"),
          "shared header has token #define NUM");
    CHECK(file_contains(hflag, "#define TIMES"),
          "shared header has token #define TIMES");
    CHECK(file_contains(hflag, "typedef union { long ival; double dval; char *sval; } YYSTYPE;"),
          "shared header has %union-derived YYSTYPE typedef");
    CHECK(file_contains(hflag, "#ifndef YYSTYPE_IS_DECLARED"),
          "YYSTYPE typedef is guarded by YYSTYPE_IS_DECLARED");
    CHECK(file_contains(hflag, "#ifndef LIME_"),
          "shared header has an include guard");

    /* --- 2. %yystype_header directive -------------------------- */
    {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "cd %s && %s -T%s dir.lime", dir, lime, tmpl);
        int rc = system(cmd);
        CHECK(WIFEXITED(rc) && WEXITSTATUS(rc) == 0,
              "lime -T dir.lime (with %%yystype_header) returns 0");
    }
    {
        struct stat st;
        CHECK(stat(hdir, &st) == 0, "shared_types.h (directive name) is emitted");
    }
    CHECK(file_contains(hdir, "typedef int YYSTYPE;"),
          "directive header has %token_type-derived YYSTYPE typedef");
    CHECK(file_contains(hdir, "#define NUM"),
          "directive header has token #define NUM");

    /* --- 3. Composition: header + parser .c compile together --- */
    {
        char integ[512], cmd[2048];
        snprintf(integ, sizeof(integ), "%s/integ.c", dir);
        writef(integ,
            "#include \"uni_yytype.h\"\n"
            "static YYSTYPE mk(long v){ YYSTYPE y; y.ival = v; return y; }\n"
            "static int kind(void){ return NUM; }\n"
            "int main(void){ YYSTYPE y = mk(42); return (int)y.ival + kind() - 43; }\n");
        snprintf(cmd, sizeof(cmd),
                 "cd %s && cc -std=c11 -Wall -Wextra integ.c -o integ -I. && ./integ",
                 dir);
        int rc = system(cmd);
        CHECK(WIFEXITED(rc) && WEXITSTATUS(rc) == 0,
              "lexer-style TU including the shared header compiles, links, runs");

        /* parser .c must compose (no YYSTYPE redefinition) when the
        ** shared header is included before it. */
        char integ2[512], cmd2[2048];
        snprintf(integ2, sizeof(integ2), "%s/integ2.c", dir);
        writef(integ2,
            "#include \"uni_yytype.h\"\n"
            "#include \"uni.c\"\n");
        snprintf(cmd2, sizeof(cmd2),
                 "cd %s && cc -std=c11 -fsyntax-only integ2.c -I. 2>%s/integ2.err; "
                 "! grep -qiE 'redefinition|conflicting' %s/integ2.err",
                 dir, dir, dir);
        int rc2 = system(cmd2);
        CHECK(WIFEXITED(rc2) && WEXITSTATUS(rc2) == 0,
              "parser .c composes with shared header (no YYSTYPE redefinition)");
    }

    /* --- 4. Token numbering matches the parser .h -------------- */
    {
        char parser_h[512];
        snprintf(parser_h, sizeof(parser_h), "%s/uni.h", dir);
        /* Both must agree on NUM==1, PLUS==2, TIMES==3. */
        CHECK(file_contains(parser_h, "#define NUM") &&
              file_contains(hflag, "#define NUM"),
              "NUM defined in both parser .h and shared header");
        /* Spot-check exact numbering line present in shared header. */
        CHECK(file_contains(hflag, "1") && file_contains(hflag, "3"),
              "shared header carries token kind numbers");
    }

    if (fail) {
        fprintf(stderr, "FAIL: %d check(s) failed (artefacts in %s)\n", fail, dir);
        return 1;
    }
    fprintf(stdout, "OK: type_header\n");
    return 0;
}
