/*
 * lsp_navigation.c -- in-process .lime tokenizer driving the
 * symbol table that backs textDocument/definition,
 * textDocument/hover, and textDocument/documentSymbol.
 *
 * The scanner is intentionally minimal -- it mirrors the
 * tokenization style in lime.c::Parse() but only enough to
 * recognize directives, rule LHS / RHS, and identifiers.  Action
 * bodies (`{ ... }`) are skipped wholesale: we count brace depth
 * but never recurse into them.  This is sufficient for the LSP
 * features in the MVP and keeps the analysis robust against
 * partial / mid-edit grammars.
 */

#include "lsp_navigation.h"

#include "lsp_documents.h"
#include "lsp_json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- token stream --------------------------------------------------- */

typedef enum {
    TK_EOF = 0,
    TK_IDENT,        /* [A-Za-z_][A-Za-z0-9_]* */
    TK_DIRECTIVE,    /* %name, %token, ...     */
    TK_ARROW,        /* ::=                    */
    TK_DOT,          /* .                      */
    TK_LBRACE,       /* {                      */
    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACK,
    TK_RBRACK,
    TK_PIPE,
    TK_OTHER         /* every other punctuation we don't care about */
} tk_kind;

typedef struct {
    tk_kind kind;
    size_t  start;
    size_t  end;     /* one past last byte */
} tk_t;

typedef struct {
    tk_t  *items;
    size_t count;
    size_t cap;
} tk_list;

static int tk_push(tk_list *l, tk_kind k, size_t s, size_t e) {
    if (l->count == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 64;
        tk_t *ni = (tk_t *)realloc(l->items, nc * sizeof(*ni));
        if (!ni) return 0;
        l->items = ni; l->cap = nc;
    }
    l->items[l->count].kind = k;
    l->items[l->count].start = s;
    l->items[l->count].end   = e;
    l->count++;
    return 1;
}

static void tk_free(tk_list *l) { free(l->items); l->items = NULL; }

static int is_id_start(unsigned char c) {
    return isalpha(c) || c == '_';
}
static int is_id_cont(unsigned char c) {
    return isalnum(c) || c == '_';
}

/* Skip whitespace and comments.  Returns the new position. */
static size_t skip_trivia(const char *t, size_t n, size_t i) {
    while (i < n) {
        unsigned char c = (unsigned char)t[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v') {
            i++;
        } else if (c == '/' && i + 1 < n && t[i+1] == '/') {
            i += 2;
            while (i < n && t[i] != '\n') i++;
        } else if (c == '/' && i + 1 < n && t[i+1] == '*') {
            i += 2;
            while (i + 1 < n && !(t[i] == '*' && t[i+1] == '/')) i++;
            if (i + 1 < n) i += 2;
            else i = n;
        } else {
            break;
        }
    }
    return i;
}

/* Skip a balanced brace block starting at `{`. */
static size_t skip_brace_block(const char *t, size_t n, size_t i) {
    int depth = 0;
    while (i < n) {
        unsigned char c = (unsigned char)t[i];
        if (c == '{') { depth++; i++; }
        else if (c == '}') { depth--; i++; if (depth == 0) return i; }
        else if (c == '/' && i + 1 < n && t[i+1] == '/') {
            i += 2;
            while (i < n && t[i] != '\n') i++;
        }
        else if (c == '/' && i + 1 < n && t[i+1] == '*') {
            i += 2;
            while (i + 1 < n && !(t[i] == '*' && t[i+1] == '/')) i++;
            if (i + 1 < n) i += 2;
            else i = n;
        }
        else if (c == '"' || c == '\'') {
            char q = (char)c;
            i++;
            while (i < n && t[i] != q) {
                if (t[i] == '\\' && i + 1 < n) i += 2;
                else i++;
            }
            if (i < n) i++;
        }
        else { i++; }
    }
    return i;
}

