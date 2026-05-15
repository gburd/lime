/*
** tests/test_lex_tokenize.c -- M1 phase 1 unit test for the .lex
** tokenizer.
**
** Exercises the token kinds the .lex grammar needs:
**   - Directives (%name_prefix, %pattern, %state, %ruleset, ...)
**   - Identifiers and the `matches` keyword
**   - Strings, regex literals, char literals
**   - Code blocks with embedded strings, comments, and nested
**     braces (the most failure-prone path)
**   - <<EOF>> marker
**   - Punctuation
**   - Comments (// and slash-star) skipped between tokens
**   - Error tokens for unrecognised input
**
** Each sub-test feeds a small input through the tokenizer and
** asserts on the (kind, lexeme) sequence.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex_tokenize.h"

/* ----- expectation helper ----- */

struct expect {
    LimeLexTokKind kind;
    const char    *lexeme;   /* expected literal lexeme; NULL = don't care */
};

static int run_case(const char *label,
                    const char *src,
                    const struct expect *expects, size_t n_expects) {
    LimeLexTokenizer *t = lime_lex_tokenize_init("<test>", src, strlen(src));
    if (!t) {
        fprintf(stderr, "  %s: lime_lex_tokenize_init returned NULL\n", label);
        return 1;
    }
    int fails = 0;
    for (size_t i = 0; i < n_expects; i++) {
        LimeLexToken tok;
        lime_lex_tokenize_next(t, &tok);
        if (tok.kind != expects[i].kind) {
            fprintf(stderr,
                    "  %s: tok %zu kind: got %s, want %s "
                    "(lexeme=\"%.*s\")\n",
                    label, i,
                    lime_lex_tok_kind_name(tok.kind),
                    lime_lex_tok_kind_name(expects[i].kind),
                    (int) tok.length, tok.lexeme);
            fails++;
            continue;
        }
        if (expects[i].lexeme) {
            size_t want_len = strlen(expects[i].lexeme);
            if (tok.length != want_len ||
                memcmp(tok.lexeme, expects[i].lexeme, want_len) != 0) {
                fprintf(stderr,
                        "  %s: tok %zu lexeme: got \"%.*s\", want \"%s\"\n",
                        label, i,
                        (int) tok.length, tok.lexeme,
                        expects[i].lexeme);
                fails++;
            }
        }
    }
    /* After the expected tokens, an EOF should follow. */
    {
        LimeLexToken tok;
        lime_lex_tokenize_next(t, &tok);
        if (tok.kind != LIME_LEX_TOK_EOF) {
            fprintf(stderr,
                    "  %s: trailing tok kind: got %s, want EOF "
                    "(lexeme=\"%.*s\")\n",
                    label,
                    lime_lex_tok_kind_name(tok.kind),
                    (int) tok.length, tok.lexeme);
            fails++;
        }
    }
    lime_lex_tokenize_free(t);
    if (fails == 0) printf("%s: PASS\n", label);
    return fails;
}

/* ----- sub-tests ----- */

static int test_empty(void) {
    return run_case("empty input", "", NULL, 0);
}

static int test_whitespace_only(void) {
    return run_case("whitespace-only", "   \n\t  \r\n  ", NULL, 0);
}

