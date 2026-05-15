/*
** tests/test_lex_regex.c -- M2.1 unit test for the regex parser.
**
** Exercises every grammar production: literals, escapes, char
** classes (positive + negated, ranges), wildcard, anchors,
** alternation, concatenation, quantifiers (* + ?), {n} and {n,m}
** repetitions, groups, error cases.
**
** Tests inspect AST shape directly via the LimeReNode struct.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex_regex.h"

static int fails = 0;

#define EXPECT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, "\n");                                  \
        fails++;                                                \
    }                                                           \
} while (0)

static LimeReNode *parse_ok(const char *src) {
    char *err = NULL;
    LimeReNode *n = lime_lex_regex_parse(src, &err);
    if (!n) {
        fprintf(stderr, "  parse_ok: \"%s\" failed: %s\n",
                src, err ? err : "(no message)");
        free(err);
        fails++;
        return NULL;
    }
    return n;
}

/* ----- sub-tests ----- */

static int test_empty(void) {
    int saved = fails;
    LimeReNode *n = parse_ok("");
    if (n) {
        EXPECT(n->kind == LIME_RE_EMPTY, "empty: kind=%d want EMPTY", n->kind);
        lime_lex_regex_free(n);
    }
    if (fails == saved) printf("test_empty: PASS\n");
    return fails - saved;
}

static int test_literal(void) {
    int saved = fails;
    LimeReNode *n = parse_ok("a");
    if (n) {
        EXPECT(n->kind == LIME_RE_LITERAL, "literal: kind=%d", n->kind);
        EXPECT(n->u.literal == 'a', "literal: byte=%d", n->u.literal);
        lime_lex_regex_free(n);
    }
    if (fails == saved) printf("test_literal: PASS\n");
    return fails - saved;
}

static int test_concat(void) {
    int saved = fails;
    LimeReNode *n = parse_ok("ab");
    if (n) {
        EXPECT(n->kind == LIME_RE_CONCAT, "concat: kind=%d", n->kind);
        if (n->kind == LIME_RE_CONCAT) {
            LimeReNode *l = n->u.binary.left;
            LimeReNode *r = n->u.binary.right;
            EXPECT(l && l->kind == LIME_RE_LITERAL && l->u.literal == 'a',
                   "concat.left wrong");
            EXPECT(r && r->kind == LIME_RE_LITERAL && r->u.literal == 'b',
                   "concat.right wrong");
        }
        lime_lex_regex_free(n);
    }
    if (fails == saved) printf("test_concat: PASS\n");
    return fails - saved;
}

static int test_alt(void) {
    int saved = fails;
    LimeReNode *n = parse_ok("a|b");
    if (n) {
        EXPECT(n->kind == LIME_RE_ALT, "alt: kind=%d", n->kind);
        if (n->kind == LIME_RE_ALT) {
            LimeReNode *l = n->u.binary.left;
            LimeReNode *r = n->u.binary.right;
            EXPECT(l && l->kind == LIME_RE_LITERAL && l->u.literal == 'a',
                   "alt.left wrong");
            EXPECT(r && r->kind == LIME_RE_LITERAL && r->u.literal == 'b',
                   "alt.right wrong");
        }
        lime_lex_regex_free(n);
    }
    if (fails == saved) printf("test_alt: PASS\n");
    return fails - saved;
}

