/*
** SIMD-Accelerated SQL Tokenizer
**
** Uses SIMD character classification (tokenize_simd.h) to accelerate
** whitespace skipping and identifier/number boundary detection, then
** falls back to scalar logic for operators and punctuation.
*/
#include "tokenize.h"
#include "tokenize_simd.h"
#include "token_table.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ======================================================================
** Internal tokenizer state
** ====================================================================== */

struct Tokenizer {
    const char *input;       /* Full input buffer */
    size_t length;           /* Length of input */
    size_t pos;              /* Current byte offset */
    uint32_t line;           /* Current line (1-based) */
    uint32_t column;         /* Current column (1-based) */
    TokenTable *table;       /* Keyword table (may be NULL) */
    ClassifyFunc classify;   /* SIMD or scalar classifier */
    bool has_peeked;         /* True if a peeked token is buffered */
    Token peeked;            /* Buffered peek token */
    size_t peek_pos;         /* Position after peeked token */
    uint32_t peek_line;      /* Line after peeked token */
    uint32_t peek_column;    /* Column after peeked token */
};

/* ======================================================================
** Helpers
** ====================================================================== */

/* Return true if c is an identifier start character. */
static inline bool is_id_start(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '_';
}

/* Return true if c is an identifier continuation character. */
static inline bool is_id_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

/* Return true if c is a digit. */
static inline bool is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

/* Return true if c is a hex digit. */
static inline bool is_hex(unsigned char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/* Count trailing zeros in a 32-bit mask. */
static inline int ctz32(uint32_t x) {
    if (x == 0) return 32;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctz(x);
#else
    int n = 0;
    if ((x & 0x0000FFFF) == 0) { n += 16; x >>= 16; }
    if ((x & 0x000000FF) == 0) { n += 8;  x >>= 8;  }
    if ((x & 0x0000000F) == 0) { n += 4;  x >>= 4;  }
    if ((x & 0x00000003) == 0) { n += 2;  x >>= 2;  }
    if ((x & 0x00000001) == 0) { n += 1; }
    return n;
#endif
}

/* Advance position past newline, updating line/column tracking. */
static inline void advance_newline(Tokenizer *tok) {
    tok->pos++;
    tok->line++;
    tok->column = 1;
}

/* Advance position by n bytes, updating column. */
static inline void advance_n(Tokenizer *tok, size_t n) {
    tok->pos += n;
    tok->column += (uint32_t)n;
}

/* Remaining bytes in input from current position. */
static inline size_t remaining(const Tokenizer *tok) {
    return tok->length - tok->pos;
}

/* ======================================================================
** SIMD-accelerated whitespace skipping
** ====================================================================== */

static void skip_whitespace(Tokenizer *tok) {
    while (tok->pos < tok->length) {
        unsigned char c = (unsigned char)tok->input[tok->pos];

        /* Fast path: if current char is not whitespace, return immediately */
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            /* Check for SQL comments */
            if (c == '-' && tok->pos + 1 < tok->length &&
                tok->input[tok->pos + 1] == '-') {
                /* Single-line comment: skip to end of line */
                tok->pos += 2;
                tok->column += 2;
                while (tok->pos < tok->length &&
                       tok->input[tok->pos] != '\n') {
                    tok->pos++;
                    tok->column++;
                }
                continue;
            }
            if (c == '/' && tok->pos + 1 < tok->length &&
                tok->input[tok->pos + 1] == '*') {
                /* C-style block comment: skip to closing */
                tok->pos += 2;
                tok->column += 2;
                while (tok->pos + 1 < tok->length) {
                    if (tok->input[tok->pos] == '*' &&
                        tok->input[tok->pos + 1] == '/') {
                        tok->pos += 2;
                        tok->column += 2;
                        break;
                    }
                    if (tok->input[tok->pos] == '\n') {
                        advance_newline(tok);
                    } else {
                        tok->pos++;
                        tok->column++;
                    }
                }
                continue;
            }
            return;
        }

        /* Use SIMD to skip whitespace in bulk when enough data remains */
        if (remaining(tok) >= 32) {
            CharClassVector cv = tok->classify(tok->input, tok->pos);
            if (cv.is_space_mask == 0xFFFFFFFF) {
                /* All 32 chars are whitespace -- count newlines for tracking */
                for (int i = 0; i < 32; i++) {
                    if (tok->input[tok->pos] == '\n') {
                        advance_newline(tok);
                    } else {
                        tok->pos++;
                        tok->column++;
                    }
                }
                continue;
            }
            if (cv.is_space_mask != 0) {
                /* Some prefix is whitespace -- find first non-space */
                uint32_t non_space = ~cv.is_space_mask;
                int first_non_space = ctz32(non_space);
                for (int i = 0; i < first_non_space; i++) {
                    if (tok->input[tok->pos] == '\n') {
                        advance_newline(tok);
                    } else {
                        tok->pos++;
                        tok->column++;
                    }
                }
                continue;
            }
            /* is_space_mask == 0: current char is not whitespace */
            return;
        }

        /* Scalar fallback for tail bytes */
        if (c == '\n') {
            advance_newline(tok);
        } else {
            tok->pos++;
            tok->column++;
        }
    }
}