static int test_directives(void) {
    const char *src =
        "%name_prefix Foo.\n"
        "%pattern digit /[0-9]/.\n"
        "%state INIT.\n"
        "%exclusive_state QUOTED.\n"
        "%ruleset r1.\n";
    struct expect e[] = {
        { LIME_LEX_TOK_DIR_NAME_PREFIX,           "%name_prefix" },
        { LIME_LEX_TOK_IDENT,                     "Foo" },
        { LIME_LEX_TOK_DOT,                       "." },
        { LIME_LEX_TOK_DIR_PATTERN,               "%pattern" },
        { LIME_LEX_TOK_IDENT,                     "digit" },
        { LIME_LEX_TOK_REGEX,                     "/[0-9]/" },
        { LIME_LEX_TOK_DOT,                       "." },
        { LIME_LEX_TOK_DIR_STATE,                 "%state" },
        { LIME_LEX_TOK_IDENT,                     "INIT" },
        { LIME_LEX_TOK_DOT,                       "." },
        { LIME_LEX_TOK_DIR_EXCLUSIVE_STATE,       "%exclusive_state" },
        { LIME_LEX_TOK_IDENT,                     "QUOTED" },
        { LIME_LEX_TOK_DOT,                       "." },
        { LIME_LEX_TOK_DIR_RULESET,               "%ruleset" },
        { LIME_LEX_TOK_IDENT,                     "r1" },
        { LIME_LEX_TOK_DOT,                       "." },
    };
    return run_case("directives", src, e, sizeof(e)/sizeof(e[0]));
}

static int test_rule_shape(void) {
    /* Bootscanner-shaped rule line. */
    const char *src =
        "<INITIAL, EXPR> rule plus matches /\\+/ { return PLUS; }";
    struct expect e[] = {
        { LIME_LEX_TOK_LANGLE,        "<" },
        { LIME_LEX_TOK_IDENT,         "INITIAL" },
        { LIME_LEX_TOK_COMMA,         "," },
        { LIME_LEX_TOK_IDENT,         "EXPR" },
        { LIME_LEX_TOK_RANGLE,        ">" },
        { LIME_LEX_TOK_IDENT,         "rule" },
        { LIME_LEX_TOK_IDENT,         "plus" },
        { LIME_LEX_TOK_KW_MATCHES,    "matches" },
        { LIME_LEX_TOK_REGEX,         "/\\+/" },
        { LIME_LEX_TOK_CODE_BLOCK,    "{ return PLUS; }" },
    };
    return run_case("rule shape", src, e, sizeof(e)/sizeof(e[0]));
}

static int test_eof_marker(void) {
    const char *src = "<xq> rule eof_xq matches <<EOF>> { abort(); }";
    struct expect e[] = {
        { LIME_LEX_TOK_LANGLE,        "<" },
        { LIME_LEX_TOK_IDENT,         "xq" },
        { LIME_LEX_TOK_RANGLE,        ">" },
        { LIME_LEX_TOK_IDENT,         "rule" },
        { LIME_LEX_TOK_IDENT,         "eof_xq" },
        { LIME_LEX_TOK_KW_MATCHES,    "matches" },
        { LIME_LEX_TOK_EOF_MARKER,    "<<EOF>>" },
        { LIME_LEX_TOK_CODE_BLOCK,    "{ abort(); }" },
    };
    return run_case("EOF marker", src, e, sizeof(e)/sizeof(e[0]));
}

static int test_code_block_nested(void) {
    /* Code block with nested braces, embedded string with brace,
    ** char literal, and slash-star comment containing a brace.
    ** The tokenizer must NOT lose count on any of these. */
    const char *src =
        "{ if (x) { y = \"a } b\"; } /* {{ */ z = '}'; }";
    struct expect e[] = {
        { LIME_LEX_TOK_CODE_BLOCK,    NULL },  /* lexeme = whole block */
    };
    int fails = run_case("code block with nested", src, e, 1);

    /* Also assert the block's exact length: the entire input. */
    LimeLexTokenizer *t = lime_lex_tokenize_init("<test>", src, strlen(src));
    LimeLexToken tok;
    lime_lex_tokenize_next(t, &tok);
    if (tok.kind != LIME_LEX_TOK_CODE_BLOCK || tok.length != strlen(src)) {
        fprintf(stderr, "  code block with nested: length=%zu want %zu\n",
                tok.length, strlen(src));
        fails++;
    }
    lime_lex_tokenize_free(t);
    return fails;
}

