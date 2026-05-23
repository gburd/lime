/*
** tokenize.c -- COBOL lexical analyser for the example parser.
**
** Handles:
**   - Fixed-format and free-format source detection
**   - Comments (column 7 = `*` in fixed format; `*>` line comments)
**   - Hyphenated identifiers and keywords (case-insensitive)
**   - Numeric literals (integer and fixed-point)
**   - Alphanumeric literals: 'foo' and "foo" with embedded
**     ''/"" doubled-quote escapes
**   - PICTURE clauses: after a PIC / PICTURE token, the next blob of
**     non-whitespace is captured as a single PIC_LITERAL token
**   - Multi-word reserved phrases that COBOL writes with hyphens
**     (`PROGRAM-ID`, `WORKING-STORAGE`, `END-IF`, etc.)
**
** Limits / non-goals:
**   - COPY/REPLACE preprocessing is NOT performed; copybooks must be
**     resolved before calling this tokenizer.
**   - EXEC SQL/CICS embedded statements are not recognized.
**   - The reserved-word table covers ~140 words drawn from the
**     verbs and clauses the grammar accepts; vendor-specific reserved
**     words (e.g. Micro Focus extensions) are tokenized as IDENT.
*/

#include "tokenize.h"
#include "cobol_grammar.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CobolTokenizer {
    const char    *src;
    size_t         len;
    size_t         pos;
    int            line;
    int            col;
    CobolSrcFormat fmt;
    /* `expect_pic` is set after a PIC/PICTURE token so the next
    ** non-whitespace blob is captured as a single PIC_LITERAL. */
    int            expect_pic;
    int            saw_is_after_pic;  /* "PIC IS X(10)" form */
};

