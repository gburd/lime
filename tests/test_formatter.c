/*
** test_formatter.c -- regression test for Lime-Letter-18:
** the formatter must preserve copyright headers and brace-block
** directive bodies, and must be idempotent under repeated runs.
**
** PG asked for both properties so they can wire `lime -F` into
** CI as a format-check (`format(format(F)) == format(F)` and
** the format output is identical to source on a canonically
** formatted file).
**
** This test runs the lime binary on tests/test_formatter_grammar.lime
** twice and asserts:
**
**   1. The first formatted output preserves the file's copyright
**      header (a marker string from the fixture must survive).
**   2. The first formatted output preserves the %include block
**      body (a marker `static int doubled` must survive).
**   3. The first formatted output preserves the %syntax_error and
**      %parse_failure block bodies.
**   4. format(format(F)) == format(F) -- formatting twice produces
**      identical bytes (idempotence).
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
        fprintf(stderr, "FAIL: %s -- did not find marker `%s`\n", tag, needle);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <lime-binary> <test-grammar.lime>\n", argv[0]);
        return 2;
    }
    const char *lime_bin = argv[1];
    const char *fixture  = argv[2];

    /* Skip if we can't stat the lime binary. */
    struct stat st;
    if (stat(lime_bin, &st) != 0) {
        fprintf(stderr, "SKIP: %s not found\n", lime_bin);
        return 77;
    }

    /* Working copy in /tmp so we don't litter the source tree. */
    const char *work     = "/tmp/lime_fmttest_input.lime";
    const char *fmt1     = "/tmp/lime_fmttest_input.lime.formatted";
    const char *fmt2     = "/tmp/lime_fmttest_input.lime.formatted.formatted";

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", fixture, work);
    if (system(cmd) != 0) {
        fprintf(stderr, "FAIL: could not copy fixture to %s\n", work);
        return 1;
    }

    /* First pass. */
    snprintf(cmd, sizeof(cmd), "'%s' -F '%s' > /dev/null 2>&1", lime_bin, work);
    if (system(cmd) != 0) {
        fprintf(stderr, "FAIL: lime -F %s exited non-zero\n", work);
        return 1;
    }

    char *first = slurp(fmt1);
    if (first == NULL) {
        fprintf(stderr, "FAIL: could not read %s\n", fmt1);
        return 1;
    }

    int ok = 1;
    ok &= contains(first, "Portions Copyright",
                   "header preservation -- copyright line");
    ok &= contains(first, "IDENTIFICATION",
                   "header preservation -- IDENTIFICATION block");
    ok &= contains(first, "static int doubled",
                   "%include body preservation");
    ok &= contains(first, "syntax error near token",
                   "%syntax_error body preservation");
    ok &= contains(first, "parse failed -- giving up",
                   "%parse_failure body preservation");
    ok &= contains(first, "doubled(L + M)",
                   "action body preservation");

    /* Second pass on the formatted output -- idempotence check. */
    snprintf(cmd, sizeof(cmd), "'%s' -F '%s' > /dev/null 2>&1", lime_bin, fmt1);
    if (system(cmd) != 0) {
        fprintf(stderr, "FAIL: lime -F %s exited non-zero\n", fmt1);
        free(first);
        return 1;
    }

    char *second = slurp(fmt2);
    if (second == NULL) {
        fprintf(stderr, "FAIL: could not read %s\n", fmt2);
        free(first);
        return 1;
    }

    if (strcmp(first, second) != 0) {
        fprintf(stderr,
                "FAIL: formatter is not idempotent -- "
                "format(format(F)) != format(F)\n");
        ok = 0;
    }

    free(first);
    free(second);

    if (!ok) return 1;
    printf("PASS: header preserved, brace bodies preserved, idempotent\n");
    return 0;
}
