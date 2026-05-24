/*
** tests/test_lex_parse.c -- M1.2 unit test for the .lex parser.
**
** Builds a LimeLexSpec from various .lex source snippets and
** asserts on the AST shape.  Each sub-test exercises one
** directive type or rule shape; the final sub-test parses a
** bootscanner-shaped grammar end-to-end.
**
** Pattern resolution (M1.3) and block desugaring (M1.4) are NOT
** exercised here -- the parser leaves {name} interpolations in
** rule patterns untouched and does not desugar <STATE>{...}
** blocks.  Those tests come in subsequent commits.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#define dup2 _dup2
#define close _close
#define open _open
#else
#include <unistd.h>
#endif
#include <fcntl.h>

#include "lex_ast.h"
#include "lex_parse.h"

/* ----- assertion helpers ----- */

static int fail_count = 0;

#define EXPECT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, "\n");                                  \
        fail_count++;                                           \
    }                                                           \
} while (0)

#define EXPECT_STR(got, want) do {                              \
    if ((got) == NULL) {                                        \
        fprintf(stderr, "  %s:%d: got NULL, want \"%s\"\n",     \
                __func__, __LINE__, (want));                    \
        fail_count++;                                           \
    } else if (strcmp((got), (want)) != 0) {                    \
        fprintf(stderr, "  %s:%d: got \"%s\", want \"%s\"\n",   \
                __func__, __LINE__, (got), (want));             \
        fail_count++;                                           \
    }                                                           \
} while (0)

static int list_len_pat(const LimeLexPattern *p) {
    int n = 0; while (p) { n++; p = p->next; } return n;
}
static int list_len_state(const LimeLexState *p) {
    int n = 0; while (p) { n++; p = p->next; } return n;
}
static int list_len_kw(const LimeLexKeywordTable *p) {
    int n = 0; while (p) { n++; p = p->next; } return n;
}
static int list_len_lb(const LimeLexLiteralBuffer *p) {
    int n = 0; while (p) { n++; p = p->next; } return n;
}
static int list_len_ruleset(const LimeLexRuleset *p) {
    int n = 0; while (p) { n++; p = p->next; } return n;
}
static int list_len_rule(const LimeLexRule *p) {
    int n = 0; while (p) { n++; p = p->next; } return n;
}

/* ----- sub-tests ----- */

