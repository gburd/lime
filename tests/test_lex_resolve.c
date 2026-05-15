/*
** tests/test_lex_resolve.c -- M1.3 unit test for %pattern fragment
** resolution.
**
** Exercises:
**   - Single-level interpolation
**   - Multi-level (pattern referencing pattern referencing pattern)
**   - Cycle detection
**   - Undefined-reference detection
**   - POSIX repetition forms `{N}` / `{N,M}` pass through
**   - Character class `[...]` content not re-interpreted
**   - Backslash escapes (`\{` literal) preserved
**   - Resolution applied to rule patterns (top-level + ruleset)
**
** Diagnostics for cycle/undefined cases are deliberate; suppress
** stderr around them to keep test output clean.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "lex_ast.h"
#include "lex_parse.h"
#include "lex_resolve.h"

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

/* Suppress stderr around a code block for tests that exercise
** deliberate diagnostic paths. */
#define SUPPRESS_STDERR_BEGIN()                                  \
    int _saved_stderr = dup(2);                                  \
    do {                                                         \
        int _devnull = open("/dev/null", 1 /*O_WRONLY*/);        \
        if (_devnull >= 0) { dup2(_devnull, 2); close(_devnull); }\
    } while (0)
#define SUPPRESS_STDERR_END()                                    \
    do {                                                         \
        fflush(stderr);                                          \
        if (_saved_stderr >= 0) {                                \
            dup2(_saved_stderr, 2); close(_saved_stderr);        \
        }                                                        \
    } while (0)

/* ----- helpers ----- */

static LimeLexPattern *find_pattern_by_name(LimeLexSpec *s, const char *n) {
    for (LimeLexPattern *p = s->patterns; p; p = p->next) {
        if (strcmp(p->name, n) == 0) return p;
    }
    return NULL;
}

static LimeLexRule *find_rule_by_name(LimeLexSpec *s, const char *n) {
    for (LimeLexRule *r = s->rules; r; r = r->next) {
        if (strcmp(r->name, n) == 0) return r;
    }
    return NULL;
}

/* ----- sub-tests ----- */

