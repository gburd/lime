/*
** src/lex/lex_parse.c -- recursive-descent parser for .lex files.
**
** Consumes tokens from lex_tokenize and builds a LimeLexSpec
** AST.  Single-token lookahead.  Sub-tokenizers used for the
** three directives whose `{...}` content is structured rather
** than C code (keyword_table, literal_buffer, ruleset).
**
** Error handling: parse errors emit a `filename:line: msg`
** diagnostic to stderr and increment spec->error_count.  The
** parser tries to recover by skipping to the next directive
** start (a token that begins with '%' or LANGLE or KW_RULE);
** subsequent directives are still parsed.
*/

#include "lex_parse.h"
#include "lex_tokenize.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
** Parser state
** ============================================================ */

typedef struct {
    LimeLexTokenizer *tok;
    LimeLexSpec      *spec;
    LimeLexToken      cur;          /* current lookahead */
    int               had_cur;      /* 0 until first pull */
    const char       *filename;
} Parser;

static void parser_init(Parser *p, LimeLexTokenizer *t,
                        LimeLexSpec *spec, const char *filename) {
    p->tok      = t;
    p->spec     = spec;
    p->had_cur  = 0;
    p->filename = filename;
    memset(&p->cur, 0, sizeof(p->cur));
}

/* ----- diagnostics ----- */

static void verror_at(Parser *p, int line, const char *fmt, va_list ap) {
    fprintf(stderr, "%s:%d: ", p->filename ? p->filename : "<input>", line);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    p->spec->error_count++;
}

static void error_at(Parser *p, int line, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(p, line, fmt, ap);
    va_end(ap);
}

static void error_here(Parser *p, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(p, p->cur.line, fmt, ap);
    va_end(ap);
}

/* ----- token stream helpers ----- */

static void pull(Parser *p) {
    lime_lex_tokenize_next(p->tok, &p->cur);
    p->had_cur = 1;
}

static int at(Parser *p, LimeLexTokKind k) {
    if (!p->had_cur) pull(p);
    return p->cur.kind == k;
}

static int consume(Parser *p, LimeLexTokKind k) {
    if (at(p, k)) { pull(p); return 1; }
    return 0;
}

static int expect(Parser *p, LimeLexTokKind k, const char *what) {
    if (consume(p, k)) return 1;
    error_here(p, "expected %s, got %s",
               what, lime_lex_tok_kind_name(p->cur.kind));
    return 0;
}

/* Skip ahead until a token that plausibly starts a top-level
** declaration: a directive, '<' (state-qualified rule), the
** `rule` keyword, or EOF.  Used for error recovery so a single
** parse pass surfaces multiple errors. */
static void synchronise(Parser *p) {
    while (!at(p, LIME_LEX_TOK_EOF)) {
        LimeLexTokKind k = p->cur.kind;
        if (k >= LIME_LEX_TOK_DIR_NAME_PREFIX &&
            k <= LIME_LEX_TOK_DIR_UNKNOWN) return;
        if (k == LIME_LEX_TOK_LANGLE) return;
        if (k == LIME_LEX_TOK_KW_RULE) return;
        pull(p);
    }
}

/* ----- string helpers ----- */

static char *strndup_lexeme(const LimeLexToken *t) {
    char *s = malloc(t->length + 1);
    if (!s) return NULL;
    memcpy(s, t->lexeme, t->length);
    s[t->length] = '\0';
    return s;
}

/* For a STRING token, strip the surrounding quotes and apply C
** escape sequences (\n, \t, \\, \", \r, \0, \xNN).  Returns a
** freshly malloc'd string. */
static char *unquote_string(const LimeLexToken *t) {
    if (t->length < 2) return strndup_lexeme(t);
    char *out = malloc(t->length);   /* result <= input length minus 2 quotes */
    if (!out) return NULL;
    char *o = out;
    const char *p = t->lexeme + 1;            /* skip opening " */
    const char *end = t->lexeme + t->length - 1; /* skip closing " */
    while (p < end) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            switch (*p) {
                case 'n':  *o++ = '\n'; break;
                case 't':  *o++ = '\t'; break;
                case 'r':  *o++ = '\r'; break;
                case '\\': *o++ = '\\'; break;
                case '"':  *o++ = '"';  break;
                case '\'': *o++ = '\''; break;
                case '0':  *o++ = '\0'; break;
                default:   *o++ = *p;   break;   /* unknown escape: pass through */
            }
            p++;
        } else {
            *o++ = *p++;
        }
    }
    *o = '\0';
    return out;
}

