/*
** src/lex/lex_tokenize.c -- tokenizer for Lime .lex source files.
**
** Hand-rolled scanner; mirrors the project's existing tokenize.c
** style.  No regex; explicit character dispatch.  Reentrant; all
** state lives in the LimeLexTokenizer struct, no globals.
**
** Token grammar (informal):
**   ws         := [ \t\r\n\f\v]
**   comment    := '/' '*' ... '*' '/' | '/' '/' [^\n]*
**   ident      := [A-Za-z_][A-Za-z0-9_]*
**   integer    := [0-9]+
**   string     := '"' (escape | [^"\\])* '"'
**   regex      := '/' (escape | [^/\\])* '/'
**   code_block := '{' (balanced) '}'    -- skips embedded strings,
**                                          char literals, comments
**   char_lit   := "'" (escape | [^'\\]) "'"
**   eof_marker := '<<EOF>>'
**   directive  := '%' ident
**   punct      := '.' | ',' | ';' | '<' | '>' | '{' | '}' | '(' |
**                 ')' | '='
**
** Whitespace between tokens is skipped.  Line counting tracks
** \n only (\r is ignored for line accounting; \r\n is one
** newline).
*/

#include "lex_tokenize.h"

#include <stdlib.h>
#include <string.h>

/* ----- Tokenizer state ----- */

struct LimeLexTokenizer {
    const char  *filename;
    const char  *src;
    size_t       len;
    size_t       pos;        /* byte cursor into src */
    int          line;       /* current 1-indexed line */
    size_t       n_emitted;
    size_t       n_errors;
};

/* ----- Static helpers ----- */

