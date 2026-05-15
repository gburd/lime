/*
** src/lex/lex_regex.c -- regex parser implementation for the
** Lime .lex DFA compiler (M2.1).
**
** Single-pass recursive descent.  Builds the AST top-down using
** the grammar from lex_regex.h.  Errors set *err_out (when
** non-NULL) to a heap-allocated diagnostic and return NULL.
*/

#include "lex_regex.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
** Bitmap helpers (exposed for tests)
** ============================================================ */

void lime_lex_regex_class_set(unsigned char *bits, unsigned char byte) {
    bits[byte >> 3] |= (unsigned char)(1u << (byte & 7));
}

int lime_lex_regex_class_has(const unsigned char *bits, unsigned char byte) {
    return (bits[byte >> 3] >> (byte & 7)) & 1;
}

/* ============================================================
** Parser state
** ============================================================ */

typedef struct {
    const char  *src;
    const char  *p;
    char       **err_out;
    int          had_error;
} P;

static int peek(const P *p)        { return (unsigned char)*p->p; }
static int peek_at(const P *p, int n) {
    for (int i = 0; i < n; i++) {
        if (p->p[i] == '\0') return -1;
    }
    return (unsigned char)p->p[n];
}
static void advance(P *p)          { if (*p->p) p->p++; }
static int  at_end(const P *p)     { return *p->p == '\0'; }

static void set_error(P *p, const char *fmt, ...) {
    if (p->had_error) return;     /* keep first error */
    p->had_error = 1;
    if (!p->err_out) return;
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    *p->err_out = malloc((size_t)n + 1);
    if (*p->err_out) memcpy(*p->err_out, buf, (size_t)n + 1);
}

/* ============================================================
** AST allocation / free
** ============================================================ */

static LimeReNode *new_node(LimeReKind k) {
    LimeReNode *n = calloc(1, sizeof(*n));
    if (n) n->kind = k;
    return n;
}

void lime_lex_regex_free(LimeReNode *n) {
    if (!n) return;
    switch (n->kind) {
        case LIME_RE_CONCAT:
        case LIME_RE_ALT:
            lime_lex_regex_free(n->u.binary.left);
            lime_lex_regex_free(n->u.binary.right);
            break;
        case LIME_RE_STAR:
        case LIME_RE_PLUS:
        case LIME_RE_QUESTION:
            lime_lex_regex_free(n->u.unary.child);
            break;
        case LIME_RE_REPEAT:
            lime_lex_regex_free(n->u.repeat.child);
            break;
        default:
            break;
    }
    free(n);
}

/* ============================================================
** Escape sequence interpretation
** ============================================================ */

/* Decode a single escape sequence following a backslash.
** On entry p->p is one past the backslash (i.e. at the escape
** char).  Returns the resolved byte value via *out and advances
** p past the consumed bytes.  Returns 1 on success, 0 on error
** (caller's set_error already invoked).
**
** Recognized:
**   \n \t \r \f \v \0  -- ASCII C-style escapes
**   \\ \" \' \/         -- escape literals
**   \xHH                -- exactly two hex digits
**   \<any-other-char>   -- yields that char literally (so user
**                         can write `\.` to mean `.`, `\(` for
**                         `(`, etc., in BOTH regex top level
**                         and inside character classes)
*/
static int read_escape(P *p, int *out) {
    if (at_end(p)) {
        set_error(p, "trailing backslash with no escape");
        return 0;
    }
    int c = peek(p);
    advance(p);
    switch (c) {
        case 'n':  *out = '\n'; return 1;
        case 't':  *out = '\t'; return 1;
        case 'r':  *out = '\r'; return 1;
        case 'f':  *out = '\f'; return 1;
        case 'v':  *out = '\v'; return 1;
        case '0':  *out = '\0'; return 1;
        case 'x': {
            int hi = peek(p);
            int lo = at_end(p) ? -1 : peek_at(p, 1);
            int hi_v = -1, lo_v = -1;
            if (hi >= '0' && hi <= '9') hi_v = hi - '0';
            else if (hi >= 'a' && hi <= 'f') hi_v = 10 + hi - 'a';
            else if (hi >= 'A' && hi <= 'F') hi_v = 10 + hi - 'A';
            if (lo >= '0' && lo <= '9') lo_v = lo - '0';
            else if (lo >= 'a' && lo <= 'f') lo_v = 10 + lo - 'a';
            else if (lo >= 'A' && lo <= 'F') lo_v = 10 + lo - 'A';
            if (hi_v < 0 || lo_v < 0) {
                set_error(p, "\\xHH requires two hex digits");
                return 0;
            }
            advance(p); advance(p);
            *out = (hi_v << 4) | lo_v;
            return 1;
        }
        default:
            *out = c;
            return 1;
    }
}

/* ============================================================
** Forward declarations
** ============================================================ */