/* For a REGEX token of the form "/.../", strip the outer slashes
** and unescape '\/' to '/'.  Returns the inner regex source. */
static char *unwrap_regex(const LimeLexToken *t) {
    if (t->length < 2) return strndup_lexeme(t);
    char *out = malloc(t->length);
    if (!out) return NULL;
    char *o = out;
    const char *p = t->lexeme + 1;
    const char *end = t->lexeme + t->length - 1;
    while (p < end) {
        if (*p == '\\' && p + 1 < end && p[1] == '/') {
            *o++ = '/';
            p += 2;
        } else {
            *o++ = *p++;
        }
    }
    *o = '\0';
    return out;
}

/* For a CODE_BLOCK token of the form "{...}" (with balanced
** braces guaranteed by the tokenizer), strip the outer braces
** and return the inner C-code text verbatim.  Returns "" if the
** block is empty or malformed. */
static char *unwrap_code_block(const LimeLexToken *t) {
    if (t->length < 2) {
        char *s = malloc(1);
        if (s) s[0] = '\0';
        return s;
    }
    size_t inner_len = t->length - 2;
    char *out = malloc(inner_len + 1);
    if (!out) return NULL;
    memcpy(out, t->lexeme + 1, inner_len);
    out[inner_len] = '\0';
    return out;
}

/* ============================================================
** Sub-tokenizer over a CODE_BLOCK's inner bytes.
**
** Used for the three directives whose `{...}` content is
** structured rather than C code (keyword_table, literal_buffer,
** ruleset).  Returns a fresh tokenizer; caller frees.
** ============================================================ */

static LimeLexTokenizer *open_inner(const LimeLexToken *block,
                                    const char *filename) {
    if (block->length < 2) return NULL;
    return lime_lex_tokenize_init(filename,
                                  block->lexeme + 1,
                                  block->length - 2);
}

/* ============================================================
** Per-directive parsers
** ============================================================ */

/* %name_prefix IDENT.   /   %name_prefix STRING.
** %token_prefix IDENT.  /   %token_prefix STRING. */
static void parse_string_or_ident_directive(Parser *p, char **slot,
                                            const char *what) {
    pull(p);   /* eat the directive token */
    char *value = NULL;
    if (at(p, LIME_LEX_TOK_IDENT)) {
        value = strndup_lexeme(&p->cur);
        pull(p);
    } else if (at(p, LIME_LEX_TOK_STRING)) {
        value = unquote_string(&p->cur);
        pull(p);
    } else {
        error_here(p, "expected identifier or string after %s", what);
        synchronise(p);
        return;
    }
    expect(p, LIME_LEX_TOK_DOT, "'.' to terminate directive");
    if (*slot) {
        free(value);
        error_here(p, "%s declared more than once", what);
        return;
    }
    *slot = value;
}

/* %token_type {C type}      /  %location_type {C type}
** %lexer_extra_argument {C type *name}  /  %include {C code} */
static void parse_code_block_directive(Parser *p, char **slot,
                                       const char *what,
                                       int needs_terminator) {
    pull(p);   /* eat the directive token */
    if (!at(p, LIME_LEX_TOK_CODE_BLOCK)) {
        error_here(p, "expected '{...}' after %s", what);
        synchronise(p);
        return;
    }
    char *body = unwrap_code_block(&p->cur);
    pull(p);
    if (needs_terminator) {
        expect(p, LIME_LEX_TOK_DOT, "'.' to terminate directive");
    }
    if (*slot) {
        if (what[1] == 'i') {  /* %include: append rather than replace */
            size_t a = strlen(*slot);
            size_t b = strlen(body);
            char *combined = malloc(a + b + 2);
            if (combined) {
                memcpy(combined, *slot, a);
                combined[a] = '\n';
                memcpy(combined + a + 1, body, b);
                combined[a + 1 + b] = '\0';
                free(*slot);
                free(body);
                *slot = combined;
            } else {
                free(body);
            }
            return;
        }
        free(body);
        error_here(p, "%s declared more than once", what);
        return;
    }
    *slot = body;
}

