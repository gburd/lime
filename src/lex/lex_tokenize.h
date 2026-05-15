/*
** src/lex/lex_tokenize.h -- tokenizer for Lime .lex source files.
**
** Stage 1 of the .lex frontend.  Produces a stream of typed tokens
** from a .lex byte buffer; the M1 parser (next phase) consumes
** these tokens to build a LimeLexSpec AST.
**
** Lifecycle: allocate a tokenizer with lime_lex_tokenize_init,
** repeatedly call lime_lex_tokenize_next to consume one token at a
** time, free with lime_lex_tokenize_free.  The token's lexeme is a
** read-only pointer into the caller's source buffer; lifetime is
** the source buffer's lifetime.  Heap copies are the caller's
** responsibility (the parser does this when it stashes lexemes
** into AST nodes).
**
** Reentrant by construction; no globals; mirrors parser-side
** push-driven philosophy.
*/
#ifndef LIME_LEX_TOKENIZE_H
#define LIME_LEX_TOKENIZE_H

#include <stddef.h>

/* Token kinds emitted by the .lex tokenizer.  Order is stable for
** the lifetime of the project (don't renumber; tests assert on
** specific values).  Kinds are grouped:
**
**   - End / error markers
**   - Lexical primitives (identifiers, numbers, strings)
**   - Regex literal (/.../ form) -- distinct from string literal
**   - Punctuation
**   - Keywords (`matches`, `<<EOF>>`)
**   - Directive starters (`%pattern`, `%state`, ...)
**   - C-code blocks (`{ ... }`)
*/
typedef enum {
    /* Terminal markers. */
    LIME_LEX_TOK_EOF = 0,             /* end of input */
    LIME_LEX_TOK_ERROR,               /* unrecognised input (lexeme/length
                                       ** point at the offending byte(s)) */

    /* Primitives. */
    LIME_LEX_TOK_IDENT,               /* [A-Za-z_][A-Za-z0-9_]* */
    LIME_LEX_TOK_INTEGER,             /* [0-9]+ */
    LIME_LEX_TOK_STRING,              /* "..." with C-style escapes */
    LIME_LEX_TOK_REGEX,               /* /.../ regex literal */
    LIME_LEX_TOK_CODE_BLOCK,          /* { ... } C-code block, balanced
                                       ** braces (lexeme INCLUDES the
                                       ** outer braces and embedded
                                       ** comments, strings, char-lits) */
    LIME_LEX_TOK_CHAR_LITERAL,        /* '.' single-quoted char (e.g. ',') */

    /* Single-char punctuation. */
    LIME_LEX_TOK_DOT,                 /* . */
    LIME_LEX_TOK_COMMA,               /* , */
    LIME_LEX_TOK_SEMICOLON,           /* ; */
    LIME_LEX_TOK_LANGLE,              /* < */
    LIME_LEX_TOK_RANGLE,              /* > */
    LIME_LEX_TOK_LBRACE,              /* { -- when not consumed as CODE_BLOCK */
    LIME_LEX_TOK_RBRACE,              /* } */
    LIME_LEX_TOK_LPAREN,              /* ( */
    LIME_LEX_TOK_RPAREN,              /* ) */
    LIME_LEX_TOK_EQUALS,              /* = */

    /* Multi-char marker tokens. */
    LIME_LEX_TOK_EOF_MARKER,          /* <<EOF>> */
    LIME_LEX_TOK_KW_MATCHES,          /* the `matches` keyword */

    /* Directives. */
    LIME_LEX_TOK_DIR_NAME_PREFIX,           /* %name_prefix */
    LIME_LEX_TOK_DIR_TOKEN_PREFIX,          /* %token_prefix */
    LIME_LEX_TOK_DIR_TOKEN_TYPE,            /* %token_type */
    LIME_LEX_TOK_DIR_LOCATION_TYPE,         /* %location_type */
    LIME_LEX_TOK_DIR_LEXER_EXTRA_ARGUMENT,  /* %lexer_extra_argument */
    LIME_LEX_TOK_DIR_INCLUDE,               /* %include */
    LIME_LEX_TOK_DIR_PATTERN,               /* %pattern */
    LIME_LEX_TOK_DIR_STATE,                 /* %state */
    LIME_LEX_TOK_DIR_EXCLUSIVE_STATE,       /* %exclusive_state */
    LIME_LEX_TOK_DIR_STATE_DESTRUCTOR,      /* %state_destructor */
    LIME_LEX_TOK_DIR_KEYWORD_TABLE,         /* %keyword_table */
    LIME_LEX_TOK_DIR_LITERAL_BUFFER,        /* %literal_buffer */
    LIME_LEX_TOK_DIR_RULESET,               /* %ruleset */
    LIME_LEX_TOK_DIR_LEXER_INCLUDE,         /* %lexer_include */
    LIME_LEX_TOK_DIR_UNKNOWN,                /* %something else */

    LIME_LEX_TOK__COUNT
} LimeLexTokKind;

/* Stable name for a token kind.  Returns a static read-only
** pointer; never NULL.  Useful for diagnostics and tests. */
const char *lime_lex_tok_kind_name(LimeLexTokKind kind);

/* Tokenizer instance.  Opaque to callers. */
typedef struct LimeLexTokenizer LimeLexTokenizer;

/* One token of output.  `lexeme` points into the caller's source
** buffer; valid until the source buffer is freed.  `length` is
** the lexeme's byte length.  For tokens carrying meaningful
** content (IDENT, INTEGER, STRING, REGEX, CODE_BLOCK, CHAR_LITERAL,
** ERROR) the lexeme is the raw matched text including delimiters
** where applicable (so STRING includes the surrounding quotes;
** REGEX includes the slashes; CODE_BLOCK includes the outer
** braces).  For punctuation the lexeme is the single-byte marker.
** For directive starters the lexeme is the full %name keyword.
**
** `line` is 1-indexed at the start of the lexeme.
*/
typedef struct {
    LimeLexTokKind  kind;
    const char     *lexeme;
    size_t          length;
    int             line;
} LimeLexToken;

/* Allocate a tokenizer for a (source, length) byte buffer.  The
** buffer is borrowed for the tokenizer's lifetime; do not free
** until after lime_lex_tokenize_free.  `filename` is borrowed
** similarly and used in diagnostics.  Returns NULL on alloc
** failure. */
LimeLexTokenizer *lime_lex_tokenize_init(const char *filename,
                                         const char *source,
                                         size_t source_len);

/* Emit the next token.  On EOF the kind is LIME_LEX_TOK_EOF and
** subsequent calls keep returning EOF (no advance).  On
** unrecognised input the kind is LIME_LEX_TOK_ERROR and the
** lexeme/length point at the offending byte(s); the tokenizer
** advances past them so callers can keep going to find more
** errors.  Returns 0 on success, non-zero on internal error
** (currently unused; reserved). */
int lime_lex_tokenize_next(LimeLexTokenizer *t, LimeLexToken *out);

/* Current line number (1-indexed).  Useful in test diagnostics. */
int lime_lex_tokenize_line(const LimeLexTokenizer *t);

/* Number of tokens emitted so far. */
size_t lime_lex_tokenize_count(const LimeLexTokenizer *t);

/* Number of LIME_LEX_TOK_ERROR tokens emitted so far. */
size_t lime_lex_tokenize_error_count(const LimeLexTokenizer *t);

/* Release the tokenizer.  Does NOT free the source buffer. */
void lime_lex_tokenize_free(LimeLexTokenizer *t);

#endif /* LIME_LEX_TOKENIZE_H */
