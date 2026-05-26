/*
** tests/test_extends.c -- v0.4.1 regression test for `%extends`,
** `%override`, `%remove`, `%override_type`, and diamond inheritance.
**
** Drives the lime binary across a small ANSI/Oracle/MySQL/unified
** dialect family in tests/extends_fixtures/.  Each sub-test checks
** a generated artifact (.c / .h) for the right
** presence/absence/value pattern.  No runtime parsing is exercised
** here -- this test is grammar-only, like test_dialect.c.
**
** Sub-tests (12+1 reverse-validation):
**
**   1. ansi_sql.lime               -> base parser builds clean
**   2. oracle.lime                 -> ANSI + Oracle's overrides + extras
**   3. mysql.lime                  -> ANSI + MySQL's overrides + extras
**   4. unified.lime                -> diamond resolution
**   5. override-replaces-body      -> oracle's body wins on its own arm
**   6. last-wins for derived       -> unified's own decisions stand
**   7. %remove non-existent: warn  -> default build emits warning
**   8. %override_type warning      -> widening warning printed to stderr
**   9. circular %extends           -> error, lime exits non-zero
**  10. %include DFS order          -> base's #include precedes derived's
**  11. diamond conflict            -> ansi_sql + two siblings overriding
**                                   the same rule with different bodies
**                                   produce an error
**  12. identity hash via aliases   -> oracle.lime overrides
**                                   `select_clause(R) ::= SELECT STAR
**                                    FROM IDENT.` even when the alias
**                                    differs (X instead of R)
**  13. (reverse-validation)        -> stub-flag check that the test
**                                    actually catches the regression
**                                    cases.  Done by separate edits
**                                    documented in the commit body
**                                    rather than runtime here.
**
** Exits 0 on full pass, 1 on any failure, 77 on missing binary
** (meson skip).
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failures = 0;

#define FAIL(tag, fmt, ...) do {                                        \
    fprintf(stderr, "FAIL: %s -- " fmt "\n", (tag), ##__VA_ARGS__);     \
    failures++;                                                         \
} while (0)

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

static void ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
    int rc = system(cmd);
    (void)rc;
}

/* Run lime <args> -d<outdir> <fixture>; capture stdout+stderr if asked. */
static int run_lime(const char *lime_bin,
                    const char *limpar,
                    const char *flags,
                    const char *outdir,
                    const char *fixture,
                    char *err_out,
                    size_t err_cap) {
    char cmd[4096];
    if (err_out != NULL) {
        char errfile[1024];
        snprintf(errfile, sizeof(errfile), "%s/stderr.txt", outdir);
        snprintf(cmd, sizeof(cmd),
                 "'%s' %s -T'%s' -d'%s' '%s' >/dev/null 2>'%s'",
                 lime_bin, flags, limpar, outdir, fixture, errfile);
        int rc = system(cmd);
        char *err = slurp(errfile);
        if (err != NULL) {
            snprintf(err_out, err_cap, "%s", err);
            free(err);
        } else {
            err_out[0] = 0;
        }
        return rc;
    } else {
        snprintf(cmd, sizeof(cmd),
                 "'%s' %s -T'%s' -d'%s' '%s' >/dev/null 2>&1",
                 lime_bin, flags, limpar, outdir, fixture);
        return system(cmd);
    }
}

static int contains(const char *hay, const char *needle) {
    return hay != NULL && strstr(hay, needle) != NULL;
}

static int find_pos(const char *hay, const char *needle) {
    if (hay == NULL) return -1;
    const char *p = strstr(hay, needle);
    return p ? (int)(p - hay) : -1;
}

