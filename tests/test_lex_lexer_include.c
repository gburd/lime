/*
** tests/test_lex_lexer_include.c -- M4.1 integration test.
**
** %lexer_include filters which rulesets contribute rules to the
** compiled DFA.  Verifies:
**
**   1. No %lexer_include => every ruleset's rules compile in
**      declaration order (legacy behaviour preserved).
**   2. %lexer_include foo. => only foo's rules compile; sibling
**      rulesets are silently skipped.
**   3. %lexer_include order overrides ruleset declaration order
**      for compile priority on length ties.
**   4. Top-level rules are always included (they're not in any
**      ruleset, so include filtering doesn't apply).
**   5. Undefined ruleset name in %lexer_include is a hard error
**      (compile->error_count > 0) and a clear stderr diagnostic.
**
** Pipeline: parse -> resolve -> compile, then drive the INITIAL
** DFA on sample inputs to confirm rule firing.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#define dup2 _dup2
#define close _close
#define open _open
#define STDERR_FILENO 2
typedef long long ssize_t;
#else
#include <unistd.h>
#endif

#include "lex_ast.h"
#include "lex_compile.h"
#include "lex_dfa.h"
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

/* Drive the compiled DFA and return either the global rule
** index (mapped via cs->rule_indices), or -1 if the input isn't
** accepted.  Single-byte inputs only -- the test grammars below
** all use single-character regexes. */
static int compile_match(const LimeLexCompiledState *cs,
                         const char *input, size_t n) {
    if (!cs || !cs->dfa) return -1;
    int s = cs->dfa->start;
    for (size_t i = 0; i < n; i++) {
        s = cs->dfa->states[s].trans[(unsigned char)input[i]];
        if (s < 0) return -1;
    }
    if (!cs->dfa->states[s].is_accept) return -1;
    int local_rule = cs->dfa->states[s].accept_rule;
    if (local_rule < 0 || local_rule >= cs->n_rules) return -1;
    return cs->rule_indices[local_rule];
}

/* Compile a source string; returns NULL on parse/resolve error.
** Prints diagnostics on failure. */
static LimeLexCompiled *compile_source(const char *src) {
    LimeLexSpec *spec = lime_lex_parse("<test>", src, strlen(src));
    if (!spec || spec->error_count > 0) {
        if (spec) lime_lex_spec_free(spec);
        return NULL;
    }
    if (lime_lex_resolve_patterns(spec) != 0) {
        lime_lex_spec_free(spec);
        return NULL;
    }
    LimeLexCompiled *c = lime_lex_compile(spec);
    lime_lex_spec_free(spec);
    return c;
}

/* Capture stderr so we can assert the diagnostic message for
** the undefined-include case without polluting test output. */
static char captured_stderr[4096];

