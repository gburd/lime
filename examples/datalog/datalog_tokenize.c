/*
 * datalog_tokenize.c
 *    Tokenizer (lexer) for the Datalog/EDN language.
 *
 * Converts a character stream into a sequence of tokens for the
 * Lime-generated parser. Handles:
 *   - Atoms (lowercase identifiers)
 *   - Variables (uppercase identifiers and _)
 *   - Keywords (:name, :namespace/name)
 *   - Numbers (integers and floats)
 *   - Strings (double-quoted, with escape sequences)
 *   - Operators (:-, ?-, \=, !=, <=, >=, <, >, =)
 *   - Punctuation (, . ( ) [ ] { } |)
 *   - Set literal opener (#{)
 *   - Comments (% to end of line, and ; to end of line)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "datalog_defs.h"
#include "datalog.h"  /* generated token definitions */

/* Token code aliases from the generated header */
/* These are defined by the Lime-generated parser header (datalog.h) */

typedef struct DlTokenizer {
    const char *input;      /* full input string */
    int         pos;        /* current position */
    int         len;        /* total length */
    int         line;       /* current line number */
    int         col;        /* current column number */
} DlTokenizer;

DlTokenizer *dl_tokenizer_create(const char *input, int len)
{
    DlTokenizer *t = (DlTokenizer *)calloc(1, sizeof(DlTokenizer));
    t->input = input;
    t->pos = 0;
    t->len = len;
    t->line = 1;
    t->col = 1;
    return t;
}

void dl_tokenizer_destroy(DlTokenizer *t)
{
    free(t);
}

static int peek(DlTokenizer *t)
{
    if (t->pos >= t->len) return -1;
    return (unsigned char)t->input[t->pos];
}

static int advance(DlTokenizer *t)
{
    if (t->pos >= t->len) return -1;
    int ch = (unsigned char)t->input[t->pos++];
    if (ch == '\n') {
        t->line++;
        t->col = 1;
    } else {
        t->col++;
    }
    return ch;
}

static void skip_whitespace_and_comments(DlTokenizer *t)
{
    while (t->pos < t->len) {
        int ch = peek(t);
        /* Whitespace */
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            advance(t);
            continue;
        }
        /* Line comment: % or ; to end of line */
        if (ch == '%' || ch == ';') {
            advance(t);
            while (t->pos < t->len && peek(t) != '\n') {
                advance(t);
            }
            continue;
        }
        break;
    }
}

/* Check if a keyword matches */
static int check_keyword(const char *s, int len, const char *kw)
{
    int klen = (int)strlen(kw);
    return len == klen && memcmp(s, kw, (size_t)klen) == 0;
}

/*
 * dl_tokenizer_next -- get the next token.
 *
 * Returns the token code (0 for EOF, >0 for valid tokens).
 * Fills in *value with the token's semantic value.
 */
