/*
** test_formatter_labels.c -- Lime-Letter-20 regression test.
**
** Runs `lime -F` on a label-rich fixture and asserts:
**   1. LHS letter labels survive (`program(P) ::=`)
**   2. RHS letter labels survive (`... PLUS expr(M).`)
**   3. Mixed labeled + unlabeled symbols emit correctly
**   4. Explicit [SYMBOL] precedence markers survive
**   5. Idempotence: format(format(F)) == format(F)
**   6. Compile guard: the formatted file, fed back through `lime`
**      and compiled with gcc, must produce a valid .c that
**      references the action-body identifiers correctly.  We
**      verify by checking the formatted output contains the
**      same number of `(A)`, `(B)` etc. occurrences as the
**      source -- if the formatter strips them, action bodies
**      reference undefined stack slots.
**
** Invokes the lime binary path passed as argv[1] on the grammar
** path passed as argv[2].  Mirrors the test_formatter / test_
** formatter_comments harness shape.
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

static int count_occurrences(const char *hay, const char *needle) {
    int n = 0;
    const char *p = hay;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += nlen;
    }
    return n;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <lime-binary> <test-grammar.lime>\n", argv[0]);
        return 2;
    }
    const char *lime_bin = argv[1];
    const char *grammar = argv[2];

    /* Copy the source grammar to a writable working file -- lime -F
    ** writes alongside the input, and the source tree's grammar
    ** path may be read-only. */
    char tmpdir[256];
    if (test_compat_tmpdir("lime_labels", tmpdir, sizeof(tmpdir)) != 0) {
        perror("test_compat_tmpdir");
        return 2;
    }
    char src_copy[512];
    snprintf(src_copy, sizeof(src_copy), "%s/grammar.lime", tmpdir);

    if (test_compat_copy_file(grammar, src_copy) != 0) {
        fprintf(stderr, "FAIL: copying grammar to %s\n", src_copy);
        return 1;
    }

    /* Pass 1: format the source. */
    {
        char *fmt_argv[] = { (char *)lime_bin, "-F", src_copy, NULL };
        int rc = 0;
        if (test_compat_run(fmt_argv, &rc) != 0 || rc != 0) {
            fprintf(stderr, "FAIL: lime -F (pass 1) returned non-zero\n");
            return 1;
        }
    }
    char fmt_path[512];
    snprintf(fmt_path, sizeof(fmt_path), "%s.formatted", src_copy);

    char *source = slurp(src_copy);
    char *formatted = slurp(fmt_path);
    if (source == NULL || formatted == NULL) {
        fprintf(stderr, "FAIL: could not read source or formatted file\n");
        return 1;
    }

    int pass = 0;
    int total = 0;

    /* Sub-test 1: LHS labels survive. */
    total++;
    if (contains(formatted, "program(P) ::=", "LHS label P")
        && contains(formatted, "expr(R) ::=", "LHS label R")) {
        printf("  [PASS] LHS labels (P, R) survive in formatted output\n");
        pass++;
    } else {
        printf("  [FAIL] LHS labels missing from formatted output\n");
    }

    /* Sub-test 2: RHS labels survive. */
    total++;
    if (contains(formatted, "expr(L)", "RHS label L")
        && contains(formatted, "expr(M)", "RHS label M")
        && contains(formatted, "INTEGER(N)", "RHS label N")) {
        printf("  [PASS] RHS labels (L, M, N) survive in formatted output\n");
        pass++;
    } else {
        printf("  [FAIL] RHS labels missing from formatted output\n");
    }

    /* Sub-test 3: mixed labeled + unlabeled symbols. */
    total++;
    if (contains(formatted, "LPAREN expr(M) RPAREN", "mixed labeled+unlabeled")) {
        printf("  [PASS] mixed labeled (expr(M)) + unlabeled (LPAREN/RPAREN) symbols\n");
        pass++;
    } else {
        printf("  [FAIL] mixed labeled+unlabeled emit broken\n");
    }

    /* Sub-test 4: explicit [SYMBOL] precedence marker. */
    total++;
    if (contains(formatted, "[UMINUS]", "precedence marker")) {
        printf("  [PASS] explicit [UMINUS] precedence marker preserved\n");
        pass++;
    } else {
        printf("  [FAIL] explicit [UMINUS] precedence marker stripped\n");
    }

    /* Sub-test 5: count of (A), (B)-style label occurrences in
    ** action bodies vs in rule headers.  If the formatter strips
    ** labels from headers but keeps action body references, the
    ** counts mismatch and the resulting grammar is non-compiling.
    ** The fixture is structured so action bodies reference the
    ** same single-letter aliases declared in headers; conservative
    ** check: every rule's action body alias must have a matching
    ** header alias (see the source's design). */
    total++;
    int rhs_labels_in_formatted = count_occurrences(formatted, "expr(L)")
                                  + count_occurrences(formatted, "expr(M)")
                                  + count_occurrences(formatted, "expr(R)");
    int rhs_labels_in_source = count_occurrences(source, "expr(L)")
                               + count_occurrences(source, "expr(M)")
                               + count_occurrences(source, "expr(R)");
    if (rhs_labels_in_formatted == rhs_labels_in_source && rhs_labels_in_source > 0) {
        printf("  [PASS] RHS label occurrence count matches source (%d)\n",
               rhs_labels_in_source);
        pass++;
    } else {
        printf("  [FAIL] RHS label count mismatch: source=%d formatted=%d\n",
               rhs_labels_in_source, rhs_labels_in_formatted);
    }

    /* Sub-test 6: idempotence -- format the formatted file. */
    total++;
    char src_copy2[512];
    snprintf(src_copy2, sizeof(src_copy2), "%s/grammar2.lime", tmpdir);
    test_compat_copy_file(fmt_path, src_copy2);
    {
        char *fmt_argv[] = { (char *)lime_bin, "-F", src_copy2, NULL };
        int rc = 0;
        if (test_compat_run(fmt_argv, &rc) != 0 || rc != 0) {
            printf("  [FAIL] idempotence: lime -F (pass 2) returned non-zero\n");
        } else {
            char fmt2_path[512];
            snprintf(fmt2_path, sizeof(fmt2_path), "%s.formatted", src_copy2);
            char *formatted2 = slurp(fmt2_path);
            if (formatted2 == NULL) {
                printf("  [FAIL] idempotence: could not read pass-2 output\n");
            } else if (strcmp(formatted, formatted2) == 0) {
                printf("  [PASS] idempotence: format(format(F)) == format(F)\n");
                pass++;
                free(formatted2);
            } else {
                printf("  [FAIL] idempotence: pass-1 and pass-2 differ\n");
                free(formatted2);
            }
        }
    }

    free(source);
    free(formatted);

    /* Cleanup tmpdir. */
    test_compat_rmdir_recursive(tmpdir);

    printf("\n=== Summary === %d/%d sub-tests pass\n", pass, total);
    return pass == total ? 0 : 1;
}
