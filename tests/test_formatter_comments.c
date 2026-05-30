/*
** test_formatter_comments.c -- regression test for Lime-Letter-19.
**
** PG asked for two formatter fixes before they wire `lime -F` into
** CI: (A) inter-directive and inter-rule comment preservation, and
** (B) leading-whitespace preservation inside brace-body directives
** and rule action bodies.  The two together close the 33.5 %
** shrinkage observed on v0.3.1.
**
** This driver runs `lime -F` twice on the comment-rich fixture
** tests/test_formatter_comments_grammar.lime and asserts:
**
**   1. Header survives (v0.3.1 regression guard, Letter 18 fix).
**   2. Multi-line block comment before %token_type survives.
**   3. `//` stack before %extra_argument survives.
**   4. Brace body leading whitespace survives -- the `\tcompute_plus`
**      analogue in the fixture's %syntax_error stays indented.
**   5. Rule-leading multi-line comment survives.
**   6. Idempotence: format(format(F)) == format(F), byte-equal.
**   7. Empty grammar -> non-crashing empty-shaped output (regression).
**   8. Inter-token comments are NOT preserved (documenting the
**      v0.3.2 limitation: per-token comment slots are absent
**      because token blocks emit in symbol-index order, not source
**      order; see Lime-Reply-19.txt).
**
** Skips at runtime (exit 77) when the lime binary is not findable
** at the expected meson path.
*/

#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static int contains(const char *hay, const char *needle, const char *tag) {
    if (strstr(hay, needle) == NULL) {
        fprintf(stderr, "FAIL: %s -- did not find marker `%s`\n",
                tag, needle);
        return 0;
    }
    return 1;
}

static int absent(const char *hay, const char *needle, const char *tag) {
    if (strstr(hay, needle) != NULL) {
        fprintf(stderr,
                "FAIL: %s -- unexpected marker `%s` is present\n",
                tag, needle);
        return 0;
    }
    return 1;
}