static LimeReNode *parse_alt(P *p);
static LimeReNode *parse_concat(P *p);
static LimeReNode *parse_term(P *p);
static LimeReNode *parse_atom(P *p);
static LimeReNode *parse_char_class(P *p);
static LimeReNode *apply_quantifier(P *p, LimeReNode *atom);

/* ============================================================
** Atom parsers
** ============================================================ */

static LimeReNode *parse_char_class(P *p) {
    /* `[` already consumed by caller. */
    LimeReNode *n = new_node(LIME_RE_CHAR_CLASS);
    if (!n) return NULL;
    if (peek(p) == '^') {
        n->u.char_class.negate = 1;
        advance(p);
    }
    /* Special case: a `]` immediately after `[` (or after `[^`)
    ** is a literal `]` rather than the close bracket.  Standard
    ** flex/regex behavior.  Single `[]` / `[^]` is otherwise
    ** ambiguous. */
    int first = 1;
    while (!at_end(p) && (first || peek(p) != ']')) {
        first = 0;
        int byte;
        if (peek(p) == '\\') {
            advance(p);
            if (!read_escape(p, &byte)) {
                lime_lex_regex_free(n);
                return NULL;
            }
        } else {
            byte = peek(p);
            advance(p);
        }
        /* Range? */
        if (peek(p) == '-' && peek_at(p, 1) != ']' && peek_at(p, 1) != -1) {
            advance(p);   /* eat '-' */
            int hi;
            if (peek(p) == '\\') {
                advance(p);
                if (!read_escape(p, &hi)) {
                    lime_lex_regex_free(n);
                    return NULL;
                }
            } else {
                hi = peek(p);
                advance(p);
            }
            if (byte > hi) {
                set_error(p, "character class range out of order: %d-%d",
                          byte, hi);
                lime_lex_regex_free(n);
                return NULL;
            }
            for (int b = byte; b <= hi; b++) {
                lime_lex_regex_class_set(n->u.char_class.bits,
                                         (unsigned char)b);
            }
        } else {
            lime_lex_regex_class_set(n->u.char_class.bits,
                                     (unsigned char)byte);
        }
    }
    if (peek(p) != ']') {
        set_error(p, "unterminated character class");
        lime_lex_regex_free(n);
        return NULL;
    }
    advance(p);   /* eat ']' */
    return n;
}

static LimeReNode *parse_atom(P *p) {
    int c = peek(p);
    switch (c) {
        case '\0':
        case '|':
        case ')':
        case '*':
        case '+':
        case '?':
        case '{':
        case '}':
        case ']':
            /* Not the start of an atom; caller decides. */
            return NULL;
        case '.': {
            advance(p);
            LimeReNode *n = new_node(LIME_RE_ANY);
            return n;
        }
        case '^': {
            advance(p);
            return new_node(LIME_RE_ANCHOR_START);
        }
        case '$': {
            advance(p);
            return new_node(LIME_RE_ANCHOR_END);
        }
        case '(': {
            advance(p);
            LimeReNode *body = parse_alt(p);
            if (p->had_error) {
                lime_lex_regex_free(body);
                return NULL;
            }
            if (peek(p) != ')') {
                set_error(p, "unterminated group: expected ')'");
                lime_lex_regex_free(body);
                return NULL;
            }
            advance(p);
            /* The group acts as a structural atom; we don't need
            ** a separate node kind.  Fall back to LIME_RE_EMPTY
            ** for `()` rather than NULL. */
            if (!body) {
                body = new_node(LIME_RE_EMPTY);
            }
            return body;
        }
        case '[':
            advance(p);
            return parse_char_class(p);
        case '\\': {
            advance(p);
            int byte;
            if (!read_escape(p, &byte)) return NULL;
            LimeReNode *n = new_node(LIME_RE_LITERAL);
            if (!n) return NULL;
            n->u.literal = (unsigned char)byte;
            return n;
        }
        default: {
            advance(p);
            LimeReNode *n = new_node(LIME_RE_LITERAL);
            if (!n) return NULL;
            n->u.literal = (unsigned char)c;
            return n;
        }
    }
}

/* ============================================================
** Quantifier
** ============================================================ */

