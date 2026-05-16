/*
** src/lex/lex_introspect.c -- compiled-lexer text serializer.
**
** Layout:
**     // lime_lex_compiled_to_text:
**     //   <N> compiled state(s), <M> compile error(s)
**     //
**     // state INITIAL (inclusive):
**     //   dfa: <S> states, <A> accept states, start=<I>
**     //   rules: <name>(idx=<G>), ...
**     //
**     // state EXPR (exclusive):
**     //   ...
**     //
**     <body emitted by lime_lex_spec_to_text>
**
** The body is verbatim .lex syntax and is what lets a tool
** parse this output back into an equivalent spec.  The stats
** header is purely informational and disappears on re-parse
** (the tokenizer drops `//` line comments).
*/

#include "lex_introspect.h"
#include "lex_ast.h"
#include "lex_compile.h"
#include "lex_dfa.h"
#include "lex_pretty.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
** Dynamic string builder (private; mirrors lex_pretty.c)
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

static int buf_append(Buf *b, const char *s, size_t n) {
    if (!buf_grow(b, b->len + n + 1)) return 0;
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
    return 1;
}

static int buf_cstr(Buf *b, const char *s) {
    return s ? buf_append(b, s, strlen(s)) : 1;
}

static int buf_printf(Buf *b, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if ((size_t)n < sizeof(tmp)) {
        return buf_append(b, tmp, (size_t)n);
    }
    /* Doesn't fit -- spill onto a heap buffer.  No use case in
    ** this serializer hits >256 chars, but be safe. */
    char *big = malloc((size_t)n + 1);
    if (!big) return 0;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    int ok = buf_append(b, big, (size_t)n);
    free(big);
    return ok;
}

/* ============================================================
** Stats header
** ============================================================ */

/* Look up the rule name for a global rule index by walking the
** spec in compile order.  This is O(n_rules) per call but the
** introspection path is cold, and we don't want to re-allocate
** the rule-name table that lex_emit.c builds. */
static const char *rule_name_for_index(const LimeLexSpec *spec,
                                        int target_index) {
    if (!spec) return NULL;
    int i = 0;
    for (const LimeLexRule *r = spec->rules; r; r = r->next) {
        if (i == target_index) return r->name;
        i++;
    }
    if (spec->n_lexer_includes == 0) {
        for (const LimeLexRuleset *rs = spec->rulesets; rs; rs = rs->next) {
            for (const LimeLexRule *r = rs->rules; r; r = r->next) {
                if (i == target_index) return r->name;
                i++;
            }
        }
        return NULL;
    }
    for (int k = 0; k < spec->n_lexer_includes; k++) {
        const char *name = spec->lexer_includes[k];
        const LimeLexRuleset *rs = NULL;
        for (const LimeLexRuleset *cand = spec->rulesets;
             cand; cand = cand->next) {
            if (strcmp(cand->name, name) == 0) { rs = cand; break; }
        }
        if (!rs) continue;
        for (const LimeLexRule *r = rs->rules; r; r = r->next) {
            if (i == target_index) return r->name;
            i++;
        }
    }
    return NULL;
}

/* Count how many DFA states are accepting. */
static int count_accept_states(const LimeDfa *dfa) {
    if (!dfa) return 0;
    int acc = 0;
    for (int i = 0; i < dfa->n_states; i++) {
        if (dfa->states[i].is_accept) acc++;
    }
    return acc;
}

static int emit_state_stats(Buf *b,
                            const LimeLexCompiledState *cs,
                            const LimeLexSpec *spec) {
    const char *kind = cs->exclusive ? "exclusive" : "inclusive";
    if (!buf_printf(b, "// state %s (%s):\n",
                    cs->state_name ? cs->state_name : "?", kind)) {
        return 0;
    }
    if (cs->dfa) {
        if (!buf_printf(b,
                "//   dfa: %d states, %d accept, start=%d\n",
                cs->dfa->n_states,
                count_accept_states(cs->dfa),
                cs->dfa->start)) {
            return 0;
        }
    } else {
        if (!buf_cstr(b, "//   dfa: <empty> (no applicable rules)\n")) {
            return 0;
        }
    }
    if (cs->n_rules == 0) {
        if (!buf_cstr(b, "//   rules: <none>\n")) return 0;
    } else {
        if (!buf_cstr(b, "//   rules:")) return 0;
        for (int i = 0; i < cs->n_rules; i++) {
            int gid = cs->rule_indices[i];
            const char *name = rule_name_for_index(spec, gid);
            if (!buf_printf(b, "%s%s(idx=%d)",
                            i == 0 ? " " : ", ",
                            name ? name : "?",
                            gid)) {
                return 0;
            }
        }
        if (!buf_cstr(b, "\n")) return 0;
    }
    return 1;
}

static int emit_stats_header(Buf *b,
                             const LimeLexCompiled *c,
                             const LimeLexSpec *spec) {
    if (!buf_cstr(b, "// lime_lex_compiled_to_text:\n")) return 0;
    if (!buf_printf(b,
            "//   %d compiled state(s), %d compile error(s)\n//\n",
            c->n_states, c->error_count)) {
        return 0;
    }
    for (int i = 0; i < c->n_states; i++) {
        if (!emit_state_stats(b, &c->states[i], spec)) return 0;
        if (i + 1 < c->n_states) {
            if (!buf_cstr(b, "//\n")) return 0;
        }
    }
    if (!buf_cstr(b, "\n")) return 0;
    return 1;
}

/* ============================================================
** Public entry point
** ============================================================ */

char *lime_lex_compiled_to_text(const LimeLexCompiled *c,
                                const LimeLexSpec *spec) {
    if (!c && !spec) return NULL;
    Buf b = {0};
    if (c) {
        if (!emit_stats_header(&b, c, spec)) {
            free(b.buf);
            return NULL;
        }
    }
    if (spec) {
        char *body = lime_lex_spec_to_text(spec);
        if (!body) { free(b.buf); return NULL; }
        int ok = buf_cstr(&b, body);
        free(body);
        if (!ok) { free(b.buf); return NULL; }
    }
    if (!b.buf) {
        b.buf = malloc(1);
        if (b.buf) b.buf[0] = '\0';
    }
    return b.buf;
}