static int tokenize(const char *t, size_t n, tk_list *out) {
    size_t i = 0;
    while (i < n) {
        i = skip_trivia(t, n, i);
        if (i >= n) break;
        unsigned char c = (unsigned char)t[i];

        if (c == '%') {
            size_t s = i++;
            while (i < n && is_id_cont((unsigned char)t[i])) i++;
            if (!tk_push(out, TK_DIRECTIVE, s, i)) return 0;
            continue;
        }
        if (is_id_start(c)) {
            size_t s = i++;
            while (i < n && is_id_cont((unsigned char)t[i])) i++;
            if (!tk_push(out, TK_IDENT, s, i)) return 0;
            continue;
        }
        if (c == ':' && i + 2 < n && t[i+1] == ':' && t[i+2] == '=') {
            if (!tk_push(out, TK_ARROW, i, i + 3)) return 0;
            i += 3;
            continue;
        }
        if (c == '.') {
            /* Avoid eating '.' inside numbers; we don't have
             * numbers in lime grammars at the structural level,
             * so any literal '.' here is the rule terminator. */
            if (!tk_push(out, TK_DOT, i, i + 1)) return 0;
            i++;
            continue;
        }
        if (c == '{') {
            size_t s = i;
            size_t e = skip_brace_block(t, n, i);
            if (!tk_push(out, TK_LBRACE, s, e)) return 0;
            i = e;
            continue;
        }
        if (c == '(') { tk_push(out, TK_LPAREN, i, i+1); i++; continue; }
        if (c == ')') { tk_push(out, TK_RPAREN, i, i+1); i++; continue; }
        if (c == '[') { tk_push(out, TK_LBRACK, i, i+1); i++; continue; }
        if (c == ']') { tk_push(out, TK_RBRACK, i, i+1); i++; continue; }
        if (c == '|') { tk_push(out, TK_PIPE,   i, i+1); i++; continue; }
        if (c == '"' || c == '\'') {
            char q = (char)c;
            size_t s = i++;
            while (i < n && t[i] != q) {
                if (t[i] == '\\' && i + 1 < n) i += 2;
                else i++;
            }
            if (i < n) i++;
            if (!tk_push(out, TK_OTHER, s, i)) return 0;
            continue;
        }
        if (!tk_push(out, TK_OTHER, i, i + 1)) return 0;
        i++;
    }
    return 1;
}

/* ---- symbol table builder ------------------------------------------- */

static int symtab_reserve(lsp_symtab *st, size_t need) {
    if (st->cap >= need) return 1;
    size_t nc = st->cap ? st->cap * 2 : 32;
    while (nc < need) nc *= 2;
    lsp_symbol *ni = (lsp_symbol *)realloc(st->items, nc * sizeof(*ni));
    if (!ni) return 0;
    st->items = ni; st->cap = nc;
    return 1;
}

static lsp_symbol *symtab_find_mut(lsp_symtab *st, const char *name,
                                   size_t name_len) {
    for (size_t i = 0; i < st->count; i++) {
        if (strlen(st->items[i].name) == name_len &&
            memcmp(st->items[i].name, name, name_len) == 0) {
            return &st->items[i];
        }
    }
    return NULL;
}

const lsp_symbol *lsp_symtab_lookup(const lsp_symtab *st, const char *name,
                                    size_t name_len) {
    for (size_t i = 0; i < st->count; i++) {
        if (strlen(st->items[i].name) == name_len &&
            memcmp(st->items[i].name, name, name_len) == 0) {
            return &st->items[i];
        }
    }
    return NULL;
}

static lsp_symbol *symtab_get_or_add(lsp_symtab *st, const char *text,
                                     size_t name_start, size_t name_end) {
    size_t nlen = name_end - name_start;
    lsp_symbol *s = symtab_find_mut(st, text + name_start, nlen);
    if (s) return s;
    if (!symtab_reserve(st, st->count + 1)) return NULL;
    lsp_symbol *ns = &st->items[st->count++];
    memset(ns, 0, sizeof(*ns));
    ns->name = (char *)malloc(nlen + 1);
    if (!ns->name) { st->count--; return NULL; }
    memcpy(ns->name, text + name_start, nlen);
    ns->name[nlen] = 0;
    ns->kind = LSP_SYM_NONTERMINAL;  /* default; refined when seen */
    ns->def_offset = (size_t)-1;
    return ns;
}