/* Try to consume a quantifier following an atom.  Returns the
** wrapped node (or the original atom if no quantifier present).
** On error, frees `atom` and returns NULL. */
static LimeReNode *apply_quantifier(P *p, LimeReNode *atom) {
    int c = peek(p);
    if (c == '*') {
        advance(p);
        LimeReNode *n = new_node(LIME_RE_STAR);
        if (!n) { lime_lex_regex_free(atom); return NULL; }
        n->u.unary.child = atom;
        return n;
    }
    if (c == '+') {
        advance(p);
        LimeReNode *n = new_node(LIME_RE_PLUS);
        if (!n) { lime_lex_regex_free(atom); return NULL; }
        n->u.unary.child = atom;
        return n;
    }
    if (c == '?') {
        advance(p);
        LimeReNode *n = new_node(LIME_RE_QUESTION);
        if (!n) { lime_lex_regex_free(atom); return NULL; }
        n->u.unary.child = atom;
        return n;
    }
    if (c == '{') {
        const char *save = p->p;
        advance(p);   /* eat { */
        /* Parse digits.  If the contents don't form a valid
        ** repetition specifier, restore cursor and treat the
        ** `{` as a literal byte (not currently handled -- we
        ** require valid {n,m} or error). */
        int min = 0, max = -1;
        if (!(peek(p) >= '0' && peek(p) <= '9')) {
            set_error(p, "{repetition} requires digit count");
            lime_lex_regex_free(atom);
            (void)save;
            return NULL;
        }
        while (peek(p) >= '0' && peek(p) <= '9') {
            min = min * 10 + (peek(p) - '0');
            advance(p);
        }
        if (peek(p) == ',') {
            advance(p);
            if (peek(p) == '}') {
                /* `{n,}` -- unbounded upper. */
                max = -1;
            } else if (peek(p) >= '0' && peek(p) <= '9') {
                max = 0;
                while (peek(p) >= '0' && peek(p) <= '9') {
                    max = max * 10 + (peek(p) - '0');
                    advance(p);
                }
                if (max < min) {
                    set_error(p, "{n,m}: m=%d less than n=%d", max, min);
                    lime_lex_regex_free(atom);
                    return NULL;
                }
            } else {
                set_error(p, "{n,m} requires digits or '}' after comma");
                lime_lex_regex_free(atom);
                return NULL;
            }
        } else {
            /* `{n}` -- exact count. */
            max = min;
        }
        if (peek(p) != '}') {
            set_error(p, "unterminated {repetition}");
            lime_lex_regex_free(atom);
            return NULL;
        }
        advance(p);
        LimeReNode *n = new_node(LIME_RE_REPEAT);
        if (!n) { lime_lex_regex_free(atom); return NULL; }
        n->u.repeat.child = atom;
        n->u.repeat.min = min;
        n->u.repeat.max = max;
        return n;
    }
    return atom;
}

/* ============================================================
** Term, concat, alt
** ============================================================ */

static LimeReNode *parse_term(P *p) {
    LimeReNode *atom = parse_atom(p);
    if (!atom) return NULL;
    return apply_quantifier(p, atom);
}

static LimeReNode *parse_concat(P *p) {
    LimeReNode *acc = NULL;
    for (;;) {
        if (p->had_error) {
            lime_lex_regex_free(acc);
            return NULL;
        }
        LimeReNode *t = parse_term(p);
        if (p->had_error) {
            lime_lex_regex_free(acc);
            lime_lex_regex_free(t);
            return NULL;
        }
        if (!t) break;            /* end of concat sequence */
        if (!acc) {
            acc = t;
        } else {
            LimeReNode *c = new_node(LIME_RE_CONCAT);
            if (!c) {
                lime_lex_regex_free(acc);
                lime_lex_regex_free(t);
                return NULL;
            }
            c->u.binary.left = acc;
            c->u.binary.right = t;
            acc = c;
        }
    }
    if (!acc) {
        /* Empty alternative branch (e.g. `a||b`) -- represent as
        ** an explicit EMPTY node so ALT has well-formed children. */
        acc = new_node(LIME_RE_EMPTY);
    }
    return acc;
}

static LimeReNode *parse_alt(P *p) {
    LimeReNode *left = parse_concat(p);
    if (p->had_error) { lime_lex_regex_free(left); return NULL; }
    if (peek(p) != '|') return left;
    advance(p);   /* eat | */
    LimeReNode *right = parse_alt(p);   /* right-associative is fine */
    if (p->had_error) {
        lime_lex_regex_free(left);
        lime_lex_regex_free(right);
        return NULL;
    }
    LimeReNode *n = new_node(LIME_RE_ALT);
    if (!n) {
        lime_lex_regex_free(left);
        lime_lex_regex_free(right);
        return NULL;
    }
    n->u.binary.left = left;
    n->u.binary.right = right;
    return n;
}

/* ============================================================
** Public entry point
** ============================================================ */

LimeReNode *lime_lex_regex_parse(const char *src, char **err_out) {
    if (err_out) *err_out = NULL;
    P p;
    p.src       = src ? src : "";
    p.p         = p.src;
    p.err_out   = err_out;
    p.had_error = 0;

    LimeReNode *root = parse_alt(&p);
    if (p.had_error) {
        lime_lex_regex_free(root);
        return NULL;
    }
    if (!at_end(&p)) {
        set_error(&p, "unexpected '%c' at column %ld",
                  *p.p, (long)(p.p - p.src));
        lime_lex_regex_free(root);
        return NULL;
    }
    if (!root) {
        /* Empty regex.  parse_alt returned NULL only via error;
        ** parse_concat returns LIME_RE_EMPTY for empty input.  But
        ** be defensive. */
        root = new_node(LIME_RE_EMPTY);
    }
    return root;
}
