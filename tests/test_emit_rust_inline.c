/*
 * test_emit_rust_inline.c
 *
 * Verifies the Rust output emitter wires jit_can_inline_rule_text()
 * into the per-rule `fn yy_rule_N` reducer emission: action bodies
 * classified as inlinable (empty, passthrough, single small
 * expression with no calls or control flow) get
 * `#[inline(always)]` so rustc inlines them into the parse loop;
 * non-inlinable bodies (function calls, control flow) get nothing
 * and let rustc decide.
 *
 * Strategy: write a small grammar that mixes inlinable and
 * non-inlinable rules with carefully chosen action bodies, run
 * `lime --rust` on it, then grep the generated .rs file rule by
 * rule.
 *
 * Skipped at runtime when LIME_BIN isn't reachable.
 */

#include <assert.h>
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
#define SKIP(reason) do { \
    printf("  SKIP: %s\n", reason); skip_count++; \
} while (0)

static const char *g_lime_bin;
static const char *g_template_path;

static int subprocess_available(void) {
    g_lime_bin = getenv("LIME_BIN");
    return g_lime_bin && access(g_lime_bin, X_OK) == 0;
}

static int resolve_template(void) {
    static char buf[1024];
    const char *t = getenv("LIME_TEMPLATE");
    if (t && access(t, R_OK) == 0) { g_template_path = t; return 1; }
    const char *root = getenv("LIME_PROJECT_ROOT");
    if (root) {
        snprintf(buf, sizeof(buf), "%s/limpar.c", root);
        if (access(buf, R_OK) == 0) { g_template_path = buf; return 1; }
    }
    return 0;
}

static char *make_tmpdir(void) {
    static char tmp[256];
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    snprintf(tmp, sizeof(tmp), "%s/lime_rust_inline_XXXXXX", base);
    return mkdtemp(tmp);
}

static void rm_dir(const char *dir) {
    char cmd[512];
    /* Test scratch dir we created with mkdtemp; safe to remove. */
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    int rc = system(cmd);
    (void)rc;
}

static int write_file(const char *path, const char *contents) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    size_t n = strlen(contents);
    int ok = fwrite(contents, 1, n, fp) == n;
    fclose(fp);
    return ok ? 0 : -1;
}

static char *slurp(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    rewind(fp);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, fp);
    (void)rd;
    fclose(fp);
    buf[n] = 0;
    if (out_len) *out_len = (size_t)n;
    return buf;
}