/* %pattern IDENT REGEX. */
static void parse_pattern(Parser *p) {
    int line = p->cur.line;
    pull(p);   /* eat %pattern */
    if (!at(p, LIME_LEX_TOK_IDENT)) {
        error_here(p, "expected pattern name after %%pattern");
        synchronise(p);
        return;
    }
    char *name = strndup_lexeme(&p->cur);
    pull(p);
    if (!at(p, LIME_LEX_TOK_REGEX)) {
        error_here(p, "expected /regex/ after %%pattern %s", name);
        free(name);
        synchronise(p);
        return;
    }
    char *regex = unwrap_regex(&p->cur);
    pull(p);
    expect(p, LIME_LEX_TOK_DOT, "'.' to terminate %pattern");

    LimeLexPattern *pat = calloc(1, sizeof(*pat));
    if (!pat) { free(name); free(regex); return; }
    pat->name  = name;
    pat->regex = regex;
    pat->line  = line;

    /* Append in declaration order. */
    LimeLexPattern **tail = &p->spec->patterns;
    while (*tail) tail = &(*tail)->next;
    *tail = pat;
}

/* %state IDENT.            (inclusive, no body)
** %state IDENT { body }.   (inclusive, typed local data)
** %exclusive_state IDENT.
** %exclusive_state IDENT { body }. */
static LimeLexState *find_or_create_state(LimeLexSpec *spec,
                                          const char *name) {
    LimeLexState *s = spec->states;
    while (s) {
        if (strcmp(s->name, name) == 0) return s;
        s = s->next;
    }
    LimeLexState *ns = calloc(1, sizeof(*ns));
    if (!ns) return NULL;
    ns->name = strdup(name);
    if (!ns->name) { free(ns); return NULL; }
    /* Append to keep declaration order. */
    LimeLexState **tail = &spec->states;
    while (*tail) tail = &(*tail)->next;
    *tail = ns;
    return ns;
}

static void parse_state(Parser *p, int exclusive) {
    int line = p->cur.line;
    pull(p);   /* eat %state or %exclusive_state */
    if (!at(p, LIME_LEX_TOK_IDENT)) {
        error_here(p, "expected state name");
        synchronise(p);
        return;
    }
    char *name_buf = strndup_lexeme(&p->cur);
    pull(p);

    char *body = NULL;
    if (at(p, LIME_LEX_TOK_CODE_BLOCK)) {
        body = unwrap_code_block(&p->cur);
        pull(p);
    }
    expect(p, LIME_LEX_TOK_DOT, "'.' to terminate state declaration");

    LimeLexState *st = find_or_create_state(p->spec, name_buf);
    free(name_buf);
    if (!st) return;
    if (st->line == 0) st->line = line;
    /* Set or merge fields.  Re-declaration is an error if it
    ** changes the exclusive flag or the local-data body, but
    ** just adding a destructor later is fine. */
    if (st->exclusive && !exclusive) {
        error_at(p, line, "state '%s' was previously declared exclusive",
                 st->name);
    }
    if (exclusive) st->exclusive = 1;
    if (body) {
        if (st->local_body) {
            error_at(p, line, "state '%s' already has local-data body",
                     st->name);
            free(body);
        } else {
            st->local_body = body;
        }
    }
}

/* %state_destructor IDENT { code }. */
static void parse_state_destructor(Parser *p) {
    int line = p->cur.line;
    pull(p);   /* eat %state_destructor */
    if (!at(p, LIME_LEX_TOK_IDENT)) {
        error_here(p, "expected state name after %%state_destructor");
        synchronise(p);
        return;
    }
    char *name_buf = strndup_lexeme(&p->cur);
    pull(p);
    if (!at(p, LIME_LEX_TOK_CODE_BLOCK)) {
        error_here(p, "expected '{...}' destructor body");
        free(name_buf);
        synchronise(p);
        return;
    }
    char *body = unwrap_code_block(&p->cur);
    pull(p);
    expect(p, LIME_LEX_TOK_DOT, "'.' to terminate %state_destructor");

    LimeLexState *st = find_or_create_state(p->spec, name_buf);
    free(name_buf);
    if (!st) { free(body); return; }
    if (st->line == 0) st->line = line;
    if (st->destructor) {
        error_at(p, line, "state '%s' already has a destructor", st->name);
        free(body);
        return;
    }
    st->destructor = body;
}

