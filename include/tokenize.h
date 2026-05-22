/*
** SIMD-Accelerated SQL Tokenizer
**
** Provides a fast tokenizer for SQL input using SIMD-accelerated character
** classification.  The tokenizer uses the character classification functions
** from tokenize_simd.h to skip whitespace and scan identifiers/numbers in
** bulk, then falls back to scalar logic for operators and punctuation.
**
** Integration points:
**   - TokenTable (token_table.h) for keyword lookup
**   - CharClassVector (tokenize_simd.h) for parallel character classification
*/
#ifndef TOKENIZE_H
#define TOKENIZE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct TokenTable TokenTable;

/* ------------------------------------------------------------------ */
/*  Token representation                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief A single token returned by the tokenizer.
 */
typedef struct Token {
    int type;          /**< Token type code (keyword code or generic) */
    const char *start; /**< Pointer into source buffer */
    size_t length;     /**< Length in bytes */
    uint32_t line;     /**< 1-based line number */
    uint32_t column;   /**< 1-based column number */
} Token;

/* Generic token type codes for non-keyword tokens.
** Keyword tokens get their type from the TokenTable.
** These codes use negative values to avoid collision with
** user-defined token codes (which are positive). */
enum {
    TK_EOF = 0,           /* End of input */
    TK_IDENTIFIER = -1,   /* Unrecognized identifier */
    TK_INTEGER = -2,      /* Integer literal */
    TK_FLOAT = -3,        /* Floating point literal */
    TK_STRING = -4,       /* Single-quoted string literal */
    TK_BLOB = -5,         /* X'...' blob literal */
    TK_LPAREN = -6,       /* ( */
    TK_RPAREN = -7,       /* ) */
    TK_SEMICOLON = -8,    /* ; */
    TK_COMMA = -9,        /* , */
    TK_DOT = -10,         /* . */
    TK_STAR = -11,        /* * */
    TK_PLUS = -12,        /* + */
    TK_MINUS = -13,       /* - */
    TK_SLASH = -14,       /* / */
    TK_PERCENT = -15,     /* % */
    TK_EQ = -16,          /* = or == */
    TK_NE = -17,          /* != or <> */
    TK_LT = -18,          /* < */
    TK_GT = -19,          /* > */
    TK_LE = -20,          /* <= */
    TK_GE = -21,          /* >= */
    TK_BITAND = -22,      /* & */
    TK_BITOR = -23,       /* | */
    TK_BITNOT = -24,      /* ~ */
    TK_LSHIFT = -25,      /* << */
    TK_RSHIFT = -26,      /* >> */
    TK_CONCAT = -27,      /* || */
    TK_DQUOTE_ID = -28,   /* "quoted identifier" */
    TK_BACKTICK_ID = -29, /* `backtick identifier` */
    TK_BRACKET_ID = -30,  /* [bracket identifier] */
    TK_USTRING = -31,     /* U&'...' Unicode escape string literal */
    TK_ILLEGAL = -32,     /* Unrecognized character */
};

/* ------------------------------------------------------------------ */
/*  Tokenizer state                                                    */
/* ------------------------------------------------------------------ */

typedef struct Tokenizer Tokenizer;

/*
** Create a new tokenizer for the given input buffer.
** The input buffer must remain valid for the lifetime of the tokenizer.
**
** table: keyword lookup table (may be NULL for identifier-only mode).
** input: NUL-terminated SQL input string.
** length: length of input in bytes (not including NUL terminator).
**         The buffer must have at least 32 bytes of readable memory
**         past the end (e.g., zero-padded) for SIMD safety.
**
** Returns NULL on allocation failure.
*/
Tokenizer *tokenizer_create(TokenTable *table, const char *input, size_t length);

/*
** Destroy a tokenizer and free its memory.
** Passing NULL is safe.
*/
void tokenizer_destroy(Tokenizer *tok);

/*
** Extract the next token from the input.
** Returns true if a token was produced, false at end-of-input.
** On false return, out->type is TK_EOF.
*/
bool tokenizer_next(Tokenizer *tok, Token *out);

/*
** Peek at the next token without consuming it.
** Returns true if a token is available, false at end-of-input.
*/
bool tokenizer_peek(Tokenizer *tok, Token *out);

/*
** Return the current position (byte offset) in the input.
*/
size_t tokenizer_position(const Tokenizer *tok);

/*
** Return the current line number (1-based).
*/
uint32_t tokenizer_line(const Tokenizer *tok);

/*
** Return the current column number (1-based).
*/
uint32_t tokenizer_column(const Tokenizer *tok);

#ifdef __cplusplus
}
#endif

#endif /* TOKENIZE_H */