static int run_lime_rust(const char *tmpdir, const char *file) {
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
        char tflag[1024];
        snprintf(tflag, sizeof(tflag), "-T%s", g_template_path);
        execlp(g_lime_bin, "lime", tflag, "--rust", file, (char *)NULL);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/*
 * Grammar layout: 5 rules with mixed action bodies plus 5 terminal
 * productions.  We avoid the lemon-quirk where a rule without an
 * explicit `{...}` body inherits the next rule's action; every
 * non-terminal rule below has an explicit body.  The terminal
 * productions (rules 5-9) deliberately have no body at all -- those
 * exercise the no_code path.
 *
 *   Rule 0: s ::= passthrough. { A = B; }       -- inlinable (passthrough)
 *   Rule 1: s ::= arith.       { A = B + 1; }   -- inlinable (small expr)
 *   Rule 2: s ::= fcall.       { A = malloc(B); }
 *                                                 -- NOT inlinable (call)
 *   Rule 3: s ::= ctrl.        { if (B > 0) { A = B; } else { A = 0; } }
 *                                                 -- NOT inlinable (if/braces)
 *   Rule 4: s ::= multi.       { A = B; A = A + 1; }
 *                                                 -- NOT inlinable (multi-stmt)
 *   Rules 5-9: terminal productions, no action body -- inlinable (no_code)
 */
static const char *G_RUST_INLINE_TEST =
    "%name IL\n"
    "%token A B C D E F.\n"
    "%first_token 257\n"
    "%start_symbol s\n"
    "s(A) ::= passthrough(B).      { A = B; }\n"
    "s(A) ::= arith(B).            { A = B + 1; }\n"
    "s(A) ::= fcall(B).            { A = malloc(B); }\n"
    "s(A) ::= ctrl(B).             { if (B > 0) { A = B; } else { A = 0; } }\n"
    "s(A) ::= multi(B).            { A = B; A = A + 1; }\n"
    "passthrough ::= A.\n"
    "arith       ::= B.\n"
    "fcall       ::= D.\n"
    "ctrl        ::= E.\n"
    "multi       ::= F.\n";

/*
 * Find `fn yy_rule_<N>(` and verify that the immediately preceding
 * non-blank line is or is not `#[inline(always)]`, depending on
 * `expect_inlined`.  Returns 1 on match, 0 otherwise.
 */
static int rule_has_inline_marker(const char *content, int rule_num) {
    char needle[64];
    snprintf(needle, sizeof(needle), "fn yy_rule_%d(", rule_num);
    const char *p = strstr(content, needle);
    if (!p) return -1;  /* rule not found at all */
    /* Walk backwards to the start of the previous non-blank line. */
    const char *cur = p;
    while (cur > content && *(cur - 1) != '\n') cur--;
    /* cur now points at start of `fn yy_rule_N(...` line. */
    if (cur == content) return 0;
    cur--;  /* '\n' before fn line */
    while (cur > content && *(cur - 1) != '\n') cur--;
    /* cur now points at start of the previous line. */
    return strncmp(cur, "#[inline(always)]", 17) == 0 ? 1 : 0;
}

static void test_rust_emit_inline(void) {
    TEST("--rust marks inlinable rule reducers #[inline(always)]");
    char *tmpdir = make_tmpdir();
    if (!tmpdir) { SKIP("mkdtemp failed"); return; }

    char gpath[512]; snprintf(gpath, sizeof(gpath), "%s/g.lime", tmpdir);
    if (write_file(gpath, G_RUST_INLINE_TEST) != 0) {
        rm_dir(tmpdir); ASSERT(0, "write grammar failed");
    }

    int rc = run_lime_rust(tmpdir, "g.lime");
    if (rc != 0) {
        rm_dir(tmpdir);
        fprintf(stderr, "  lime --rust exited %d\n", rc);
        ASSERT(0, "lime --rust failed");
    }

    char rspath[512]; snprintf(rspath, sizeof(rspath), "%s/g.rs", tmpdir);
    size_t n;
    char *content = slurp(rspath, &n);
    if (!content) {
        rm_dir(tmpdir);
        ASSERT(0, "g.rs not produced");
    }

    /* Rule 0: passthrough `A = B;` -- inlinable. */
    int r0 = rule_has_inline_marker(content, 0);
    /* Rule 1: arithmetic `A = B + 1;` -- inlinable. */
    int r1 = rule_has_inline_marker(content, 1);
    /* Rule 2: function call `A = malloc(B);` -- NOT inlinable. */
    int r2 = rule_has_inline_marker(content, 2);
    /* Rule 3: control flow `if (...) ... else ...` -- NOT inlinable. */
    int r3 = rule_has_inline_marker(content, 3);
    /* Rule 4: multi-statement `A = B; A = A + 1;` -- NOT inlinable. */
    int r4 = rule_has_inline_marker(content, 4);
    /* Rule 5+: terminal productions with no action body -- inlinable. */
    int r5 = rule_has_inline_marker(content, 5);
    int r6 = rule_has_inline_marker(content, 6);

    /* Sanity: count total `fn yy_rule_` callbacks and total
    ** `#[inline(always)]` markers that *immediately precede* a
    ** `fn yy_rule_N` line.  The emit_parser_runtime path also
    ** decorates `top_state` / `find_shift_action` / `find_goto`
    ** with `#[inline(always)]`, so a global count would overcount
    ** for our purposes -- only the per-rule reducers are governed
    ** by jit_can_inline_rule_text. */
    int total_rules = 0;
    int total_inline = 0;
    {
        const char *q = content;
        while ((q = strstr(q, "fn yy_rule_")) != NULL) {
            total_rules++;
            int n = -1;
            if (sscanf(q, "fn yy_rule_%d", &n) == 1 && n >= 0) {
                if (rule_has_inline_marker(content, n) == 1) {
                    total_inline++;
                }
            }
            q += 11;
        }
    }

    free(content);
    rm_dir(tmpdir);

    /* Hard assertions on the five explicit rules + two terminals. */
    ASSERT(r0 == 1, "rule 0 (passthrough) should be #[inline(always)]");
    ASSERT(r1 == 1, "rule 1 (arithmetic A=B+1) should be #[inline(always)]");
    ASSERT(r2 == 0, "rule 2 (malloc call) should NOT be #[inline(always)]");
    ASSERT(r3 == 0, "rule 3 (if/else control flow) should NOT be #[inline(always)]");
    ASSERT(r4 == 0, "rule 4 (multi-statement body) should NOT be #[inline(always)]");
    ASSERT(r5 == 1, "rule 5 (terminal, no body) should be #[inline(always)]");
    ASSERT(r6 == 1, "rule 6 (terminal, no body) should be #[inline(always)]");

    /* Distribution sanity: at least one rule did NOT get inlined,
    ** at least one rule DID, and totals are consistent. */
    ASSERT(total_rules >= 5, "expected at least 5 yy_rule_N callbacks");
    ASSERT(total_inline >= 1, "expected at least 1 inlined rule");
    ASSERT(total_inline < total_rules,
           "expected at least one rule NOT inlined "
           "(classifier was too permissive?)");

    printf("  rules=%d  inline(always)=%d\n", total_rules, total_inline);
    PASS();
}

int main(void) {
    printf("=== test_emit_rust_inline ===\n");
    if (!subprocess_available()) {
        printf("SKIP: LIME_BIN unset / unreachable\n");
        return 77;
    }
    if (!resolve_template()) {
        printf("SKIP: limpar.c template not found\n");
        return 77;
    }
    test_rust_emit_inline();

    int effective = test_count - skip_count;
    printf("\n=== Results: %d/%d passed (%d skipped) ===\n",
           pass_count, effective, skip_count);
    return (pass_count == effective) ? 0 : 1;
}
