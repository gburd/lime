/*
** examples/pg_bootscanner/test_bootscanner.c -- end-to-end
** test for the M5.1 port of PG's bootscanner.l.
**
** What this verifies:
**
**   1. The bootscanner.lex source compiles via `lime -X` (the
**      meson custom_target invokes the generator; if this
**      driver builds at all, that step worked).
**   2. The generated push-driven runtime tokenises real BKI
**      input the same way the original flex scanner does.
**   3. Action primitives exercised by the port behave per
**      spec: LEX_SKIP() drops whitespace/newlines/comments,
**      auto-emit fires for every other matched rule, and
**      LEX_ERROR_AT() is raised by the catch-all `.` rule on
**      truly unmatched input.
**
** GROUND TRUTH.  We don't run flex against the same fixture;
** instead each EXPECT() assertion encodes the rule id and
** text that flex would have emitted (read from the original
** bootscanner.l line by line).  When PG eventually consumes
** this port via a real parser-side shim, the test will be
** rebased onto pg-regress's own bootstrap test corpus.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootscanner_lex.h"

static int fails = 0;

#define EXPECT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, "\n");                                  \
        fails++;                                                \
    }                                                           \
} while (0)

#define MAX_TOKENS 64

struct capture {
    int    n;
    int    rules[MAX_TOKENS];
    char   texts[MAX_TOKENS][64];
};

static void emit_cb(void *user, int rule,
                    const char *text, size_t len) {
    struct capture *c = user;
    if (c->n >= MAX_TOKENS) return;
    c->rules[c->n] = rule;
    if (len >= sizeof(c->texts[0])) len = sizeof(c->texts[0]) - 1;
    memcpy(c->texts[c->n], text, len);
    c->texts[c->n][len] = '\0';
    c->n++;
}

static BootLexResult feed(const char *input, struct capture *out) {
    BootLexer *lex = BootLexAlloc(malloc);
    if (!lex) return BOOT_LEX_ERROR;
    out->n = 0;
    BootLexResult r = BootLexFeedBytes(lex, input, strlen(input),
                                       emit_cb, out);
    if (r == BOOT_LEX_OK) {
        BootLexFeedEOF(lex, emit_cb, out);
    }
    BootLexFree(lex, free);
    return r;
}

/* Pretty-print the captured token stream on assertion failure
** so a regression is debuggable from CI logs alone. */
static void dump_capture(const struct capture *c) {
    fprintf(stderr, "  captured %d token(s):\n", c->n);
    for (int i = 0; i < c->n; i++) {
        fprintf(stderr, "    [%2d] rule=%2d (%s) text=\"%s\"\n",
                i, c->rules[i],
                BootRuleNames[c->rules[i]],
                c->texts[i]);
    }
}

/* ----- sub-tests ----- */

/* Single keyword: the simplest case.  Verifies a keyword rule
** beats the catch-all `ident` rule on declaration-order tie. */
static int test_single_keyword(void) {
    int saved = fails;
    struct capture c = {0};
    BootLexResult r = feed("bootstrap", &c);
    EXPECT(r == BOOT_LEX_OK, "feed failed (%d)", r);
    EXPECT(c.n == 1, "want 1 token, got %d", c.n);
    if (c.n >= 1) {
        EXPECT(c.rules[0] == BOOT_RULE_KW_BOOTSTRAP,
               "rule[0]=%d want KW_BOOTSTRAP=%d (%s)",
               c.rules[0], BOOT_RULE_KW_BOOTSTRAP,
               BootRuleNames[c.rules[0]]);
        EXPECT(strcmp(c.texts[0], "bootstrap") == 0,
               "text[0]=\"%s\" want \"bootstrap\"", c.texts[0]);
    }
    if (fails != saved) dump_capture(&c);
    if (fails == saved) printf("test_single_keyword: PASS\n");
    return fails - saved;
}

/* Identifier longer than any keyword: longest-match wins, so
** the `ident` rule fires even though the prefix matches `open`. */
static int test_identifier_beats_keyword_prefix(void) {
    int saved = fails;
    struct capture c = {0};
    BootLexResult r = feed("openrelation", &c);
    EXPECT(r == BOOT_LEX_OK, "feed failed (%d)", r);
    EXPECT(c.n == 1, "want 1 token, got %d", c.n);
    if (c.n >= 1) {
        EXPECT(c.rules[0] == BOOT_RULE_IDENT,
               "rule[0]=%d want IDENT=%d (%s)",
               c.rules[0], BOOT_RULE_IDENT,
               BootRuleNames[c.rules[0]]);
        EXPECT(strcmp(c.texts[0], "openrelation") == 0,
               "text[0]=\"%s\" want \"openrelation\"", c.texts[0]);
    }
    if (fails != saved) dump_capture(&c);
    if (fails == saved) printf("test_identifier_beats_keyword_prefix: PASS\n");
    return fails - saved;
}

