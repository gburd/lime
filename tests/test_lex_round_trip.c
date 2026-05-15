/*
** tests/test_lex_round_trip.c -- M1.5 integration test.
**
** Round-trip property:
**   parse(t)  -> A
**   pretty(A) -> t'
**   parse(t') -> A'
**   pretty(A') -> t''
** Assert: t' == t''  (pretty-print is idempotent)
** Assert: A == A' structurally  (parse + pretty preserves AST)
**
** This is the M1 ship gate.  After this test passes for the
** worked .lex examples we care about, M1 is functionally
** complete and we can move on to M2 (DFA compiler).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex_ast.h"
#include "lex_parse.h"
#include "lex_pretty.h"

static int fails = 0;

#define EXPECT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, "\n");                                  \
        fails++;                                                \
    }                                                           \
} while (0)

#define EXPECT_STR_EQ(a, b, label) do {                         \
    const char *_a = (a);                                       \
    const char *_b = (b);                                       \
    if (_a == NULL && _b == NULL) break;                        \
    if (_a == NULL || _b == NULL || strcmp(_a, _b) != 0) {      \
        fprintf(stderr, "  %s:%d: %s mismatch:\n",              \
                __func__, __LINE__, (label));                   \
        fprintf(stderr, "    A: \"%s\"\n", _a ? _a : "(null)"); \
        fprintf(stderr, "    B: \"%s\"\n", _b ? _b : "(null)"); \
        fails++;                                                \
    }                                                           \
} while (0)

/* Structural comparison of two specs.  Walks the lists in
** declaration order and asserts each pair matches. */