static void record_definition(lsp_symbol *s, const char *text, size_t text_len,
                              size_t start, size_t end, lsp_sym_kind kind) {
    if (s->def_offset == (size_t)-1) {
        s->def_offset = start;
        s->def_end    = end;
        long long ln, col, ec_line, ec_col;
        lsp_offset_to_position(text, text_len, start, &ln, &col);
        lsp_offset_to_position(text, text_len, end,   &ec_line, &ec_col);
        s->def_line = ln;
        s->def_col  = col;
        s->def_end_col = ec_col;
    }
    s->kind = kind;
}

void lsp_symtab_build(lsp_symtab *st, const char *text, size_t text_len) {
    memset(st, 0, sizeof(*st));
    tk_list tk = {0};
    if (!tokenize(text, text_len, &tk)) {
        tk_free(&tk);
        return;
    }
    /* Walk tokens.  States:
     *   - TOP_LEVEL: looking for either a `%directive` or an
     *     `IDENT ::=` rule head.
     *   - IN_RHS: inside a rule's RHS.  Identifiers we see here
     *     are references; LHS aliases come in `(alias)` groups
     *     which we skip.
     *   - IN_TOKEN_DECL: inside `%token A B C.` -- following
     *     identifiers are terminal definitions.
     */
    enum { TOP, IN_RHS, IN_TOKEN_DECL } state = TOP;
    int after_directive = 0; /* names following %name/%left/%right/etc */
    enum { DIR_OTHER, DIR_TOKEN, DIR_NAMELIST } dir_kind = DIR_OTHER;

    for (size_t i = 0; i < tk.count; i++) {
        tk_t *t = &tk.items[i];

        if (state == TOP) {
            if (t->kind == TK_DIRECTIVE) {
                /* Record the directive itself.  Keys are the
                 * literal text including the '%' so hover can
                 * dispatch on them. */
                lsp_symbol *d = symtab_get_or_add(st, text, t->start, t->end);
                if (d) record_definition(d, text, text_len, t->start, t->end,
                                         LSP_SYM_DIRECTIVE);
                /* Decide what the directive's argument list means. */
                size_t dlen = t->end - t->start;
                const char *dn = text + t->start + 1; /* skip '%' */
                size_t      dnl = dlen - 1;
                dir_kind = DIR_OTHER;
                if ((dnl == 5 && memcmp(dn, "token", 5) == 0)) {
                    dir_kind = DIR_TOKEN;
                } else if ((dnl == 4 && memcmp(dn, "left", 4) == 0) ||
                           (dnl == 5 && memcmp(dn, "right", 5) == 0) ||
                           (dnl == 8 && memcmp(dn, "nonassoc", 8) == 0) ||
                           (dnl == 4 && memcmp(dn, "type", 4) == 0) ||
                           (dnl == 8 && memcmp(dn, "fallback", 8) == 0) ||
                           (dnl == 8 && memcmp(dn, "wildcard", 8) == 0) ||
                           (dnl == 11 && memcmp(dn, "token_class", 11) == 0)) {
                    dir_kind = DIR_NAMELIST;
                }
                if (dir_kind == DIR_TOKEN)
                    state = IN_TOKEN_DECL;
                else
                    after_directive = 1;
                continue;
            }
            if (t->kind == TK_IDENT) {
                /* Possible LHS of a rule.  Look ahead past
                 * optional `(alias)` for `::=`.
                 */
                size_t lhs_start = t->start;
                size_t lhs_end   = t->end;
                size_t j = i + 1;
                if (j < tk.count && tk.items[j].kind == TK_LPAREN) {
                    j++;
                    if (j < tk.count && tk.items[j].kind == TK_IDENT) j++;
                    if (j < tk.count && tk.items[j].kind == TK_RPAREN) j++;
                }
                if (j < tk.count && tk.items[j].kind == TK_ARROW) {
                    lsp_symbol *s = symtab_get_or_add(st, text, lhs_start,
                                                       lhs_end);
                    if (s) record_definition(s, text, text_len,
                                             lhs_start, lhs_end,
                                             LSP_SYM_NONTERMINAL);
                    i = j;        /* skip past `::=` */
                    state = IN_RHS;
                    continue;
                }
                if (after_directive) {
                    /* %left FOO BAR ... -- treat names as refs */
                    lsp_symbol *s = symtab_get_or_add(st, text, lhs_start,
                                                      lhs_end);
                    if (s) s->uses++;
                    continue;
                }
                /* Bare identifier with neither a directive prefix
                 * nor a `::=` follower.  Likely the start of a
                 * rule the user is mid-typing -- count as a use
                 * so symbol-walk code is robust.
                 */
                lsp_symbol *s = symtab_get_or_add(st, text, lhs_start,
                                                  lhs_end);
                if (s) s->uses++;
                continue;
            }
            if (t->kind == TK_DOT) {
                after_directive = 0;
                dir_kind = DIR_OTHER;
                continue;
            }
            continue;
        }

        if (state == IN_TOKEN_DECL) {
            if (t->kind == TK_DOT) {
                state = TOP;
                dir_kind = DIR_OTHER;
                continue;
            }
            if (t->kind == TK_IDENT) {
                lsp_symbol *s = symtab_get_or_add(st, text, t->start, t->end);
                if (s) record_definition(s, text, text_len,
                                         t->start, t->end, LSP_SYM_TERMINAL);
                continue;
            }
            /* %token TOK(typecast) -- skip optional `(...)` */
            if (t->kind == TK_LPAREN) {
                while (i + 1 < tk.count && tk.items[i+1].kind != TK_RPAREN) i++;
                if (i + 1 < tk.count) i++;
                continue;
            }
            continue;
        }

        if (state == IN_RHS) {
            if (t->kind == TK_DOT) {
                state = TOP;
                continue;
            }
            if (t->kind == TK_LBRACE) {
                /* action body skipped wholesale */
                continue;
            }
            if (t->kind == TK_LPAREN) {
                /* (alias) -- skip until ) */
                while (i + 1 < tk.count && tk.items[i+1].kind != TK_RPAREN) i++;
                if (i + 1 < tk.count) i++;
                continue;
            }
            if (t->kind == TK_LBRACK) {
                /* [PRECEDENCE] -- the inner identifier counts as
                 * a reference */
                if (i + 1 < tk.count && tk.items[i+1].kind == TK_IDENT) {
                    tk_t *id = &tk.items[i+1];
                    lsp_symbol *s = symtab_get_or_add(st, text,
                                                      id->start, id->end);
                    if (s) s->uses++;
                    i++;
                }
                while (i + 1 < tk.count && tk.items[i+1].kind != TK_RBRACK) i++;
                if (i + 1 < tk.count) i++;
                continue;
            }
            if (t->kind == TK_IDENT) {
                lsp_symbol *s = symtab_get_or_add(st, text, t->start, t->end);
                if (s) s->uses++;
                continue;
            }
            continue;
        }
    }
    tk_free(&tk);
}