static int test_empty(void) {
    fail_count = 0;
    LimeLexSpec *s = lime_lex_parse("<empty>", "", 0);
    EXPECT(s != NULL, "lime_lex_parse returned NULL");
    if (s) {
        EXPECT(s->error_count == 0, "errors=%d on empty input", s->error_count);
        EXPECT(s->name_prefix == NULL, "name_prefix should be NULL");
        EXPECT(list_len_pat(s->patterns) == 0, "patterns should be empty");
        EXPECT(list_len_state(s->states) == 0, "states should be empty");
        EXPECT(list_len_rule(s->rules) == 0, "rules should be empty");
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_empty: PASS\n");
    return fail_count;
}

static int test_simple_directives(void) {
    fail_count = 0;
    const char *src =
        "%name_prefix Foo.\n"
        "%token_prefix FOO_.\n"
        "%token_type { int }\n"
        "%location_type { struct LimeLocation }\n"
        "%lexer_extra_argument { void *user }\n"
        "%include {\n"
        "    #include \"foo.h\"\n"
        "}\n";
    LimeLexSpec *s = lime_lex_parse("<simple>", src, strlen(src));
    EXPECT(s != NULL && s->error_count == 0, "errors=%d", s ? s->error_count : -1);
    if (s) {
        EXPECT_STR(s->name_prefix, "Foo");
        EXPECT_STR(s->token_prefix, "FOO_");
        EXPECT_STR(s->token_type, " int ");
        EXPECT_STR(s->location_type, " struct LimeLocation ");
        EXPECT_STR(s->extra_argument, " void *user ");
        EXPECT(s->include_block != NULL, "include_block missing");
        EXPECT(strstr(s->include_block, "#include \"foo.h\"") != NULL,
               "include block content missing");
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_simple_directives: PASS\n");
    return fail_count;
}

static int test_patterns(void) {
    fail_count = 0;
    const char *src =
        "%pattern digit /[0-9]/.\n"
        "%pattern hexdigit /[0-9A-Fa-f]/.\n"
        "%pattern identifier /[A-Za-z_][A-Za-z0-9_]*/.\n";
    LimeLexSpec *s = lime_lex_parse("<patterns>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "errors=%d", s ? s->error_count : -1);
    if (s) {
        EXPECT(list_len_pat(s->patterns) == 3, "n_patterns=%d want 3",
               list_len_pat(s->patterns));
        if (s->patterns) {
            EXPECT_STR(s->patterns->name, "digit");
            EXPECT_STR(s->patterns->regex, "[0-9]");
            EXPECT_STR(s->patterns->next->name, "hexdigit");
            EXPECT_STR(s->patterns->next->regex, "[0-9A-Fa-f]");
            EXPECT_STR(s->patterns->next->next->name, "identifier");
            EXPECT_STR(s->patterns->next->next->regex, "[A-Za-z_][A-Za-z0-9_]*");
        }
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_patterns: PASS\n");
    return fail_count;
}

static int test_states(void) {
    fail_count = 0;
    const char *src =
        "%state EXPR.\n"
        "%exclusive_state QUOTED { char *tag; size_t len; }.\n"
        "%state_destructor QUOTED { free(state_data->tag); }.\n";
    LimeLexSpec *s = lime_lex_parse("<states>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "errors=%d", s ? s->error_count : -1);
    if (s) {
        EXPECT(list_len_state(s->states) == 2, "n_states=%d want 2",
               list_len_state(s->states));
        if (s->states) {
            EXPECT_STR(s->states->name, "EXPR");
            EXPECT(s->states->exclusive == 0, "EXPR should be inclusive");
            EXPECT(s->states->local_body == NULL, "EXPR should have no body");
            EXPECT(s->states->destructor == NULL, "EXPR should have no destructor");

            LimeLexState *q = s->states->next;
            EXPECT_STR(q->name, "QUOTED");
            EXPECT(q->exclusive == 1, "QUOTED should be exclusive");
            EXPECT(q->local_body != NULL, "QUOTED should have local body");
            EXPECT(q->destructor != NULL, "QUOTED should have destructor");
            EXPECT(strstr(q->local_body, "char *tag") != NULL, "local body content");
            EXPECT(strstr(q->destructor, "free(state_data->tag)") != NULL,
                   "destructor content");
        }
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_states: PASS\n");
    return fail_count;
}

static int test_keyword_table(void) {
    fail_count = 0;
    const char *src =
        "%keyword_table sql_kw (case_insensitive, prefix=K_) {\n"
        "    \"select\", \"from\", \"where\"\n"
        "}.\n";
    LimeLexSpec *s = lime_lex_parse("<kw>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "errors=%d", s ? s->error_count : -1);
    if (s) {
        EXPECT(list_len_kw(s->keyword_tables) == 1, "n_kw=%d want 1",
               list_len_kw(s->keyword_tables));
        if (s->keyword_tables) {
            LimeLexKeywordTable *k = s->keyword_tables;
            EXPECT_STR(k->name, "sql_kw");
            EXPECT(k->case_insensitive == 1, "should be case-insensitive");
            EXPECT_STR(k->prefix, "K_");
            EXPECT(k->n_keywords == 3, "n_keywords=%d want 3", k->n_keywords);
            if (k->n_keywords >= 3) {
                EXPECT_STR(k->keywords[0], "select");
                EXPECT_STR(k->keywords[1], "from");
                EXPECT_STR(k->keywords[2], "where");
            }
        }
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_keyword_table: PASS\n");
    return fail_count;
}

static int test_literal_buffer(void) {
    fail_count = 0;
    const char *src =
        "%literal_buffer scanstr {\n"
        "    type    char\n"
        "    initial 64\n"
        "    grow    \"*2\"\n"
        "    alloc   palloc\n"
        "    realloc repalloc\n"
        "    free    pfree\n"
        "}.\n";
    LimeLexSpec *s = lime_lex_parse("<lb>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "errors=%d", s ? s->error_count : -1);
    if (s) {
        EXPECT(list_len_lb(s->literal_buffers) == 1, "n_lb=%d want 1",
               list_len_lb(s->literal_buffers));
        if (s->literal_buffers) {
            LimeLexLiteralBuffer *b = s->literal_buffers;
            EXPECT_STR(b->name, "scanstr");
            EXPECT_STR(b->element_type, "char");
            EXPECT(b->initial_capacity == 64,
                   "initial=%d want 64", b->initial_capacity);
            EXPECT_STR(b->alloc_fn,   "palloc");
            EXPECT_STR(b->realloc_fn, "repalloc");
            EXPECT_STR(b->free_fn,    "pfree");
        }
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_literal_buffer: PASS\n");
    return fail_count;
}

static int test_top_level_rules(void) {
    fail_count = 0;
    const char *src =
        "rule plus matches /\\+/ { LEX_EMIT_NOVAL('+'); }\n"
        "<EXPR> rule lparen matches /\\(/ { LEX_EMIT_NOVAL('('); }\n"
        "<xq, xd, xb> rule eof_string matches <<EOF>> { abort(); }\n";
    LimeLexSpec *s = lime_lex_parse("<rules>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "errors=%d", s ? s->error_count : -1);
    if (s) {
        EXPECT(list_len_rule(s->rules) == 3, "n_rules=%d want 3",
               list_len_rule(s->rules));
        if (s->rules) {
            LimeLexRule *r = s->rules;
            /* Rule 1: plus, INITIAL (no states), pattern \+ */
            EXPECT_STR(r->name, "plus");
            EXPECT(r->n_states == 0, "n_states=%d want 0", r->n_states);
            EXPECT(r->is_eof == 0, "should not be EOF");
            EXPECT_STR(r->pattern, "\\+");
            EXPECT(strstr(r->action, "LEX_EMIT_NOVAL") != NULL,
                   "action content");

            /* Rule 2: lparen, <EXPR> */
            r = r->next;
            EXPECT_STR(r->name, "lparen");
            EXPECT(r->n_states == 1, "n_states=%d want 1", r->n_states);
            if (r->n_states >= 1) EXPECT_STR(r->states[0], "EXPR");
            EXPECT_STR(r->pattern, "\\(");

            /* Rule 3: eof_string, <xq, xd, xb> with <<EOF>>. */
            r = r->next;
            EXPECT_STR(r->name, "eof_string");
            EXPECT(r->n_states == 3, "n_states=%d want 3", r->n_states);
            EXPECT(r->is_eof == 1, "should be EOF rule");
            EXPECT(r->pattern == NULL, "EOF rule should have no pattern");
            if (r->n_states >= 3) {
                EXPECT_STR(r->states[0], "xq");
                EXPECT_STR(r->states[1], "xd");
                EXPECT_STR(r->states[2], "xb");
            }
        }
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_top_level_rules: PASS\n");
    return fail_count;
}

static int test_ruleset_and_include(void) {
    fail_count = 0;
    const char *src =
        "%ruleset basic_punct {\n"
        "    rule comma  matches /,/  { LEX_EMIT_NOVAL(','); }\n"
        "    rule lparen matches /\\(/ { LEX_EMIT_NOVAL('('); }\n"
        "}.\n"
        "%lexer_include basic_punct, sql_keywords.\n";
    LimeLexSpec *s = lime_lex_parse("<rs>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "errors=%d", s ? s->error_count : -1);
    if (s) {
        EXPECT(list_len_ruleset(s->rulesets) == 1, "n_rulesets=%d want 1",
               list_len_ruleset(s->rulesets));
        if (s->rulesets) {
            LimeLexRuleset *rs = s->rulesets;
            EXPECT_STR(rs->name, "basic_punct");
            EXPECT(list_len_rule(rs->rules) == 2,
                   "ruleset rules=%d want 2", list_len_rule(rs->rules));
            if (rs->rules) {
                EXPECT_STR(rs->rules->name, "comma");
                EXPECT_STR(rs->rules->next->name, "lparen");
            }
        }
        EXPECT(s->n_lexer_includes == 2, "n_includes=%d want 2",
               s->n_lexer_includes);
        if (s->n_lexer_includes >= 2) {
            EXPECT_STR(s->lexer_includes[0], "basic_punct");
            EXPECT_STR(s->lexer_includes[1], "sql_keywords");
        }
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_ruleset_and_include: PASS\n");
    return fail_count;
}

static int test_bootscanner_shape(void) {
    fail_count = 0;
    /* End-to-end shape close to the bootscanner.l worked example
    ** in docs/LEXER_DESIGN.md.  All directive types in one
    ** spec; verifies they coexist. */
    const char *src =
        "%name_prefix     boot.\n"
        "%token_prefix    BOOT_.\n"
        "%token_type      { union BootValue }\n"
        "%location_type   { LimeLocation }\n"
        "%include {\n"
        "    union BootValue { const char *kw; char *str; };\n"
        "}\n"
        "%pattern id  /[-A-Za-z0-9_]+/.\n"
        "%pattern sid /'([^']|'')*'/.\n"
        "%keyword_table boot_kw (case_sensitive, prefix=K_) {\n"
        "    \"open\", \"close\", \"create\", \"OID\", \"bootstrap\"\n"
        "}.\n"
        "rule whitespace matches /[ \\t\\r]+/ { /* skip */ }\n"
        "rule comma matches /,/ { LEX_EMIT_NOVAL(BOOT_COMMA); }\n"
        "rule ident matches /{id}/ {\n"
        "    int kw = boot_kw_lookup(matched, matched_len);\n"
        "    if (kw >= 0) LEX_EMIT(kw, NULL);\n"
        "    else LEX_EMIT(BOOT_ID, NULL);\n"
        "}\n";
    LimeLexSpec *s = lime_lex_parse("<bootscanner>", src, strlen(src));
    EXPECT(s != NULL, "spec NULL");
    if (s) {
        EXPECT(s->error_count == 0, "error_count=%d", s->error_count);
        EXPECT_STR(s->name_prefix, "boot");
        EXPECT_STR(s->token_prefix, "BOOT_");
        EXPECT(list_len_pat(s->patterns) == 2, "n_patterns=%d want 2",
               list_len_pat(s->patterns));
        EXPECT(list_len_kw(s->keyword_tables) == 1, "n_kw=%d want 1",
               list_len_kw(s->keyword_tables));
        if (s->keyword_tables) {
            EXPECT(s->keyword_tables->n_keywords == 5,
                   "kw[0].n=%d want 5", s->keyword_tables->n_keywords);
        }
        EXPECT(list_len_rule(s->rules) == 3, "n_rules=%d want 3",
               list_len_rule(s->rules));
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_bootscanner_shape: PASS\n");
    return fail_count;
}

static int test_error_recovery(void) {
    fail_count = 0;
    /* Inject a malformed directive between valid ones; verify
    ** the parser flags the error and continues to parse what
    ** follows.  Suppress the deliberate stderr diagnostic. */
    fflush(stderr);
    int saved_stderr = dup(2);
    int devnull = open("/dev/null", 1 /* O_WRONLY */);
    if (devnull >= 0) { dup2(devnull, 2); close(devnull); }
    const char *src =
        "%name_prefix Foo.\n"
        "%pattern\n"            /* missing name + regex + dot */
        "%token_prefix FOO_.\n";
    LimeLexSpec *s = lime_lex_parse("<errors>", src, strlen(src));
    fflush(stderr);
    if (saved_stderr >= 0) { dup2(saved_stderr, 2); close(saved_stderr); }
    EXPECT(s != NULL, "spec NULL");
    if (s) {
        EXPECT(s->error_count >= 1, "expected >=1 error, got %d",
               s->error_count);
        EXPECT_STR(s->name_prefix, "Foo");
        EXPECT_STR(s->token_prefix, "FOO_");
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_error_recovery: PASS\n");
    return fail_count;
}

static int test_state_block_desugaring(void) {
    fail_count = 0;
    /* M1.4: <STATES> { rule ... rule ... } block form.
    ** Each inner rule should inherit the outer state qualifier. */
    const char *src =
        "<EXPR> {\n"
        "    rule plus  matches /\\+/ { LEX_EMIT_NOVAL('+'); }\n"
        "    rule minus matches /-/  { LEX_EMIT_NOVAL('-'); }\n"
        "    rule times matches /\\*/ { LEX_EMIT_NOVAL('*'); }\n"
        "}\n";
    LimeLexSpec *s = lime_lex_parse("<block>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "errors=%d", s ? s->error_count : -1);
    if (s) {
        EXPECT(list_len_rule(s->rules) == 3, "n_rules=%d want 3",
               list_len_rule(s->rules));
        for (LimeLexRule *r = s->rules; r; r = r->next) {
            EXPECT(r->n_states == 1, "%s: n_states=%d want 1",
                   r->name, r->n_states);
            if (r->n_states >= 1) EXPECT_STR(r->states[0], "EXPR");
        }
        if (s->rules) {
            EXPECT_STR(s->rules->name, "plus");
            EXPECT_STR(s->rules->next->name, "minus");
            EXPECT_STR(s->rules->next->next->name, "times");
        }
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_state_block_desugaring: PASS\n");
    return fail_count;
}

static int test_multi_state_block_desugaring(void) {
    fail_count = 0;
    /* Block with multi-state qualifier: each inner rule should
    ** inherit ALL of the outer states. */
    const char *src =
        "<xq, xqc, xe> {\n"
        "    rule eof  matches <<EOF>> { abort(); }\n"
        "    rule body matches /[^']+/ { /* */ }\n"
        "}\n";
    LimeLexSpec *s = lime_lex_parse("<mblock>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "errors=%d", s ? s->error_count : -1);
    if (s) {
        EXPECT(list_len_rule(s->rules) == 2, "n_rules=%d want 2",
               list_len_rule(s->rules));
        for (LimeLexRule *r = s->rules; r; r = r->next) {
            EXPECT(r->n_states == 3, "%s: n_states=%d want 3",
                   r->name, r->n_states);
            if (r->n_states >= 3) {
                EXPECT_STR(r->states[0], "xq");
                EXPECT_STR(r->states[1], "xqc");
                EXPECT_STR(r->states[2], "xe");
            }
        }
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_multi_state_block_desugaring: PASS\n");
    return fail_count;
}

static int test_block_inner_qualifier_rejected(void) {
    fail_count = 0;
    /* Inner rules MUST NOT have their own state qualifier inside
    ** a block; the parser flags it as an error and continues. */
    fflush(stderr);
    int saved_stderr = dup(2);
    int devnull = open("/dev/null", 1);
    if (devnull >= 0) { dup2(devnull, 2); close(devnull); }

    const char *src =
        "<EXPR> {\n"
        "    <OTHER> rule confused matches /x/ { /* */ }\n"
        "    rule plain matches /y/ { /* */ }\n"
        "}\n";
    LimeLexSpec *s = lime_lex_parse("<bad-block>", src, strlen(src));

    fflush(stderr);
    if (saved_stderr >= 0) { dup2(saved_stderr, 2); close(saved_stderr); }

    EXPECT(s != NULL, "spec NULL");
    if (s) {
        EXPECT(s->error_count >= 1,
               "expected >=1 error for inner-qualifier, got %d",
               s->error_count);
        /* Both rules should still parse (with the outer EXPR state
        ** -- the inner qualifier is consumed but ignored). */
        EXPECT(list_len_rule(s->rules) >= 1,
               "expected at least one rule parsed despite error, got %d",
               list_len_rule(s->rules));
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_block_inner_qualifier_rejected: PASS\n");
    return fail_count;
}

int main(void) {
    int total = 0;
    /* Suppress stderr noise for the deliberate-error sub-test by
    ** redirecting it; restore after. */
    total += test_empty();
    total += test_simple_directives();
    total += test_patterns();
    total += test_states();
    total += test_keyword_table();
    total += test_literal_buffer();
    total += test_top_level_rules();
    total += test_ruleset_and_include();
    total += test_bootscanner_shape();
    total += test_error_recovery();
    total += test_state_block_desugaring();
    total += test_multi_state_block_desugaring();
    total += test_block_inner_qualifier_rejected();
    if (total == 0) {
        printf("\ntest_lex_parse: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_parse: %d sub-test failure(s)\n", total);
    return 1;
}
