/*
** tests/test_lex_emit.c -- M3.1 unit test for code emission.
**
** Drives the full pipeline: spec source -> compile -> emit C
** code into in-memory buffers (via fmemopen).  Asserts the
** emitted text contains the expected structural elements:
**   - State constants
**   - Rule constants
**   - Per-state DFA tables (n_states, start, trans, accept)
**   - Rule-name string array
**   - Foo_match function definition with switch dispatch
*/

#define _POSIX_C_SOURCE 200809L  /* fmemopen */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex_ast.h"
#include "lex_compile.h"
#include "lex_emit.h"
#include "lex_parse.h"
#include "lex_resolve.h"

static int fails = 0;

#define EXPECT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, "\n");                                  \
        fails++;                                                \
    }                                                           \
} while (0)

#define EXPECT_CONTAINS(haystack, needle) do {                  \
    if (strstr((haystack), (needle)) == NULL) {                 \
        fprintf(stderr,                                         \
            "  %s:%d: emitted text missing \"%s\"\n",           \
            __func__, __LINE__, (needle));                      \
        fails++;                                                \
    }                                                           \
} while (0)

/* Compile a source string and emit (.h, .c) into heap buffers.
** Caller frees both. */
static int emit_for(const char *src, char **h_out, char **c_out) {
    LimeLexSpec *spec = lime_lex_parse("<test>", src, strlen(src));
    if (!spec || spec->error_count > 0) {
        if (spec) lime_lex_spec_free(spec);
        return -1;
    }
    if (lime_lex_resolve_patterns(spec) != 0) {
        lime_lex_spec_free(spec);
        return -1;
    }
    LimeLexCompiled *c = lime_lex_compile(spec);
    if (!c || c->error_count > 0) {
        if (c) lime_lex_compiled_free(c);
        lime_lex_spec_free(spec);
        return -1;
    }

    char **rule_names = NULL;
    int n_rules = 0;
    if (lime_lex_collect_rule_names(spec, &rule_names, &n_rules) != 0) {
        lime_lex_compiled_free(c);
        lime_lex_spec_free(spec);
        return -1;
    }

    const char *name_prefix = spec->name_prefix ? spec->name_prefix : "Lex";

    char *h_buf = NULL;
    size_t h_size = 0;
    FILE *h_f = open_memstream(&h_buf, &h_size);
    int h_rc = lime_lex_emit_h(c, name_prefix,
                               (const char *const *)rule_names, n_rules, h_f);
    fclose(h_f);

    char *c_buf = NULL;
    size_t c_size = 0;
    FILE *c_f = open_memstream(&c_buf, &c_size);
    int c_rc = lime_lex_emit_c(c, name_prefix, "foo_lex.h",
                               (const char *const *)rule_names, n_rules, c_f);
    fclose(c_f);

    for (int i = 0; i < n_rules; i++) free(rule_names[i]);
    free(rule_names);
    lime_lex_compiled_free(c);
    lime_lex_spec_free(spec);

    if (h_rc != 0 || c_rc != 0) {
        free(h_buf); free(c_buf);
        return -1;
    }

    *h_out = h_buf;
    *c_out = c_buf;
    return 0;
}

/* ----- sub-tests ----- */

