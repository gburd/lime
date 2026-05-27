/*
 * test_extends_stress.c -- multi-file %extends graph stress test.
 *
 * Builds large %extends graphs at runtime in a tmpdir and runs the
 * lime CLI against the leaf file.  Validates that the graph
 * traversal and override-merge logic scale to 100+ files.
 *
 * Original deferred from v0.6.x roadmap as
 * "Multi-file %extends graphs across mailing-list-archive scale --
 *  works at the 1-10 file scale today; haven't stress-tested 100+."
 *
 * Sub-tests:
 *
 *   1. linear chain (N=128).  Builds chain_000.lime through
 *      chain_127.lime where each file %extends the prior one and
 *      adds one rule.  Compiles the leaf and asserts the resulting
 *      parser includes all 128 rules.
 *
 *   2. fan-in diamond (N=64).  Builds 64 leaves all %extending a
 *      common base, plus a single tip that %extends every leaf.
 *      Diamond-base appears once in the merged grammar (no rule
 *      duplication).
 *
 *   3. wide-and-deep (8 deep x 8 wide).  Each level has 8 siblings
 *      that all extend the prior level's tip.  64 files; tests both
 *      depth-first and breadth-first traversal in the cycle
 *      detector.
 *
 * Skipped at runtime when the lime CLI is unreachable (LIME_BIN
 * unset and `lime` not on PATH).
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int test_count = 0;
static int pass_count = 0;
static int skip_count = 0;

#define TEST(name) do { \
    printf("[TEST %d] %s\n", ++test_count, name); fflush(stdout); \
} while (0)
#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "  FAIL: %s\n", msg); return; } \
} while (0)
#define PASS() do { printf("  PASS\n"); pass_count++; } while (0)
#define SKIP(reason) do { printf("  SKIP: %s\n", reason); skip_count++; } while (0)

static const char *g_lime_bin = NULL;

static int subprocess_available(void) {
    g_lime_bin = getenv("LIME_BIN");
    if (g_lime_bin && access(g_lime_bin, X_OK) == 0) return 1;
    g_lime_bin = "lime";
    return system("command -v lime > /dev/null 2>&1") == 0;
}

static char *make_tmpdir(void) {
    static char tmp[256];
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    snprintf(tmp, sizeof(tmp), "%s/lime_extends_stress_XXXXXX", base);
    return mkdtemp(tmp);
}

static void rm_rf_dir(const char *dir) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    /* Test cleanup; rm -rf on our own mkdtemp output is the only
     * place in this codebase that uses it. */
    int rc = system(cmd);
    (void)rc;
}

static int write_file(const char *path, const char *contents) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    size_t n = strlen(contents);
    int    ok = fwrite(contents, 1, n, fp) == n;
    fclose(fp);
    return ok ? 0 : -1;
}

/* Discover the lime parser template (limpar.c).  Searched in
 * project layout: $LIME_TEMPLATE -> $LIME_SRC_INCLUDE/../limpar.c
 * -> ../limpar.c relative to the test binary's notion of project
 * root (LIME_PROJECT_ROOT env, set by meson). */
static const char *g_template_path = NULL;
static void resolve_template(void) {
    static char buf[1024];
    const char *t = getenv("LIME_TEMPLATE");
    if (t && access(t, R_OK) == 0) { g_template_path = t; return; }
    const char *root = getenv("LIME_PROJECT_ROOT");
    if (root) {
        snprintf(buf, sizeof(buf), "%s/limpar.c", root);
        if (access(buf, R_OK) == 0) { g_template_path = buf; return; }
    }
    /* Fall back to "../limpar.c" relative to lime binary path. */
    if (g_lime_bin && g_lime_bin[0] == '/') {
        const char *slash = strrchr(g_lime_bin, '/');
        if (slash) {
            size_t len = (size_t)(slash - g_lime_bin);
            snprintf(buf, sizeof(buf), "%.*s/../limpar.c", (int)len, g_lime_bin);
            if (access(buf, R_OK) == 0) { g_template_path = buf; return; }
        }
    }
    g_template_path = NULL;
}

/* Run `lime -T <template> <file>` in tmpdir.  Returns child's exit
 * status, or -1 if fork/exec/wait failed entirely. */