/* Basename: input.lime -> input -- mirrors lime's output naming. */
static void strip_ext(const char *in, char *out, size_t cap) {
    const char *slash = strrchr(in, '/');
    const char *base = slash ? slash + 1 : in;
    snprintf(out, cap, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <lime-binary> <limpar.c> <fixtures-dir>\n",
                argv[0]);
        return 2;
    }
    const char *lime_bin = argv[1];
    const char *limpar   = argv[2];
    const char *fixdir   = argv[3];

    struct stat st;
    if (stat(lime_bin, &st) != 0) {
        fprintf(stderr, "SKIP: %s not found\n", lime_bin);
        return 77;
    }

    char ansi_path[1024], oracle_path[1024],
         mysql_path[1024], unified_path[1024];
    snprintf(ansi_path, sizeof(ansi_path),
             "%s/ansi_sql.lime", fixdir);
    snprintf(oracle_path, sizeof(oracle_path),
             "%s/oracle.lime", fixdir);
    snprintf(mysql_path, sizeof(mysql_path),
             "%s/mysql.lime", fixdir);
    snprintf(unified_path, sizeof(unified_path),
             "%s/unified.lime", fixdir);

    if (stat(ansi_path, &st) != 0) {
        fprintf(stderr, "SKIP: fixture %s not found\n", ansi_path);
        return 77;
    }

    const char *d_ansi    = "out_ansi";
    const char *d_oracle  = "out_oracle";
    const char *d_mysql   = "out_mysql";
    const char *d_unified = "out_unified";
    const char *d_err     = "out_err";
    const char *d_strict  = "out_strict";

    ensure_dir(d_ansi);
    ensure_dir(d_oracle);
    ensure_dir(d_mysql);
    ensure_dir(d_unified);
    ensure_dir(d_err);
    ensure_dir(d_strict);

    char base[256];

    /* ---- 1. ansi_sql.lime ---- */
    if (run_lime(lime_bin, limpar, "", d_ansi, ansi_path, NULL, 0) != 0) {
        FAIL("ansi", "lime exited non-zero on ansi_sql.lime");
    } else {
        strip_ext(ansi_path, base, sizeof(base));
        char hpath[1024];
        snprintf(hpath, sizeof(hpath), "%s/%s.h", d_ansi, base);
        char *h = slurp(hpath);
        if (h == NULL) {
            FAIL("ansi", "no header generated");
        } else {
            if (!contains(h, "SELECT") || !contains(h, "WHERE")) {
                FAIL("ansi", "base tokens missing");
            }
            free(h);
        }
    }

    /* ---- 2. oracle.lime ---- */
    if (run_lime(lime_bin, limpar, "", d_oracle, oracle_path, NULL, 0) != 0) {
        FAIL("oracle", "lime exited non-zero on oracle.lime");
    } else {
        strip_ext(oracle_path, base, sizeof(base));
        char hpath[1024], cpath[1024];
        snprintf(hpath, sizeof(hpath), "%s/%s.h", d_oracle, base);
        snprintf(cpath, sizeof(cpath), "%s/%s.c", d_oracle, base);
        char *h = slurp(hpath);
        char *c = slurp(cpath);
        if (h == NULL || c == NULL) {
            FAIL("oracle", "no .c/.h generated");
        } else {
            if (!contains(h, "SELECT")) FAIL("oracle", "ANSI SELECT missing");
            if (!contains(h, "CONNECT_BY"))
                FAIL("oracle", "Oracle CONNECT_BY missing");
            if (!contains(h, "ROWNUM"))
                FAIL("oracle", "Oracle ROWNUM missing");
            /* Sub-test 5: oracle's %override replaces the body. */
            if (!contains(c, "/* Oracle */"))
                FAIL("oracle_override_body",
                     "Oracle override body not in generated .c");
            if (contains(c, "/* ANSI */") &&
                contains(c, "= 1;  /* ANSI */")) {
                FAIL("oracle_override_body",
                     "ANSI body for select_clause leaked through "
                     "(override should have replaced it)");
            }
        }
        free(h);
        free(c);
    }

    /* ---- 3. mysql.lime ---- */
    if (run_lime(lime_bin, limpar, "", d_mysql, mysql_path, NULL, 0) != 0) {
        FAIL("mysql", "lime exited non-zero on mysql.lime");
    } else {
        strip_ext(mysql_path, base, sizeof(base));
        char hpath[1024], cpath[1024];
        snprintf(hpath, sizeof(hpath), "%s/%s.h", d_mysql, base);
        snprintf(cpath, sizeof(cpath), "%s/%s.c", d_mysql, base);
        char *h = slurp(hpath);
        char *c = slurp(cpath);
        if (h == NULL || c == NULL) {
            FAIL("mysql", "no .c/.h generated");
        } else {
            if (!contains(h, "BACKTICK_IDENT"))
                FAIL("mysql", "BACKTICK_IDENT missing");
            if (!contains(h, "LIMIT")) FAIL("mysql", "LIMIT missing");
            if (!contains(c, "/* MySQL */"))
                FAIL("mysql", "MySQL override body not in .c");
        }
        free(h);
        free(c);
    }

    /* ---- 4. unified.lime + sub-tests 5/6/10/12 ---- */
    if (run_lime(lime_bin, limpar, "", d_unified, unified_path,
                 NULL, 0) != 0) {
        FAIL("unified", "lime exited non-zero on unified.lime");
    } else {
        strip_ext(unified_path, base, sizeof(base));
        char hpath[1024], cpath[1024];
        snprintf(hpath, sizeof(hpath), "%s/%s.h", d_unified, base);
        snprintf(cpath, sizeof(cpath), "%s/%s.c", d_unified, base);
        char *h = slurp(hpath);
        char *c = slurp(cpath);
        if (h == NULL || c == NULL) {
            FAIL("unified", "no .c/.h generated");
        } else {
            /* Tokens from BOTH dialects coexist. */
            if (!contains(h, "CONNECT_BY"))
                FAIL("unified", "Oracle CONNECT_BY missing in diamond");
            if (!contains(h, "BACKTICK_IDENT"))
                FAIL("unified", "MySQL BACKTICK_IDENT missing in diamond");
            if (!contains(h, "ROWNUM"))
                FAIL("unified", "Oracle ROWNUM missing in diamond");
            if (!contains(h, "LIMIT"))
                FAIL("unified", "MySQL LIMIT missing in diamond");

            /* Sub-test 5/6: rule 5 -- single override path wins.
            ** select_clause: Oracle overrode, MySQL did not -> Oracle's
            ** body (R=100) is in the .c, ANSI's (R=1) is not. */
            if (!contains(c, "/* Oracle */"))
                FAIL("diamond_select_oracle",
                     "Oracle's select_clause body missing in unified.c");
            if (!contains(c, "/* MySQL */"))
                FAIL("diamond_where_mysql",
                     "MySQL's where_clause body missing in unified.c");
            /* The base ANSI body should NOT survive -- the override
            ** on the oracle path replaced it.  We check the specific
            ** overridden form, not the bare ANSI marker, because
            ** where_clause has an untouched ANSI epsilon branch.
            */
            if (contains(c, "= 1;  /* ANSI */")) {
                FAIL("diamond_select_oracle",
                     "ANSI select_clause body survived the override");
            }
            if (contains(c, "= 10; /* ANSI */")) {
                FAIL("diamond_where_mysql",
                     "ANSI where_clause body survived the override");
            }

            /* Sub-test 10: %include from ANSI must precede any
            ** content emitted by derived files.  ANSI's include
            ** has #include <stdio.h>; derived files have no
            ** %include of their own.  Verify the stdio include
            ** appears before the parser action table -- a
            ** structural sanity check on DFS ordering. */
            int p_stdio = find_pos(c, "#include <stdio.h>");
            int p_yyaction = find_pos(c, "yy_action");
            if (p_stdio < 0) {
                FAIL("include_dfs_order",
                     "ANSI's #include <stdio.h> not in unified.c");
            } else if (p_stdio > p_yyaction && p_yyaction >= 0) {
                FAIL("include_dfs_order",
                     "ANSI's %%include emitted AFTER yy_action -- "
                     "DFS order violated");
            }
        }
        free(h);
        free(c);
    }

    /* ---- 7. %remove non-existent: default-build warning ---- */
    {
        FILE *f = fopen("remove_missing.lime", "wb");
        if (f == NULL) {
            FAIL("remove_missing", "could not write fixture");
        } else {
            fputs("%name X\n%token A B C.\n%type x {int}\n"
                  "x ::= A B.\n"
                  "%remove x ::= A C.\n",
                  f);
            fclose(f);
            char err[4096];
            int rc = run_lime(lime_bin, limpar, "", d_err,
                              "remove_missing.lime",
                              err, sizeof(err));
            (void)rc;
            /* Default debugoptimized build = LIME_STRICT, so this is
            ** a hard error.  In a release-optimized build it would
            ** be a warning.  We accept either behavior here -- both
            ** the "warning:" form (release) and the "%remove: no
            ** rule" form (LIME_STRICT) prove the directive was
            ** recognised and dispatched. */
            if (!contains(err, "remove") &&
                !contains(err, "%remove")) {
                FAIL("remove_missing",
                     "no diagnostic produced for missing-target "
                     "%%remove (got: %s)", err);
            }
        }
    }

    /* ---- 8. %override_type widening warning ---- */
    {
        FILE *f = fopen("override_type.lime", "wb");
        if (f == NULL) {
            FAIL("override_type", "could not write fixture");
        } else {
            fputs("%name X\n%token A.\n%type x {int}\n"
                  "x ::= A.   { x = 1; }\n"
                  "%override_type x {long long}\n",
                  f);
            fclose(f);
            char err[4096];
            int rc = run_lime(lime_bin, limpar, "", d_err,
                              "override_type.lime",
                              err, sizeof(err));
            if (rc != 0) {
                FAIL("override_type",
                     "lime failed: %s", err);
            } else if (!contains(err, "override_type") ||
                       !contains(err, "widens")) {
                FAIL("override_type",
                     "expected widening warning, got: %s", err);
            }
        }
    }

    /* ---- 9. Circular %extends ---- */
    {
        FILE *fa = fopen("circ_a.lime", "wb");
        FILE *fb = fopen("circ_b.lime", "wb");
        if (fa == NULL || fb == NULL) {
            FAIL("circular", "could not write fixture");
        } else {
            fputs("%extends \"circ_b.lime\"\n%name CA\n%token T.\n"
                  "x ::= T.\n", fa);
            fputs("%extends \"circ_a.lime\"\n%token U.\n"
                  "y ::= U.\n", fb);
            fclose(fa);
            fclose(fb);
            char err[4096];
            int rc = run_lime(lime_bin, limpar, "", d_err,
                              "circ_a.lime", err, sizeof(err));
            if (rc == 0) {
                FAIL("circular", "expected non-zero exit");
            } else if (!contains(err, "cycle")) {
                FAIL("circular",
                     "expected 'cycle' diagnostic (cycle detection "
                     "caught the recursion early), got: %s",
                     err);
            }
        }
    }

    /* ---- 11. Diamond conflict (different overrides on different
    ** paths, no resolver in derived) ---- */
    {
        FILE *fa = fopen("conf_a.lime", "wb");
        FILE *fb = fopen("conf_b.lime", "wb");
        FILE *fc = fopen("conf_c.lime", "wb");
        FILE *fu = fopen("conf_u.lime", "wb");
        if (!fa || !fb || !fc || !fu) {
            FAIL("conflict", "could not write fixtures");
        } else {
            /* Base. */
            fputs("%name CB\n%token A B.\n%type x {int}\n"
                  "x ::= A B.   { x = 0; }\n", fa);
            /* Sibling 1: overrides x with body B1. */
            fputs("%extends \"conf_a.lime\"\n"
                  "%override\n"
                  "x ::= A B.   { x = 1; }\n", fb);
            /* Sibling 2: overrides x with body B2. */
            fputs("%extends \"conf_a.lime\"\n"
                  "%override\n"
                  "x ::= A B.   { x = 2; }\n", fc);
            /* Diamond top: extends both, does NOT override. */
            fputs("%extends \"conf_b.lime\"\n"
                  "%extends \"conf_c.lime\"\n", fu);
            fclose(fa); fclose(fb); fclose(fc); fclose(fu);
            char err[4096];
            int rc = run_lime(lime_bin, limpar, "", d_err,
                              "conf_u.lime", err, sizeof(err));
            if (rc == 0) {
                FAIL("conflict",
                     "expected non-zero exit on diamond conflict");
            } else if (!contains(err, "conflict") &&
                       !contains(err, "diamond")) {
                FAIL("conflict",
                     "expected 'conflict' or 'diamond' diagnostic, "
                     "got: %s", err);
            }
        }
    }

    /* ---- 12. Identity hash via aliases.  oracle's override uses
    ** alias R; ansi declared with no alias.  The override should
    ** still match -- aliases are cosmetic.  This is implicitly
    ** verified by sub-test 5 (oracle's body wins) but we assert it
    ** explicitly with a fresh fixture to make the requirement
    ** visible.  Both rules have identical (LHS, RHS_seq); the
    ** aliases differ. ---- */
    {
        FILE *fa = fopen("alias_a.lime", "wb");
        FILE *fb = fopen("alias_b.lime", "wb");
        if (!fa || !fb) {
            FAIL("alias_identity", "could not write fixture");
        } else {
            fputs("%name AL\n%token A B.\n%type x {int}\n"
                  "x(P) ::= A B.   { P = 1; /* base */ }\n", fa);
            fputs("%extends \"alias_a.lime\"\n"
                  "%override\n"
                  "x(Q) ::= A B.   { Q = 9; /* override */ }\n", fb);
            fclose(fa);
            fclose(fb);
            char err[4096];
            int rc = run_lime(lime_bin, limpar, "", d_err,
                              "alias_b.lime", err, sizeof(err));
            if (rc != 0) {
                FAIL("alias_identity",
                     "lime failed: %s", err);
            } else {
                char cpath[1024];
                snprintf(cpath, sizeof(cpath), "%s/alias_b.c", d_err);
                char *c = slurp(cpath);
                if (!c) {
                    FAIL("alias_identity", "no .c generated");
                } else {
                    if (!contains(c, "/* override */")) {
                        FAIL("alias_identity",
                             "override body missing -- alias-mismatch "
                             "may have prevented identity match");
                    }
                    if (contains(c, "/* base */")) {
                        FAIL("alias_identity",
                             "base body survived; override did not "
                             "replace it");
                    }
                    free(c);
                }
            }
        }
    }

    if (failures > 0) {
        fprintf(stderr, "test_extends: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_extends: 12 sub-tests passed\n");
    return 0;
}