static int test_minimal_emit(void) {
    int saved = fails;
    const char *src =
        "%name_prefix Foo.\n"
        "rule kw_if matches /if/ { /* */ }\n"
        "rule ident matches /[A-Za-z_][A-Za-z0-9_]*/ { /* */ }\n"
        "rule num   matches /[0-9]+/ { /* */ }\n";

    char *h = NULL, *c = NULL;
    EXPECT(emit_for(src, &h, &c) == 0, "emit_for failed");
    if (!h || !c) {
        free(h); free(c);
        return fails - saved;
    }

    /* Header structure. */
    EXPECT_CONTAINS(h, "#ifndef FOO_LEX_H");
    EXPECT_CONTAINS(h, "#define FOO_STATE_INITIAL");
    EXPECT_CONTAINS(h, "FOO_RULE_KW_IF");
    EXPECT_CONTAINS(h, "FOO_RULE_IDENT");
    EXPECT_CONTAINS(h, "FOO_RULE_NUM");
    EXPECT_CONTAINS(h, "extern const char *const FooRuleNames[];");
    EXPECT_CONTAINS(h, "int Foo_match(");

    /* Source structure. */
    EXPECT_CONTAINS(c, "#include \"foo_lex.h\"");
    EXPECT_CONTAINS(c, "const char *const FooRuleNames[]");
    EXPECT_CONTAINS(c, "\"kw_if\"");
    EXPECT_CONTAINS(c, "\"ident\"");
    EXPECT_CONTAINS(c, "\"num\"");
    EXPECT_CONTAINS(c, "Foo_dfa_INITIAL_n_states");
    EXPECT_CONTAINS(c, "Foo_dfa_INITIAL_trans");
    EXPECT_CONTAINS(c, "Foo_dfa_INITIAL_accept");
    EXPECT_CONTAINS(c, "int Foo_match(int state");
    EXPECT_CONTAINS(c, "last_accept_rule");
    EXPECT_CONTAINS(c, "last_accept_pos");

    free(h);
    free(c);
    if (fails == saved) printf("test_minimal_emit: PASS\n");
    return fails - saved;
}

static int test_state_constants(void) {
    int saved = fails;
    const char *src =
        "%name_prefix Bar.\n"
        "%state EXPR.\n"
        "%exclusive_state QUOTED.\n"
        "rule any matches /./ { /* */ }\n";

    char *h = NULL, *c = NULL;
    EXPECT(emit_for(src, &h, &c) == 0, "emit_for failed");
    if (!h || !c) { free(h); free(c); return fails - saved; }

    EXPECT_CONTAINS(h, "#define BAR_STATE_INITIAL");
    EXPECT_CONTAINS(h, "#define BAR_STATE_EXPR");
    EXPECT_CONTAINS(h, "#define BAR_STATE_QUOTED");

    EXPECT_CONTAINS(c, "Bar_dfa_INITIAL_trans");
    EXPECT_CONTAINS(c, "Bar_dfa_EXPR_trans");
    EXPECT_CONTAINS(c, "Bar_dfa_QUOTED_trans");

    free(h);
    free(c);
    if (fails == saved) printf("test_state_constants: PASS\n");
    return fails - saved;
}

static int test_no_prefix_falls_back_to_lex(void) {
    int saved = fails;
    /* No %name_prefix.  Default should be "Lex". */
    const char *src =
        "rule a matches /a/ { /* */ }\n";
    char *h = NULL, *c = NULL;
    EXPECT(emit_for(src, &h, &c) == 0, "emit_for failed");
    if (!h || !c) { free(h); free(c); return fails - saved; }

    EXPECT_CONTAINS(h, "#define LEX_LEX_H");
    EXPECT_CONTAINS(h, "int Lex_match(");
    EXPECT_CONTAINS(c, "const char *const LexRuleNames[]");

    free(h);
    free(c);
    if (fails == saved) printf("test_no_prefix_falls_back_to_lex: PASS\n");
    return fails - saved;
}

static int test_eof_rule_in_names_table(void) {
    int saved = fails;
    /* EOF rules don't appear in the DFA but DO appear in the
    ** rule-names table (their global rule id is needed for
    ** future LexFeedEOF dispatch). */
    const char *src =
        "%name_prefix Eof.\n"
        "rule body matches /a/ { /* */ }\n"
        "rule end_of_input matches <<EOF>> { /* */ }\n";
    char *h = NULL, *c = NULL;
    EXPECT(emit_for(src, &h, &c) == 0, "emit_for failed");
    if (!h || !c) { free(h); free(c); return fails - saved; }

    EXPECT_CONTAINS(h, "EOF_RULE_BODY");
    EXPECT_CONTAINS(h, "EOF_RULE_END_OF_INPUT");
    EXPECT_CONTAINS(c, "\"body\"");
    EXPECT_CONTAINS(c, "\"end_of_input\"");

    free(h);
    free(c);
    if (fails == saved) printf("test_eof_rule_in_names_table: PASS\n");
    return fails - saved;
}

int main(void) {
    test_minimal_emit();
    test_state_constants();
    test_no_prefix_falls_back_to_lex();
    test_eof_rule_in_names_table();
    if (fails == 0) {
        printf("\ntest_lex_emit: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_emit: %d sub-test failure(s)\n", fails);
    return 1;
}