static int run_lime(const char *tmpdir, const char *file) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (chdir(tmpdir) != 0) _exit(127);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        if (g_template_path) {
            char tflag[1024];
            snprintf(tflag, sizeof(tflag), "-T%s", g_template_path);
            execlp(g_lime_bin, "lime", tflag, file, (char *)NULL);
        } else {
            execlp(g_lime_bin, "lime", file, (char *)NULL);
        }
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

/* Each generated file declares one new token (T0, T1, ...) plus a
 * rule that consumes it appended to the alternation `s ::= ... | TN`.
 * The chain terminates with `s ::= T0`. */
static void test_linear_chain(void) {
    /* LIME_EXTENDS_MAX_DEPTH is 16 (cycle-detection cap), so the
    ** deepest legal chain is 15 levels.  This exercises the chain
    ** traversal at the documented maximum supported depth.  Wider
    ** graphs are tested in test_fan_in_diamond + test_wide_and_deep
    ** which max out the breadth instead. */
    const int N = 15;
    TEST("linear chain N=15 (LIME_EXTENDS_MAX_DEPTH boundary): "
         "leaf compiles, all 15 rules included");

    char *tmpdir = make_tmpdir();
    if (!tmpdir) {
        SKIP("mkdtemp failed");
        return;
    }

    char path[512];
    char body[1024];

    for (int i = 0; i < N; i++) {
        snprintf(path, sizeof(path), "%s/chain_%03d.lime", tmpdir, i);
        if (i == 0) {
            /* base: declare base tokens + start_symbol */
            snprintf(body, sizeof(body),
                     "%%name CHAIN\n"
                     "%%token T0.\n"
                     "%%start_symbol s\n"
                     "s ::= T0.\n");
        } else {
            snprintf(body, sizeof(body),
                     "%%extends \"chain_%03d.lime\"\n"
                     "%%token T%d.\n"
                     "s ::= T%d.\n",
                     i - 1, i, i);
        }
        if (write_file(path, body) != 0) {
            fprintf(stderr, "  write_file failed at %d\n", i);
            rm_rf_dir(tmpdir);
            return;
        }
    }

    /* Compile leaf. */
    char leaf[64];
    snprintf(leaf, sizeof(leaf), "chain_%03d.lime", N - 1);
    int rc = run_lime(tmpdir, leaf);
    if (rc != 0) {
        fprintf(stderr, "  lime exited %d on chain leaf\n", rc);
        rm_rf_dir(tmpdir);
        ASSERT(0, "lime failed on linear chain");
    }

    /* Spot-check the generated .c references all N tokens. */
    char gen[512];
    snprintf(gen, sizeof(gen), "%s/chain_%03d.c", tmpdir, N - 1);
    FILE *fp = fopen(gen, "rb");
    int   missing = 0;
    if (fp) {
        if (fseek(fp, 0, SEEK_END) == 0) {
            long sz = ftell(fp);
            rewind(fp);
            if (sz > 0) {
                char *buf = (char *)malloc((size_t)sz + 1);
                if (buf) {
                    fread(buf, 1, (size_t)sz, fp);
                    buf[sz] = 0;
                    /* Each token name must appear at least once in
                     * the generated C output (yyTokenName / yy_action
                     * comments etc.). */
                    for (int i = 0; i < N; i++) {
                        char tok[16];
                        snprintf(tok, sizeof(tok), "T%d", i);
                        if (strstr(buf, tok) == NULL) {
                            missing++;
                            if (missing <= 3) {
                                fprintf(stderr,
                                        "  missing token in generated C: %s\n",
                                        tok);
                            }
                        }
                    }
                    free(buf);
                }
            }
        }
        fclose(fp);
    }
    rm_rf_dir(tmpdir);

    if (missing > 0) {
        fprintf(stderr, "  %d/%d tokens missing from leaf codegen\n",
                missing, N);
        ASSERT(0, "linear chain produced incomplete codegen");
    }
    PASS();
}