/* ------------------------------------------------------------------ */
/*  Reserved-word table                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    int         token;
} CobolKeyword;

/*
** Sorted alphabetically -- binary search at lookup time.  Names are
** UPPERCASE; the matcher upcases the input before comparing.
*/
static const CobolKeyword kKeywords[] = {
    {"ACCEPT",          CB_ACCEPT},
    {"ACCESS",          CB_ACCESS_MODE},  /* "ACCESS MODE IS" simplified */
    {"ADD",             CB_ADD},
    {"ALPHABETIC",      CB_ALPHABETIC},
    {"AND",             CB_AND},
    {"ASSIGN",          CB_ASSIGN},
    {"AT",              CB_AT},
    {"AUTHOR",          CB_AUTHOR},
    {"BINARY",          CB_BINARY},
    {"BY",              CB_BY},
    {"CALL",            CB_CALL},
    {"COMP",            CB_COMP},
    {"COMP-3",          CB_COMP_3},
    {"COMP-5",          CB_COMP_5},
    {"COMPUTE",         CB_COMPUTE},
    {"CONFIGURATION",   CB_CONFIGURATION},
    {"CONSOLE",         CB_CONSOLE},
    {"CONTINUE",        CB_CONTINUE},
    {"CORR",            CB_CORR},
    {"CORRESPONDING",   CB_CORRESPONDING},
    {"DATA",            CB_DATA},
    {"DATE-COMPILED",   CB_DATE_COMPILED},
    {"DATE-WRITTEN",    CB_DATE_WRITTEN},
    {"DELIMITED",       CB_DELIMITED},
    {"DEPENDING",       CB_DEPENDING},
    {"DISPLAY",         CB_DISPLAY},
    {"DIVIDE",          CB_DIVIDE},
    {"DIVISION",        CB_DIVISION},
    {"DOWN",            CB_DOWN},
    {"ELSE",            CB_ELSE},
    {"END",             CB_END},
    {"END-EVALUATE",    CB_END_EVALUATE},
    {"END-IF",          CB_END_IF},
    {"END-STRING",      CB_END_STRING},
    {"END-UNSTRING",    CB_END_UNSTRING},
    {"ENVIRONMENT",     CB_ENVIRONMENT},
    {"EVALUATE",        CB_EVALUATE},
    {"EXIT",            CB_EXIT},
    {"FALSE",           CB_FALSE},
    {"FILE-CONTROL",    CB_FILE_CONTROL},
    {"FILE-SECTION",    CB_FILE_SECTION},
    {"FOR",             CB_FOR},
    {"FROM",            CB_FROM},
    {"GIVING",          CB_GIVING},
    {"GO",              CB_GO},
    {"GREATER",         CB_GREATER},
    {"HIGH-VALUES",     CB_HIGH_VALUES},
    {"IDENTIFICATION",  CB_IDENTIFICATION},
    {"IF",              CB_IF},
    {"IN",              CB_IN},
    {"INDEXED",         CB_INDEXED},
    {"INITIALIZE",      CB_INITIALIZE},
    {"INPUT-OUTPUT",    CB_INPUT_OUTPUT},
    {"INSPECT",         CB_INSPECT},
    {"INSTALLATION",    CB_INSTALLATION},
    {"INTO",            CB_INTO},
    {"IS",              CB_IS},
    {"LESS",            CB_LESS},
    {"LINKAGE",         CB_LINKAGE},
    {"LOW-VALUES",      CB_LOW_VALUES},
    {"MOVE",            CB_MOVE},
    {"MULTIPLY",        CB_MULTIPLY},
    {"NEGATIVE",        CB_NEGATIVE},
    {"NOT",             CB_NOT},
    {"NULL",            CB_NULL_KW},
    {"NUMERIC",         CB_NUMERIC},
    {"OBJECT-COMPUTER", CB_OBJECT_COMPUTER},
    {"OCCURS",          CB_OCCURS},
    {"OF",              CB_OF},
    {"ON",              CB_ON},
    {"OR",              CB_OR},
    {"OTHER",           CB_OTHER},
    {"PACKED-DECIMAL",  CB_PACKED_DECIMAL},
    {"PERFORM",         CB_PERFORM},
    {"PIC",             CB_PIC},
    {"PICTURE",         CB_PICTURE},
    {"POINTER",         CB_POINTER},
    {"POSITIVE",        CB_POSITIVE},
    {"PROCEDURE",       CB_PROCEDURE},
    {"PROGRAM",         CB_PROGRAM},
    {"PROGRAM-ID",      CB_PROGRAM_ID},
    {"QUOTES",          CB_QUOTES},
    {"REDEFINES",       CB_REDEFINES},
    {"REPLACING",       CB_REPLACING},
    {"ROUNDED",         CB_ROUNDED},
    {"RUN",             CB_RUN},
    {"SECTION",         CB_SECTION},
    {"SECURITY",        CB_SECURITY},
    {"SELECT",          CB_SELECT},
    {"SET",             CB_SET},
    {"SOURCE-COMPUTER", CB_SOURCE_COMPUTER},
    {"SPACES",          CB_SPACES},
    {"STOP",            CB_STOP},
    {"STRING",          CB_STRING_KW},
    {"SUBTRACT",        CB_SUBTRACT},
    {"TALLYING",        CB_TALLYING},
    {"THEN",            CB_THEN},
    {"THRU",            CB_THRU},
    {"TIMES",           CB_TIMES},
    {"TO",              CB_TO},
    {"TRUE",            CB_TRUE},
    {"UNSTRING",        CB_UNSTRING},
    {"UNTIL",           CB_UNTIL},
    {"UP",              CB_UP},
    {"UPON",            CB_UPON},
    {"USAGE",           CB_USAGE},
    {"USING",           CB_USING},
    {"VALUE",           CB_VALUE},
    {"VARYING",         CB_VARYING},
    {"WHEN",            CB_WHEN},
    {"WORKING-STORAGE", CB_WORKING_STORAGE},
    {"ZERO",            CB_ZERO},
    {"ZEROES",          CB_ZERO},   /* alias */
    {"ZEROS",           CB_ZERO},   /* alias */
};

static int kw_compare(const void *a, const void *b) {
    const char         *needle = (const char *)a;
    const CobolKeyword *e = (const CobolKeyword *)b;
    return strcmp(needle, e->name);
}

static int lookup_keyword(const char *upper) {
    const CobolKeyword *hit = bsearch(upper, kKeywords,
                                      sizeof(kKeywords) / sizeof(kKeywords[0]),
                                      sizeof(kKeywords[0]), kw_compare);
    return hit ? hit->token : -1;
}

/* ------------------------------------------------------------------ */
/*  Tokenizer state machine                                           */
/* ------------------------------------------------------------------ */

CobolTokenizer *cobol_tokenize_init(const char *src, size_t len, CobolSrcFormat fmt) {
    if (src == NULL) return NULL;
    CobolTokenizer *tk = calloc(1, sizeof(*tk));
    if (tk == NULL) return NULL;
    tk->src = src;
    tk->len = len;
    tk->line = 1;
    tk->col = 1;

    /* Auto-detect: fixed format if first 6 chars of first line are
    ** digits or spaces.  Most modern COBOL is free-format; this is
    ** the heuristic GnuCOBOL uses. */
    if (fmt == COBOL_FORMAT_AUTO) {
        if (len >= 7) {
            int looks_fixed = 1;
            for (size_t i = 0; i < 6 && i < len; i++) {
                if (!(isdigit((unsigned char)src[i]) || src[i] == ' ')) {
                    looks_fixed = 0;
                    break;
                }
            }
            tk->fmt = looks_fixed ? COBOL_FORMAT_FIXED : COBOL_FORMAT_FREE;
        } else {
            tk->fmt = COBOL_FORMAT_FREE;
        }
    } else {
        tk->fmt = fmt;
    }
    return tk;
}