/* ======================================================================
** SIMD-accelerated identifier scanning
** ====================================================================== */

/*
** Scan an identifier (or keyword) starting at tok->pos.
** Assumes the first character has already been verified as is_id_start.
** Returns the length of the identifier.
*/
static size_t scan_identifier_simd(Tokenizer *tok) {
    size_t start = tok->pos;

    /* Skip first char (already validated) */
    tok->pos++;
    tok->column++;

    /* Scan continuation characters using SIMD */
    while (tok->pos < tok->length && remaining(tok) >= 32) {
        CharClassVector cv = tok->classify(tok->input, tok->pos);
        /* Identifier chars are alpha | digit */
        uint32_t id_mask = cv.is_alpha_mask | cv.is_digit_mask;
        if (id_mask == 0xFFFFFFFF) {
            /* All 32 are identifier chars */
            advance_n(tok, 32);
            continue;
        }
        if (id_mask != 0) {
            /* Some prefix is identifier chars */
            uint32_t non_id = ~id_mask;
            int count = ctz32(non_id);
            advance_n(tok, (size_t)count);
        }
        break;
    }

    /* Scalar tail for remaining characters */
    while (tok->pos < tok->length &&
           is_id_char((unsigned char)tok->input[tok->pos])) {
        tok->pos++;
        tok->column++;
    }

    return tok->pos - start;
}

/* ======================================================================
** SIMD-accelerated number scanning
** ====================================================================== */

/*
** Scan a number literal starting at tok->pos.
** Handles integers and floats (including scientific notation).
** Returns the token type (TK_INTEGER or TK_FLOAT).
*/
static int scan_number(Tokenizer *tok) {
    int type = TK_INTEGER;

    /* Check for hex: 0x... */
    if (tok->input[tok->pos] == '0' &&
        tok->pos + 1 < tok->length &&
        (tok->input[tok->pos + 1] == 'x' || tok->input[tok->pos + 1] == 'X')) {
        advance_n(tok, 2);
        while (tok->pos < tok->length &&
               is_hex((unsigned char)tok->input[tok->pos])) {
            tok->pos++;
            tok->column++;
        }
        return TK_INTEGER;
    }

    /* Scan digit run using SIMD */
    while (tok->pos < tok->length && remaining(tok) >= 32) {
        CharClassVector cv = tok->classify(tok->input, tok->pos);
        if (cv.is_digit_mask == 0xFFFFFFFF) {
            advance_n(tok, 32);
            continue;
        }
        if (cv.is_digit_mask != 0) {
            uint32_t non_digit = ~cv.is_digit_mask;
            int count = ctz32(non_digit);
            advance_n(tok, (size_t)count);
        }
        break;
    }

    /* Scalar tail for digits */
    while (tok->pos < tok->length &&
           is_digit((unsigned char)tok->input[tok->pos])) {
        tok->pos++;
        tok->column++;
    }

    /* Decimal point */
    if (tok->pos < tok->length && tok->input[tok->pos] == '.') {
        type = TK_FLOAT;
        tok->pos++;
        tok->column++;

        /* Fractional digits */
        while (tok->pos < tok->length &&
               is_digit((unsigned char)tok->input[tok->pos])) {
            tok->pos++;
            tok->column++;
        }
    }

    /* Exponent */
    if (tok->pos < tok->length &&
        (tok->input[tok->pos] == 'e' || tok->input[tok->pos] == 'E')) {
        type = TK_FLOAT;
        tok->pos++;
        tok->column++;
        if (tok->pos < tok->length &&
            (tok->input[tok->pos] == '+' || tok->input[tok->pos] == '-')) {
            tok->pos++;
            tok->column++;
        }
        while (tok->pos < tok->length &&
               is_digit((unsigned char)tok->input[tok->pos])) {
            tok->pos++;
            tok->column++;
        }
    }

    return type;
}