void lsp_symtab_free(lsp_symtab *st) {
    if (!st) return;
    for (size_t i = 0; i < st->count; i++) free(st->items[i].name);
    free(st->items);
    memset(st, 0, sizeof(*st));
}

/* ---- word-at-cursor ------------------------------------------------- */

void lsp_word_at_offset(const char *text, size_t text_len, size_t offset,
                        size_t *out_start, size_t *out_end) {
    if (offset > text_len) offset = text_len;
    /* Allow cursor to sit just past the last char of an
     * identifier; LSP gives positions where a click landed and
     * the user might be at end-of-token. */
    size_t s = offset;
    while (s > 0 && (is_id_cont((unsigned char)text[s - 1]) ||
                     (s >= 1 && text[s - 1] == '%' && (s == 1 ||
                       !is_id_cont((unsigned char)text[s - 2]))))) {
        s--;
    }
    /* If we walked back past a '%' that wasn't actually attached
     * to this identifier (e.g. cursor on bare identifier), drop
     * the '%' if the next char isn't an id-start. */
    if (s < text_len && text[s] == '%' &&
        (s + 1 >= text_len || !is_id_start((unsigned char)text[s + 1]))) {
        s++;
    }
    size_t e = offset;
    while (e < text_len && is_id_cont((unsigned char)text[e])) e++;
    if (e == s) {
        /* maybe cursor is on '%' itself -- expand forward only */
        if (s < text_len && text[s] == '%') {
            e = s + 1;
            while (e < text_len && is_id_cont((unsigned char)text[e])) e++;
        }
    }
    *out_start = s;
    *out_end   = e;
}

