/*
** tests/test_action_string_literal.c -- P0-NEW-11 runtime check.
**
** The bug: lime.c:translate_code walked rp->code byte-by-byte
** and substituted bare alphabetic identifiers that match an
** LHS or RHS alias into stack-slot references.  The walk did
** not track lexical state, so identifiers inside string
** literals, char literals, line comments, and block comments
** were rewritten too.  PG's contrib/cube uncovered it:
**
**     errdetail("A cube cannot have more than %d ...");
**
** with LHS alias `(A)` became `errdetail("yylhsminor.yy0 cube
** ...")` in the generated parser.
**
** Strategy: drive the generated parser through one reduce of
** the box rule, capture the strings/chars that the action body
** stashes, and assert each of them is the literal text the
** grammar wrote.  If the substitution leaks into a literal,
** the captured byte sequence will contain `yymsp[...]`,
** `yylhsminor`, or similar and the test fails.
**
** A second sub-test reads the generated .c file (path injected
** via -DACTION_STRLIT_GEN_C) and asserts the source-level
** invariant: the bare-`A`-in-code-position assignment WAS
** substituted (something other than literal `A = N;`), while
** the strings, char literals, and comments still contain
** literal `A` and `N` text.  Reading the generated .c is a
** stronger source-of-truth check than the runtime test alone:
** even if the runtime test passed by accident (e.g., the
** substituted text happened to equal the original), the
** source-level test pins the codegen contract.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_action_string_literal.h"
#include "test_action_string_literal_grammar.h"

#ifndef ACTION_STRLIT_GEN_C
#  error "ACTION_STRLIT_GEN_C must be defined to the generated .c path"
#endif

void *PaslAlloc(void *(*mallocProc)(size_t));
void  PaslFree(void *, void (*freeProc)(void *));
void  Pasl(void *yyp, int yymajor, int yyminor,
           struct pasl_capture *cap);

static int fails = 0;

#define EXPECT_STR(got, want, label) do {                          \
    const char *_g = (got);                                        \
    const char *_w = (want);                                       \
    if (_g == NULL || strcmp(_g, _w) != 0) {                       \
        fprintf(stderr, "  %s: got \"%s\", want \"%s\"\n",         \
                label, _g ? _g : "(null)", _w);                    \
        fails++;                                                   \
    }                                                              \
} while (0)

#define EXPECT_CH(got, want, label) do {                           \
    char _g = (got);                                               \
    char _w = (want);                                              \
    if (_g != _w) {                                                \
        fprintf(stderr, "  %s: got '%c' (0x%02x), want '%c'\n",    \
                label, _g, (unsigned)(unsigned char)_g, _w);       \
        fails++;                                                   \
    }                                                              \
} while (0)

#define EXPECT(cond, ...) do {                                     \
    if (!(cond)) {                                                 \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);          \
        fprintf(stderr, __VA_ARGS__);                              \
        fprintf(stderr, "\n");                                     \
        fails++;                                                   \
    }                                                              \
} while (0)

/* (1) Runtime test: feed `( 7 )` to the parser, the box(A)
** action runs and stashes literals into `cap`.  Each captured
** field must equal the literal text the grammar wrote -- if
** the substitution rewrote `A` or `N` inside a literal, the
** captured bytes will diverge. */
static int test_runtime_literals_preserved(void) {
    int saved = fails;
    struct pasl_capture cap = {0};
    void *p = PaslAlloc(malloc);
    EXPECT(p != NULL, "alloc");
    if (!p) return fails - saved;

    Pasl(p, PASL_LP,   0, &cap);
    Pasl(p, PASL_NUM,  7, &cap);
    Pasl(p, PASL_RP,   0, &cap);
    Pasl(p, 0,         0, &cap);   /* end-of-input */
    PaslFree(p, free);

    EXPECT_STR(cap.s_plain,
               "A cube cannot have more than N dimensions.",
               "s_plain");
    EXPECT_STR(cap.s_with_double_slash,
               "A//still-in-string-N",
               "s_with_double_slash");
    EXPECT_STR(cap.s_with_block_open,
               "A/*still-in-string-N*/",
               "s_with_block_open");
    EXPECT_STR(cap.s_with_escaped_quote,
               "an \"A\" inside an N",
               "s_with_escaped_quote");
    EXPECT_STR(cap.s_adjacent,
               "A first then N",
               "s_adjacent (adjacent string concatenation)");
    EXPECT_CH(cap.c_a, 'A', "c_a");
    EXPECT_CH(cap.c_n, 'N', "c_n");
    EXPECT_CH(cap.c_apos, '\'', "c_apos (escaped apostrophe in char literal)");
    EXPECT(cap.c_after_block == 1,
           "c_after_block: code after /*...*/ ran (got %d)",
           cap.c_after_block);
    EXPECT(cap.c_after_line == 1,
           "c_after_line: code after //... ran (got %d)",
           cap.c_after_line);

    if (fails == saved) printf("test_runtime_literals_preserved: PASS\n");
    return fails - saved;
}

/* Read the entire generated .c into a heap buffer (NUL-terminated). */
static char *slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

/* (2) Source-level test: the generator's output must contain
** the literal strings/chars unchanged, AND the bare-A code
** position must have been substituted to a stack-slot ref. */
static int test_generated_source_shape(void) {
    int saved = fails;
    size_t len = 0;
    char *src = slurp(ACTION_STRLIT_GEN_C, &len);
    EXPECT(src != NULL, "open generated .c (%s)", ACTION_STRLIT_GEN_C);
    if (!src) return fails - saved;

    /* Each of these literals must appear verbatim in the
    ** generated .c -- they live inside string/char/comment
    ** regions the substitution must skip. */
    const char *needles[] = {
        "\"A cube cannot have more than N dimensions.\"",
        "\"A//still-in-string-N\"",
        "\"A/*still-in-string-N*/\"",
        "\"an \\\"A\\\" inside an N\"",
        "\"A first\" \" then N\"",
        "'A'",
        "'N'",
        "'\\''",
        "/* A and N inside block */",
        "// line comment: A and N stay literal here",
    };
    for (size_t i = 0; i < sizeof(needles)/sizeof(needles[0]); i++) {
        EXPECT(strstr(src, needles[i]) != NULL,
               "generated .c missing literal: %s", needles[i]);
    }

    /* The bare `A = N;` in code position must have been
    ** rewritten.  Two specific shapes are acceptable: either
    ** `yymsp[...].minor.yyN = ...` (lhsdirect) or
    ** `yylhsminor.yyN = ...` (lhs-via-temp).  In neither case
    ** should the literal text `A = N;` appear in the generated
    ** source. */
    EXPECT(strstr(src, "A = N;") == NULL,
           "generated .c still contains unsubstituted `A = N;`");

    /* And the substitution-target string must appear: at
    ** minimum the LHS slot reference `yy0` (the int dtnum is 0
    ** for the first %type-bearing symbol).  We don't pin the
    ** exact slot number; we just confirm the substitution
    ** machinery ran by looking for any `yymsp[` reference
    ** inside the generated code (every action body emission
    ** has these around). */
    EXPECT(strstr(src, "yymsp[") != NULL,
           "generated .c missing yymsp[...] -- substitution did not run");

    free(src);
    if (fails == saved) printf("test_generated_source_shape: PASS\n");
    return fails - saved;
}

int main(void) {
    test_runtime_literals_preserved();
    test_generated_source_shape();
    if (fails == 0) {
        printf("\ntest_action_string_literal: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr,
            "\ntest_action_string_literal: %d sub-test failure(s)\n", fails);
    return 1;
}
