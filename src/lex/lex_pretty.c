/*
** src/lex/lex_pretty.c -- pretty-printer for LimeLexSpec ASTs.
**
** Emits a canonical .lex-syntax representation.  Idempotent:
** running the output back through the parser produces an
** equivalent AST whose pretty-print is byte-identical.
**
** Layout choices:
**   - Each top-level directive on its own line.
**   - Code blocks emitted with a leading space inside the
**     braces, body verbatim, trailing space inside.
**   - Pattern names emit before regex; regex re-escapes any
**     embedded `/` as `\/`.
**   - String literals re-quote with C-style escapes for the
**     non-printable / quote / backslash subset.
**   - Block-form rule shorthand is NOT preserved -- the M1.4
**     parser desugars blocks into per-rule qualifiers; pretty-
**     printer matches that.
*/

#include "lex_pretty.h"
#include "lex_ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
** Dynamic string builder
** ============================================================ */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} Buf;

static int buf_grow(Buf *b, size_t need) {
    if (b->cap >= need) return 1;
    size_t newcap = b->cap ? b->cap : 256;
    while (newcap < need) newcap *= 2;
    char *nb = realloc(b->buf, newcap);
    if (!nb) return 0;
    b->buf = nb;
    b->cap = newcap;
    return 1;
}

static void buf_append(Buf *b, const char *s, size_t n) {
    if (!buf_grow(b, b->len + n + 1)) return;
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void buf_cstr(Buf *b, const char *s) {
    if (!s) return;
    buf_append(b, s, strlen(s));
}

static void buf_byte(Buf *b, char c) { buf_append(b, &c, 1); }

/* ============================================================
** Escape helpers
** ============================================================ */

/* Emit `s` as a regex literal: surround with /.../, and escape
** any embedded `/` as `\/`.  Backslash-escapes already in `s`
** pass through verbatim so the source `\.` stays `\.`. */
static void emit_regex(Buf *b, const char *s) {
    buf_byte(b, '/');
    if (s) {
        for (const char *p = s; *p; p++) {
            if (*p == '\\' && p[1]) {
                buf_byte(b, '\\');
                buf_byte(b, p[1]);
                p++;
                continue;
            }
            if (*p == '/') {
                buf_cstr(b, "\\/");
                continue;
            }
            buf_byte(b, *p);
        }
    }
    buf_byte(b, '/');
}

/* Emit `s` as a C-style string literal: surround with "..." and
** escape \n, \r, \t, \\, \". */
static void emit_string(Buf *b, const char *s) {
    buf_byte(b, '"');
    if (s) {
        for (const char *p = s; *p; p++) {
            switch (*p) {
                case '\n': buf_cstr(b, "\\n"); break;
                case '\r': buf_cstr(b, "\\r"); break;
                case '\t': buf_cstr(b, "\\t"); break;
                case '\\': buf_cstr(b, "\\\\"); break;
                case '"':  buf_cstr(b, "\\\""); break;
                default:
                    buf_byte(b, *p);
                    break;
            }
        }
    }
    buf_byte(b, '"');
}

/* Emit a code block: `{ body }`.  Body is C source verbatim;
** newlines inside are preserved.  Adds a single space inside
** the braces for readability when body is short and on one
** line. */
static void emit_code_block(Buf *b, const char *body) {
    buf_byte(b, '{');
    if (body && *body) {
        if (body[0] != ' ' && body[0] != '\n' && body[0] != '\t') {
            buf_byte(b, ' ');
        }
        buf_cstr(b, body);
        size_t bl = strlen(body);
        if (bl > 0) {
            char last = body[bl - 1];
            if (last != ' ' && last != '\n' && last != '\t') {
                buf_byte(b, ' ');
            }
        }
    }
    buf_byte(b, '}');
}

/* Emit a state-list `<a, b, c>` (or nothing if empty). */
static void emit_state_list(Buf *b, char **states, int n) {
    if (n == 0) return;
    buf_byte(b, '<');
    for (int i = 0; i < n; i++) {
        if (i > 0) buf_cstr(b, ", ");
        buf_cstr(b, states[i]);
    }
    buf_cstr(b, "> ");
}

/* ============================================================
** Per-section emitters
** ============================================================ */

static void emit_directive_string(Buf *b, const char *what,
                                  const char *value) {
    if (!value) return;
    buf_cstr(b, what);
    buf_byte(b, ' ');
    buf_cstr(b, value);
    buf_cstr(b, ".\n");
}

static void emit_directive_block(Buf *b, const char *what,
                                 const char *body) {
    if (!body) return;
    buf_cstr(b, what);
    buf_byte(b, ' ');
    emit_code_block(b, body);
    buf_byte(b, '\n');
}

static void emit_patterns(Buf *b, const LimeLexPattern *p) {
    while (p) {
        buf_cstr(b, "%pattern ");
        buf_cstr(b, p->name);
        buf_byte(b, ' ');
        emit_regex(b, p->regex);
        buf_cstr(b, ".\n");
        p = p->next;
    }
}

static void emit_states(Buf *b, const LimeLexState *s) {
    while (s) {
        buf_cstr(b, s->exclusive ? "%exclusive_state " : "%state ");
        buf_cstr(b, s->name);
        if (s->local_body) {
            buf_byte(b, ' ');
            emit_code_block(b, s->local_body);
        }
        buf_cstr(b, ".\n");
        if (s->destructor) {
            buf_cstr(b, "%state_destructor ");
            buf_cstr(b, s->name);
            buf_byte(b, ' ');
            emit_code_block(b, s->destructor);
            buf_cstr(b, ".\n");
        }
        s = s->next;
    }
}

static void emit_keyword_tables(Buf *b, const LimeLexKeywordTable *k) {
    while (k) {
        buf_cstr(b, "%keyword_table ");
        buf_cstr(b, k->name);
        /* Always emit options block so case-sensitivity and
        ** prefix are explicit. */
        buf_cstr(b, " (");
        buf_cstr(b, k->case_insensitive ?
                    "case_insensitive" : "case_sensitive");
        if (k->prefix) {
            buf_cstr(b, ", prefix=");
            buf_cstr(b, k->prefix);
        }
        buf_cstr(b, ") {\n");
        for (int i = 0; i < k->n_keywords; i++) {
            buf_cstr(b, "    ");
            emit_string(b, k->keywords[i]);
            if (i + 1 < k->n_keywords) buf_byte(b, ',');
            buf_byte(b, '\n');
        }
        buf_cstr(b, "}.\n");
        k = k->next;
    }
}

static void emit_literal_buffers(Buf *b, const LimeLexLiteralBuffer *lb) {
    while (lb) {
        buf_cstr(b, "%literal_buffer ");
        buf_cstr(b, lb->name);
        buf_cstr(b, " {\n");
        if (lb->element_type) {
            buf_cstr(b, "    type    ");
            buf_cstr(b, lb->element_type);
            buf_byte(b, '\n');
        }
        if (lb->initial_capacity > 0) {
            char num[32];
            snprintf(num, sizeof(num), "%d", lb->initial_capacity);
            buf_cstr(b, "    initial ");
            buf_cstr(b, num);
            buf_byte(b, '\n');
        }
        if (lb->grow_policy) {
            buf_cstr(b, "    grow    ");
            emit_string(b, lb->grow_policy);
            buf_byte(b, '\n');
        }
        if (lb->alloc_fn) {
            buf_cstr(b, "    alloc   ");
            buf_cstr(b, lb->alloc_fn);
            buf_byte(b, '\n');
        }
        if (lb->realloc_fn) {
            buf_cstr(b, "    realloc ");
            buf_cstr(b, lb->realloc_fn);
            buf_byte(b, '\n');
        }
        if (lb->free_fn) {
            buf_cstr(b, "    free    ");
            buf_cstr(b, lb->free_fn);
            buf_byte(b, '\n');
        }
        buf_cstr(b, "}.\n");
        lb = lb->next;
    }
}

static void emit_rule(Buf *b, const LimeLexRule *r) {
    emit_state_list(b, r->states, r->n_states);
    buf_cstr(b, "rule ");
    buf_cstr(b, r->name);
    buf_cstr(b, " matches ");
    if (r->is_eof) {
        buf_cstr(b, "<<EOF>>");
    } else if (r->pattern) {
        emit_regex(b, r->pattern);
    } else {
        /* Defensive: missing pattern. */
        emit_regex(b, "");
    }
    buf_byte(b, ' ');
    if (r->action) {
        emit_code_block(b, r->action);
    } else {
        buf_cstr(b, "{ }");
    }
    buf_byte(b, '\n');
}

static void emit_rule_list(Buf *b, const LimeLexRule *r) {
    while (r) {
        emit_rule(b, r);
        r = r->next;
    }
}

static void emit_rulesets(Buf *b, const LimeLexRuleset *rs) {
    while (rs) {
        buf_cstr(b, "%ruleset ");
        buf_cstr(b, rs->name);
        buf_cstr(b, " {\n");
        const LimeLexRule *r = rs->rules;
        while (r) {
            buf_cstr(b, "    ");
            emit_rule(b, r);
            r = r->next;
        }
        buf_cstr(b, "}.\n");
        rs = rs->next;
    }
}

static void emit_lexer_includes(Buf *b, char **arr, int n) {
    if (n <= 0) return;
    buf_cstr(b, "%lexer_include ");
    for (int i = 0; i < n; i++) {
        if (i > 0) buf_cstr(b, ", ");
        buf_cstr(b, arr[i]);
    }
    buf_cstr(b, ".\n");
}

/* ============================================================
** Public entry point
** ============================================================ */

char *lime_lex_spec_to_text(const LimeLexSpec *spec) {
    if (!spec) return NULL;
    Buf b = {0};

    /* Header directives in canonical order. */
    emit_directive_string(&b, "%name_prefix",  spec->name_prefix);
    emit_directive_string(&b, "%token_prefix", spec->token_prefix);
    emit_directive_block (&b, "%token_type",   spec->token_type);
    emit_directive_block (&b, "%location_type", spec->location_type);
    emit_directive_block (&b, "%lexer_extra_argument", spec->extra_argument);
    emit_directive_block (&b, "%include",      spec->include_block);

    emit_patterns(&b, spec->patterns);
    emit_states(&b, spec->states);
    emit_keyword_tables(&b, spec->keyword_tables);
    emit_literal_buffers(&b, spec->literal_buffers);
    emit_rulesets(&b, spec->rulesets);
    emit_lexer_includes(&b, spec->lexer_includes, spec->n_lexer_includes);
    emit_rule_list(&b, spec->rules);

    if (!b.buf) {
        b.buf = malloc(1);
        if (b.buf) b.buf[0] = '\0';
    }
    return b.buf;
}