static int test_quantifiers(void) {
    int saved = fails;
    /* a* */
    {
        LimeReNode *n = parse_ok("a*");
        if (n) {
            EXPECT(n->kind == LIME_RE_STAR, "a*: kind=%d", n->kind);
            EXPECT(n->u.unary.child &&
                   n->u.unary.child->kind == LIME_RE_LITERAL,
                   "a* child wrong");
            lime_lex_regex_free(n);
        }
    }
    /* a+ */
    {
        LimeReNode *n = parse_ok("a+");
        if (n) {
            EXPECT(n->kind == LIME_RE_PLUS, "a+: kind=%d", n->kind);
            lime_lex_regex_free(n);
        }
    }
    /* a? */
    {
        LimeReNode *n = parse_ok("a?");
        if (n) {
            EXPECT(n->kind == LIME_RE_QUESTION, "a?: kind=%d", n->kind);
            lime_lex_regex_free(n);
        }
    }
    /* a{3} */
    {
        LimeReNode *n = parse_ok("a{3}");
        if (n) {
            EXPECT(n->kind == LIME_RE_REPEAT, "a{3}: kind=%d", n->kind);
            EXPECT(n->u.repeat.min == 3 && n->u.repeat.max == 3,
                   "a{3}: min=%d max=%d", n->u.repeat.min, n->u.repeat.max);
            lime_lex_regex_free(n);
        }
    }
    /* a{2,5} */
    {
        LimeReNode *n = parse_ok("a{2,5}");
        if (n) {
            EXPECT(n->kind == LIME_RE_REPEAT, "a{2,5}: kind=%d", n->kind);
            EXPECT(n->u.repeat.min == 2 && n->u.repeat.max == 5,
                   "a{2,5}: min=%d max=%d", n->u.repeat.min, n->u.repeat.max);
            lime_lex_regex_free(n);
        }
    }
    /* a{2,} */
    {
        LimeReNode *n = parse_ok("a{2,}");
        if (n) {
            EXPECT(n->kind == LIME_RE_REPEAT, "a{2,}: kind=%d", n->kind);
            EXPECT(n->u.repeat.min == 2 && n->u.repeat.max == -1,
                   "a{2,}: min=%d max=%d", n->u.repeat.min, n->u.repeat.max);
            lime_lex_regex_free(n);
        }
    }
    if (fails == saved) printf("test_quantifiers: PASS\n");
    return fails - saved;
}

static int test_char_class(void) {
    int saved = fails;
    /* [a-z] */
    {
        LimeReNode *n = parse_ok("[a-z]");
        if (n) {
            EXPECT(n->kind == LIME_RE_CHAR_CLASS, "kind=%d", n->kind);
            EXPECT(n->u.char_class.negate == 0, "should not be negated");
            EXPECT(lime_lex_regex_class_has(n->u.char_class.bits, 'a'),
                   "class missing 'a'");
            EXPECT(lime_lex_regex_class_has(n->u.char_class.bits, 'm'),
                   "class missing 'm'");
            EXPECT(lime_lex_regex_class_has(n->u.char_class.bits, 'z'),
                   "class missing 'z'");
            EXPECT(!lime_lex_regex_class_has(n->u.char_class.bits, 'A'),
                   "class should not have 'A'");
            EXPECT(!lime_lex_regex_class_has(n->u.char_class.bits, '0'),
                   "class should not have '0'");
            lime_lex_regex_free(n);
        }
    }
    /* [^a-z] */
    {
        LimeReNode *n = parse_ok("[^a-z]");
        if (n) {
            EXPECT(n->kind == LIME_RE_CHAR_CLASS, "kind=%d", n->kind);
            EXPECT(n->u.char_class.negate == 1, "should be negated");
            EXPECT(lime_lex_regex_class_has(n->u.char_class.bits, 'a'),
                   "negated class bitmap should still have 'a' set");
            lime_lex_regex_free(n);
        }
    }
    /* [0-9A-Fa-f] */
    {
        LimeReNode *n = parse_ok("[0-9A-Fa-f]");
        if (n) {
            for (int c = '0'; c <= '9'; c++) {
                EXPECT(lime_lex_regex_class_has(n->u.char_class.bits,
                                                (unsigned char)c),
                       "missing '%c'", c);
            }
            for (int c = 'A'; c <= 'F'; c++) {
                EXPECT(lime_lex_regex_class_has(n->u.char_class.bits,
                                                (unsigned char)c),
                       "missing '%c'", c);
            }
            for (int c = 'a'; c <= 'f'; c++) {
                EXPECT(lime_lex_regex_class_has(n->u.char_class.bits,
                                                (unsigned char)c),
                       "missing '%c'", c);
            }
            EXPECT(!lime_lex_regex_class_has(n->u.char_class.bits, 'g'),
                   "should not have 'g'");
            lime_lex_regex_free(n);
        }
    }
    /* []]  -- ] as the first class member is literal */
    {
        LimeReNode *n = parse_ok("[]]");
        if (n) {
            EXPECT(n->kind == LIME_RE_CHAR_CLASS, "kind=%d", n->kind);
            EXPECT(lime_lex_regex_class_has(n->u.char_class.bits, ']'),
                   "literal-leading-]: missing ']'");
            lime_lex_regex_free(n);
        }
    }
    if (fails == saved) printf("test_char_class: PASS\n");
    return fails - saved;
}

