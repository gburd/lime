/*
 * lsp_semantic_tokens.c -- textDocument/semanticTokens/full handler.
 *
 * The legend (see lsp_semantic_token_types[] / lsp_semantic_token_modifiers[])
 * is the same one advertised in `initialize.semanticTokensProvider.legend`,
 * so token-type IDs decoded by the editor map directly to the
 * names below:
 *
 *   0  keyword     -- %first_token, %literal_buffer, ...
 *   1  namespace   -- %dialect / %extends / %module_name argument
 *   2  class       -- terminal (uppercase by lime convention)
 *   3  variable    -- non-terminal
 *   4  function    -- not used by the lime tokenizer's coarse pass
 *                     (action bodies are emitted as a single chunk
 *                     and individual identifiers inside them are
 *                     skipped wholesale -- editors with C syntax
 *                     highlighting handle the contents)
 *   5  string      -- "..." and '...' literals
 *   6  number      -- digit runs (rare but legal in `%expect <N>`)
 *   7  comment     -- block-comment and line-comment runs
 *   8  operator    -- ::= | . , ;
 *   9  type        -- not currently emitted (kept for future use)
 *  10  macro       -- not currently emitted
 *  11  label       -- not currently emitted
 *
 * Modifier bits (none currently emitted; reserved):
 *   bit 0 -- definition
 *   bit 1 -- deprecated
 *
 * Encoding follows the LSP spec for SemanticTokens:
 *
 *   data := flat array of int5 tuples per token, in document
 *           order:
 *
 *     [ deltaLine, deltaStart, length, tokenType, tokenMods ]
 *
 *   deltaLine  = thisToken.line - prevToken.line
 *   deltaStart = (thisToken.line == prevToken.line)
 *                  ? thisToken.col - prevToken.col
 *                  : thisToken.col
 *
 * For the very first token both deltas are absolute.  Tokens that
 * span multiple lines (block comments, brace bodies, multi-line
 * strings) are split into one entry per line so the encoding
 * stays single-line per tuple.
 */

#include "lsp_navigation.h"
#include "lsp_documents.h"
#include "lsp_json.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

const char *const lsp_semantic_token_types[] = {
    "keyword", "namespace", "class", "variable",
    "function", "string", "number", "comment",
    "operator", "type", "macro", "label",
    NULL
};

const char *const lsp_semantic_token_modifiers[] = {
    "definition", "deprecated", NULL
};

enum {
    ST_KEYWORD   = 0,
    ST_NAMESPACE = 1,
    ST_CLASS     = 2,
    ST_VARIABLE  = 3,
    ST_STRING    = 5,
    ST_NUMBER    = 6,
    ST_COMMENT   = 7,
    ST_OPERATOR  = 8
};

typedef struct {
    long long line;
    long long col;
    long long length;
    int       type;
    int       mods;
} stoken;

typedef struct {
    stoken *items;
    size_t  count;
    size_t  cap;
} stokens;

static int stokens_push(stokens *s, long long line, long long col,
                        long long length, int type, int mods) {
    if (length <= 0) return 1;
    if (s->count == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 64;
        stoken *ni = (stoken *)realloc(s->items, nc * sizeof(*ni));
        if (!ni) return 0;
        s->items = ni; s->cap = nc;
    }
    s->items[s->count].line   = line;
    s->items[s->count].col    = col;
    s->items[s->count].length = length;
    s->items[s->count].type   = type;
    s->items[s->count].mods   = mods;
    s->count++;
    return 1;
}

/* Emit one stoken per source line covered by [start,end), so the
 * delta encoding can stay within a single line per entry.  The
 * input range may straddle line boundaries (block comments,
 * brace bodies, multi-line string literals). */
static void emit_range(stokens *out, const char *text, size_t text_len,
                       size_t start, size_t end, int type, int mods) {
    if (start >= end || start >= text_len) return;
    if (end > text_len) end = text_len;
    /* Compute initial line/col of `start`. */
    long long line, col;
    lsp_offset_to_position(text, text_len, start, &line, &col);
    size_t i = start;
    long long cur_col = col;
    while (i < end) {
        size_t lstart = i;
        long long lcol = cur_col;
        while (i < end && text[i] != '\n') i++;
        long long len = (long long)(i - lstart);
        if (len > 0) stokens_push(out, line, lcol, len, type, mods);
        if (i < end && text[i] == '\n') {
            i++;
            line++;
            cur_col = 0;
        }
    }
}