/* ======================================================================
** String and blob literal scanning
** ====================================================================== */

static void scan_string(Tokenizer *tok) {
    /* Skip opening quote */
    tok->pos++;
    tok->column++;

    while (tok->pos < tok->length) {
        char c = tok->input[tok->pos];
        if (c == '\'') {
            tok->pos++;
            tok->column++;
            /* SQL-style escaped quote: '' */
            if (tok->pos < tok->length && tok->input[tok->pos] == '\'') {
                tok->pos++;
                tok->column++;
                continue;
            }
            return;
        }
        if (c == '\n') {
            advance_newline(tok);
        } else {
            tok->pos++;
            tok->column++;
        }
    }
    /* Unterminated string -- pos is at end of input */
}

/* Scan X'...' blob literal. Assumes pos is at the opening quote. */
static void scan_blob(Tokenizer *tok) {
    /* Skip opening quote */
    tok->pos++;
    tok->column++;

    while (tok->pos < tok->length) {
        char c = tok->input[tok->pos];
        if (c == '\'') {
            tok->pos++;
            tok->column++;
            return;
        }
        tok->pos++;
        tok->column++;
    }
}

/* Scan "..." double-quoted identifier. */
static void scan_dquote_id(Tokenizer *tok) {
    tok->pos++;
    tok->column++;
    while (tok->pos < tok->length) {
        if (tok->input[tok->pos] == '"') {
            tok->pos++;
            tok->column++;
            /* Escaped double-quote: "" */
            if (tok->pos < tok->length && tok->input[tok->pos] == '"') {
                tok->pos++;
                tok->column++;
                continue;
            }
            return;
        }
        if (tok->input[tok->pos] == '\n') {
            advance_newline(tok);
        } else {
            tok->pos++;
            tok->column++;
        }
    }
}

/* Scan `...` backtick-quoted identifier. */
static void scan_backtick_id(Tokenizer *tok) {
    tok->pos++;
    tok->column++;
    while (tok->pos < tok->length) {
        if (tok->input[tok->pos] == '`') {
            tok->pos++;
            tok->column++;
            return;
        }
        if (tok->input[tok->pos] == '\n') {
            advance_newline(tok);
        } else {
            tok->pos++;
            tok->column++;
        }
    }
}

/* Scan [...] bracket-quoted identifier. */
static void scan_bracket_id(Tokenizer *tok) {
    tok->pos++;
    tok->column++;
    while (tok->pos < tok->length) {
        if (tok->input[tok->pos] == ']') {
            tok->pos++;
            tok->column++;
            return;
        }
        if (tok->input[tok->pos] == '\n') {
            advance_newline(tok);
        } else {
            tok->pos++;
            tok->column++;
        }
    }
}

/* ======================================================================
** Main token extraction
** ====================================================================== */