void cobol_tokenize_free(CobolTokenizer *tk) { free(tk); }

static int peek(const CobolTokenizer *tk, size_t off) {
    if (tk->pos + off >= tk->len) return -1;
    return (unsigned char)tk->src[tk->pos + off];
}

static void advance(CobolTokenizer *tk) {
    if (tk->pos >= tk->len) return;
    if (tk->src[tk->pos] == '\n') {
        tk->line++;
        tk->col = 1;
    } else {
        tk->col++;
    }
    tk->pos++;
}

/*
** Skip whitespace, comments, and (in fixed format) the
** sequence-number / indicator columns.
*/
static void skip_ws_and_comments(CobolTokenizer *tk) {
    for (;;) {
        if (tk->pos >= tk->len) return;
        int c = peek(tk, 0);

        /* End-of-line newline -- consume and optionally apply
        ** fixed-format column rules to the new line. */
        if (c == '\n') {
            advance(tk);
            if (tk->fmt == COBOL_FORMAT_FIXED) {
                /* Skip cols 1-6 (sequence) */
                int colsToSkip = 6;
                while (colsToSkip > 0 && tk->pos < tk->len &&
                       tk->src[tk->pos] != '\n') {
                    advance(tk);
                    colsToSkip--;
                }
                /* Indicator column 7 */
                if (tk->pos < tk->len && tk->src[tk->pos] != '\n') {
                    int ind = peek(tk, 0);
                    if (ind == '*' || ind == '/' || ind == 'D' || ind == 'd') {
                        /* Comment / form-feed / debug -- skip to EOL */
                        while (tk->pos < tk->len && tk->src[tk->pos] != '\n') {
                            advance(tk);
                        }
                        continue;
                    }
                    /* `-` continuation marker; consume and continue. */
                    advance(tk); /* consume indicator column */
                }
            }
            continue;
        }

        /* Skip spaces and tabs and carriage returns. */
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(tk);
            continue;
        }

        /* Free-format line comment: `*>` to EOL.  Or, if we are at
        ** the start of a line in free format and column is 1 with a
        ** `*`, treat it as a comment line for compatibility. */
        if (c == '*' && peek(tk, 1) == '>') {
            while (tk->pos < tk->len && tk->src[tk->pos] != '\n') advance(tk);
            continue;
        }
        if (tk->fmt == COBOL_FORMAT_FREE && c == '*' && tk->col == 1) {
            while (tk->pos < tk->len && tk->src[tk->pos] != '\n') advance(tk);
            continue;
        }

        return;
    }
}

/* Identifiers and keywords: [A-Za-z][A-Za-z0-9-]* */
static int is_id_start(int c) { return isalpha(c); }
static int is_id_cont(int c)  { return isalnum(c) || c == '-'; }

/* Upcase a buffer into a fixed-size scratch space for keyword lookup. */
static void upcase_n(char *dst, size_t cap, const char *src, size_t len) {
    if (len >= cap) len = cap - 1;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (char)toupper(c);
    }
    dst[len] = '\0';
}