static int test_strings_and_regexes(void) {
    /* Strings, regexes, and char literals must survive escapes. */
    const char *src = "\"hello \\\"world\\\"\" /a\\/b/ '\\''";
    struct expect e[] = {
        { LIME_LEX_TOK_STRING,         "\"hello \\\"world\\\"\"" },
        { LIME_LEX_TOK_REGEX,          "/a\\/b/" },
        { LIME_LEX_TOK_CHAR_LITERAL,   "'\\''" },
    };
    return run_case("strings/regexes/char-lits with escapes",
                    src, e, sizeof(e)/sizeof(e[0]));
}

static int test_comments_skipped(void) {
    const char *src =
        "// line comment\n"
        "/* block\n"
        "   comment */\n"
        "ident // trailing\n"
        "42";
    struct expect e[] = {
        { LIME_LEX_TOK_IDENT,    "ident" },
        { LIME_LEX_TOK_INTEGER,  "42" },
    };
    return run_case("comments skipped", src, e, sizeof(e)/sizeof(e[0]));
}

static int test_unknown_directive(void) {
    /* An unknown %directive tokenises as DIR_UNKNOWN, not as
    ** ERROR (the parser surfaces the diagnostic). */
    const char *src = "%totally_made_up";
    struct expect e[] = {
        { LIME_LEX_TOK_DIR_UNKNOWN,  "%totally_made_up" },
    };
    return run_case("unknown directive", src, e, sizeof(e)/sizeof(e[0]));
}

static int test_error_recovery(void) {
    /* `@` is not a valid .lex byte; expect ERROR followed by
    ** continued tokenisation of the rest. */
    const char *src = "ident @ more";
    LimeLexTokenizer *t = lime_lex_tokenize_init("<test>", src, strlen(src));
    LimeLexToken tok;
    int fails = 0;
    lime_lex_tokenize_next(t, &tok);
    if (tok.kind != LIME_LEX_TOK_IDENT) { fails++; }
    lime_lex_tokenize_next(t, &tok);
    if (tok.kind != LIME_LEX_TOK_ERROR) {
        fprintf(stderr, "  error recovery: expected ERROR, got %s\n",
                lime_lex_tok_kind_name(tok.kind));
        fails++;
    }
    lime_lex_tokenize_next(t, &tok);
    if (tok.kind != LIME_LEX_TOK_IDENT) {
        fprintf(stderr, "  error recovery: expected IDENT after error, got %s\n",
                lime_lex_tok_kind_name(tok.kind));
        fails++;
    }
    if (lime_lex_tokenize_error_count(t) != 1) fails++;
    lime_lex_tokenize_free(t);
    if (fails == 0) printf("error recovery: PASS\n");
    return fails;
}

static int test_line_tracking(void) {
    const char *src = "a\nb\nc";
    LimeLexTokenizer *t = lime_lex_tokenize_init("<test>", src, strlen(src));
    LimeLexToken tok;
    int fails = 0;
    lime_lex_tokenize_next(t, &tok);
    if (tok.line != 1) { fprintf(stderr, "  line a=%d want 1\n", tok.line); fails++; }
    lime_lex_tokenize_next(t, &tok);
    if (tok.line != 2) { fprintf(stderr, "  line b=%d want 2\n", tok.line); fails++; }
    lime_lex_tokenize_next(t, &tok);
    if (tok.line != 3) { fprintf(stderr, "  line c=%d want 3\n", tok.line); fails++; }
    lime_lex_tokenize_free(t);
    if (fails == 0) printf("line tracking: PASS\n");
    return fails;
}

int main(void) {
    int fails = 0;
    fails += test_empty();
    fails += test_whitespace_only();
    fails += test_directives();
    fails += test_rule_shape();
    fails += test_eof_marker();
    fails += test_code_block_nested();
    fails += test_strings_and_regexes();
    fails += test_comments_skipped();
    fails += test_unknown_directive();
    fails += test_error_recovery();
    fails += test_line_tracking();
    if (fails == 0) {
        printf("\ntest_lex_tokenize: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_tokenize: %d sub-test(s) FAILED\n", fails);
    return 1;
}