int dl_tokenizer_next(DlTokenizer *t, DatalogToken *value)
{
    memset(value, 0, sizeof(*value));
    skip_whitespace_and_comments(t);

    if (t->pos >= t->len)
        return 0;  /* EOF */

    int ch = peek(t);
    int start = t->pos;

    /* Two-character operators */
    if (ch == ':' && t->pos + 1 < t->len && t->input[t->pos + 1] == '-') {
        advance(t); advance(t);
        return RULE_OP;
    }
    if (ch == '?' && t->pos + 1 < t->len && t->input[t->pos + 1] == '-') {
        advance(t); advance(t);
        return QUERY_OP;
    }
    if (ch == '\\' && t->pos + 1 < t->len && t->input[t->pos + 1] == '=') {
        advance(t); advance(t);
        return NEQ_OP;
    }
    if (ch == '!' && t->pos + 1 < t->len && t->input[t->pos + 1] == '=') {
        advance(t); advance(t);
        return NEQ_OP;
    }
    if (ch == '<' && t->pos + 1 < t->len && t->input[t->pos + 1] == '=') {
        advance(t); advance(t);
        return LTE_OP;
    }
    if (ch == '>' && t->pos + 1 < t->len && t->input[t->pos + 1] == '=') {
        advance(t); advance(t);
        return GTE_OP;
    }
    if (ch == '\\' && t->pos + 1 < t->len && t->input[t->pos + 1] == '+') {
        advance(t); advance(t);
        return NOT_OP;
    }
    if (ch == '#' && t->pos + 1 < t->len && t->input[t->pos + 1] == '{') {
        advance(t); advance(t);
        return HASH_LBRACE;
    }

    /* Single-character tokens */
    switch (ch) {
        case '(': advance(t); return LPAREN;
        case ')': advance(t); return RPAREN;
        case '[': advance(t); return LBRACKET;
        case ']': advance(t); return RBRACKET;
        case '{': advance(t); return LBRACE;
        case '}': advance(t); return RBRACE;
        case ',': advance(t); return COMMA;
        case '.': advance(t); return DOT;
        case '|': advance(t); return PIPE;
        case '=': advance(t); return EQ_OP;
        case '<': advance(t); return LT_OP;
        case '>': advance(t); return GT_OP;
    }

    /* Keywords (EDN): :name or :namespace/name */
    if (ch == ':') {
        advance(t);  /* skip the colon */
        start = t->pos;
        while (t->pos < t->len) {
            int c = peek(t);
            if (isalnum(c) || c == '_' || c == '-' || c == '/' || c == '.') {
                advance(t);
            } else {
                break;
            }
        }
        value->str.val = t->input + start;
        value->str.len = t->pos - start;
        return KEYWORD;
    }

    /* String literal */
    if (ch == '"') {
        advance(t);  /* skip opening quote */
        start = t->pos;
        while (t->pos < t->len) {
            int c = peek(t);
            if (c == '\\') {
                advance(t);
                advance(t);  /* skip escaped char */
            } else if (c == '"') {
                break;
            } else {
                advance(t);
            }
        }
        value->str.val = t->input + start;
        value->str.len = t->pos - start;
        if (t->pos < t->len) advance(t);  /* skip closing quote */
        return STRING;
    }

    /* Number: integer or float */
    if (isdigit(ch) || (ch == '-' && t->pos + 1 < t->len &&
                         isdigit((unsigned char)t->input[t->pos + 1]))) {
        start = t->pos;
        if (ch == '-') advance(t);
        while (t->pos < t->len && isdigit(peek(t))) {
            advance(t);
        }
        int is_float = 0;
        if (t->pos < t->len && peek(t) == '.') {
            /* Check it's not a clause terminator: digit before, digit after */
            if (t->pos + 1 < t->len &&
                isdigit((unsigned char)t->input[t->pos + 1])) {
                advance(t);  /* skip decimal point */
                while (t->pos < t->len && isdigit(peek(t))) {
                    advance(t);
                }
                is_float = 1;
            }
        }
        /* Exponent part */
        if (t->pos < t->len && (peek(t) == 'e' || peek(t) == 'E')) {
            advance(t);
            if (t->pos < t->len && (peek(t) == '+' || peek(t) == '-')) {
                advance(t);
            }
            while (t->pos < t->len && isdigit(peek(t))) {
                advance(t);
            }
            is_float = 1;
        }
        value->str.val = t->input + start;
        value->str.len = t->pos - start;
        return is_float ? FLOAT : INTEGER;
    }

    /* Identifier: atom (lowercase start) or variable (uppercase start) */
    if (isalpha(ch) || ch == '_') {
        start = t->pos;
        advance(t);
        while (t->pos < t->len) {
            int c = peek(t);
            if (isalnum(c) || c == '_') {
                advance(t);
            } else {
                break;
            }
        }
        value->str.val = t->input + start;
        value->str.len = t->pos - start;
        int toklen = t->pos - start;
        const char *tokstr = t->input + start;

        /* Check for reserved keywords */
        if (check_keyword(tokstr, toklen, "not"))   return NOT_OP;
        if (check_keyword(tokstr, toklen, "true"))  return TRUE_K;
        if (check_keyword(tokstr, toklen, "false")) return FALSE_K;
        if (check_keyword(tokstr, toklen, "nil"))   return NIL_K;
        if (check_keyword(tokstr, toklen, "count")) return COUNT;
        if (check_keyword(tokstr, toklen, "sum"))   return SUM;
        if (check_keyword(tokstr, toklen, "min"))   return MIN;
        if (check_keyword(tokstr, toklen, "max"))   return MAX;
        if (check_keyword(tokstr, toklen, "avg"))   return AVG;

        /* Underscore alone is the anonymous variable */
        if (toklen == 1 && tokstr[0] == '_') return UNDERSCORE;

        /* Uppercase start = variable, lowercase start = atom */
        if (isupper((unsigned char)tokstr[0]) || tokstr[0] == '_') {
            return VARIABLE;
        }
        return ATOM;
    }

    /* Unknown character -- skip and report error */
    advance(t);
    return -1;
}
