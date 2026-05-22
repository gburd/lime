/*
** src/lex/lex_resolve.c -- pattern fragment resolution.
**
** Two-phase algorithm:
**
**   Phase 1: For each %pattern declaration, recursively expand
**            its regex source by substituting {name} references.
**            Memoize via expanded_regex.  Detect cycles via the
**            transient _resolve_visit flag.
**
**   Phase 2: Walk all rules (top-level and inside rulesets) and
**            apply the same substitution to each rule's pattern.
**
** Pattern lookup is O(N) per reference (linear scan of the
** patterns list).  For practical .lex files this is fine -- the
** largest PG scanner has ~30 named patterns, so a 30-entry scan
** per substitution.  A future optimization could build a hash
** table.
*/

#include "lex_resolve.h"
#include "lex_ast.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
** Dynamic string builder
** ============================================================ */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} Buf;

static void buf_init(Buf *b) {
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}

static int buf_grow(Buf *b, size_t need) {
    if (b->cap >= need) return 1;
    size_t newcap = b->cap ? b->cap : 32;
    while (newcap < need)
        newcap *= 2;
    char *nb = realloc(b->buf, newcap);
    if (!nb) return 0;
    b->buf = nb;
    b->cap = newcap;
    return 1;
}

static int buf_append(Buf *b, const char *src, size_t n) {
    if (!buf_grow(b, b->len + n + 1)) return 0;
    memcpy(b->buf + b->len, src, n);
    b->len += n;
    b->buf[b->len] = '\0';
    return 1;
}

static int buf_append_cstr(Buf *b, const char *s) {
    return buf_append(b, s, strlen(s));
}

static int buf_append_byte(Buf *b, char c) {
    return buf_append(b, &c, 1);
}

static char *buf_take(Buf *b) {
    if (b->buf) return b->buf;
    /* Always return a non-NULL string for empty result so callers
    ** can free unconditionally. */
    char *s = malloc(1);
    if (s) s[0] = '\0';
    return s;
}

/* ============================================================
** Helpers
** ============================================================ */