/* ---- definition / hover / documentSymbol ---------------------------- */

static json_value *make_range(long long sl, long long sc,
                              long long el, long long ec) {
    json_value *r = json_make_object();
    json_value *s = json_make_object();
    json_object_set(s, "line",      json_make_int(sl));
    json_object_set(s, "character", json_make_int(sc));
    json_value *e = json_make_object();
    json_object_set(e, "line",      json_make_int(el));
    json_object_set(e, "character", json_make_int(ec));
    json_object_set(r, "start", s);
    json_object_set(r, "end",   e);
    return r;
}

json_value *lsp_navigation_definition(const char *uri,
                                      const char *text, size_t text_len,
                                      long long line, long long character) {
    size_t off = lsp_position_to_offset(text, text_len, line, character);
    size_t s, e;
    lsp_word_at_offset(text, text_len, off, &s, &e);
    if (s == e) return json_make_null();

    /* Skip a leading '%' for directives -- definition lookups
     * only resolve grammar symbols. */
    size_t name_start = s;
    if (text[name_start] == '%') return json_make_null();

    lsp_symtab st;
    lsp_symtab_build(&st, text, text_len);
    const lsp_symbol *sym = lsp_symtab_lookup(&st, text + name_start,
                                              e - name_start);
    if (!sym || sym->def_offset == (size_t)-1) {
        lsp_symtab_free(&st);
        return json_make_null();
    }
    json_value *loc = json_make_object();
    json_object_set(loc, "uri", json_make_string(uri));
    json_object_set(loc, "range",
                    make_range(sym->def_line, sym->def_col,
                               sym->def_line, sym->def_end_col));
    lsp_symtab_free(&st);
    return loc;
}

/* Brief docs for each %-directive.  Strings live in static
 * storage; they end up serialized verbatim into the hover
 * response. */