/* The reserved word _null_ (NULLVAL in the original flex).
** Listed before the catch-all id rule, so it wins on length tie. */
static int test_null_literal(void) {
    int saved = fails;
    struct capture c = {0};
    EXPECT(feed("_null_", &c) == BOOT_LEX_OK, "feed failed");
    EXPECT(c.n == 1, "want 1 token, got %d", c.n);
    if (c.n >= 1) {
        EXPECT(c.rules[0] == BOOT_RULE_NULL_LITERAL,
               "rule[0]=%d want NULL_LITERAL=%d",
               c.rules[0], BOOT_RULE_NULL_LITERAL);
    }
    if (fails != saved) dump_capture(&c);
    if (fails == saved) printf("test_null_literal: PASS\n");
    return fails - saved;
}

/* Whitespace, newlines, and comments must be SKIPPED, not
** emitted.  Confirms LEX_SKIP() in three different rules. */
static int test_whitespace_and_comments(void) {
    int saved = fails;
    struct capture c = {0};
    /* Two keywords surrounded by every flavour of "skipped"
    ** input the scanner recognises. */
    EXPECT(feed("  open\t\rclose\n# comment trailing\nbuild  ", &c)
           == BOOT_LEX_OK, "feed failed");
    EXPECT(c.n == 3, "want 3 tokens (whitespace skipped), got %d", c.n);
    if (c.n >= 3) {
        EXPECT(c.rules[0] == BOOT_RULE_KW_OPEN,
               "[0]: rule=%d (%s) want KW_OPEN",
               c.rules[0], BootRuleNames[c.rules[0]]);
        EXPECT(c.rules[1] == BOOT_RULE_KW_CLOSE,
               "[1]: rule=%d (%s) want KW_CLOSE",
               c.rules[1], BootRuleNames[c.rules[1]]);
        EXPECT(c.rules[2] == BOOT_RULE_KW_BUILD,
               "[2]: rule=%d (%s) want KW_BUILD",
               c.rules[2], BootRuleNames[c.rules[2]]);
    }
    if (fails != saved) dump_capture(&c);
    if (fails == saved) printf("test_whitespace_and_comments: PASS\n");
    return fails - saved;
}

/* Punctuation: the four single-char tokens that flex returned
** as character literals.  Lime emits them as their own rules. */
static int test_punctuation(void) {
    int saved = fails;
    struct capture c = {0};
    EXPECT(feed("(,=)", &c) == BOOT_LEX_OK, "feed failed");
    EXPECT(c.n == 4, "want 4 tokens, got %d", c.n);
    if (c.n >= 4) {
        EXPECT(c.rules[0] == BOOT_RULE_LPAREN, "[0]: %s",
               BootRuleNames[c.rules[0]]);
        EXPECT(c.rules[1] == BOOT_RULE_COMMA, "[1]: %s",
               BootRuleNames[c.rules[1]]);
        EXPECT(c.rules[2] == BOOT_RULE_EQUALS, "[2]: %s",
               BootRuleNames[c.rules[2]]);
        EXPECT(c.rules[3] == BOOT_RULE_RPAREN, "[3]: %s",
               BootRuleNames[c.rules[3]]);
    }
    if (fails != saved) dump_capture(&c);
    if (fails == saved) printf("test_punctuation: PASS\n");
    return fails - saved;
}