/* %keyword_table IDENT (opts) { strings }.
** Options are a comma-separated list of `case_insensitive`,
** `case_sensitive`, `prefix=IDENT`. */
static void parse_keyword_table(Parser *p) {
    int line = p->cur.line;
    pull(p);   /* eat %keyword_table */
    if (!at(p, LIME_LEX_TOK_IDENT)) {
        error_here(p, "expected keyword-table name");
        synchronise(p);
        return;
    }
    char *name = strndup_lexeme(&p->cur);
    pull(p);

    int case_insensitive = 0;
    char *prefix = NULL;
    if (consume(p, LIME_LEX_TOK_LPAREN)) {
        for (;;) {
            if (at(p, LIME_LEX_TOK_RPAREN)) break;
            if (!at(p, LIME_LEX_TOK_IDENT)) {
                error_here(p, "expected option name in keyword-table options");
                break;
            }
            const char *opt = p->cur.lexeme;
            size_t opt_len  = p->cur.length;
            if (opt_len == 16 && memcmp(opt, "case_insensitive", 16) == 0) {
                case_insensitive = 1;
                pull(p);
            } else if (opt_len == 14 &&
                       memcmp(opt, "case_sensitive", 14) == 0) {
                case_insensitive = 0;
                pull(p);
            } else if (opt_len == 6 && memcmp(opt, "prefix", 6) == 0) {
                pull(p);
                expect(p, LIME_LEX_TOK_EQUALS, "'=' after 'prefix'");
                if (at(p, LIME_LEX_TOK_IDENT)) {
                    if (prefix) free(prefix);
                    prefix = strndup_lexeme(&p->cur);
                    pull(p);
                } else {
                    error_here(p, "expected identifier after 'prefix='");
                }
            } else {
                error_here(p, "unknown keyword-table option");
                pull(p);
            }
            if (!consume(p, LIME_LEX_TOK_COMMA)) break;
        }
        expect(p, LIME_LEX_TOK_RPAREN, "')' to close options");
    }

    if (!at(p, LIME_LEX_TOK_CODE_BLOCK)) {
        error_here(p, "expected '{ \"keyword\", ... }' for %%keyword_table %s",
                   name);
        free(name); free(prefix);
        synchronise(p);
        return;
    }

    /* Sub-tokenizer over the keyword block's inner bytes. */
    LimeLexTokenizer *inner = open_inner(&p->cur, p->filename);
    pull(p);   /* eat the CODE_BLOCK */

    char **kws = NULL;
    int    n_kws = 0;
    int    cap = 0;
    if (inner) {
        for (;;) {
            LimeLexToken t;
            lime_lex_tokenize_next(inner, &t);
            if (t.kind == LIME_LEX_TOK_EOF) break;
            if (t.kind == LIME_LEX_TOK_STRING) {
                if (n_kws == cap) {
                    cap = cap ? cap * 2 : 16;
                    char **nk = realloc(kws, cap * sizeof(char *));
                    if (!nk) break;
                    kws = nk;
                }
                kws[n_kws++] = unquote_string(&t);
                continue;
            }
            if (t.kind == LIME_LEX_TOK_COMMA) continue;
            error_at(p, line,
                     "unexpected %s inside %%keyword_table %s",
                     lime_lex_tok_kind_name(t.kind), name);
            break;
        }
        lime_lex_tokenize_free(inner);
    }
    expect(p, LIME_LEX_TOK_DOT, "'.' to terminate %keyword_table");

    LimeLexKeywordTable *kt = calloc(1, sizeof(*kt));
    if (!kt) {
        free(name); free(prefix);
        for (int i = 0; i < n_kws; i++) free(kws[i]);
        free(kws);
        return;
    }
    kt->name             = name;
    kt->case_insensitive = case_insensitive;
    kt->prefix           = prefix;
    kt->keywords         = kws;
    kt->n_keywords       = n_kws;
    kt->line             = line;

    LimeLexKeywordTable **tail = &p->spec->keyword_tables;
    while (*tail) tail = &(*tail)->next;
    *tail = kt;
}

