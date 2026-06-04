/*
 * test_rust_target_v0_12.c
 *
 * Regression tests for the three issues reported in
 * /tmp/lime-rust-target-repro/README.md (Ra crates/ra-parser):
 *
 *   Issue 1: multi-line action bodies were silently DROPPED on the
 *            Rust target (--target=rust).  The empty-check at
 *            emit_rust.c:~305 short-circuited on `code[0] == '\n'`,
 *            which is exactly the byte the body starts with when
 *            the rule uses the conventional
 *
 *                stmt(A) ::= ... . {
 *                    body;
 *                }
 *
 *            layout.  Fixed in v0.12 by walking past leading
 *            whitespace and only treating the body as empty when
 *            no non-whitespace remains.
 *
 *   Issue 2: no per-target action selection.  Pre-v0.12 only the
 *            existing `%rust_action` directive offered a Rust-only
 *            override.  v0.12 adds the symmetric pair
 *            `%action_c { ... }` / `%action_rust { ... }` (the
 *            latter is an alias for `%rust_action`); when both are
 *            present, the C target uses the C body and the Rust
 *            target uses the Rust body.
 *
 *   Issue 3: `--target=rust` ran the C-emit pipeline unconditionally
 *            after the Rust emit, which (a) tried to open the
 *            historic-Lemon-named `lempar.c` template (Lime ships
 *            `limpar.c`) and (b) bumped errorcnt, making the whole
 *            run exit non-zero even though the .rs WAS produced.
 *            Fixed in v0.12 by skipping ReportTable/ReportHeader/
 *            ReportAOTTable when the requested target is Rust-only.
 *
 * Skipped at runtime when LIME_BIN isn't reachable.
 */

#include "test_compat.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_total = 0;
static int g_pass  = 0;

#define CHECK(cond, fmt, ...) do {                                      \
    g_total++;                                                          \
    if (cond) { g_pass++; printf("  ok  " fmt "\n", ##__VA_ARGS__); }   \
    else { printf("  FAIL " fmt " (at %s:%d)\n", ##__VA_ARGS__,         \
                  __FILE__, __LINE__); }                                \
} while (0)

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int write_file(const char *path, const char *contents) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    size_t n = strlen(contents);
    int ok = fwrite(contents, 1, n, fp) == n;
    if (fclose(fp) != 0) ok = 0;
    return ok ? 0 : -1;
}

static char *slurp(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    rewind(fp);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, fp);
    (void)rd;
    if (fclose(fp) != 0) { free(buf); return NULL; }
    buf[n] = 0;
    return buf;
}

/* ------- grammars under test ------- */

/* Issue 1: two rules differing only in action-body layout.  The
** multi-line body MUST appear in yy_rule_0; the single-line body
** in yy_rule_1. */
static const char *kIssue1Grammar =
    "%name calc1\n"
    "%token_type {i64}\n"
    "%default_type {i64}\n"
    "%token NUM PLUS.\n"
    "stmt(A) ::= NUM(B) PLUS NUM(C). {\n"
    "    A = B + C;\n"
    "}\n"
    "stmt(A) ::= NUM(B). { A = B; }\n";

/* Issue 2: %action_c / %action_rust per-rule.  C target sees the
** C body, Rust target sees the Rust body, neither sees the other. */
static const char *kIssue2Grammar =
    "%name calc2\n"
    "%token_type {i64}\n"
    "%default_type {i64}\n"
    "%token NUM PLUS.\n"
    "stmt(A) ::= NUM(B) PLUS NUM(C).\n"
    "    %action_c    { A = c_only_marker(B, C); }\n"
    "    %action_rust { A = rust_only_marker(B, C); }\n"
    "stmt(A) ::= NUM(B). { A = B; }\n";

/* ------- helpers ------- */

static int run_capture(char *const argv[], char *errbuf, size_t errlen,
                       int *exit_code) {
    return test_compat_run_capture_stderr(argv, errbuf, errlen, exit_code);
}