static bool extract_token(Tokenizer *tok, Token *out) {
    skip_whitespace(tok);

    if (tok->pos >= tok->length) {
        out->type = TK_EOF;
        out->start = tok->input + tok->pos;
        out->length = 0;
        out->line = tok->line;
        out->column = tok->column;
        return false;
    }

    const char *token_start = tok->input + tok->pos;
    uint32_t token_line = tok->line;
    uint32_t token_col = tok->column;
    unsigned char c = (unsigned char)tok->input[tok->pos];

    /* Blob literal: X'...' or x'...' -- must check before identifier */
    if ((c == 'X' || c == 'x') && tok->pos + 1 < tok->length &&
        tok->input[tok->pos + 1] == '\'') {
        tok->pos++;
        tok->column++;
        scan_blob(tok);
        out->type = TK_BLOB;
        out->start = token_start;
        out->length = (size_t)(tok->input + tok->pos - token_start);
        out->line = token_line;
        out->column = token_col;
        return true;
    }

    /* Identifier or keyword */
    if (is_id_start(c)) {
        size_t len = scan_identifier_simd(tok);

        out->start = token_start;
        out->length = len;
        out->line = token_line;
        out->column = token_col;

        /* Check keyword table */
        if (tok->table) {
            int code = lookup_token(tok->table, token_start, len);
            if (code >= 0) {
                out->type = code;
                return true;
            }
        }
        out->type = TK_IDENTIFIER;
        return true;
    }

    /* Number literal */
    if (is_digit(c)) {
        int type = scan_number(tok);
        out->type = type;
        out->start = token_start;
        out->length = (size_t)(tok->input + tok->pos - token_start);
        out->line = token_line;
        out->column = token_col;
        return true;
    }

    /* Dot followed by digit is a float (e.g., .5) */
    if (c == '.' && tok->pos + 1 < tok->length &&
        is_digit((unsigned char)tok->input[tok->pos + 1])) {
        int type = scan_number(tok);
        out->type = type;
        out->start = token_start;
        out->length = (size_t)(tok->input + tok->pos - token_start);
        out->line = token_line;
        out->column = token_col;
        return true;
    }

    /* String literal */
    if (c == '\'') {
        scan_string(tok);
        out->type = TK_STRING;
        out->start = token_start;
        out->length = (size_t)(tok->input + tok->pos - token_start);
        out->line = token_line;
        out->column = token_col;
        return true;
    }

    /* Double-quoted identifier */
    if (c == '"') {
        scan_dquote_id(tok);
        out->type = TK_DQUOTE_ID;
        out->start = token_start;
        out->length = (size_t)(tok->input + tok->pos - token_start);
        out->line = token_line;
        out->column = token_col;
        return true;
    }

    /* Backtick identifier */
    if (c == '`') {
        scan_backtick_id(tok);
        out->type = TK_BACKTICK_ID;
        out->start = token_start;
        out->length = (size_t)(tok->input + tok->pos - token_start);
        out->line = token_line;
        out->column = token_col;
        return true;
    }

    /* Bracket identifier */
    if (c == '[') {
        scan_bracket_id(tok);
        out->type = TK_BRACKET_ID;
        out->start = token_start;
        out->length = (size_t)(tok->input + tok->pos - token_start);
        out->line = token_line;
        out->column = token_col;
        return true;
    }

    /* Operators and punctuation */
    out->start = token_start;
    out->line = token_line;
    out->column = token_col;

    switch (c) {
    case '(':
        out->type = TK_LPAREN;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case ')':
        out->type = TK_RPAREN;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case ';':
        out->type = TK_SEMICOLON;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case ',':
        out->type = TK_COMMA;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case '.':
        out->type = TK_DOT;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case '*':
        out->type = TK_STAR;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case '+':
        out->type = TK_PLUS;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case '-':
        out->type = TK_MINUS;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case '/':
        out->type = TK_SLASH;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case '%':
        out->type = TK_PERCENT;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case '&':
        out->type = TK_BITAND;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case '~':
        out->type = TK_BITNOT;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case '=':
        advance_n(tok, 1);
        if (tok->pos < tok->length && tok->input[tok->pos] == '=') {
            advance_n(tok, 1);
            out->length = 2;
        } else {
            out->length = 1;
        }
        out->type = TK_EQ;
        return true;
    case '!':
        if (tok->pos + 1 < tok->length && tok->input[tok->pos + 1] == '=') {
            out->type = TK_NE;
            advance_n(tok, 2);
            out->length = 2;
            return true;
        }
        out->type = TK_ILLEGAL;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    case '<':
        advance_n(tok, 1);
        if (tok->pos < tok->length) {
            if (tok->input[tok->pos] == '=') {
                out->type = TK_LE;
                advance_n(tok, 1);
                out->length = 2;
                return true;
            }
            if (tok->input[tok->pos] == '>') {
                out->type = TK_NE;
                advance_n(tok, 1);
                out->length = 2;
                return true;
            }
            if (tok->input[tok->pos] == '<') {
                out->type = TK_LSHIFT;
                advance_n(tok, 1);
                out->length = 2;
                return true;
            }
        }
        out->type = TK_LT;
        out->length = 1;
        return true;
    case '>':
        advance_n(tok, 1);
        if (tok->pos < tok->length) {
            if (tok->input[tok->pos] == '=') {
                out->type = TK_GE;
                advance_n(tok, 1);
                out->length = 2;
                return true;
            }
            if (tok->input[tok->pos] == '>') {
                out->type = TK_RSHIFT;
                advance_n(tok, 1);
                out->length = 2;
                return true;
            }
        }
        out->type = TK_GT;
        out->length = 1;
        return true;
    case '|':
        advance_n(tok, 1);
        if (tok->pos < tok->length && tok->input[tok->pos] == '|') {
            out->type = TK_CONCAT;
            advance_n(tok, 1);
            out->length = 2;
            return true;
        }
        out->type = TK_BITOR;
        out->length = 1;
        return true;
    default:
        out->type = TK_ILLEGAL;
        advance_n(tok, 1);
        out->length = 1;
        return true;
    }
}