/* %literal_buffer IDENT { config }.
** Config is a sequence of "key value" pairs separated by
** whitespace.  Recognised keys: type, initial, grow, alloc,
** realloc, free.  Values are a single token (IDENT, INTEGER,
** STRING, or operator-prefixed integer for grow). */
static void parse_literal_buffer(Parser *p) {
    int line = p->cur.line;
    pull(p);   /* eat %literal_buffer */
    if (!at(p, LIME_LEX_TOK_IDENT)) {
        error_here(p, "expected buffer name");
        synchronise(p);
        return;
    }
    char *name = strndup_lexeme(&p->cur);
    pull(p);

    if (!at(p, LIME_LEX_TOK_CODE_BLOCK)) {
        error_here(p, "expected '{ ... }' config for %%literal_buffer %s",
                   name);
        free(name);
        synchronise(p);
        return;
    }

    LimeLexLiteralBuffer *lb = calloc(1, sizeof(*lb));
    if (!lb) { free(name); pull(p); return; }
    lb->name             = name;
    lb->element_type     = NULL;     /* default 'char' applied later */
    lb->initial_capacity = 64;
    lb->grow_policy      = NULL;
    lb->alloc_fn         = NULL;
    lb->realloc_fn       = NULL;
    lb->free_fn          = NULL;
    lb->line             = line;

    LimeLexTokenizer *inner = open_inner(&p->cur, p->filename);
    pull(p);   /* eat CODE_BLOCK */

    if (inner) {
        for (;;) {
            LimeLexToken kt;
            lime_lex_tokenize_next(inner, &kt);
            if (kt.kind == LIME_LEX_TOK_EOF) break;
            if (kt.kind != LIME_LEX_TOK_IDENT) {
                error_at(p, line,
                         "expected config key (type/initial/grow/alloc/...) "
                         "inside %%literal_buffer");
                break;
            }
            char *key = strndup_lexeme(&kt);

            LimeLexToken vt;
            lime_lex_tokenize_next(inner, &vt);
            char *val = NULL;
            if (vt.kind == LIME_LEX_TOK_IDENT) {
                val = strndup_lexeme(&vt);
            } else if (vt.kind == LIME_LEX_TOK_INTEGER) {
                val = strndup_lexeme(&vt);
            } else if (vt.kind == LIME_LEX_TOK_STRING) {
                val = unquote_string(&vt);
            } else {
                error_at(p, line,
                         "missing value for '%s' in %%literal_buffer", key);
                free(key);
                break;
            }

            if (strcmp(key, "type") == 0) {
                free(lb->element_type);
                lb->element_type = val;
            } else if (strcmp(key, "initial") == 0) {
                lb->initial_capacity = atoi(val);
                free(val);
            } else if (strcmp(key, "grow") == 0) {
                free(lb->grow_policy);
                lb->grow_policy = val;
            } else if (strcmp(key, "alloc") == 0) {
                free(lb->alloc_fn);
                lb->alloc_fn = val;
            } else if (strcmp(key, "realloc") == 0) {
                free(lb->realloc_fn);
                lb->realloc_fn = val;
            } else if (strcmp(key, "free") == 0) {
                free(lb->free_fn);
                lb->free_fn = val;
            } else {
                error_at(p, line, "unknown %%literal_buffer key '%s'", key);
                free(val);
            }
            free(key);
        }
        lime_lex_tokenize_free(inner);
    }
    expect(p, LIME_LEX_TOK_DOT, "'.' to terminate %literal_buffer");

    LimeLexLiteralBuffer **tail = &p->spec->literal_buffers;
    while (*tail) tail = &(*tail)->next;
    *tail = lb;
}

/* %lexer_include IDENT, IDENT, ... . */
static void parse_lexer_include(Parser *p) {
    pull(p);   /* eat %lexer_include */
    int cap = p->spec->n_lexer_includes;
    char **arr = p->spec->lexer_includes;
    int n = cap;
    for (;;) {
        if (!at(p, LIME_LEX_TOK_IDENT)) {
            error_here(p, "expected ruleset name in %%lexer_include");
            break;
        }
        if (n == cap) {
            int nc = cap ? cap * 2 : 8;
            char **na = realloc(arr, nc * sizeof(char *));
            if (!na) break;
            arr = na;
            cap = nc;
        }
        arr[n++] = strndup_lexeme(&p->cur);
        pull(p);
        if (!consume(p, LIME_LEX_TOK_COMMA)) break;
    }
    expect(p, LIME_LEX_TOK_DOT, "'.' to terminate %lexer_include");
    p->spec->lexer_includes  = arr;
    p->spec->n_lexer_includes = n;
}