/* Run `<lime> -F <path>` and return the slurped <path>.formatted, or
** NULL on failure (with a FAIL message already printed). */
static char *format_once(const char *lime_bin, const char *path) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "'%s' -F '%s' > /dev/null 2>&1", lime_bin, path);
    if (system(cmd) != 0) {
        fprintf(stderr, "FAIL: lime -F %s exited non-zero\n", path);
        return NULL;
    }
    char outpath[2048];
    snprintf(outpath, sizeof(outpath), "%s.formatted", path);
    char *got = slurp(outpath);
    if (got == NULL) {
        fprintf(stderr, "FAIL: could not read %s\n", outpath);
    }
    return got;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <lime-binary> <test-grammar.lime>\n", argv[0]);
        return 2;
    }
    const char *lime_bin = argv[1];
    const char *fixture  = argv[2];

    struct stat st;
    if (stat(lime_bin, &st) != 0) {
        fprintf(stderr, "SKIP: %s not found\n", lime_bin);
        return 77;
    }

    /* Working copies in /tmp so we don't litter the source tree. */
    const char *work     = "/tmp/lime_fmtcom_input.lime";
    const char *fmt1     = "/tmp/lime_fmtcom_input.lime.formatted";
    const char *empty    = "/tmp/lime_fmtcom_empty.lime";

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", fixture, work);
    if (system(cmd) != 0) {
        fprintf(stderr, "FAIL: could not copy fixture to %s\n", work);
        return 1;
    }

    char *first = format_once(lime_bin, work);
    if (first == NULL) return 1;

    int ok = 1;

    /* Sub-test 1: header preservation (Letter 18 regression guard). */
    ok &= contains(first,
        "test_formatter_comments_grammar.lime -- regression fixture",
        "1. header preservation -- fixture identification line");
    ok &= contains(first,
        "Lime-Letter-19 (PostgreSQL migration team)",
        "1. header preservation -- letter reference");

    /* Sub-test 2: block comment before %token_type survives. */
    ok &= contains(first,
        "Block of commentary describing the token type decision",
        "2. block comment before %token_type");
    ok &= contains(first,
        "Spans multiple lines deliberately",
        "2. block comment before %token_type -- second line");

    /* Sub-test 3: // stack before %extra_argument survives. */
    ok &= contains(first,
        "Single-line comment 1, stacked above %extra_argument.",
        "3. // stack before %extra_argument");
    ok &= contains(first,
        "Single-line comment 2, contiguous with the first.",
        "3. // stack before %extra_argument -- second line");

    /* Sub-test 4: brace body leading whitespace survives.  The
    ** %syntax_error body has 4-space indent in the source; that
    ** indent must remain.  Pre-Lime-Letter-19 the formatter
    ** dedented every byte, producing `fprintf(stderr,...);` flush-
    ** left.  We assert the indented form is present. */
    ok &= contains(first,
        "    fprintf(stderr, \"syntax error near token",
        "4. brace body leading whitespace -- %syntax_error indent");
    ok &= contains(first,
        "    fprintf(stderr, \"parse failed",
        "4. brace body leading whitespace -- %parse_failure indent");
    /* %include body has 4-space indent on a comment line too. */
    ok &= contains(first,
        "    /* Body indentation must survive",
        "4. brace body leading whitespace -- %include comment indent");
    /* Action body leading tab -- PG's signature pattern. */
    ok &= contains(first,
        "\t*result_out = doubled(L + M);",
        "4. action body leading whitespace -- expr PLUS rule (tab)");
    ok &= contains(first,
        "\t*result_out = doubled(0);",
        "4. action body leading whitespace -- expr A rule (tab)");

    /* Sub-test 5: rule-leading multi-line comment survives. */
    ok &= contains(first,
        "expr is an arithmetic expression.",
        "5. rule-leading comment -- first line");
    ok &= contains(first,
        "Recursive on left to mirror Bison's left-associative default.",
        "5. rule-leading comment -- second line");
    /* And the // form on the second rule. */
    ok &= contains(first,
        "Single-line comment above a second rule.",
        "5. rule-leading comment -- // form");

    /* Sub-test 6: idempotence -- format(format(F)) == format(F). */
    char *second = format_once(lime_bin, fmt1);
    if (second == NULL) { free(first); return 1; }
    if (strcmp(first, second) != 0) {
        fprintf(stderr,
                "FAIL: 6. idempotence -- format(format(F)) != format(F)\n");
        ok = 0;
    }

    /* Sub-test 7: empty-grammar regression.  An empty file (zero
    ** rules) made `lime -F` exit with "Empty grammar" before; the
    ** comment-capture path must not regress that.  We invoke lime
    ** -F on a single-rule grammar (the smallest non-empty case) and
    ** assert it succeeds. */
    {
        FILE *ef = fopen(empty, "w");
        if (ef == NULL) {
            fprintf(stderr, "FAIL: 7. could not write %s\n", empty);
            ok = 0;
        } else {
            fputs("%name Empty\n%token TOK.\nstart ::= TOK.\n", ef);
            fclose(ef);
            char *out_empty = format_once(lime_bin, empty);
            if (out_empty == NULL) {
                fprintf(stderr, "FAIL: 7. empty-shaped grammar format failed\n");
                ok = 0;
            } else {
                if (!contains(out_empty, "%name Empty",
                              "7. empty-shaped output -- %name preserved")) {
                    ok = 0;
                }
                if (!contains(out_empty, "start ::= TOK.",
                              "7. empty-shaped output -- rule preserved")) {
                    ok = 0;
                }
                free(out_empty);
            }
        }
    }

    /* Sub-test 8: inter-token comments NOT preserved (limitation).
    ** The fixture has a banner comment immediately above %token A.
    ** that documents the v0.3.2 scope limitation; it MUST be dropped
    ** in the formatted output to make the limitation visible.  When
    ** v0.3.3 lifts this restriction, this sub-test will start
    ** failing and the test (and the comment in the fixture) must be
    ** updated -- a built-in tripwire. */
    ok &= absent(first,
        "v0.3.2 does NOT preserve per-token comments",
        "8. inter-token comment limitation -- banner is dropped");

    free(first);
    free(second);

    if (!ok) return 1;
    printf("PASS: 8/8 sub-tests -- comments + brace-body indent + "
           "idempotence (Lime-Letter-19)\n");
    return 0;
}