static const struct {
    const char *name;
    const char *doc;
} kDirectiveDocs[] = {
    {"name",        "`%name <prefix>` -- prefix every generated symbol with <prefix>."},
    {"token",       "`%token <NAME> ...<NAME>.` -- declare terminal tokens."},
    {"token_type",  "`%token_type {C-type}` -- C type carried by every terminal."},
    {"token_prefix","`%token_prefix <PFX>` -- prepend <PFX> to every emitted token enum."},
    {"type",        "`%type <NAME> {C-type}` -- semantic type for a non-terminal."},
    {"left",        "`%left <T> ...` -- declare left-associative operators."},
    {"right",       "`%right <T> ...` -- declare right-associative operators."},
    {"nonassoc",    "`%nonassoc <T> ...` -- declare non-associative operators."},
    {"start_symbol","`%start_symbol <N>` -- override the default start non-terminal."},
    {"include",     "`%include {C-code}` -- inject C into the generated header."},
    {"code",        "`%code {C-code}` -- inject C into the generated source."},
    {"extra_argument","`%extra_argument {T name}` -- thread an extra parameter through Parse()."},
    {"extra_context","`%extra_context {T name}` -- thread a per-parse context object."},
    {"syntax_error","`%syntax_error {C-code}` -- code run when a syntax error is hit."},
    {"parse_failure","`%parse_failure {C-code}` -- code run when the parser gives up."},
    {"parse_accept","`%parse_accept {C-code}` -- code run on successful accept."},
    {"stack_overflow","`%stack_overflow {C-code}` -- code run when the stack grows beyond limit."},
    {"stack_size",  "`%stack_size <N>` -- initial parser stack capacity."},
    {"stack_size_limit","`%stack_size_limit <N>` -- maximum parser stack capacity."},
    {"default_type","`%default_type {C-type}` -- type for non-terminals lacking %type."},
    {"default_destructor","`%default_destructor {C-code}` -- destructor for un-typed values."},
    {"destructor",  "`%destructor <NAME> {C-code}` -- destructor for one symbol."},
    {"token_destructor","`%token_destructor {C-code}` -- destructor for all terminals."},
    {"fallback",    "`%fallback <FALLBACK> <T> ...` -- token fallback chain."},
    {"wildcard",    "`%wildcard <T>.` -- token that matches any single token."},
    {"expect",      "`%expect <N>.` -- expected unresolved-conflict count."},
    {"first_token", "`%first_token <T>.` -- pseudo-token signalling start-of-input."},
    {"locations",   "`%locations.` -- enable LSP-style YYLTYPE tracking."},
    {"location_type","`%location_type {C-type}` -- struct used for source ranges."},
    {"module_name", "`%module_name <NAME>` -- module identifier (snapshot/extension)."},
    {"module_version","`%module_version <semver>` -- semver of this grammar module."},
    {"module_description","`%module_description <STRING>` -- one-liner for tooling."},
    {"export",      "`%export <N>.` -- mark a non-terminal as part of the public surface."},
    {"import",      "`%import <module>.` -- pull symbols from another grammar module."},
    {"require",     "`%require <module> <semver>.` -- declare a module dependency."},
    {"from",        "`%from <module> import <N>.` -- selective import."},
    {"ifdef",       "`%ifdef <SYM>` -- conditional grammar block."},
    {"ifndef",      "`%ifndef <SYM>` -- conditional grammar block."},
    {"else",        "`%else` -- else arm of an %ifdef block."},
    {"endif",       "`%endif` -- close an %ifdef block."},
    {"error_sync",  "`%error_sync <SYM>.` -- synchronization token for error recovery."},
    {"ast_auto",    "`%ast_auto.` -- emit auto-generated AST node types."},
    {"ast_node",    "`%ast_node <N> { ... }` -- declare an AST node shape."},
    {"ast_list",    "`%ast_list <N> <ELT>.` -- declare an AST list node."},
    {"ast_prefix",  "`%ast_prefix <PFX>.` -- prefix for generated AST node names."},
    {"symbol_prefix","`%symbol_prefix <PFX>.` -- prefix added to every emitted symbol."},
    {"name_prefix", "`%name_prefix <PFX>.` -- legacy alias for %name."},
    {"realloc",     "`%realloc <fn>.` -- override realloc used inside the runtime."},
    {"free",        "`%free <fn>.` -- override free used inside the runtime."},
    {NULL, NULL}
};

static const char *directive_doc(const char *name, size_t name_len) {
    /* skip leading '%' if present */
    if (name_len && name[0] == '%') { name++; name_len--; }
    for (size_t i = 0; kDirectiveDocs[i].name; i++) {
        if (strlen(kDirectiveDocs[i].name) == name_len &&
            memcmp(kDirectiveDocs[i].name, name, name_len) == 0) {
            return kDirectiveDocs[i].doc;
        }
    }
    return NULL;
}