static int is_id_start(unsigned char c) { return isalpha(c) || c == '_'; }
static int is_id_cont(unsigned char c)  { return isalnum(c) || c == '_'; }

/* True if all alphabetic chars in [s,e) are uppercase.  Lime's
 * convention is uppercase for terminals.  This is the
 * fallback when the symbol table doesn't have a definition for
 * an identifier (e.g. mid-edit or partial grammar).
 */
static int looks_like_terminal(const char *t, size_t s, size_t e) {
    int has_alpha = 0;
    for (size_t i = s; i < e; i++) {
        unsigned char c = (unsigned char)t[i];
        if (isalpha(c)) {
            has_alpha = 1;
            if (islower(c)) return 0;
        }
    }
    return has_alpha;
}

/* Skip a balanced brace block starting at the `{` at index i.
 * Returns the offset just past the closing `}` (or text_len). */
static size_t skip_braces(const char *t, size_t n, size_t i) {
    int depth = 0;
    while (i < n) {
        unsigned char c = (unsigned char)t[i];
        if (c == '{') { depth++; i++; }
        else if (c == '}') { depth--; i++; if (depth == 0) return i; }
        else if (c == '/' && i + 1 < n && t[i+1] == '/') {
            while (i < n && t[i] != '\n') i++;
        } else if (c == '/' && i + 1 < n && t[i+1] == '*') {
            i += 2;
            while (i + 1 < n && !(t[i] == '*' && t[i+1] == '/')) i++;
            if (i + 1 < n) i += 2; else i = n;
        } else if (c == '"' || c == '\'') {
            char q = (char)c;
            i++;
            while (i < n && t[i] != q) {
                if (t[i] == '\\' && i + 1 < n) i += 2;
                else i++;
            }
            if (i < n) i++;
        } else {
            i++;
        }
    }
    return i;
}

/* True if the directive name (without leading `%`) takes a
 * namespace-style argument: %dialect, %extends, %module_name,
 * %import, %require, %from.  When such a directive is seen, the
 * very next identifier is classified as `namespace` rather than
 * the usual class / variable. */
static int dir_namespace_arg(const char *name, size_t nlen) {
    static const struct { const char *n; size_t l; } kList[] = {
        {"dialect",     7},
        {"extends",     7},
        {"module_name", 11},
        {"import",      6},
        {"require",     7},
        {"from",        4},
        {"name",        4},
        {"name_prefix", 11},
        {NULL, 0}
    };
    for (size_t i = 0; kList[i].n; i++) {
        if (kList[i].l == nlen && memcmp(kList[i].n, name, nlen) == 0)
            return 1;
    }
    return 0;
}

/* Single-pass scanner that produces classified tokens.  We
 * deliberately do this in one walk rather than reusing the
 * lsp_navigation tokenizer because semantic tokens need
 * comment/string/number/operator ranges that the navigation
 * scanner discards.
 */