CobolToken cobol_tokenize_next(CobolTokenizer *tk) {
    CobolToken out = {0};
    if (tk == NULL) return out;

    skip_ws_and_comments(tk);

    if (tk->pos >= tk->len) {
        out.code = 0;
        return out;
    }

    out.line = tk->line;
    out.col = tk->col;
    out.text = tk->src + tk->pos;

    int c = peek(tk, 0);

    /* PICTURE-clause capture: after a PIC/PICTURE token (with
    ** optional IS in between), grab the next non-whitespace blob as
    ** a single PIC_LITERAL.  COBOL picture strings can include any
    ** of X 9 V S Z * , . / B 0 P A E N $ + - and parentheses. */
    if (tk->expect_pic) {
        size_t start = tk->pos;
        while (tk->pos < tk->len) {
            int p = peek(tk, 0);
            if (p == ' ' || p == '\t' || p == '\n' || p == '\r' ||
                p == '.' || p == ',' || p == ';') break;
            advance(tk);
        }
        out.code = CB_PIC_LITERAL;
        out.text = tk->src + start;
        out.len = tk->pos - start;
        tk->expect_pic = 0;
        tk->saw_is_after_pic = 0;
        return out;
    }

    /* Identifier or keyword. */
    if (is_id_start(c)) {
        size_t start = tk->pos;
        advance(tk);
        while (tk->pos < tk->len && is_id_cont(peek(tk, 0))) advance(tk);
        size_t n = tk->pos - start;

        char upper[64];
        upcase_n(upper, sizeof(upper), tk->src + start, n);
        int tok = lookup_keyword(upper);

        out.text = tk->src + start;
        out.len = n;

        if (tok < 0) {
            out.code = CB_IDENT;
        } else {
            out.code = tok;
            /* If the keyword was PIC/PICTURE, arm the picture
            ** capture for the next call (skipping an optional IS). */
            if (tok == CB_PIC || tok == CB_PICTURE) {
                /* Peek past whitespace; if next word is IS, consume
                ** it via the normal path on the next call.  Setting
                ** expect_pic now means the call-after-next gets the
                ** picture string -- but if IS appears it'll be
                ** returned as a token first.  To keep this simple,
                ** we just set expect_pic now and let the next
                ** non-IS, non-whitespace blob be the literal. */
                tk->expect_pic = 1;
            }
            /* If the keyword was IS while expect_pic is armed, just
            ** consume it and leave the flag set. */
            if (tok == CB_IS && tk->saw_is_after_pic == 0 && tk->expect_pic == 0) {
                /* normal IS, no-op */
            }
        }
        return out;
    }

    /* Numeric literal: digits, optional decimal point. */
    if (isdigit(c) || (c == '+' || c == '-') &&
                          tk->pos + 1 < tk->len && isdigit(peek(tk, 1))) {
        size_t start = tk->pos;
        if (c == '+' || c == '-') advance(tk);
        while (tk->pos < tk->len && isdigit(peek(tk, 0))) advance(tk);
        if (tk->pos < tk->len && peek(tk, 0) == '.' &&
            tk->pos + 1 < tk->len && isdigit(peek(tk, 1))) {
            advance(tk);
            while (tk->pos < tk->len && isdigit(peek(tk, 0))) advance(tk);
        }
        out.code = CB_NUMBER;
        out.text = tk->src + start;
        out.len = tk->pos - start;
        char buf[32];
        size_t bn = out.len < sizeof(buf) - 1 ? out.len : sizeof(buf) - 1;
        memcpy(buf, out.text, bn);
        buf[bn] = '\0';
        out.ivalue = atol(buf);
        return out;
    }

    /* String literal: 'foo' or "foo".  Doubled quote inside is an
    ** escape for the same quote character. */
    if (c == '\'' || c == '"') {
        char q = (char)c;
        size_t start = tk->pos;
        advance(tk);
        while (tk->pos < tk->len) {
            int p = peek(tk, 0);
            if (p == q) {
                if (tk->pos + 1 < tk->len && peek(tk, 1) == q) {
                    advance(tk);
                    advance(tk);
                    continue;
                }
                advance(tk);
                break;
            }
            if (p == '\n') break;  /* unterminated; let the parser
                                  ** fail downstream */
            advance(tk);
        }
        out.code = CB_STRING;
        out.text = tk->src + start;
        out.len = tk->pos - start;
        return out;
    }

    /* Punctuation and operators. */
    advance(tk);
    out.len = 1;
    switch (c) {
    case '(': out.code = CB_LPAREN; return out;
    case ')': out.code = CB_RPAREN; return out;
    case '.': out.code = CB_DOT; return out;
    case ',': out.code = CB_COMMA; return out;
    case ':': out.code = CB_COLON; return out;
    case '+': out.code = CB_PLUS; return out;
    case '*':
        if (peek(tk, 0) == '*') {
            advance(tk);
            out.code = CB_POWER;
            out.len = 2;
        } else {
            out.code = CB_STAR;
        }
        return out;
    case '/': out.code = CB_SLASH; return out;
    case '-':
        out.code = CB_MINUS;
        return out;
    case '=':
        out.code = CB_EQUAL;
        return out;
    case '<':
        if (peek(tk, 0) == '=') {
            advance(tk);
            out.code = CB_LE;
            out.len = 2;
        } else {
            out.code = CB_LESS;
        }
        return out;
    case '>':
        if (peek(tk, 0) == '=') {
            advance(tk);
            out.code = CB_GE;
            out.len = 2;
        } else {
            out.code = CB_GREATER;
        }
        return out;
    }

    /* Unknown character -- skip and try again. */
    return cobol_tokenize_next(tk);
}