json_value *lsp_navigation_hover(const char *text, size_t text_len,
                                 long long line, long long character) {
    size_t off = lsp_position_to_offset(text, text_len, line, character);
    size_t s, e;
    lsp_word_at_offset(text, text_len, off, &s, &e);
    if (s == e) return json_make_null();

    long long start_line, start_col, end_line, end_col;
    lsp_offset_to_position(text, text_len, s, &start_line, &start_col);
    lsp_offset_to_position(text, text_len, e, &end_line,   &end_col);

    char message[1024];
    if (text[s] == '%') {
        const char *doc = directive_doc(text + s, e - s);
        if (doc) {
            snprintf(message, sizeof(message), "%s", doc);
        } else {
            snprintf(message, sizeof(message),
                     "Lime directive `%.*s` (no documentation entry).",
                     (int)(e - s), text + s);
        }
    } else {
        lsp_symtab st;
        lsp_symtab_build(&st, text, text_len);
        const lsp_symbol *sym = lsp_symtab_lookup(&st, text + s, e - s);
        if (!sym) {
            lsp_symtab_free(&st);
            return json_make_null();
        }
        const char *kind =
            sym->kind == LSP_SYM_TERMINAL    ? "terminal" :
            sym->kind == LSP_SYM_NONTERMINAL ? "non-terminal" :
                                                "directive";
        if (sym->def_offset != (size_t)-1) {
            snprintf(message, sizeof(message),
                     "**%s** (%s)\n\nDeclared at line %lld; %ld reference%s.",
                     sym->name, kind,
                     (long long)(sym->def_line + 1),
                     sym->uses,
                     sym->uses == 1 ? "" : "s");
        } else {
            snprintf(message, sizeof(message),
                     "**%s** (%s, no declaration found)\n\n%ld reference%s.",
                     sym->name, kind, sym->uses,
                     sym->uses == 1 ? "" : "s");
        }
        lsp_symtab_free(&st);
    }

    json_value *contents = json_make_object();
    json_object_set(contents, "kind",  json_make_string("markdown"));
    json_object_set(contents, "value", json_make_string(message));
    json_value *hov = json_make_object();
    json_object_set(hov, "contents", contents);
    json_object_set(hov, "range",
                    make_range(start_line, start_col, end_line, end_col));
    return hov;
}

/* SymbolKind values from the LSP spec. */
enum {
    LSP_KIND_FUNCTION = 12,   /* used for non-terminals */
    LSP_KIND_CONSTANT = 14,   /* used for terminals     */
    LSP_KIND_KEY      = 20    /* used for directives    */
};

json_value *lsp_navigation_document_symbol(const char *text, size_t text_len) {
    json_value *arr = json_make_array();
    if (!arr) return NULL;

    lsp_symtab st;
    lsp_symtab_build(&st, text, text_len);

    /* Emit directives first (in declaration order), then
     * terminals, then non-terminals.  Within each group we
     * preserve insertion order, which matches the order they
     * appear in the file. */
    for (int pass = 0; pass < 3; pass++) {
        lsp_sym_kind want = pass == 0 ? LSP_SYM_DIRECTIVE :
                            pass == 1 ? LSP_SYM_TERMINAL :
                                        LSP_SYM_NONTERMINAL;
        int kind = pass == 0 ? LSP_KIND_KEY :
                   pass == 1 ? LSP_KIND_CONSTANT : LSP_KIND_FUNCTION;
        for (size_t i = 0; i < st.count; i++) {
            const lsp_symbol *s = &st.items[i];
            if (s->kind != want) continue;
            if (s->def_offset == (size_t)-1) continue;
            json_value *sym = json_make_object();
            json_object_set(sym, "name", json_make_string(s->name));
            json_object_set(sym, "kind", json_make_int(kind));
            json_value *r = make_range(s->def_line, s->def_col,
                                       s->def_line, s->def_end_col);
            /* DocumentSymbol requires both `range` and
             * `selectionRange`; for our flat outline they coincide. */
            json_value *r2 = make_range(s->def_line, s->def_col,
                                        s->def_line, s->def_end_col);
            json_object_set(sym, "range",          r);
            json_object_set(sym, "selectionRange", r2);
            json_array_push(arr, sym);
        }
    }
    lsp_symtab_free(&st);
    return arr;
}