/* Single-quoted string with an embedded escaped quote.  The
** flex pattern is '([^']|'')*' -- a quoted string where ''
** doubles as an escaped quote. */
static int test_quoted_string(void) {
    int saved = fails;
    struct capture c = {0};
    /* Plain quoted string. */
    EXPECT(feed("'hello world'", &c) == BOOT_LEX_OK, "feed failed");
    EXPECT(c.n == 1, "want 1 token, got %d", c.n);
    if (c.n >= 1) {
        EXPECT(c.rules[0] == BOOT_RULE_SQSTRING,
               "[0]: rule=%d (%s) want SQSTRING",
               c.rules[0], BootRuleNames[c.rules[0]]);
        EXPECT(strcmp(c.texts[0], "'hello world'") == 0,
               "[0]: text=\"%s\" want \"'hello world'\"",
               c.texts[0]);
    }
    if (fails != saved) dump_capture(&c);

    /* Embedded escape: 'it''s' -> the whole string including
    ** the doubled quotes is one match. */
    struct capture c2 = {0};
    EXPECT(feed("'it''s'", &c2) == BOOT_LEX_OK, "feed (escape) failed");
    EXPECT(c2.n == 1, "(escape) want 1 token, got %d", c2.n);
    if (c2.n >= 1) {
        EXPECT(c2.rules[0] == BOOT_RULE_SQSTRING,
               "(escape) [0]: %s",
               BootRuleNames[c2.rules[0]]);
        EXPECT(strcmp(c2.texts[0], "'it''s'") == 0,
               "(escape) [0]: text=\"%s\"", c2.texts[0]);
    }
    if (fails != saved) dump_capture(&c2);

    if (fails == saved) printf("test_quoted_string: PASS\n");
    return fails - saved;
}

/* A realistic BKI directive.  The exact byte stream
**     create bootstrap pg_proc 1255 bootstrap rowtype_oid 81
** is the kind of line the original bootscanner is fed when
** initdb processes postgres.bki.  This is the "is the port
** actually wired up end-to-end" test. */
static int test_realistic_bki_line(void) {
    int saved = fails;
    struct capture c = {0};
    const char *input =
        "create bootstrap pg_proc 1255 bootstrap rowtype_oid 81\n";
    EXPECT(feed(input, &c) == BOOT_LEX_OK, "feed failed");
    /* Expected stream (whitespace dropped):
    ** create  bootstrap  pg_proc  1255  bootstrap  rowtype_oid  81 */
    EXPECT(c.n == 7, "want 7 tokens, got %d", c.n);
    static const struct { int rule; const char *text; } expected[] = {
        { BOOT_RULE_KW_CREATE,      "create"      },
        { BOOT_RULE_KW_BOOTSTRAP,   "bootstrap"   },
        { BOOT_RULE_IDENT,          "pg_proc"     },
        { BOOT_RULE_IDENT,          "1255"        },
        { BOOT_RULE_KW_BOOTSTRAP,   "bootstrap"   },
        { BOOT_RULE_KW_ROWTYPE_OID, "rowtype_oid" },
        { BOOT_RULE_IDENT,          "81"          },
    };
    int n = sizeof(expected) / sizeof(expected[0]);
    if (c.n >= n) {
        for (int i = 0; i < n; i++) {
            EXPECT(c.rules[i] == expected[i].rule,
                   "[%d]: rule=%d (%s) want %s",
                   i, c.rules[i], BootRuleNames[c.rules[i]],
                   BootRuleNames[expected[i].rule]);
            EXPECT(strcmp(c.texts[i], expected[i].text) == 0,
                   "[%d]: text=\"%s\" want \"%s\"",
                   i, c.texts[i], expected[i].text);
        }
    }
    if (fails != saved) dump_capture(&c);
    if (fails == saved) printf("test_realistic_bki_line: PASS\n");
    return fails - saved;
}

/* A second realistic line: the form of a "declare index"
** directive complete with the parenthesised column list and
** the embedded index-method qualifier. */
static int test_index_directive(void) {
    int saved = fails;
    struct capture c = {0};
    const char *input =
        "declare unique index pg_class_oid_index 2662 "
        "on pg_class using btree(oid OID)\n";
    EXPECT(feed(input, &c) == BOOT_LEX_OK, "feed failed");
    /* declare unique index pg_class_oid_index 2662 on pg_class
    ** using btree ( oid OID ) */
    EXPECT(c.n == 13, "want 13 tokens, got %d", c.n);
    static const struct { int rule; const char *text; } expected[] = {
        { BOOT_RULE_KW_DECLARE,       "declare"             },
        { BOOT_RULE_KW_UNIQUE,        "unique"              },
        { BOOT_RULE_KW_INDEX,         "index"               },
        { BOOT_RULE_IDENT,            "pg_class_oid_index"  },
        { BOOT_RULE_IDENT,            "2662"                },
        { BOOT_RULE_KW_ON,            "on"                  },
        { BOOT_RULE_IDENT,            "pg_class"            },
        { BOOT_RULE_KW_USING,         "using"               },
        { BOOT_RULE_IDENT,            "btree"               },
        { BOOT_RULE_LPAREN,           "("                   },
        { BOOT_RULE_IDENT,            "oid"                 },
        { BOOT_RULE_KW_OID,           "OID"                 },
        { BOOT_RULE_RPAREN,           ")"                   },
    };
    int n = sizeof(expected) / sizeof(expected[0]);
    if (c.n >= n) {
        for (int i = 0; i < n; i++) {
            EXPECT(c.rules[i] == expected[i].rule,
                   "[%d]: rule=%d (%s) want %s",
                   i, c.rules[i], BootRuleNames[c.rules[i]],
                   BootRuleNames[expected[i].rule]);
            EXPECT(strcmp(c.texts[i], expected[i].text) == 0,
                   "[%d]: text=\"%s\" want \"%s\"",
                   i, c.texts[i], expected[i].text);
        }
    }
    if (fails != saved) dump_capture(&c);
    if (fails == saved) printf("test_index_directive: PASS\n");
    return fails - saved;
}