static int specs_equivalent(const LimeLexSpec *a, const LimeLexSpec *b) {
    int local = 0;
    int saved = fails;

    EXPECT_STR_EQ(a->name_prefix,    b->name_prefix,    "name_prefix");
    EXPECT_STR_EQ(a->token_prefix,   b->token_prefix,   "token_prefix");
    EXPECT_STR_EQ(a->token_type,     b->token_type,     "token_type");
    EXPECT_STR_EQ(a->location_type,  b->location_type,  "location_type");
    EXPECT_STR_EQ(a->extra_argument, b->extra_argument, "extra_argument");
    EXPECT_STR_EQ(a->include_block,  b->include_block,  "include_block");

    /* Patterns. */
    {
        const LimeLexPattern *pa = a->patterns;
        const LimeLexPattern *pb = b->patterns;
        while (pa && pb) {
            EXPECT_STR_EQ(pa->name, pb->name, "pattern name");
            EXPECT_STR_EQ(pa->regex, pb->regex, "pattern regex");
            pa = pa->next;
            pb = pb->next;
        }
        if (pa || pb) {
            fprintf(stderr, "  patterns list length mismatch\n");
            fails++;
        }
    }

    /* States. */
    {
        const LimeLexState *sa = a->states, *sb = b->states;
        while (sa && sb) {
            EXPECT_STR_EQ(sa->name, sb->name, "state name");
            EXPECT(sa->exclusive == sb->exclusive,
                   "state %s exclusive %d vs %d",
                   sa->name, sa->exclusive, sb->exclusive);
            EXPECT_STR_EQ(sa->local_body, sb->local_body, "state local_body");
            EXPECT_STR_EQ(sa->destructor, sb->destructor, "state destructor");
            sa = sa->next; sb = sb->next;
        }
        if (sa || sb) {
            fprintf(stderr, "  states list length mismatch\n");
            fails++;
        }
    }

    /* Keyword tables. */
    {
        const LimeLexKeywordTable *ka = a->keyword_tables, *kb = b->keyword_tables;
        while (ka && kb) {
            EXPECT_STR_EQ(ka->name, kb->name, "kw name");
            EXPECT(ka->case_insensitive == kb->case_insensitive,
                   "kw %s case_insensitive mismatch", ka->name);
            EXPECT_STR_EQ(ka->prefix, kb->prefix, "kw prefix");
            EXPECT(ka->n_keywords == kb->n_keywords,
                   "kw %s n_keywords %d vs %d",
                   ka->name, ka->n_keywords, kb->n_keywords);
            int n = ka->n_keywords < kb->n_keywords ?
                    ka->n_keywords : kb->n_keywords;
            for (int i = 0; i < n; i++) {
                EXPECT_STR_EQ(ka->keywords[i], kb->keywords[i], "kw entry");
            }
            ka = ka->next; kb = kb->next;
        }
        if (ka || kb) {
            fprintf(stderr, "  keyword_tables list length mismatch\n");
            fails++;
        }
    }

    /* Literal buffers. */
    {
        const LimeLexLiteralBuffer *la = a->literal_buffers, *lb = b->literal_buffers;
        while (la && lb) {
            EXPECT_STR_EQ(la->name, lb->name, "lb name");
            EXPECT_STR_EQ(la->element_type, lb->element_type, "lb type");
            EXPECT(la->initial_capacity == lb->initial_capacity,
                   "lb %s initial %d vs %d",
                   la->name, la->initial_capacity, lb->initial_capacity);
            EXPECT_STR_EQ(la->grow_policy, lb->grow_policy, "lb grow");
            EXPECT_STR_EQ(la->alloc_fn, lb->alloc_fn, "lb alloc");
            EXPECT_STR_EQ(la->realloc_fn, lb->realloc_fn, "lb realloc");
            EXPECT_STR_EQ(la->free_fn, lb->free_fn, "lb free");
            la = la->next; lb = lb->next;
        }
        if (la || lb) {
            fprintf(stderr, "  literal_buffers list length mismatch\n");
            fails++;
        }
    }

    /* Top-level rules. */
    {
        const LimeLexRule *ra = a->rules, *rb = b->rules;
        while (ra && rb) {
            EXPECT_STR_EQ(ra->name, rb->name, "rule name");
            EXPECT(ra->is_eof == rb->is_eof,
                   "rule %s is_eof mismatch", ra->name);
            EXPECT_STR_EQ(ra->pattern, rb->pattern, "rule pattern");
            EXPECT_STR_EQ(ra->action, rb->action, "rule action");
            EXPECT(ra->n_states == rb->n_states,
                   "rule %s n_states %d vs %d",
                   ra->name, ra->n_states, rb->n_states);
            int n = ra->n_states < rb->n_states ? ra->n_states : rb->n_states;
            for (int i = 0; i < n; i++) {
                EXPECT_STR_EQ(ra->states[i], rb->states[i], "rule state");
            }
            ra = ra->next; rb = rb->next;
        }
        if (ra || rb) {
            fprintf(stderr, "  rules list length mismatch\n");
            fails++;
        }
    }

    /* %lexer_include list. */
    {
        EXPECT(a->n_lexer_includes == b->n_lexer_includes,
               "n_lexer_includes %d vs %d",
               a->n_lexer_includes, b->n_lexer_includes);
        int n = a->n_lexer_includes < b->n_lexer_includes ?
                a->n_lexer_includes : b->n_lexer_includes;
        for (int i = 0; i < n; i++) {
            EXPECT_STR_EQ(a->lexer_includes[i], b->lexer_includes[i],
                          "lexer_include");
        }
    }

    local = fails - saved;
    return local == 0;
}

/* Run the round-trip pipeline and assert idempotency + AST
** equivalence. */
static int round_trip(const char *label, const char *src) {
    int saved = fails;

    LimeLexSpec *a = lime_lex_parse(label, src, strlen(src));
    EXPECT(a != NULL, "parse #1 returned NULL");
    if (!a) return fails - saved;
    EXPECT(a->error_count == 0, "parse #1 errors=%d", a->error_count);

    char *t1 = lime_lex_spec_to_text(a);
    EXPECT(t1 != NULL, "pretty #1 returned NULL");
    if (!t1) { lime_lex_spec_free(a); return fails - saved; }

    LimeLexSpec *b = lime_lex_parse(label, t1, strlen(t1));
    EXPECT(b != NULL, "parse #2 returned NULL");
    if (!b) { free(t1); lime_lex_spec_free(a); return fails - saved; }
    EXPECT(b->error_count == 0, "parse #2 errors=%d", b->error_count);

    char *t2 = lime_lex_spec_to_text(b);
    EXPECT(t2 != NULL, "pretty #2 returned NULL");
    if (!t2) { lime_lex_spec_free(b); free(t1); lime_lex_spec_free(a); return fails - saved; }

    /* Idempotency: t1 == t2. */
    if (strcmp(t1, t2) != 0) {
        fprintf(stderr,
                "  %s: pretty-print not idempotent\n--- t1 ---\n%s\n--- t2 ---\n%s\n---\n",
                label, t1, t2);
        fails++;
    }

    /* Structural equivalence: a == b. */
    specs_equivalent(a, b);

    int local = fails - saved;
    if (local == 0) printf("%s: PASS\n", label);

    free(t2);
    free(t1);
    lime_lex_spec_free(b);
    lime_lex_spec_free(a);
    return local;
}