/* Parse a state list `<NAME, NAME, ...>` (the LANGLE token is
** at the current cursor on entry).  Stores the result in *out;
** returns 1 on success, 0 on parse error (after emitting the
** diagnostic and synchronising).  When the input is a single
** rule with no state qualifier, the caller doesn't call this at
** all (states/n_states stay 0/0). */
static int parse_state_list(Parser *p, char ***out, int *out_n) {
    char **states = NULL;
    int n_states = 0;
    int cap = 0;
    /* LANGLE already at p->cur. */
    pull(p);
    for (;;) {
        if (!at(p, LIME_LEX_TOK_IDENT)) {
            error_here(p, "expected state name in '<...>' qualifier");
            synchronise(p);
            for (int i = 0; i < n_states; i++) free(states[i]);
            free(states);
            return 0;
        }
        if (n_states == cap) {
            cap = cap ? cap * 2 : 4;
            char **ns = realloc(states, cap * sizeof(char *));
            if (!ns) {
                for (int i = 0; i < n_states; i++) free(states[i]);
                free(states);
                return 0;
            }
            states = ns;
        }
        states[n_states++] = strndup_lexeme(&p->cur);
        pull(p);
        if (!consume(p, LIME_LEX_TOK_COMMA)) break;
    }
    if (!expect(p, LIME_LEX_TOK_RANGLE, "'>' to close state list")) {
        for (int i = 0; i < n_states; i++) free(states[i]);
        free(states);
        return 0;
    }
    *out = states;
    *out_n = n_states;
    return 1;
}

/* Duplicate a state-name array (for block desugaring -- each
** inner rule gets its own copy of the outer block's states). */
static char **dup_state_array(char **src, int n) {
    if (n == 0) return NULL;
    char **out = malloc(n * sizeof(char *));
    if (!out) return NULL;
    for (int i = 0; i < n; i++) {
        out[i] = src[i] ? strdup(src[i]) : NULL;
    }
    return out;
}

/* Parse one rule's body starting at the `rule` keyword.  States
** are passed in pre-parsed (block form pre-applies the outer
** block's states; single form passes its own state list).  On
** success, transfers ownership of `states` to the new rule.  On
** error, frees `states` and emits diagnostics. */
static void parse_rule_body(Parser *p, char **states, int n_states,
                            int line, LimeLexRule **list) {
    if (!expect(p, LIME_LEX_TOK_KW_RULE, "'rule' keyword")) {
        for (int i = 0; i < n_states; i++) free(states[i]);
        free(states);
        return;
    }

    if (!at(p, LIME_LEX_TOK_IDENT)) {
        error_here(p, "expected rule name");
        for (int i = 0; i < n_states; i++) free(states[i]);
        free(states);
        synchronise(p);
        return;
    }
    char *name = strndup_lexeme(&p->cur);
    pull(p);

    if (!expect(p, LIME_LEX_TOK_KW_MATCHES, "'matches' keyword")) {
        free(name);
        for (int i = 0; i < n_states; i++) free(states[i]);
        free(states);
        return;
    }

    char *pattern = NULL;
    int   is_eof  = 0;
    if (at(p, LIME_LEX_TOK_REGEX)) {
        pattern = unwrap_regex(&p->cur);
        pull(p);
    } else if (at(p, LIME_LEX_TOK_EOF_MARKER)) {
        is_eof = 1;
        pull(p);
    } else if (at(p, LIME_LEX_TOK_STRING)) {
        pattern = unquote_string(&p->cur);
        pull(p);
    } else {
        error_here(p, "expected /regex/, \"string\", or <<EOF>> after 'matches'");
        free(name);
        for (int i = 0; i < n_states; i++) free(states[i]);
        free(states);
        synchronise(p);
        return;
    }

    if (!at(p, LIME_LEX_TOK_CODE_BLOCK)) {
        error_here(p, "expected '{ action }' for rule '%s'", name);
        free(name); free(pattern);
        for (int i = 0; i < n_states; i++) free(states[i]);
        free(states);
        synchronise(p);
        return;
    }
    char *action = unwrap_code_block(&p->cur);
    pull(p);

    LimeLexRule *r = calloc(1, sizeof(*r));
    if (!r) {
        free(name); free(pattern); free(action);
        for (int i = 0; i < n_states; i++) free(states[i]);
        free(states);
        return;
    }
    r->name     = name;
    r->states   = states;
    r->n_states = n_states;
    r->is_eof   = is_eof;
    r->pattern  = pattern;
    r->action   = action;
    r->line     = line;

    LimeLexRule **tail = list;
    while (*tail) tail = &(*tail)->next;
    *tail = r;
}