static void scan_semantic(const char *t, size_t n, const lsp_symtab *st,
                          stokens *out) {
    int next_id_namespace = 0;
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)t[i];
        /* Whitespace */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v') {
            i++;
            continue;
        }
        /* Line comment */
        if (c == '/' && i + 1 < n && t[i+1] == '/') {
            size_t s = i;
            while (i < n && t[i] != '\n') i++;
            emit_range(out, t, n, s, i, ST_COMMENT, 0);
            continue;
        }
        /* Block comment */
        if (c == '/' && i + 1 < n && t[i+1] == '*') {
            size_t s = i;
            i += 2;
            while (i + 1 < n && !(t[i] == '*' && t[i+1] == '/')) i++;
            if (i + 1 < n) i += 2; else i = n;
            emit_range(out, t, n, s, i, ST_COMMENT, 0);
            continue;
        }
        /* Directive */
        if (c == '%') {
            size_t s = i++;
            while (i < n && is_id_cont((unsigned char)t[i])) i++;
            emit_range(out, t, n, s, i, ST_KEYWORD, 0);
            if (i - s > 1 && dir_namespace_arg(t + s + 1, i - s - 1)) {
                next_id_namespace = 1;
            }
            continue;
        }
        /* Identifier */
        if (is_id_start(c)) {
            size_t s = i++;
            while (i < n && is_id_cont((unsigned char)t[i])) i++;
            int type;
            if (next_id_namespace) {
                type = ST_NAMESPACE;
                next_id_namespace = 0;
            } else if (st) {
                const lsp_symbol *sym = lsp_symtab_lookup(st, t + s, i - s);
                if (sym && sym->kind == LSP_SYM_TERMINAL)        type = ST_CLASS;
                else if (sym && sym->kind == LSP_SYM_NONTERMINAL) type = ST_VARIABLE;
                else if (looks_like_terminal(t, s, i))            type = ST_CLASS;
                else                                               type = ST_VARIABLE;
            } else {
                type = looks_like_terminal(t, s, i) ? ST_CLASS : ST_VARIABLE;
            }
            emit_range(out, t, n, s, i, type, 0);
            continue;
        }
        /* Number */
        if (isdigit(c)) {
            size_t s = i++;
            while (i < n && isdigit((unsigned char)t[i])) i++;
            emit_range(out, t, n, s, i, ST_NUMBER, 0);
            continue;
        }
        /* String */
        if (c == '"' || c == '\'') {
            char q = (char)c;
            size_t s = i++;
            while (i < n && t[i] != q) {
                if (t[i] == '\\' && i + 1 < n) i += 2;
                else i++;
            }
            if (i < n) i++;
            emit_range(out, t, n, s, i, ST_STRING, 0);
            continue;
        }
        /* Brace block: skip wholesale; emit braces as operators
         * but skip the contents (action bodies are C, the editor's
         * C syntax highlighter handles them). */
        if (c == '{') {
            long long bl, bc;
            lsp_offset_to_position(t, n, i, &bl, &bc);
            stokens_push(out, bl, bc, 1, ST_OPERATOR, 0);
            size_t e = skip_braces(t, n, i);
            if (e > i + 1) {
                long long el, ec;
                lsp_offset_to_position(t, n, e - 1, &el, &ec);
                stokens_push(out, el, ec, 1, ST_OPERATOR, 0);
            }
            i = e;
            continue;
        }
        /* `::=` */
        if (c == ':' && i + 2 < n && t[i+1] == ':' && t[i+2] == '=') {
            long long ln, col;
            lsp_offset_to_position(t, n, i, &ln, &col);
            stokens_push(out, ln, col, 3, ST_OPERATOR, 0);
            i += 3;
            continue;
        }
        /* Single-char operators. */
        if (c == '.' || c == '|' || c == ',' || c == ';' ||
            c == '(' || c == ')' || c == '[' || c == ']' ||
            c == '}') {
            long long ln, col;
            lsp_offset_to_position(t, n, i, &ln, &col);
            stokens_push(out, ln, col, 1, ST_OPERATOR, 0);
            i++;
            continue;
        }
        /* Anything else: skip silently.  We don't error -- partial
         * / mid-edit grammars must keep producing usable highlights. */
        i++;
    }
}

/* Stable comparator on (line, col) used to sort the emitted token
 * list before delta-encoding.  scan_semantic emits tokens in
 * source order with one exception: brace blocks emit the open
 * brace, then optionally the close brace, before continuing past
 * the body.  Tokens inside the body are already skipped, so the
 * sequence is monotone -- but we sort defensively to keep the
 * encoder oblivious to scanner order quirks.
 */
static int stoken_cmp(const void *a, const void *b) {
    const stoken *x = (const stoken *)a;
    const stoken *y = (const stoken *)b;
    if (x->line != y->line) return (x->line < y->line) ? -1 : 1;
    if (x->col  != y->col ) return (x->col  < y->col ) ? -1 : 1;
    return 0;
}

json_value *lsp_navigation_semantic_tokens(const char *text, size_t text_len) {
    lsp_symtab st;
    lsp_symtab_build(&st, text, text_len);

    stokens toks = {0};
    scan_semantic(text, text_len, &st, &toks);
    qsort(toks.items, toks.count, sizeof(*toks.items), stoken_cmp);

    json_value *data = json_make_array();
    long long prev_line = 0;
    long long prev_col  = 0;
    for (size_t i = 0; i < toks.count; i++) {
        stoken *tk = &toks.items[i];
        if (tk->length <= 0) continue;
        long long dl = tk->line - prev_line;
        long long ds = (tk->line == prev_line) ? (tk->col - prev_col)
                                                : tk->col;
        json_array_push(data, json_make_int(dl));
        json_array_push(data, json_make_int(ds));
        json_array_push(data, json_make_int(tk->length));
        json_array_push(data, json_make_int(tk->type));
        json_array_push(data, json_make_int(tk->mods));
        prev_line = tk->line;
        prev_col  = tk->col;
    }
    free(toks.items);
    lsp_symtab_free(&st);

    json_value *out = json_make_object();
    json_object_set(out, "data", data);
    return out;
}