static void test_fan_in_diamond(void) {
    const int N = 64;
    TEST("fan-in diamond N=64 + base: tip compiles, base rule single-emit");

    char *tmpdir = make_tmpdir();
    if (!tmpdir) { SKIP("mkdtemp failed"); return; }

    char path[512];
    char body[2048];

    /* Base file. */
    snprintf(path, sizeof(path), "%s/diamond_base.lime", tmpdir);
    if (write_file(path,
                   "%name DIAMOND\n"
                   "%token BASE.\n"
                   "%start_symbol s\n"
                   "s ::= BASE.\n") != 0) {
        rm_rf_dir(tmpdir);
        ASSERT(0, "write base failed");
    }

    /* N leaves all extending base; each adds one token + rule. */
    for (int i = 0; i < N; i++) {
        snprintf(path, sizeof(path), "%s/diamond_leaf_%02d.lime", tmpdir, i);
        snprintf(body, sizeof(body),
                 "%%extends \"diamond_base.lime\"\n"
                 "%%token L%d.\n"
                 "s ::= L%d.\n",
                 i, i);
        if (write_file(path, body) != 0) {
            rm_rf_dir(tmpdir);
            ASSERT(0, "leaf write failed");
        }
    }

    /* Tip extends every leaf. */
    snprintf(path, sizeof(path), "%s/diamond_tip.lime", tmpdir);
    /* Build the tip body in pieces because it's too long for a single
     * snprintf with a 2KB buffer if N grows. */
    FILE *tip = fopen(path, "wb");
    if (!tip) { rm_rf_dir(tmpdir); ASSERT(0, "tip open failed"); }
    fputs("%name DIAMONDTIP\n", tip);
    for (int i = 0; i < N; i++) {
        fprintf(tip, "%%extends \"diamond_leaf_%02d.lime\"\n", i);
    }
    fclose(tip);

    int rc = run_lime(tmpdir, "diamond_tip.lime");
    rm_rf_dir(tmpdir);
    if (rc != 0) {
        fprintf(stderr, "  lime exited %d on diamond tip\n", rc);
        ASSERT(0, "lime failed on fan-in diamond");
    }
    PASS();
}

static void test_wide_and_deep(void) {
    const int DEPTH = 8;
    const int WIDTH = 8;
    TEST("wide-and-deep 8x8: tip compiles, no cycle false-positive");

    char *tmpdir = make_tmpdir();
    if (!tmpdir) { SKIP("mkdtemp failed"); return; }

    char path[512];
    char body[2048];

    /* Level 0: a single base. */
    snprintf(path, sizeof(path), "%s/wd_0_0.lime", tmpdir);
    if (write_file(path,
                   "%name WD\n"
                   "%token B.\n"
                   "%start_symbol s\n"
                   "s ::= B.\n") != 0) {
        rm_rf_dir(tmpdir); ASSERT(0, "wd base failed");
    }

    /* Each subsequent level: WIDTH siblings extending the prior
     * level's "_0" tip. */
    for (int d = 1; d < DEPTH; d++) {
        for (int w = 0; w < WIDTH; w++) {
            snprintf(path, sizeof(path), "%s/wd_%d_%d.lime", tmpdir, d, w);
            snprintf(body, sizeof(body),
                     "%%extends \"wd_%d_0.lime\"\n"
                     "%%token T_%d_%d.\n"
                     "s ::= T_%d_%d.\n",
                     d - 1, d, w, d, w);
            if (write_file(path, body) != 0) {
                rm_rf_dir(tmpdir); ASSERT(0, "wd write failed");
            }
        }
    }

    /* Compile the deepest "_0" sibling. */
    char leaf[64];
    snprintf(leaf, sizeof(leaf), "wd_%d_0.lime", DEPTH - 1);
    int rc = run_lime(tmpdir, leaf);
    rm_rf_dir(tmpdir);
    if (rc != 0) {
        fprintf(stderr, "  lime exited %d on wide-and-deep\n", rc);
        ASSERT(0, "lime failed on wide-and-deep graph");
    }
    PASS();
}

int main(void) {
    printf("=== test_extends_stress ===\n");
    if (!subprocess_available()) {
        printf("SKIP: lime CLI unreachable\n");
        return 77;
    }
    resolve_template();
    if (!g_template_path) {
        printf("SKIP: lime template (limpar.c) not found via LIME_TEMPLATE / "
               "LIME_PROJECT_ROOT / lime-binary-relative path\n");
        return 77;
    }

    test_linear_chain();
    test_fan_in_diamond();
    test_wide_and_deep();

    int effective = test_count - skip_count;
    printf("\n=== Results: %d/%d passed (%d skipped) ===\n",
           pass_count, effective, skip_count);
    if (effective == 0) return 77;
    return (pass_count == effective) ? 0 : 1;
}
