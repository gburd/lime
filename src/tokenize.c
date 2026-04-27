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
#include "utf8.h"

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

/* Return true if c is an ASCII identifier start character. */
static inline bool is_id_start_ascii(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '_';
}

/* Return true if c is an ASCII identifier continuation character. */
static inline bool is_id_char_ascii(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

/*
** Check if the byte at position p (within input ending at end) starts a
** valid UTF-8 identifier start character. For ASCII bytes, uses the fast
** inline check. For high bytes (>= 0x80), decodes the UTF-8 sequence and
** checks the Unicode ID_Start property.
*/
static bool is_id_start_at(const char *p, const char *end) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x80) return is_id_start_ascii(c);
    const char *tmp = p;
    int32_t cp = utf8_decode(&tmp, end);
    return cp >= 0 && utf8_is_id_start(cp);
}

/*
** Check if the byte at position p starts a valid UTF-8 identifier
** continuation character. For high bytes, decodes and checks ID_Continue.
*/
static bool is_id_continue_at(const char *p, const char *end) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x80) return is_id_char_ascii(c);
    const char *tmp = p;
    int32_t cp = utf8_decode(&tmp, end);
    return cp >= 0 && utf8_is_id_continue(cp);
}

/*
** Advance past one UTF-8 character at tok->pos. Updates pos by the byte
** length and column by 1 (one codepoint). Returns the number of bytes
** consumed.
*/
static int advance_one_codepoint(Tokenizer *tok) {
    unsigned char c = (unsigned char)tok->input[tok->pos];
    int len;
    if (c < 0x80) {
        len = 1;
    } else {
        len = utf8_char_length(c);
        if (len == 0) len = 1; /* skip invalid byte */
    }
    if (tok->pos + (size_t)len > tok->length) {
        len = (int)(tok->length - tok->pos);
    }
    tok->pos += (size_t)len;
    tok->column++;
    return len;
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
** Assumes the first character has already been verified as an ID start.
** Handles both ASCII and UTF-8 identifier characters.
** Returns the length of the identifier in bytes.
*/
static size_t scan_identifier_simd(Tokenizer *tok) {
    size_t start = tok->pos;
    const char *end = tok->input + tok->length;

    /* Skip first char (already validated) -- may be multibyte */
    advance_one_codepoint(tok);

    /* Scan continuation characters using SIMD */
    while (tok->pos < tok->length && remaining(tok) >= 32) {
        CharClassVector cv = tok->classify(tok->input, tok->pos);
        /* ASCII identifier chars are alpha | digit; high bytes need
        ** individual UTF-8 decoding so we break out of the SIMD loop
        ** when we hit one. */
        uint32_t id_mask = cv.is_alpha_mask | cv.is_digit_mask;
        uint32_t high = cv.is_high_byte_mask;

        if (high != 0) {
            /* There are high bytes in this chunk. Process the ASCII prefix
            ** before the first high byte, then fall through to scalar. */
            int first_high = ctz32(high);
            /* Consume ASCII id chars before the high byte */
            uint32_t prefix_mask = id_mask & (((uint32_t)1 << first_high) - 1);
            if (first_high > 0 && prefix_mask == (((uint32_t)1 << first_high) - 1)) {
                advance_n(tok, (size_t)first_high);
            } else if (prefix_mask != 0) {
                uint32_t non_id = ~prefix_mask;
                int count = ctz32(non_id);
                if (count < first_high) {
                    advance_n(tok, (size_t)count);
                    goto done;
                }
                advance_n(tok, (size_t)count);
            } else if (first_high > 0) {
                /* No ASCII id chars before the high byte -- stop */
                goto done;
            }
            /* Now at a high byte -- fall to scalar UTF-8 loop below */
            break;
        }

        if (id_mask == 0xFFFFFFFF) {
            /* All 32 are ASCII identifier chars */
            advance_n(tok, 32);
            continue;
        }
        if (id_mask != 0) {
            /* Some prefix is identifier chars */
            uint32_t non_id = ~id_mask;
            int count = ctz32(non_id);
            advance_n(tok, (size_t)count);
        }
        goto done;
    }

    /* Scalar tail: handles both ASCII and UTF-8 continuation characters */
    while (tok->pos < tok->length) {
        unsigned char c = (unsigned char)tok->input[tok->pos];
        if (c < 0x80) {
            if (is_id_char_ascii(c)) {
                tok->pos++;
                tok->column++;
            } else {
                break;
            }
        } else {
            /* UTF-8 multibyte: decode and check ID_Continue */
            if (is_id_continue_at(tok->input + tok->pos, end)) {
                advance_one_codepoint(tok);
            } else {
                break;
            }
        }
    }

done:
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

/*
** Scan a U&'...' Unicode escape string literal (SQL standard).
** Assumes tok->pos is at the opening single-quote (the 'U' and '&' have
** already been consumed). The string body may contain:
**   \XXXX      - 4-hex-digit Unicode escape (using default backslash)
**   \+XXXXXX   - 6-hex-digit Unicode escape for supplementary codepoints
**   ''         - escaped single-quote (as in regular SQL strings)
** After the closing quote, an optional UESCAPE clause may follow:
**   UESCAPE 'c'  - sets the escape character to 'c' instead of backslash
** We scan the entire literal including any UESCAPE clause as a single token.
** The caller is responsible for semantic validation of escape sequences.
*/
static void scan_ustring(Tokenizer *tok) {
    /* Skip opening quote */
    tok->pos++;
    tok->column++;

    /* Scan string body */
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
            /* End of string body. Check for optional UESCAPE clause. */
            goto check_uescape;
        }
        if (c == '\n') {
            advance_newline(tok);
        } else {
            tok->pos++;
            tok->column++;
        }
    }
    /* Unterminated string */
    return;

check_uescape:
    /* Skip whitespace between closing quote and UESCAPE keyword */
    {
        size_t saved_pos = tok->pos;
        uint32_t saved_line = tok->line;
        uint32_t saved_col = tok->column;

        /* Skip spaces/tabs/newlines */
        while (tok->pos < tok->length) {
            char ws = tok->input[tok->pos];
            if (ws == ' ' || ws == '\t') {
                tok->pos++;
                tok->column++;
            } else if (ws == '\n') {
                advance_newline(tok);
            } else if (ws == '\r') {
                tok->pos++;
                tok->column++;
            } else {
                break;
            }
        }

        /* Check for "UESCAPE" keyword (case-insensitive) */
        if (tok->pos + 7 <= tok->length) {
            const char *kw = tok->input + tok->pos;
            if ((kw[0] == 'U' || kw[0] == 'u') &&
                (kw[1] == 'E' || kw[1] == 'e') &&
                (kw[2] == 'S' || kw[2] == 's') &&
                (kw[3] == 'C' || kw[3] == 'c') &&
                (kw[4] == 'A' || kw[4] == 'a') &&
                (kw[5] == 'P' || kw[5] == 'p') &&
                (kw[6] == 'E' || kw[6] == 'e') &&
                /* UESCAPE must not be followed by an identifier char */
                (tok->pos + 7 >= tok->length ||
                 !is_id_char_ascii((unsigned char)tok->input[tok->pos + 7]))) {
                /* Consume UESCAPE keyword */
                advance_n(tok, 7);

                /* Skip whitespace */
                while (tok->pos < tok->length) {
                    char ws2 = tok->input[tok->pos];
                    if (ws2 == ' ' || ws2 == '\t') {
                        tok->pos++;
                        tok->column++;
                    } else if (ws2 == '\n') {
                        advance_newline(tok);
                    } else if (ws2 == '\r') {
                        tok->pos++;
                        tok->column++;
                    } else {
                        break;
                    }
                }

                /* Expect single-quoted single character: 'c' */
                if (tok->pos + 3 <= tok->length &&
                    tok->input[tok->pos] == '\'' &&
                    tok->input[tok->pos + 2] == '\'') {
                    advance_n(tok, 3);
                }
                /* If malformed, we stop here; semantic errors are for the parser */
                return;
            }
        }

        /* No UESCAPE clause found; revert to position after closing quote */
        tok->pos = saved_pos;
        tok->line = saved_line;
        tok->column = saved_col;
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

    /* Unicode escape string: U&'...' -- must check before identifier */
    if ((c == 'U' || c == 'u') && tok->pos + 2 < tok->length &&
        tok->input[tok->pos + 1] == '&' &&
        tok->input[tok->pos + 2] == '\'') {
        tok->pos += 2;
        tok->column += 2;
        scan_ustring(tok);
        out->type = TK_USTRING;
        out->start = token_start;
        out->length = (size_t)(tok->input + tok->pos - token_start);
        out->line = token_line;
        out->column = token_col;
        return true;
    }

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

    /* Identifier or keyword (ASCII or Unicode ID_Start) */
    if (is_id_start_at(tok->input + tok->pos, tok->input + tok->length)) {
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
        if (c >= 0x80) {
            /* Consume the full UTF-8 sequence so we don't emit separate
            ** ILLEGAL tokens for each continuation byte. */
            int byte_len = utf8_char_length(c);
            if (byte_len == 0) byte_len = 1;
            if (tok->pos + (size_t)byte_len > tok->length)
                byte_len = (int)(tok->length - tok->pos);
            tok->pos += (size_t)byte_len;
            tok->column++;
            out->length = (size_t)byte_len;
        } else {
            advance_n(tok, 1);
            out->length = 1;
        }
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