/* Parse a rule or rule-block starting at the current token
** (LANGLE or KW_RULE).  Single-rule form:
**     [<STATES>] rule NAME matches /regex|<<EOF>>|"str"/ { action }
** Block form (M1.4, audit gap B):
**     <STATES> { (rule NAME matches ...)+ }
** In the block form, inner rules MUST NOT have their own state
** qualifier (it would be redundant); they inherit the outer
** block's states.  Inner rule violations report a diagnostic
** and skip the offending rule.  Both forms append all parsed
** rules to *list. */
static void parse_rule(Parser *p, LimeLexRule **list) {
    int line = p->cur.line;
    char **outer_states = NULL;
    int    n_outer = 0;

    if (at(p, LIME_LEX_TOK_LANGLE)) {
        if (!parse_state_list(p, &outer_states, &n_outer)) return;
    }

    /* Block form?  After <STATES> a CODE_BLOCK opens a list of
    ** rules that all inherit the outer state qualifier. */
    if (at(p, LIME_LEX_TOK_CODE_BLOCK) && n_outer > 0) {
        LimeLexTokenizer *inner = open_inner(&p->cur, p->filename);
        pull(p);   /* eat CODE_BLOCK */
        if (!inner) {
            for (int i = 0; i < n_outer; i++) free(outer_states[i]);
            free(outer_states);
            return;
        }

        Parser inner_p;
        parser_init(&inner_p, inner, p->spec, p->filename);
        pull(&inner_p);
        while (!at(&inner_p, LIME_LEX_TOK_EOF)) {
            if (at(&inner_p, LIME_LEX_TOK_LANGLE)) {
                error_at(p, inner_p.cur.line,
                         "inner rule must not declare its own state qualifier "
                         "(inherits the enclosing block's <...>)");
                /* Skip the qualifier and continue. */
                char **dummy = NULL; int dn = 0;
                if (parse_state_list(&inner_p, &dummy, &dn)) {
                    for (int i = 0; i < dn; i++) free(dummy[i]);
                    free(dummy);
                }
            }
            if (!at(&inner_p, LIME_LEX_TOK_KW_RULE)) {
                error_at(p, inner_p.cur.line,
                         "expected 'rule' inside <...>{...} block");
                break;
            }
            int rule_line = inner_p.cur.line;
            char **dup = dup_state_array(outer_states, n_outer);
            if (!dup && n_outer > 0) break;
            parse_rule_body(&inner_p, dup, n_outer, rule_line, list);
        }
        lime_lex_tokenize_free(inner);
        for (int i = 0; i < n_outer; i++) free(outer_states[i]);
        free(outer_states);
        return;
    }

    /* Single-rule form: parse_rule_body takes ownership of outer_states. */
    parse_rule_body(p, outer_states, n_outer, line, list);
}

/* %ruleset IDENT { rule ... }. */
static void parse_ruleset(Parser *p) {
    int line = p->cur.line;
    pull(p);   /* eat %ruleset */
    if (!at(p, LIME_LEX_TOK_IDENT)) {
        error_here(p, "expected ruleset name");
        synchronise(p);
        return;
    }
    char *name = strndup_lexeme(&p->cur);
    pull(p);

    if (!at(p, LIME_LEX_TOK_CODE_BLOCK)) {
        error_here(p, "expected '{ rule ... }' for %%ruleset %s", name);
        free(name);
        synchronise(p);
        return;
    }

    LimeLexTokenizer *inner = open_inner(&p->cur, p->filename);
    pull(p);   /* eat CODE_BLOCK */

    LimeLexRuleset *rs = calloc(1, sizeof(*rs));
    if (!rs) {
        free(name);
        if (inner) lime_lex_tokenize_free(inner);
        return;
    }
    rs->name = name;
    rs->line = line;

    if (inner) {
        Parser inner_p;
        parser_init(&inner_p, inner, p->spec, p->filename);
        pull(&inner_p);
        while (!at(&inner_p, LIME_LEX_TOK_EOF)) {
            if (!at(&inner_p, LIME_LEX_TOK_LANGLE) &&
                !at(&inner_p, LIME_LEX_TOK_KW_RULE)) {
                error_at(p, inner_p.cur.line,
                         "expected 'rule' or '<state>' in %%ruleset %s",
                         rs->name);
                break;
            }
            parse_rule(&inner_p, &rs->rules);
        }
        lime_lex_tokenize_free(inner);
    }
    expect(p, LIME_LEX_TOK_DOT, "'.' to terminate %ruleset");

    LimeLexRuleset **tail = &p->spec->rulesets;
    while (*tail) tail = &(*tail)->next;
    *tail = rs;
}