/* ======================================================================
** Public API
** ====================================================================== */

Tokenizer *tokenizer_create(TokenTable *table, const char *input, size_t length) {
    Tokenizer *tok = calloc(1, sizeof(Tokenizer));
    if (!tok) return NULL;

    tok->input = input;
    tok->length = length;
    tok->pos = 0;
    tok->line = 1;
    tok->column = 1;
    tok->table = table;
    tok->classify = get_classify_func();
    tok->has_peeked = false;

    return tok;
}

void tokenizer_destroy(Tokenizer *tok) {
    free(tok);
}

bool tokenizer_next(Tokenizer *tok, Token *out) {
    if (tok->has_peeked) {
        *out = tok->peeked;
        tok->pos = tok->peek_pos;
        tok->line = tok->peek_line;
        tok->column = tok->peek_column;
        tok->has_peeked = false;
        return out->type != TK_EOF;
    }
    return extract_token(tok, out);
}

bool tokenizer_peek(Tokenizer *tok, Token *out) {
    if (tok->has_peeked) {
        *out = tok->peeked;
        return out->type != TK_EOF;
    }

    /* Save state */
    size_t saved_pos = tok->pos;
    uint32_t saved_line = tok->line;
    uint32_t saved_column = tok->column;

    bool result = extract_token(tok, &tok->peeked);

    /* Save post-token state for next() to restore */
    tok->peek_pos = tok->pos;
    tok->peek_line = tok->line;
    tok->peek_column = tok->column;

    /* Restore pre-token state */
    tok->pos = saved_pos;
    tok->line = saved_line;
    tok->column = saved_column;

    tok->has_peeked = true;
    *out = tok->peeked;
    return result;
}

size_t tokenizer_position(const Tokenizer *tok) {
    return tok->pos;
}

uint32_t tokenizer_line(const Tokenizer *tok) {
    return tok->line;
}

uint32_t tokenizer_column(const Tokenizer *tok) {
    return tok->column;
}
