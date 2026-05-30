/*
 * test_formatter_idempotence.c -- regression fixture asserting that
 * `lime -F` is single-pass idempotent: format(format(F)) == format(F)
 * byte-for-byte across every formatter fixture.
 *
 * Background.  The PostgreSQL migration team flagged a "2-pass
 * workaround" against early v0.3.x formatter behaviour, where rules
 * carrying explicit [PRECSYM] markers occasionally drifted on the
 * second format pass because format_grammar() ran before
 * FindRulePrecedences() and the marker was sometimes re-attributed
 * after a parse round-trip.  Letter-22 (v0.3.5 -> v0.4.0) and
 * Letter-23 (v0.5.2) closed every individual case; the v0.6.0
 * directive-emit registry made the structural cure permanent.
 *
 * This test locks that property in a single executable so any future
 * regression -- in any directive's emit path, in any precedence
 * variant, in any of the formatter's body/header/lexer categories --
 * fails CI loudly.
 *
 * Method.  For each .lime fixture, run `lime -F fix.lime` (which
 * writes fix.lime.formatted), copy the output to a fresh path,
 * format it again, and assert the second-pass output is byte-equal
 * to the first.  The comparison uses memcmp on the entire file,
 * not just a normalised whitespace-equal -- the contract is byte
 * identity.
 *
 * Usage.  Driven by tests/meson.build with two arguments:
 *   argv[1]  -- path to the lime CLI binary
 *   argv[2]  -- path to the project source root (so we can locate
 *               the .lime fixtures under tests/)
 */

#include <assert.h>
#include <errno.h>
#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char *fixtures[] = {
    "tests/test_formatter_grammar.lime",
    "tests/test_formatter_comments_grammar.lime",
    "tests/test_formatter_precedence_grammar.lime",
    "tests/test_formatter_prec_comments_grammar.lime",
    "tests/test_formatter_loc_directives_grammar.lime",
    "tests/test_formatter_token_groups_grammar.lime",
    "tests/test_formatter_labels_grammar.lime",
};
static const size_t n_fixtures = sizeof(fixtures) / sizeof(fixtures[0]);

/* Slurp a file into a freshly-malloc'd buffer.  Caller frees. */
static char *slurp(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long n = ftell(fp);
    if (n < 0) { fclose(fp); return NULL; }
    rewind(fp);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    if (got != (size_t)n) { free(buf); return NULL; }
    buf[n] = 0;
    if (out_len) *out_len = (size_t)n;
    return buf;
}

/* Copy src -> dst by reading + writing.  Returns 0 on success. */
static int copy_file(const char *src, const char *dst) {
    size_t n;
    char *buf = slurp(src, &n);
    if (!buf) return -1;
    FILE *fp = fopen(dst, "wb");
    if (!fp) { free(buf); return -1; }
    size_t put = fwrite(buf, 1, n, fp);
    int ok = (put == n);
    fclose(fp);
    free(buf);
    return ok ? 0 : -1;
}

/* Run `lime -F <path>` and return 0 on success.  The CLI writes
 * its output to <path>.formatted; we don't redirect stdout. */
static int run_format(const char *lime_exe, const char *path) {
    char cmd[8192];
    int n = snprintf(cmd, sizeof(cmd), "%s -F %s > /dev/null 2>&1",
                     lime_exe, path);
    if (n <= 0 || n >= (int)sizeof(cmd)) return -1;
    return system(cmd);
}

int main(int argc, char **argv) {
    char g_tmpdir[256];
    if (test_compat_tmpdir("lime_idem", g_tmpdir, sizeof(g_tmpdir)) != 0) {
        return 1;
    }

    if (argc < 3) {
        fprintf(stderr, "usage: %s <lime_exe> <project_source_root>\n",
                argv[0]);
        return 2;
    }
    const char *lime_exe = argv[1];
    const char *src_root = argv[2];

    char path1[4096], path1_out[4096];
    char path2[4096], path2_out[4096];

    int failed = 0;

    for (size_t i = 0; i < n_fixtures; i++) {
        char src[4096];
        snprintf(src, sizeof(src), "%s/%s", src_root, fixtures[i]);

        /* p1 = first-pass formatted output */
        snprintf(path1, sizeof(path1), "%s/idem_%zu_p1.lime", g_tmpdir, i);
        snprintf(path1_out, sizeof(path1_out),
                 "%s.formatted", path1);
        if (copy_file(src, path1) != 0) {
            fprintf(stderr, "FAIL %s: cannot stage input\n", fixtures[i]);
            failed++;
            continue;
        }
        if (run_format(lime_exe, path1) != 0) {
            fprintf(stderr, "FAIL %s: first format pass failed\n",
                    fixtures[i]);
            failed++;
            continue;
        }

        /* p2 = format applied to p1's output */
        snprintf(path2, sizeof(path2), "%s/idem_%zu_p2.lime", g_tmpdir, i);
        snprintf(path2_out, sizeof(path2_out),
                 "%s.formatted", path2);
        if (copy_file(path1_out, path2) != 0) {
            fprintf(stderr, "FAIL %s: cannot stage p1 output\n",
                    fixtures[i]);
            failed++;
            continue;
        }
        if (run_format(lime_exe, path2) != 0) {
            fprintf(stderr, "FAIL %s: second format pass failed\n",
                    fixtures[i]);
            failed++;
            continue;
        }

        /* Byte-equality */
        size_t n1, n2;
        char *b1 = slurp(path1_out, &n1);
        char *b2 = slurp(path2_out, &n2);
        if (!b1 || !b2) {
            fprintf(stderr, "FAIL %s: cannot slurp results\n",
                    fixtures[i]);
            free(b1); free(b2);
            failed++;
            continue;
        }
        if (n1 != n2 || memcmp(b1, b2, n1) != 0) {
            fprintf(stderr, "FAIL %s: drift on second pass "
                    "(p1 len=%zu, p2 len=%zu)\n",
                    fixtures[i], n1, n2);
            /* Spit a small diff hint */
            size_t lim = (n1 < n2 ? n1 : n2);
            for (size_t k = 0; k < lim; k++) {
                if (b1[k] != b2[k]) {
                    fprintf(stderr, "  first byte diff at offset %zu: "
                            "p1=0x%02x p2=0x%02x\n",
                            k, (unsigned char)b1[k],
                            (unsigned char)b2[k]);
                    break;
                }
            }
            failed++;
        } else {
            printf("PASS %s (%zu bytes)\n", fixtures[i], n1);
        }
        free(b1); free(b2);
        unlink(path1); unlink(path1_out);
        unlink(path2); unlink(path2_out);
    }

    if (failed) {
        fprintf(stderr, "FAIL: %d of %zu fixtures drifted\n",
                failed, n_fixtures);
        return 1;
    }
    printf("OK: %zu fixtures all idempotent\n", n_fixtures);
    return 0;
}