int main(int argc, char **argv) {
    const char *lime_bin = (argc > 1) ? argv[1] : getenv("LIME_BIN");
    if (lime_bin == NULL || lime_bin[0] == 0) {
        fprintf(stderr, "SKIP: no LIME_BIN\n");
        return 77;
    }
    /* Resolve template the same way the other rust tests do. */
    const char *tmpl = getenv("LIME_TEMPLATE");
    char tmpl_buf[1024];
    if (tmpl == NULL || access(tmpl, R_OK) != 0) {
        const char *root = getenv("LIME_PROJECT_ROOT");
        if (root) {
            snprintf(tmpl_buf, sizeof(tmpl_buf), "%s/limpar.c", root);
            if (access(tmpl_buf, R_OK) == 0) tmpl = tmpl_buf;
        }
    }
    /* Note: tmpl is allowed to be NULL.  The whole point of Issue 3
    ** is that --target=rust no longer NEEDS the C template. */

    char tmpdir[256];
    if (test_compat_tmpdir("lime_rust_v012", tmpdir, sizeof(tmpdir)) != 0) {
        fprintf(stderr, "FAIL: tmpdir\n");
        return 1;
    }
    if (chdir(tmpdir) != 0) { perror("chdir"); return 1; }

    char errbuf[4096];
    int rc = 0;

    /* === Issue 3: --target=rust on a vanilla grammar exits 0 and
    ** does NOT emit C-side artefacts (no .c, no .h, no template
    ** complaint on stderr). === */
    {
        if (write_file("issue1.lime", kIssue1Grammar) != 0) {
            fprintf(stderr, "FAIL: write issue1.lime\n"); return 1;
        }
        char *cargv[] = { (char *)lime_bin, "--target=rust",
                          "issue1.lime", NULL };
        run_capture(cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0,
              "Issue 3: --target=rust exits 0 (got rc=%d, stderr=%.200s)",
              rc, errbuf);
        CHECK(file_exists("issue1.rs"),
              "Issue 3: --target=rust produces issue1.rs");
        CHECK(!file_exists("issue1.c"),
              "Issue 3: --target=rust does NOT emit issue1.c");
        CHECK(!file_exists("issue1.h"),
              "Issue 3: --target=rust does NOT emit issue1.h");
        CHECK(strstr(errbuf, "Can't open the template") == NULL
              && strstr(errbuf, "Can't find the parser driver template") == NULL,
              "Issue 3: --target=rust does NOT complain about C template");
    }

    /* === Issue 1: multi-line action body is preserved.  The
    ** generated yy_rule_0 must contain the body text `A = B + C;`
    ** and must NOT carry the `// empty action` marker. === */
    {
        char *rs = slurp("issue1.rs");
        CHECK(rs != NULL, "Issue 1: slurp issue1.rs");
        if (rs) {
            const char *r0 = strstr(rs, "fn yy_rule_0");
            const char *r1 = r0 ? strstr(r0 + 1, "fn yy_rule_") : NULL;
            CHECK(r0 != NULL, "Issue 1: yy_rule_0 present in output");
            if (r0 && r1) {
                /* Restrict the search to the bytes of yy_rule_0. */
                size_t span = (size_t)(r1 - r0);
                char *body = malloc(span + 1);
                memcpy(body, r0, span);
                body[span] = 0;
                CHECK(strstr(body, "A = B + C") != NULL,
                      "Issue 1: yy_rule_0 carries the multi-line body");
                CHECK(strstr(body, "// empty action") == NULL,
                      "Issue 1: yy_rule_0 does NOT carry // empty action");
                free(body);
            }
            free(rs);
        }
    }

    /* === Issue 2: %action_c + %action_rust dispatch per target. === */
    {
        if (write_file("issue2.lime", kIssue2Grammar) != 0) {
            fprintf(stderr, "FAIL: write issue2.lime\n"); return 1;
        }

        /* Rust target: must see rust_only_marker, must NOT see
        ** c_only_marker. */
        unlink("issue2.rs");
        char *cargv_rust[] = { (char *)lime_bin, "--target=rust",
                               "issue2.lime", NULL };
        run_capture(cargv_rust, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0,
              "Issue 2 (rust): exits 0 (got rc=%d)", rc);
        char *rs = slurp("issue2.rs");
        CHECK(rs != NULL, "Issue 2 (rust): slurp issue2.rs");
        if (rs) {
            CHECK(strstr(rs, "rust_only_marker") != NULL,
                  "Issue 2 (rust): %%action_rust body present");
            CHECK(strstr(rs, "c_only_marker") == NULL,
                  "Issue 2 (rust): %%action_c body NOT present");
            free(rs);
        }

        /* C target: must see c_only_marker, must NOT see
        ** rust_only_marker.  Skip if no template. */
        if (tmpl != NULL) {
            unlink("issue2.c"); unlink("issue2.h"); unlink("issue2.out");
            char tflag[1024];
            snprintf(tflag, sizeof(tflag), "-T%s", tmpl);
            char *cargv_c[] = { (char *)lime_bin, tflag, "-q",
                                "issue2.lime", NULL };
            run_capture(cargv_c, errbuf, sizeof(errbuf), &rc);
            CHECK(rc == 0,
                  "Issue 2 (c): exits 0 (got rc=%d)", rc);
            char *cs = slurp("issue2.c");
            CHECK(cs != NULL, "Issue 2 (c): slurp issue2.c");
            if (cs) {
                CHECK(strstr(cs, "c_only_marker") != NULL,
                      "Issue 2 (c): %%action_c body present");
                CHECK(strstr(cs, "rust_only_marker") == NULL,
                      "Issue 2 (c): %%action_rust body NOT present");
                free(cs);
            }
        } else {
            printf("  skip Issue 2 (c) -- no LIME_TEMPLATE / LIME_PROJECT_ROOT\n");
        }
    }

    /* Cleanup */
    unlink("issue1.lime"); unlink("issue1.rs");
    unlink("issue2.lime"); unlink("issue2.rs");
    unlink("issue2.c");    unlink("issue2.h"); unlink("issue2.out");
    test_compat_chdir_temp();
    test_compat_rmdir_recursive(tmpdir);

    printf("\n=== Results: %d/%d passed ===\n", g_pass, g_total);
    return (g_pass == g_total) ? 0 : 1;
}