/* ============================================================
** Top-level dispatch
** ============================================================ */

static void parse_one_top_level(Parser *p) {
    switch (p->cur.kind) {
        case LIME_LEX_TOK_DIR_NAME_PREFIX:
            parse_string_or_ident_directive(p, &p->spec->name_prefix,
                                            "%name_prefix");
            break;
        case LIME_LEX_TOK_DIR_TOKEN_PREFIX:
            parse_string_or_ident_directive(p, &p->spec->token_prefix,
                                            "%token_prefix");
            break;
        case LIME_LEX_TOK_DIR_TOKEN_TYPE:
            parse_code_block_directive(p, &p->spec->token_type,
                                       "%token_type", 0);
            break;
        case LIME_LEX_TOK_DIR_LOCATION_TYPE:
            parse_code_block_directive(p, &p->spec->location_type,
                                       "%location_type", 0);
            break;
        case LIME_LEX_TOK_DIR_LEXER_EXTRA_ARGUMENT:
            parse_code_block_directive(p, &p->spec->extra_argument,
                                       "%lexer_extra_argument", 0);
            break;
        case LIME_LEX_TOK_DIR_INCLUDE:
            parse_code_block_directive(p, &p->spec->include_block,
                                       "%include", 0);
            break;
        case LIME_LEX_TOK_DIR_PATTERN:
            parse_pattern(p);
            break;
        case LIME_LEX_TOK_DIR_STATE:
            parse_state(p, /*exclusive=*/0);
            break;
        case LIME_LEX_TOK_DIR_EXCLUSIVE_STATE:
            parse_state(p, /*exclusive=*/1);
            break;
        case LIME_LEX_TOK_DIR_STATE_DESTRUCTOR:
            parse_state_destructor(p);
            break;
        case LIME_LEX_TOK_DIR_KEYWORD_TABLE:
            parse_keyword_table(p);
            break;
        case LIME_LEX_TOK_DIR_LITERAL_BUFFER:
            parse_literal_buffer(p);
            break;
        case LIME_LEX_TOK_DIR_RULESET:
            parse_ruleset(p);
            break;
        case LIME_LEX_TOK_DIR_LEXER_INCLUDE:
            parse_lexer_include(p);
            break;
        case LIME_LEX_TOK_DIR_UNKNOWN:
            error_here(p, "unknown directive '%.*s'",
                       (int)p->cur.length, p->cur.lexeme);
            pull(p);
            synchronise(p);
            break;
        case LIME_LEX_TOK_LANGLE:
        case LIME_LEX_TOK_KW_RULE:
            parse_rule(p, &p->spec->rules);
            break;
        default:
            error_here(p, "unexpected %s at top level",
                       lime_lex_tok_kind_name(p->cur.kind));
            pull(p);
            synchronise(p);
            break;
    }
}

/* ============================================================
** Public entry point
** ============================================================ */

LimeLexSpec *lime_lex_parse(const char *filename,
                            const char *source,
                            size_t source_len) {
    LimeLexSpec *spec = lime_lex_spec_new(filename);
    if (!spec) return NULL;

    LimeLexTokenizer *tok = lime_lex_tokenize_init(filename, source, source_len);
    if (!tok) {
        lime_lex_spec_free(spec);
        return NULL;
    }

    Parser p;
    parser_init(&p, tok, spec, filename);
    pull(&p);
    while (!at(&p, LIME_LEX_TOK_EOF)) {
        parse_one_top_level(&p);
    }

    spec->line_count = lime_lex_tokenize_line(tok);
    lime_lex_tokenize_free(tok);
    return spec;
}