/* Insert with a quoted string literal and a _null_ literal.
** Mirrors the form of `insert OID = N ( v1 v2 _null_ ... )`
** that postgres.bki uses heavily. */
static int test_insert_with_string_and_null(void) {
    int saved = fails;
    struct capture c = {0};
    const char *input =
        "insert ( 'a string with ''quoted'' word' _null_ )";
    EXPECT(feed(input, &c) == BOOT_LEX_OK, "feed failed");
    /* insert ( 'a string with ''quoted'' word' _null_ ) */
    EXPECT(c.n == 5, "want 5 tokens, got %d", c.n);
    static const struct { int rule; const char *text; } expected[] = {
        { BOOT_RULE_KW_INSERT,    "insert"                              },
        { BOOT_RULE_LPAREN,       "("                                   },
        { BOOT_RULE_SQSTRING,     "'a string with ''quoted'' word'"     },
        { BOOT_RULE_NULL_LITERAL, "_null_"                              },
        { BOOT_RULE_RPAREN,       ")"                                   },
    };
    int n = sizeof(expected) / sizeof(expected[0]);
    if (c.n >= n) {
        for (int i = 0; i < n; i++) {
            EXPECT(c.rules[i] == expected[i].rule,
                   "[%d]: %s want %s",
                   i, BootRuleNames[c.rules[i]],
                   BootRuleNames[expected[i].rule]);
            EXPECT(strcmp(c.texts[i], expected[i].text) == 0,
                   "[%d]: text=\"%s\" want \"%s\"",
                   i, c.texts[i], expected[i].text);
        }
    }
    if (fails != saved) dump_capture(&c);
    if (fails == saved) printf("test_insert_with_string_and_null: PASS\n");
    return fails - saved;
}

/* Unmatched character must trigger LEX_ERROR_AT.  In the
** original flex, this is `.  { elog(ERROR, ...); }`; in the
** port it's the catch-all `unexpected` rule.  The byte `@` is
** not part of any other rule's pattern. */
static int test_unexpected_char(void) {
    int saved = fails;
    BootLexer *lex = BootLexAlloc(malloc);
    EXPECT(lex != NULL, "alloc returned NULL");
    if (!lex) return fails - saved;

    struct capture c = {0};
    BootLexResult r = BootLexFeedBytes(lex, "open @ close", 12,
                                       emit_cb, &c);
    EXPECT(r == BOOT_LEX_ERROR, "want LEX_ERROR, got %d", r);
    /* "open" was emitted before the bad byte. */
    EXPECT(c.n >= 1, "should have captured at least KW_OPEN, got %d", c.n);
    if (c.n >= 1) {
        EXPECT(c.rules[0] == BOOT_RULE_KW_OPEN,
               "[0]: %s", BootRuleNames[c.rules[0]]);
    }
    const char *msg = BootLexErrorMessage(lex);
    EXPECT(msg != NULL, "err_msg should be set");
    if (msg) {
        EXPECT(strstr(msg, "syntax error") != NULL,
               "err_msg=\"%s\" should mention 'syntax error'", msg);
    }
    BootLexFree(lex, free);
    if (fails != saved) dump_capture(&c);
    if (fails == saved) printf("test_unexpected_char: PASS\n");
    return fails - saved;
}

int main(void) {
    test_single_keyword();
    test_identifier_beats_keyword_prefix();
    test_null_literal();
    test_whitespace_and_comments();
    test_punctuation();
    test_quoted_string();
    test_realistic_bki_line();
    test_index_directive();
    test_insert_with_string_and_null();
    test_unexpected_char();

    if (fails == 0) {
        printf("\ntest_bootscanner: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_bootscanner: %d sub-test failure(s)\n", fails);
    return 1;
}