static int test_escapes(void) {
    int saved = fails;
    /* \n */
    {
        LimeReNode *n = parse_ok("\\n");
        if (n) {
            EXPECT(n->kind == LIME_RE_LITERAL && n->u.literal == '\n',
                   "\\n: byte=%d", n->u.literal);
            lime_lex_regex_free(n);
        }
    }
    /* \xff */
    {
        LimeReNode *n = parse_ok("\\xff");
        if (n) {
            EXPECT(n->kind == LIME_RE_LITERAL && n->u.literal == 0xff,
                   "\\xff: byte=%d", n->u.literal);
            lime_lex_regex_free(n);
        }
    }
    /* \. is a literal . */
    {
        LimeReNode *n = parse_ok("\\.");
        if (n) {
            EXPECT(n->kind == LIME_RE_LITERAL && n->u.literal == '.',
                   "\\.: byte=%d", n->u.literal);
            lime_lex_regex_free(n);
        }
    }
    if (fails == saved) printf("test_escapes: PASS\n");
    return fails - saved;
}

static int test_wildcard_and_anchors(void) {
    int saved = fails;
    {
        LimeReNode *n = parse_ok(".");
        if (n) { EXPECT(n->kind == LIME_RE_ANY, "."); lime_lex_regex_free(n); }
    }
    {
        LimeReNode *n = parse_ok("^");
        if (n) { EXPECT(n->kind == LIME_RE_ANCHOR_START, "^"); lime_lex_regex_free(n); }
    }
    {
        LimeReNode *n = parse_ok("$");
        if (n) { EXPECT(n->kind == LIME_RE_ANCHOR_END, "$"); lime_lex_regex_free(n); }
    }
    if (fails == saved) printf("test_wildcard_and_anchors: PASS\n");
    return fails - saved;
}

static int test_groups(void) {
    int saved = fails;
    /* (a|b)+ */
    LimeReNode *n = parse_ok("(a|b)+");
    if (n) {
        EXPECT(n->kind == LIME_RE_PLUS, "(a|b)+: kind=%d", n->kind);
        if (n->kind == LIME_RE_PLUS) {
            LimeReNode *c = n->u.unary.child;
            EXPECT(c && c->kind == LIME_RE_ALT, "child should be ALT");
        }
        lime_lex_regex_free(n);
    }
    if (fails == saved) printf("test_groups: PASS\n");
    return fails - saved;
}

static int test_complex_real_world(void) {
    int saved = fails;
    /* Identifier-shaped */
    {
        LimeReNode *n = parse_ok("[A-Za-z_][A-Za-z0-9_]*");
        EXPECT(n != NULL, "identifier regex");
        lime_lex_regex_free(n);
    }
    /* Hex literal */
    {
        LimeReNode *n = parse_ok("0x[0-9A-Fa-f]+");
        EXPECT(n != NULL, "hex regex");
        lime_lex_regex_free(n);
    }
    /* Quoted string with escapes */
    {
        LimeReNode *n = parse_ok("\"([^\"\\\\]|\\\\.)*\"");
        EXPECT(n != NULL, "quoted-string regex");
        lime_lex_regex_free(n);
    }
    if (fails == saved) printf("test_complex_real_world: PASS\n");
    return fails - saved;
}

static int test_errors(void) {
    int saved = fails;
    struct {
        const char *src;
        const char *what;
    } cases[] = {
        { "[",        "unterminated class" },
        { "(a",       "unterminated group" },
        { "a{",       "unterminated repetition" },
        { "a{,5}",    "missing min digits" },
        { "a{5,2}",   "max < min" },
        { "\\",       "trailing backslash" },
        { "\\xZZ",    "invalid hex" },
        { ")",        "unexpected )" },
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        char *err = NULL;
        LimeReNode *n = lime_lex_regex_parse(cases[i].src, &err);
        if (n != NULL) {
            fprintf(stderr, "  expected error on \"%s\" (%s) but parse succeeded\n",
                    cases[i].src, cases[i].what);
            fails++;
            lime_lex_regex_free(n);
        } else if (err == NULL) {
            fprintf(stderr, "  expected error message on \"%s\" but err was NULL\n",
                    cases[i].src);
            fails++;
        }
        free(err);
    }
    if (fails == saved) printf("test_errors: PASS\n");
    return fails - saved;
}

int main(void) {
    test_empty();
    test_literal();
    test_concat();
    test_alt();
    test_quantifiers();
    test_char_class();
    test_escapes();
    test_wildcard_and_anchors();
    test_groups();
    test_complex_real_world();
    test_errors();
    if (fails == 0) {
        printf("\ntest_lex_regex: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_regex: %d sub-test failure(s)\n", fails);
    return 1;
}