static int test_single_level(void) {
    fail_count = 0;
    const char *src =
        "%pattern digit /[0-9]/.\n"
        "rule num matches /{digit}+/ { /* */ }\n";
    LimeLexSpec *s = lime_lex_parse("<single>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "parse errors");
    if (s) {
        int rc = lime_lex_resolve_patterns(s);
        EXPECT(rc == 0, "resolve returned %d", rc);
        EXPECT(s->error_count == 0, "resolve errors=%d", s->error_count);
        LimeLexPattern *digit = find_pattern_by_name(s, "digit");
        if (digit) EXPECT_STR(digit->expanded_regex, "[0-9]");
        LimeLexRule *num = find_rule_by_name(s, "num");
        if (num) EXPECT_STR(num->expanded_pattern, "([0-9])+");
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_single_level: PASS\n");
    return fail_count;
}

static int test_multi_level(void) {
    fail_count = 0;
    const char *src =
        "%pattern alpha /[A-Za-z_]/.\n"
        "%pattern alnum /[A-Za-z0-9_]/.\n"
        "%pattern ident /{alpha}{alnum}*/.\n"
        "rule id matches /{ident}/ { /* */ }\n";
    LimeLexSpec *s = lime_lex_parse("<multi>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "parse errors");
    if (s) {
        int rc = lime_lex_resolve_patterns(s);
        EXPECT(rc == 0, "resolve returned %d", rc);
        LimeLexPattern *ident = find_pattern_by_name(s, "ident");
        if (ident) {
            EXPECT_STR(ident->expanded_regex,
                       "([A-Za-z_])([A-Za-z0-9_])*");
        }
        LimeLexRule *id = find_rule_by_name(s, "id");
        if (id) EXPECT_STR(id->expanded_pattern,
                           "(([A-Za-z_])([A-Za-z0-9_])*)");
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_multi_level: PASS\n");
    return fail_count;
}

static int test_posix_repetition_passthrough(void) {
    fail_count = 0;
    /* {3} and {2,5} are POSIX repetition; must NOT be interpreted
    ** as pattern interpolations. */
    const char *src =
        "%pattern d /[0-9]/.\n"
        "rule three_digits matches /{d}{3}/ { /* */ }\n"
        "rule range_digits matches /{d}{2,5}/ { /* */ }\n";
    LimeLexSpec *s = lime_lex_parse("<posix>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "parse errors");
    if (s) {
        int rc = lime_lex_resolve_patterns(s);
        EXPECT(rc == 0, "resolve returned %d", rc);
        LimeLexRule *r3 = find_rule_by_name(s, "three_digits");
        if (r3) EXPECT_STR(r3->expanded_pattern, "([0-9]){3}");
        LimeLexRule *rr = find_rule_by_name(s, "range_digits");
        if (rr) EXPECT_STR(rr->expanded_pattern, "([0-9]){2,5}");
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_posix_repetition_passthrough: PASS\n");
    return fail_count;
}

static int test_char_class_literal(void) {
    fail_count = 0;
    /* Inside `[...]`, `{` and `}` are literal class members,
    ** NOT interpolation triggers.  Even if a class contains
    ** `{name}`, it must pass through verbatim. */
    const char *src =
        "%pattern x /[A-Z]/.\n"
        "rule literal_braces matches /[a-z{}]/ { /* */ }\n"
        "rule with_x matches /{x}+/ { /* */ }\n";
    LimeLexSpec *s = lime_lex_parse("<class>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "parse errors");
    if (s) {
        int rc = lime_lex_resolve_patterns(s);
        EXPECT(rc == 0, "resolve returned %d", rc);
        LimeLexRule *lb = find_rule_by_name(s, "literal_braces");
        if (lb) EXPECT_STR(lb->expanded_pattern, "[a-z{}]");
        LimeLexRule *wx = find_rule_by_name(s, "with_x");
        if (wx) EXPECT_STR(wx->expanded_pattern, "([A-Z])+");
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_char_class_literal: PASS\n");
    return fail_count;
}

static int test_backslash_escape(void) {
    fail_count = 0;
    /* `\{` in a regex is a literal `{`; must not trigger
    ** interpolation. */
    const char *src =
        "rule backslash matches /\\{not_a_pattern\\}/ { /* */ }\n";
    LimeLexSpec *s = lime_lex_parse("<bksl>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "parse errors");
    if (s) {
        int rc = lime_lex_resolve_patterns(s);
        EXPECT(rc == 0, "resolve returned %d", rc);
        LimeLexRule *r = find_rule_by_name(s, "backslash");
        if (r) EXPECT_STR(r->expanded_pattern, "\\{not_a_pattern\\}");
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_backslash_escape: PASS\n");
    return fail_count;
}

static int test_cycle_detection(void) {
    fail_count = 0;
    const char *src =
        "%pattern a /{b}x/.\n"
        "%pattern b /{a}y/.\n";
    LimeLexSpec *s;
    SUPPRESS_STDERR_BEGIN();
    s = lime_lex_parse("<cycle>", src, strlen(src));
    int rc = s ? lime_lex_resolve_patterns(s) : -1;
    SUPPRESS_STDERR_END();
    EXPECT(s != NULL, "spec NULL");
    EXPECT(rc != 0, "expected resolve failure on cycle, got %d", rc);
    if (s) {
        EXPECT(s->error_count >= 1, "expected error_count>=1 (cycle), got %d",
               s->error_count);
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_cycle_detection: PASS\n");
    return fail_count;
}

static int test_undefined_reference(void) {
    fail_count = 0;
    const char *src = "rule bad matches /{nonesuch}/ { /* */ }\n";
    LimeLexSpec *s;
    SUPPRESS_STDERR_BEGIN();
    s = lime_lex_parse("<undef>", src, strlen(src));
    int rc = s ? lime_lex_resolve_patterns(s) : -1;
    SUPPRESS_STDERR_END();
    EXPECT(s != NULL, "spec NULL");
    EXPECT(rc != 0, "expected resolve failure on undef ref, got %d", rc);
    if (s) {
        EXPECT(s->error_count >= 1,
               "expected error_count>=1 (undef), got %d", s->error_count);
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_undefined_reference: PASS\n");
    return fail_count;
}

static int test_ruleset_rules_resolved(void) {
    fail_count = 0;
    const char *src =
        "%pattern d /[0-9]/.\n"
        "%ruleset numbers {\n"
        "    rule integer matches /{d}+/ { /* */ }\n"
        "}.\n";
    LimeLexSpec *s = lime_lex_parse("<rs>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "parse errors");
    if (s) {
        int rc = lime_lex_resolve_patterns(s);
        EXPECT(rc == 0, "resolve returned %d", rc);
        if (s->rulesets && s->rulesets->rules) {
            LimeLexRule *r = s->rulesets->rules;
            EXPECT_STR(r->expanded_pattern, "([0-9])+");
        } else {
            EXPECT(0, "no rulesets/rules found");
        }
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_ruleset_rules_resolved: PASS\n");
    return fail_count;
}

static int test_eof_rule_unaffected(void) {
    fail_count = 0;
    /* <<EOF>> rules have NULL pattern and should not gain an
    ** expanded_pattern from resolution. */
    const char *src =
        "<xq> rule eof_xq matches <<EOF>> { /* */ }\n";
    LimeLexSpec *s = lime_lex_parse("<eof>", src, strlen(src));
    EXPECT(s && s->error_count == 0, "parse errors");
    if (s) {
        int rc = lime_lex_resolve_patterns(s);
        EXPECT(rc == 0, "resolve returned %d", rc);
        LimeLexRule *r = find_rule_by_name(s, "eof_xq");
        EXPECT(r != NULL, "missing eof rule");
        if (r) {
            EXPECT(r->is_eof == 1, "should be EOF rule");
            EXPECT(r->expanded_pattern == NULL,
                   "EOF rule expanded_pattern should be NULL");
        }
        lime_lex_spec_free(s);
    }
    if (fail_count == 0) printf("test_eof_rule_unaffected: PASS\n");
    return fail_count;
}

int main(void) {
    int total = 0;
    total += test_single_level();
    total += test_multi_level();
    total += test_posix_repetition_passthrough();
    total += test_char_class_literal();
    total += test_backslash_escape();
    total += test_cycle_detection();
    total += test_undefined_reference();
    total += test_ruleset_rules_resolved();
    total += test_eof_rule_unaffected();
    if (total == 0) {
        printf("\ntest_lex_resolve: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_resolve: %d sub-test failure(s)\n", total);
    return 1;
}