/* ----- sub-tests ----- */

static int test_minimal(void) {
    return round_trip("minimal", "%name_prefix Foo.\n");
}

static int test_directives(void) {
    return round_trip("directives",
        "%name_prefix Boot.\n"
        "%token_prefix BOOT_.\n"
        "%token_type { int }\n"
        "%location_type { LimeLocation }\n");
}

static int test_patterns(void) {
    return round_trip("patterns",
        "%pattern digit /[0-9]/.\n"
        "%pattern hex /[0-9A-Fa-f]/.\n"
        "%pattern ident /[A-Za-z_][A-Za-z0-9_]*/.\n");
}

static int test_states(void) {
    return round_trip("states",
        "%state EXPR.\n"
        "%exclusive_state QUOTED { char *tag; size_t len; }.\n"
        "%state_destructor QUOTED { free(state_data->tag); }.\n");
}

static int test_keyword_table(void) {
    return round_trip("keyword_table",
        "%keyword_table sql_kw (case_insensitive, prefix=K_) {\n"
        "    \"select\", \"from\", \"where\"\n"
        "}.\n");
}

static int test_rules(void) {
    return round_trip("rules",
        "rule plus matches /\\+/ { LEX_EMIT_NOVAL('+'); }\n"
        "<EXPR> rule lparen matches /\\(/ { LEX_EMIT_NOVAL('('); }\n"
        "<xq, xd, xb> rule eof_string matches <<EOF>> { abort(); }\n");
}

static int test_ruleset_and_include(void) {
    return round_trip("ruleset_include",
        "%ruleset basic {\n"
        "    rule a matches /a/ { /* */ }\n"
        "    rule b matches /b/ { /* */ }\n"
        "}.\n"
        "%lexer_include basic, more.\n");
}

static int test_block_form_desugaring_round_trip(void) {
    /* Block-form input desugars to per-rule qualifiers; pretty-
    ** print emits per-rule.  Re-parsing the per-rule output
    ** matches the block-form's resolved AST. */
    return round_trip("block_form_desugaring",
        "<EXPR> {\n"
        "    rule plus  matches /\\+/ { /* */ }\n"
        "    rule minus matches /-/  { /* */ }\n"
        "}\n");
}

static int test_bootscanner_shape(void) {
    return round_trip("bootscanner_shape",
        "%name_prefix boot.\n"
        "%token_prefix BOOT_.\n"
        "%token_type { union BootValue }\n"
        "%location_type { LimeLocation }\n"
        "%pattern id /[-A-Za-z0-9_]+/.\n"
        "%keyword_table boot_kw (case_sensitive, prefix=K_) {\n"
        "    \"open\", \"close\", \"create\"\n"
        "}.\n"
        "rule whitespace matches /[ \\t\\r]+/ { /* skip */ }\n"
        "rule comma matches /,/ { LEX_EMIT_NOVAL(BOOT_COMMA); }\n");
}

int main(void) {
    test_minimal();
    test_directives();
    test_patterns();
    test_states();
    test_keyword_table();
    test_rules();
    test_ruleset_and_include();
    test_block_form_desugaring_round_trip();
    test_bootscanner_shape();
    if (fails == 0) {
        printf("\ntest_lex_round_trip: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr,
            "\ntest_lex_round_trip: %d sub-test failure(s)\n", fails);
    return 1;
}