static int is_ident_start(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_ident_cont(int c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int is_digit(int c) {
    return c >= '0' && c <= '9';
}

static LimeLexPattern *find_pattern(LimeLexSpec *spec, const char *name, size_t name_len) {
    LimeLexPattern *p = spec->patterns;
    while (p) {
        if (strlen(p->name) == name_len && memcmp(p->name, name, name_len) == 0) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

static void resolver_error(LimeLexSpec *spec, int line, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d: ", spec->filename ? spec->filename : "<input>", line);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    spec->error_count++;
}

/* ============================================================
** Core expansion
** ============================================================ */

/* Forward declaration -- expand_pattern recurses through this. */
static int expand_string(LimeLexSpec *spec, const char *src, int caller_line, Buf *out);

/* Expand a pattern declaration's regex.  Memoizes via
** pat->expanded_regex.  Sets _resolve_visit to detect cycles.
** Returns 1 on success, 0 on cycle (with diagnostic emitted).
*/
static int expand_pattern(LimeLexSpec *spec, LimeLexPattern *pat) {
    if (pat->expanded_regex) return 1; /* already done */
    if (pat->_resolve_visit == 1) {
        resolver_error(spec, pat->line, "pattern '%s' is part of a recursive cycle", pat->name);
        /* Set a safe placeholder so subsequent expansions don't
        ** loop indefinitely on the same cycle. */
        pat->expanded_regex = strdup("");
        pat->_resolve_visit = 2;
        return 0;
    }
    pat->_resolve_visit = 1;

    Buf out;
    buf_init(&out);
    int ok = expand_string(spec, pat->regex, pat->line, &out);
    /* If a recursive call set expanded_regex via the cycle-break
    ** placeholder path, free it before installing the real
    ** expansion (which the outer call computed even when the
    ** cycle flag was raised). */
    free(pat->expanded_regex);
    pat->expanded_regex = buf_take(&out);
    pat->_resolve_visit = 2;
    return ok;
}

/* Expand a string by walking it byte-by-byte and substituting
** any `{name}` references (where the first char inside `{` is
** a letter or underscore).  POSIX repetition forms `{N}` and
** `{N,M}` (digit first) pass through verbatim.  Returns 1 on
** success, 0 on any error (caller's error_count increments via
** resolver_error). */
static int expand_string(LimeLexSpec *spec, const char *src, int caller_line, Buf *out) {
    if (!src) return 1;
    int ok = 1;
    const char *p = src;
    while (*p) {
        /* Backslash-escapes pass through verbatim (including the
        ** following character, even if it's a `{`).  Important so
        ** users can write a literal `\{` in their regex. */
        if (*p == '\\' && p[1]) {
            buf_append_byte(out, *p++);
            buf_append_byte(out, *p++);
            continue;
        }
        /* Character class `[...]` -- pass through.  Inside a
        ** class, `{` and `}` are literal characters; we must not
        ** try to interpret `{name}` references.  We still respect
        ** backslash escapes inside the class. */
        if (*p == '[') {
            buf_append_byte(out, *p++);
            while (*p && *p != ']') {
                if (*p == '\\' && p[1]) {
                    buf_append_byte(out, *p++);
                    buf_append_byte(out, *p++);
                    continue;
                }
                buf_append_byte(out, *p++);
            }
            if (*p == ']') buf_append_byte(out, *p++);
            continue;
        }
        /* `{` could be the start of either a {name} interpolation
        ** or a {N,M} repetition.  Disambiguate by the first
        ** non-{ char. */
        if (*p == '{') {
            const char *peek = p + 1;
            if (is_ident_start(*peek)) {
                /* Interpolation: scan to closing `}`. */
                const char *name_start = peek;
                while (is_ident_cont(*peek))
                    peek++;
                if (*peek != '}') {
                    /* Malformed: `{name<garbage>` -- emit the `{`
                    ** verbatim and continue from the next byte. */
                    buf_append_byte(out, *p++);
                    continue;
                }
                size_t name_len = (size_t)(peek - name_start);
                LimeLexPattern *target = find_pattern(spec, name_start, name_len);
                if (!target) {
                    resolver_error(spec, caller_line, "undefined pattern '%.*s'", (int)name_len,
                                   name_start);
                    /* Pass through verbatim so subsequent passes
                    ** see something rather than nothing. */
                    buf_append(out, p, (size_t)(peek - p) + 1);
                    p = peek + 1;
                    ok = 0;
                    continue;
                }
                /* Recursively expand the target's regex. */
                if (!expand_pattern(spec, target)) ok = 0;
                /* Wrap in (...) so substitution doesn't bleed into
                ** surrounding context. */
                buf_append_byte(out, '(');
                buf_append_cstr(out, target->expanded_regex);
                buf_append_byte(out, ')');
                p = peek + 1;
                continue;
            }
            if (is_digit(*peek)) {
                /* POSIX repetition `{N}` or `{N,M}` -- pass
                ** through verbatim up to and including the close
                ** brace. */
                const char *close = peek;
                while (*close && *close != '}')
                    close++;
                if (*close == '}') {
                    buf_append(out, p, (size_t)(close - p) + 1);
                    p = close + 1;
                    continue;
                }
                /* Unterminated; emit the `{` and continue. */
                buf_append_byte(out, *p++);
                continue;
            }
            /* `{` followed by something else (or end of string):
            ** treat as a literal byte. */
            buf_append_byte(out, *p++);
            continue;
        }
        buf_append_byte(out, *p++);
    }
    return ok;
}

/* Resolve every rule in a list (top-level or ruleset). */
static int resolve_rule_list(LimeLexSpec *spec, LimeLexRule *r) {
    int ok = 1;
    while (r) {
        if (!r->is_eof && r->pattern && !r->expanded_pattern) {
            Buf out;
            buf_init(&out);
            if (!expand_string(spec, r->pattern, r->line, &out)) ok = 0;
            r->expanded_pattern = buf_take(&out);
        }
        r = r->next;
    }
    return ok;
}

/* ============================================================
** Public entry point
** ============================================================ */

int lime_lex_resolve_patterns(LimeLexSpec *spec) {
    if (!spec) return -1;
    int ok = 1;

    /* Phase 1: expand every %pattern declaration.  Memoization
    ** in expand_pattern handles the topological order; cycles
    ** are detected and reported. */
    {
        LimeLexPattern *p = spec->patterns;
        while (p) {
            if (!expand_pattern(spec, p)) ok = 0;
            p = p->next;
        }
    }

    /* Phase 2: substitute pattern references into all rule
    ** patterns -- both top-level rules and rules inside
    ** %ruleset blocks. */
    if (!resolve_rule_list(spec, spec->rules)) ok = 0;
    {
        LimeLexRuleset *rs = spec->rulesets;
        while (rs) {
            if (!resolve_rule_list(spec, rs->rules)) ok = 0;
            rs = rs->next;
        }
    }

    return ok ? 0 : -1;
}
