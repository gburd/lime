/*
 * test_emit_rust_cargo.c
 *
 * v0.8 feat/rust-output stage-6 regression: drives the
 * examples/rust_calc/ end-to-end pipeline.
 *
 *   1. Run `lime --rust examples/rust_calc/calc.lime` to
 *      regenerate src/parser.rs from the .lime grammar.
 *   2. Run `cargo test --manifest-path examples/rust_calc/Cargo.toml`
 *      to build + run the example's unit tests.
 *
 * Skipped at runtime when LIME_BIN, cargo, or rustc is unavailable.
 *
 * The example's tests verify concrete arithmetic results (1+2=3,
 * 1+2*3=7, 10-3-2=5, syntax errors, bad chars), which is the
 * functional equivalent of "the Rust output produces the same
 * parse decisions as the C output" -- the C output of the same
 * grammar gives the same expression-evaluation results when run
 * through the C bench.
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
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
#define SKIP(reason) do { \
    printf("  SKIP: %s\n", reason); skip_count++; \
} while (0)

static const char *g_lime_bin;
static const char *g_template;
static const char *g_project_root;

static int tool_available(const char *cmd) {
    char buf[256];
    snprintf(buf, sizeof(buf), "command -v %s > /dev/null 2>&1", cmd);
    return system(buf) == 0;
}

static int subprocess_available(void) {
    g_lime_bin = getenv("LIME_BIN");
    if (!g_lime_bin || access(g_lime_bin, X_OK) != 0) return 0;
    return tool_available("cargo") && tool_available("rustc");
}

static int resolve_template(void) {
    static char buf[1024];
    g_project_root = getenv("LIME_PROJECT_ROOT");
    if (!g_project_root) return 0;
    snprintf(buf, sizeof(buf), "%s/limpar.c", g_project_root);
    if (access(buf, R_OK) != 0) return 0;
    g_template = buf;
    return 1;
}

static int run_silent(const char *const *argv, const char *cwd) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (cwd && chdir(cwd) != 0) _exit(127);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void test_regenerate_and_test(void) {
    TEST("regenerate parser.rs and run cargo test");

    char calc_lime[1024];
    snprintf(calc_lime, sizeof(calc_lime),
             "%s/examples/rust_calc/calc.lime", g_project_root);
    char calc_rs[1024];
    snprintf(calc_rs, sizeof(calc_rs),
             "%s/examples/rust_calc/calc.rs", g_project_root);
    char target_rs[1024];
    snprintf(target_rs, sizeof(target_rs),
             "%s/examples/rust_calc/src/parser.rs", g_project_root);
    char tflag[1024];
    snprintf(tflag, sizeof(tflag), "-T%s", g_template);

    /* Step 1: lime --rust on the grammar */
    const char *lime_argv[] = {
        g_lime_bin, tflag, "--rust", calc_lime, NULL
    };
    int rc = run_silent(lime_argv, g_project_root);
    ASSERT(rc == 0, "lime --rust failed");

    /* Move calc.rs to src/parser.rs */
    if (rename(calc_rs, target_rs) != 0) {
        fprintf(stderr, "  rename %s -> %s failed: %s\n",
                calc_rs, target_rs, strerror(errno));
        ASSERT(0, "could not stage parser.rs");
    }

    /* Step 2: cargo test in the example dir */
    char manifest[1024];
    snprintf(manifest, sizeof(manifest),
             "%s/examples/rust_calc/Cargo.toml", g_project_root);
    char target_dir[1024];
    snprintf(target_dir, sizeof(target_dir),
             "%s/examples/rust_calc/target", g_project_root);
    /* Use a worktree-local target dir so test runs don't clash. */
    setenv("CARGO_TARGET_DIR", target_dir, 1);
    const char *cargo_argv[] = {
        "cargo", "test", "--lib",
        "--manifest-path", manifest,
        "--quiet",
        NULL
    };
    rc = run_silent(cargo_argv, g_project_root);
    ASSERT(rc == 0, "cargo test failed");
    PASS();
}

int main(void) {
    printf("=== test_emit_rust_cargo ===\n");
    if (!subprocess_available()) {
        printf("SKIP: LIME_BIN / cargo / rustc unavailable\n");
        return 77;
    }
    if (!resolve_template()) {
        printf("SKIP: LIME_PROJECT_ROOT unset or limpar.c not found\n");
        return 77;
    }

    test_regenerate_and_test();

    int effective = test_count - skip_count;
    printf("\n=== Results: %d/%d passed (%d skipped) ===\n",
           pass_count, effective, skip_count);
    return (pass_count == effective) ? 0 : 1;
}