static int is_ident_start(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_ident_cont(int c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int is_digit(int c) {
    return c >= '0' && c <= '9';
}

static int is_space_no_nl(int c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

/* Look at the current byte without advancing.  Returns -1 at EOF. */
static int peek(const LimeLexTokenizer *t) {
    if (t->pos >= t->len) return -1;
    return (unsigned char) t->src[t->pos];
}

static int peek_at(const LimeLexTokenizer *t, size_t offset) {
    if (t->pos + offset >= t->len) return -1;
    return (unsigned char) t->src[t->pos + offset];
}

/* Advance one byte; track line number. */
static void advance(LimeLexTokenizer *t) {
    if (t->pos >= t->len) return;
    if (t->src[t->pos] == '\n') t->line++;
    t->pos++;
}

/* Skip whitespace and comments.  Returns when the cursor is on a
** non-ws non-comment byte or at EOF. */
static void skip_ws_and_comments(LimeLexTokenizer *t) {
    for (;;) {
        int c = peek(t);
        if (c < 0) return;
        if (c == '\n' || is_space_no_nl(c)) {
            advance(t);
            continue;
        }
        if (c == '/' && peek_at(t, 1) == '/') {
            /* Line comment: //... to end of line */
            while (peek(t) >= 0 && peek(t) != '\n') advance(t);
            continue;
        }
        if (c == '/' && peek_at(t, 1) == '*') {
            /* Block comment: slash-star ... star-slash */
            advance(t); advance(t);   /* eat the slash and star */
            while (peek(t) >= 0) {
                if (peek(t) == '*' && peek_at(t, 1) == '/') {
                    advance(t); advance(t);
                    break;
                }
                advance(t);
            }
            continue;
        }
        return;
    }
}

/* ----- Directive name -> token kind dispatch table.
** Linear scan; the table is small and called once per directive
** opener (which is uncommon vs identifiers/punct), so a
** sophisticated lookup isn't worth the complexity. */

static const struct {
    const char       *name;       /* without leading % */
    LimeLexTokKind    kind;
} k_directive_table[] = {
    { "name_prefix",            LIME_LEX_TOK_DIR_NAME_PREFIX           },
    { "token_prefix",           LIME_LEX_TOK_DIR_TOKEN_PREFIX          },
    { "token_type",             LIME_LEX_TOK_DIR_TOKEN_TYPE            },
    { "location_type",          LIME_LEX_TOK_DIR_LOCATION_TYPE         },
    { "lexer_extra_argument",   LIME_LEX_TOK_DIR_LEXER_EXTRA_ARGUMENT  },
    { "include",                LIME_LEX_TOK_DIR_INCLUDE               },
    { "pattern",                LIME_LEX_TOK_DIR_PATTERN               },
    { "state",                  LIME_LEX_TOK_DIR_STATE                 },
    { "exclusive_state",        LIME_LEX_TOK_DIR_EXCLUSIVE_STATE       },
    { "state_destructor",       LIME_LEX_TOK_DIR_STATE_DESTRUCTOR      },
    { "keyword_table",          LIME_LEX_TOK_DIR_KEYWORD_TABLE         },
    { "literal_buffer",         LIME_LEX_TOK_DIR_LITERAL_BUFFER        },
    { "ruleset",                LIME_LEX_TOK_DIR_RULESET               },
    { "lexer_include",          LIME_LEX_TOK_DIR_LEXER_INCLUDE         },
};

static LimeLexTokKind directive_kind(const char *name, size_t len) {
    size_t i;
    size_t n = sizeof(k_directive_table) / sizeof(k_directive_table[0]);
    for (i = 0; i < n; i++) {
        const char *cand = k_directive_table[i].name;
        if (strlen(cand) == len && memcmp(cand, name, len) == 0) {
            return k_directive_table[i].kind;
        }
    }
    return LIME_LEX_TOK_DIR_UNKNOWN;
}

/* ----- Token-kind name table for diagnostics. ----- */
const char *lime_lex_tok_kind_name(LimeLexTokKind kind) {
    switch (kind) {
        case LIME_LEX_TOK_EOF:                     return "EOF";
        case LIME_LEX_TOK_ERROR:                   return "ERROR";
        case LIME_LEX_TOK_IDENT:                   return "IDENT";
        case LIME_LEX_TOK_INTEGER:                 return "INTEGER";
        case LIME_LEX_TOK_STRING:                  return "STRING";
        case LIME_LEX_TOK_REGEX:                   return "REGEX";
        case LIME_LEX_TOK_CODE_BLOCK:              return "CODE_BLOCK";
        case LIME_LEX_TOK_CHAR_LITERAL:            return "CHAR_LITERAL";
        case LIME_LEX_TOK_DOT:                     return "DOT";
        case LIME_LEX_TOK_COMMA:                   return "COMMA";
        case LIME_LEX_TOK_SEMICOLON:               return "SEMICOLON";
        case LIME_LEX_TOK_LANGLE:                  return "LANGLE";
        case LIME_LEX_TOK_RANGLE:                  return "RANGLE";
        case LIME_LEX_TOK_LBRACE:                  return "LBRACE";
        case LIME_LEX_TOK_RBRACE:                  return "RBRACE";
        case LIME_LEX_TOK_LPAREN:                  return "LPAREN";
        case LIME_LEX_TOK_RPAREN:                  return "RPAREN";
        case LIME_LEX_TOK_EQUALS:                  return "EQUALS";
        case LIME_LEX_TOK_EOF_MARKER:              return "EOF_MARKER";
        case LIME_LEX_TOK_KW_MATCHES:              return "KW_MATCHES";
        case LIME_LEX_TOK_KW_RULE:                 return "KW_RULE";
        case LIME_LEX_TOK_DIR_NAME_PREFIX:         return "DIR_NAME_PREFIX";
        case LIME_LEX_TOK_DIR_TOKEN_PREFIX:        return "DIR_TOKEN_PREFIX";
        case LIME_LEX_TOK_DIR_TOKEN_TYPE:          return "DIR_TOKEN_TYPE";
        case LIME_LEX_TOK_DIR_LOCATION_TYPE:       return "DIR_LOCATION_TYPE";
        case LIME_LEX_TOK_DIR_LEXER_EXTRA_ARGUMENT:return "DIR_LEXER_EXTRA_ARGUMENT";
        case LIME_LEX_TOK_DIR_INCLUDE:             return "DIR_INCLUDE";
        case LIME_LEX_TOK_DIR_PATTERN:             return "DIR_PATTERN";
        case LIME_LEX_TOK_DIR_STATE:               return "DIR_STATE";
        case LIME_LEX_TOK_DIR_EXCLUSIVE_STATE:     return "DIR_EXCLUSIVE_STATE";
        case LIME_LEX_TOK_DIR_STATE_DESTRUCTOR:    return "DIR_STATE_DESTRUCTOR";
        case LIME_LEX_TOK_DIR_KEYWORD_TABLE:       return "DIR_KEYWORD_TABLE";
        case LIME_LEX_TOK_DIR_LITERAL_BUFFER:      return "DIR_LITERAL_BUFFER";
        case LIME_LEX_TOK_DIR_RULESET:             return "DIR_RULESET";
        case LIME_LEX_TOK_DIR_LEXER_INCLUDE:       return "DIR_LEXER_INCLUDE";
        case LIME_LEX_TOK_DIR_UNKNOWN:             return "DIR_UNKNOWN";
        case LIME_LEX_TOK__COUNT:                  return "?COUNT";
    }
    return "?invalid";
}

/* ----- Lifecycle ----- */

LimeLexTokenizer *lime_lex_tokenize_init(const char *filename,
                                         const char *source,
                                         size_t source_len) {
    LimeLexTokenizer *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->filename = filename;
    t->src      = source;
    t->len      = source_len;
    t->pos      = 0;
    t->line     = 1;
    t->n_emitted = 0;
    t->n_errors  = 0;
    return t;
}

void lime_lex_tokenize_free(LimeLexTokenizer *t) {
    free(t);
}

int    lime_lex_tokenize_line(const LimeLexTokenizer *t)        { return t->line; }
size_t lime_lex_tokenize_count(const LimeLexTokenizer *t)       { return t->n_emitted; }
size_t lime_lex_tokenize_error_count(const LimeLexTokenizer *t) { return t->n_errors; }

/* ----- Token recognizers ----- */

/* Set out fields and bump counters.  Caller has already advanced
** the cursor past the lexeme. */
static void emit(LimeLexTokenizer *t,
                 LimeLexToken *out,
                 LimeLexTokKind kind,
                 size_t start,
                 size_t end,
                 int start_line) {
    out->kind   = kind;
    out->lexeme = t->src + start;
    out->length = end - start;
    out->line   = start_line;
    t->n_emitted++;
    if (kind == LIME_LEX_TOK_ERROR) t->n_errors++;
}

/* Consume a balanced `{ ... }` C-code block.  The cursor is
** positioned at the opening `{`.  Skips over embedded strings,
** char literals, and comments so braces inside them don't
** miscount.  On success the cursor is one past the closing `}`.
** Returns the end position; on EOF without close, returns
** t->len and emits ERROR through the caller (we report by
** returning a value past len, which the caller checks). */
static size_t consume_code_block(LimeLexTokenizer *t) {
    int depth = 0;
    /* Cursor is at the opening { */
    while (peek(t) >= 0) {
        int c = peek(t);
        if (c == '{') {
            depth++;
            advance(t);
            continue;
        }
        if (c == '}') {
            depth--;
            advance(t);
            if (depth == 0) return t->pos;
            continue;
        }
        if (c == '/' && peek_at(t, 1) == '/') {
            /* Line comment */
            while (peek(t) >= 0 && peek(t) != '\n') advance(t);
            continue;
        }
        if (c == '/' && peek_at(t, 1) == '*') {
            advance(t); advance(t);
            while (peek(t) >= 0) {
                if (peek(t) == '*' && peek_at(t, 1) == '/') {
                    advance(t); advance(t);
                    break;
                }
                advance(t);
            }
            continue;
        }
        if (c == '"') {
            /* Double-quoted string */
            advance(t);
            while (peek(t) >= 0 && peek(t) != '"') {
                if (peek(t) == '\\' && peek_at(t, 1) >= 0) {
                    advance(t);   /* eat the backslash */
                }
                advance(t);
            }
            if (peek(t) == '"') advance(t);
            continue;
        }
        if (c == '\'') {
            /* Char literal */
            advance(t);
            while (peek(t) >= 0 && peek(t) != '\'') {
                if (peek(t) == '\\' && peek_at(t, 1) >= 0) {
                    advance(t);
                }
                advance(t);
            }
            if (peek(t) == '\'') advance(t);
            continue;
        }
        advance(t);
    }
    /* Hit EOF mid-block; depth != 0 but we've consumed everything. */
    return t->pos;
}

/* Consume "..." string literal.  Cursor positioned at the
** opening quote.  Returns position one past closing quote, or
** t->len on EOF without close. */
static size_t consume_string(LimeLexTokenizer *t) {
    /* eat opening quote */
    advance(t);
    while (peek(t) >= 0 && peek(t) != '"') {
        if (peek(t) == '\\' && peek_at(t, 1) >= 0) {
            advance(t);
        }
        advance(t);
    }
    if (peek(t) == '"') {
        advance(t);
    }
    return t->pos;
}

/* Consume /.../ regex literal.  Cursor positioned at the opening
** slash.  Returns position one past closing slash, or t->len on
** EOF without close. */
static size_t consume_regex(LimeLexTokenizer *t) {
    advance(t);   /* opening slash */
    while (peek(t) >= 0 && peek(t) != '/') {
        if (peek(t) == '\\' && peek_at(t, 1) >= 0) {
            advance(t);
        }
        if (peek(t) == '\n') {
            /* Disallow newlines in regex literals to avoid
            ** runaway-tokenizer diagnostics on missing close
            ** slash.  Caller can split the regex across multiple
            ** %pattern declarations if it really needs to be
            ** that long. */
            return t->pos;
        }
        advance(t);
    }
    if (peek(t) == '/') {
        advance(t);
    }
    return t->pos;
}

/* Consume '.' char literal.  Cursor at opening single quote. */
static size_t consume_char_literal(LimeLexTokenizer *t) {
    advance(t);   /* opening ' */
    while (peek(t) >= 0 && peek(t) != '\'') {
        if (peek(t) == '\\' && peek_at(t, 1) >= 0) {
            advance(t);
        }
        if (peek(t) == '\n') return t->pos;
        advance(t);
    }
    if (peek(t) == '\'') advance(t);
    return t->pos;
}

/* Consume an identifier.  Returns end position. */
static size_t consume_ident(LimeLexTokenizer *t) {
    while (is_ident_cont(peek(t))) advance(t);
    return t->pos;
}

/* Consume a run of digits. */
static size_t consume_integer(LimeLexTokenizer *t) {
    while (is_digit(peek(t))) advance(t);
    return t->pos;
}

/* ----- Top-level dispatch ----- */

int lime_lex_tokenize_next(LimeLexTokenizer *t, LimeLexToken *out) {
    skip_ws_and_comments(t);

    int start_line = t->line;
    size_t start  = t->pos;

    int c = peek(t);
    if (c < 0) {
        out->kind   = LIME_LEX_TOK_EOF;
        out->lexeme = t->src + t->len;
        out->length = 0;
        out->line   = t->line;
        return 0;
    }

    /* << for <<EOF>> -- check before single < punct. */
    if (c == '<' && peek_at(t, 1) == '<') {
        /* Expect <<EOF>> exactly. */
        static const char k_marker[] = "<<EOF>>";
        size_t mlen = sizeof(k_marker) - 1;
        if (t->pos + mlen <= t->len &&
            memcmp(t->src + t->pos, k_marker, mlen) == 0) {
            for (size_t i = 0; i < mlen; i++) advance(t);
            emit(t, out, LIME_LEX_TOK_EOF_MARKER, start, t->pos, start_line);
            return 0;
        }
        /* Fall through: lone '<' followed by '<' is malformed but
        ** we tokenise as two LANGLE tokens; the parser surfaces
        ** the diagnostic. */
    }

    /* Directive: %ident */
    if (c == '%') {
        advance(t);   /* eat % */
        if (!is_ident_start(peek(t))) {
            /* lone %: emit as ERROR */
            emit(t, out, LIME_LEX_TOK_ERROR, start, t->pos, start_line);
            return 0;
        }
        size_t name_start = t->pos;
        consume_ident(t);
        LimeLexTokKind kind = directive_kind(t->src + name_start,
                                             t->pos - name_start);
        emit(t, out, kind, start, t->pos, start_line);
        return 0;
    }

    /* Identifier / keyword.  `matches` and `rule` are keywords. */
    if (is_ident_start(c)) {
        consume_ident(t);
        size_t len = t->pos - start;
        if (len == 7 && memcmp(t->src + start, "matches", 7) == 0) {
            emit(t, out, LIME_LEX_TOK_KW_MATCHES, start, t->pos, start_line);
        } else if (len == 4 && memcmp(t->src + start, "rule", 4) == 0) {
            emit(t, out, LIME_LEX_TOK_KW_RULE, start, t->pos, start_line);
        } else {
            emit(t, out, LIME_LEX_TOK_IDENT, start, t->pos, start_line);
        }
        return 0;
    }

    /* Integer */
    if (is_digit(c)) {
        consume_integer(t);
        emit(t, out, LIME_LEX_TOK_INTEGER, start, t->pos, start_line);
        return 0;
    }

    /* String literal */
    if (c == '"') {
        consume_string(t);
        emit(t, out, LIME_LEX_TOK_STRING, start, t->pos, start_line);
        return 0;
    }

    /* Regex literal */
    if (c == '/') {
        consume_regex(t);
        emit(t, out, LIME_LEX_TOK_REGEX, start, t->pos, start_line);
        return 0;
    }

    /* Char literal */
    if (c == '\'') {
        consume_char_literal(t);
        emit(t, out, LIME_LEX_TOK_CHAR_LITERAL, start, t->pos, start_line);
        return 0;
    }

    /* Code block */
    if (c == '{') {
        consume_code_block(t);
        emit(t, out, LIME_LEX_TOK_CODE_BLOCK, start, t->pos, start_line);
        return 0;
    }

    /* Single-char punctuation */
    {
        LimeLexTokKind kind;
        switch (c) {
            case '.': kind = LIME_LEX_TOK_DOT;       break;
            case ',': kind = LIME_LEX_TOK_COMMA;     break;
            case ';': kind = LIME_LEX_TOK_SEMICOLON; break;
            case '<': kind = LIME_LEX_TOK_LANGLE;    break;
            case '>': kind = LIME_LEX_TOK_RANGLE;    break;
            case '}': kind = LIME_LEX_TOK_RBRACE;    break;
            case '(': kind = LIME_LEX_TOK_LPAREN;    break;
            case ')': kind = LIME_LEX_TOK_RPAREN;    break;
            case '=': kind = LIME_LEX_TOK_EQUALS;    break;
            default:
                advance(t);
                emit(t, out, LIME_LEX_TOK_ERROR, start, t->pos, start_line);
                return 0;
        }
        advance(t);
        emit(t, out, kind, start, t->pos, start_line);
        return 0;
    }
}