static void capture_stderr_begin(int *saved_fd, int *pipe_rd) {
    fflush(stderr);
    int fds[2];
    if (pipe(fds) != 0) { *saved_fd = -1; *pipe_rd = -1; return; }
    *saved_fd = dup(STDERR_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    *pipe_rd = fds[0];
}

static void capture_stderr_end(int saved_fd, int pipe_rd) {
    fflush(stderr);
    dup2(saved_fd, STDERR_FILENO);
    close(saved_fd);
    ssize_t got = read(pipe_rd, captured_stderr,
                       sizeof(captured_stderr) - 1);
    if (got < 0) got = 0;
    captured_stderr[got] = '\0';
    close(pipe_rd);
}

/* ----- sub-tests ----- */

/* No %lexer_include => legacy behaviour: every ruleset is in. */
static int test_no_include_means_all(void) {
    int saved = fails;
    const char *src =
        "%ruleset alpha {\n"
        "    rule a matches /a/ { /* */ }\n"
        "}.\n"
        "%ruleset beta {\n"
        "    rule b matches /b/ { /* */ }\n"
        "}.\n";
    LimeLexCompiled *c = compile_source(src);
    EXPECT(c != NULL, "compile NULL");
    if (c) {
        EXPECT(c->error_count == 0, "error_count=%d want 0", c->error_count);
        const LimeLexCompiledState *cs =
            lime_lex_compiled_find_state(c, "INITIAL");
        EXPECT(cs && cs->n_rules == 2,
               "INITIAL n_rules=%d want 2 (legacy: all rulesets)",
               cs ? cs->n_rules : -1);
        if (cs) {
            EXPECT(compile_match(cs, "a", 1) == 0, "a -> rule 0");
            EXPECT(compile_match(cs, "b", 1) == 1, "b -> rule 1");
        }
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_no_include_means_all: PASS\n");
    return fails - saved;
}

/* %lexer_include alpha. drops beta entirely. */
static int test_include_filters(void) {
    int saved = fails;
    const char *src =
        "%ruleset alpha {\n"
        "    rule a matches /a/ { /* */ }\n"
        "}.\n"
        "%ruleset beta {\n"
        "    rule b matches /b/ { /* */ }\n"
        "}.\n"
        "%lexer_include alpha.\n";
    LimeLexCompiled *c = compile_source(src);
    EXPECT(c != NULL, "compile NULL");
    if (c) {
        EXPECT(c->error_count == 0, "error_count=%d want 0", c->error_count);
        const LimeLexCompiledState *cs =
            lime_lex_compiled_find_state(c, "INITIAL");
        EXPECT(cs && cs->n_rules == 1,
               "INITIAL n_rules=%d want 1 (only alpha)",
               cs ? cs->n_rules : -1);
        if (cs) {
            EXPECT(compile_match(cs, "a", 1) == 0, "a -> rule 0");
            EXPECT(compile_match(cs, "b", 1) == -1,
                   "b -> reject (beta filtered out)");
        }
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_include_filters: PASS\n");
    return fails - saved;
}

/* %lexer_include order overrides declaration order: beta is
** declared AFTER alpha but listed FIRST in the include, so
** beta's rules get the lower compile indices. */
static int test_include_order_overrides_declaration(void) {
    int saved = fails;
    const char *src =
        "%ruleset alpha {\n"
        "    rule x_a matches /x/ { /* */ }\n"
        "}.\n"
        "%ruleset beta {\n"
        "    rule x_b matches /x/ { /* */ }\n"
        "}.\n"
        "%lexer_include beta, alpha.\n";
    LimeLexCompiled *c = compile_source(src);
    EXPECT(c != NULL, "compile NULL");
    if (c) {
        EXPECT(c->error_count == 0, "error_count=%d want 0", c->error_count);
        const LimeLexCompiledState *cs =
            lime_lex_compiled_find_state(c, "INITIAL");
        EXPECT(cs && cs->n_rules == 2,
               "INITIAL n_rules=%d want 2", cs ? cs->n_rules : -1);
        if (cs) {
            /* On length-tie, the lower compile index wins.  beta
            ** is first in the include list, so x_b is rule 0. */
            EXPECT(compile_match(cs, "x", 1) == 0,
                   "x -> rule 0 (x_b: include order priority)");
        }
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_include_order_overrides_declaration: PASS\n");
    return fails - saved;
}

/* Top-level rules are not subject to filtering. */
static int test_top_level_rules_always_included(void) {
    int saved = fails;
    const char *src =
        "%ruleset extras {\n"
        "    rule b matches /b/ { /* */ }\n"
        "}.\n"
        "%lexer_include extras.\n"
        "rule a matches /a/ { /* */ }\n";
    LimeLexCompiled *c = compile_source(src);
    EXPECT(c != NULL, "compile NULL");
    if (c) {
        EXPECT(c->error_count == 0, "error_count=%d want 0", c->error_count);
        const LimeLexCompiledState *cs =
            lime_lex_compiled_find_state(c, "INITIAL");
        EXPECT(cs && cs->n_rules == 2,
               "INITIAL n_rules=%d want 2 (top-level + extras)",
               cs ? cs->n_rules : -1);
        if (cs) {
            /* Top-level rules go first, so 'a' = rule 0, 'b' = rule 1. */
            EXPECT(compile_match(cs, "a", 1) == 0, "a -> rule 0 (top-level)");
            EXPECT(compile_match(cs, "b", 1) == 1, "b -> rule 1 (extras)");
        }
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_top_level_rules_always_included: PASS\n");
    return fails - saved;
}

/* %lexer_include referencing an undefined ruleset is a hard
** error: error_count bumped, diagnostic emitted. */
static int test_undefined_include_diagnoses(void) {
    int saved = fails;
    const char *src =
        "%ruleset present {\n"
        "    rule p matches /p/ { /* */ }\n"
        "}.\n"
        "%lexer_include missing, present.\n";

    int saved_fd = -1, pipe_rd = -1;
    capture_stderr_begin(&saved_fd, &pipe_rd);
    LimeLexCompiled *c = compile_source(src);
    capture_stderr_end(saved_fd, pipe_rd);

    EXPECT(c != NULL, "compile NULL");
    if (c) {
        EXPECT(c->error_count >= 1,
               "error_count=%d want >=1 (undefined ruleset)",
               c->error_count);
        EXPECT(strstr(captured_stderr, "missing") != NULL,
               "diagnostic should mention the bad name; got: %s",
               captured_stderr);
        EXPECT(strstr(captured_stderr, "lexer_include") != NULL,
               "diagnostic should mention %%lexer_include; got: %s",
               captured_stderr);
        /* The 'present' ruleset still compiles. */
        const LimeLexCompiledState *cs =
            lime_lex_compiled_find_state(c, "INITIAL");
        EXPECT(cs && cs->n_rules == 1,
               "INITIAL n_rules=%d want 1 (present only)",
               cs ? cs->n_rules : -1);
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_undefined_include_diagnoses: PASS\n");
    return fails - saved;
}

int main(void) {
    test_no_include_means_all();
    test_include_filters();
    test_include_order_overrides_declaration();
    test_top_level_rules_always_included();
    test_undefined_include_diagnoses();
    if (fails == 0) {
        printf("\ntest_lex_lexer_include: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr,
            "\ntest_lex_lexer_include: %d sub-test failure(s)\n", fails);
    return 1;
}
