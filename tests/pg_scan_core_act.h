/*
** tests/pg_scan_core_act.h -- PostgreSQL-free actions for
** tests/pg_scan_core.lex (B4).
**
** One function, pgsc_action(), reproduces the CONTROL FLOW of every
** action in PostgreSQL's Lime scanner port (scan.lex): state changes,
** pushback, the nested-comment depth, the XQS "which string did we come
** from" state, the dollar-quote tag match, the operator trimming
** (scan_lex_op_keep, copied from scan.c), the ->> etc. self/2-char
** re-classification is left to the parser side as in the port.  What it
** drops is PostgreSQL itself: no palloc, no ereport, no literal-buffer
** contents (the emitted token is the token's byte span, which is what
** a downstream comparison needs).
**
** Both engines call it after each match:
**   - the generated scanner (lime -X of pg_scan_core.lex) through the
**     PGSC(rule) action body;
**   - the runtime interpreter driver in tests/test_lex_program.c.
** The function sees only (rule, text, len, state) and returns its
** effects, so the two engines cannot differ in their actions, only in
** what they matched.
*/
#ifndef PG_SCAN_CORE_ACT_H
#define PG_SCAN_CORE_ACT_H

#include <stddef.h>
#include <string.h>

/* State and rule numbers are the generated header's (CORE_STATE_*,
** CORE_RULE_*); the interpreter driver checks at startup that its
** program agrees with them by name. */
#include "pg_scan_core_lex.h"

typedef struct PgscCtx {
    int xcdepth;
    int prev_state;
    const char *dolq; /* tag text (points into the input) */
    size_t dolq_len;
    size_t litstart; /* offset of the compound token's first byte */
    /* emitted tokens: (kind, start, end) hashed + counted by the sink */
    void (*sink)(void *u, int kind, size_t start, size_t end);
    void *sink_user;
    const char *base; /* start of the input, for offsets */
    int error;        /* set by an <<EOF>> error rule */
} PgscCtx;

/* Effects of one action. */
typedef struct PgscOut {
    size_t keep;   /* bytes of the match to consume (rest is pushed back) */
    int new_state; /* state after the action */
} PgscOut;

/* Token kinds: the port's SCAN_TOK_* sentinels (scan_lex_internal.h). */
enum {
    PGT_RAW_CHAR = 1, PGT_TYPECAST, PGT_DOT_DOT, PGT_COLON_EQUALS, PGT_OP, PGT_PARAM,
    PGT_PARAM_JUNK, PGT_ICONST_DEC, PGT_ICONST_HEX, PGT_ICONST_OCT, PGT_ICONST_BIN, PGT_HEXFAIL,
    PGT_OCTFAIL, PGT_BINFAIL, PGT_FCONST_NUMERIC, PGT_FCONST_REAL, PGT_TRAILING_JUNK_NUM,
    PGT_IDENT_RAW, PGT_IDENT_QUOTED, PGT_NCHAR, PGT_BCONST, PGT_XCONST, PGT_SCONST, PGT_USCONST,
    PGT_UIDENT, PGT_BAD_UNICODE_ESCAPE, PGT_BAD_HEX_ESCAPE, PGT_BAD_SURROGATE, PGT_EOF_ERROR
};

/* scan.c's scan_lex_op_keep, verbatim logic. */
static inline size_t pgsc_op_keep(const char *text, size_t len) {
    const char *slashstar = NULL, *dashdash = NULL;
    size_t nchars = len;
    for (size_t i = 0; i + 1 < len; i++) {
        if (slashstar == NULL && text[i] == '/' && text[i + 1] == '*') slashstar = text + i;
        if (dashdash == NULL && text[i] == '-' && text[i + 1] == '-') dashdash = text + i;
    }
    if (slashstar && dashdash) {
        if (slashstar > dashdash) slashstar = dashdash;
    } else if (!slashstar)
        slashstar = dashdash;
    if (slashstar) nchars = (size_t)(slashstar - text);
    if (nchars > 1 && (text[nchars - 1] == '+' || text[nchars - 1] == '-')) {
        int ic, special = 0;
        for (ic = (int)nchars - 2; ic >= 0; ic--)
            if (strchr("~!@#^&|`?%", text[ic])) {
                special = 1;
                break;
            }
        if (!special)
            while (nchars > 1 && (text[nchars - 1] == '+' || text[nchars - 1] == '-')) nchars--;
    }
    return nchars;
}

static inline int pgsc_is_surrogate_first(unsigned long c) {
    return c >= 0xD800 && c <= 0xDBFF;
}

static inline unsigned long pgsc_hex(const char *s, size_t n) {
    unsigned long v = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        v = v * 16 + (unsigned long)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return v;
}

#define PGSC_EMIT(kind, s, e) cx->sink(cx->sink_user, (kind), (size_t)(s), (size_t)(e))

/* Run the action for `rule` on text[0..len) (len = 0 and text = end of
** input for an <<EOF>> rule) in `state`.  Fills *o.  The token offsets
** are relative to cx->base. */
static inline void pgsc_action(PgscCtx *cx, int rule, const char *text, size_t len, int state,
                        PgscOut *o) {
    size_t at = (size_t)(text - cx->base);
    o->keep = len;
    o->new_state = state;
    switch (rule) {
    case CORE_RULE_WS_INITIAL: break;
    case CORE_RULE_XC_OPEN:
        cx->xcdepth = 0;
        o->keep = 2;
        o->new_state = CORE_STATE_XC;
        break;
    case CORE_RULE_XB_OPEN: cx->litstart = at; o->new_state = CORE_STATE_XB; break;
    case CORE_RULE_XH_OPEN: cx->litstart = at; o->new_state = CORE_STATE_XH; break;
    case CORE_RULE_XN_OPEN: o->keep = 1; PGSC_EMIT(PGT_NCHAR, at, at + 1); break;
    case CORE_RULE_XQ_OPEN: cx->litstart = at; o->new_state = CORE_STATE_XQ; break;
    case CORE_RULE_XE_OPEN: cx->litstart = at; o->new_state = CORE_STATE_XE; break;
    case CORE_RULE_XUS_OPEN: cx->litstart = at; o->new_state = CORE_STATE_XUS; break;
    case CORE_RULE_XUI_OPEN: cx->litstart = at; o->new_state = CORE_STATE_XUI; break;
    case CORE_RULE_XU_FAILED: o->keep = 1; PGSC_EMIT(PGT_IDENT_RAW, at, at + 1); break;
    case CORE_RULE_XDOLQ_OPEN:
        cx->dolq = text;
        cx->dolq_len = len;
        cx->litstart = at;
        o->new_state = CORE_STATE_XDOLQ;
        break;
    case CORE_RULE_XDOLQ_FAILED: o->keep = 1; PGSC_EMIT(PGT_RAW_CHAR, at, at + 1); break;
    case CORE_RULE_XD_OPEN: cx->litstart = at; o->new_state = CORE_STATE_XD; break;
    case CORE_RULE_TYPECAST: PGSC_EMIT(PGT_TYPECAST, at, at + len); break;
    case CORE_RULE_DOT_DOT: PGSC_EMIT(PGT_DOT_DOT, at, at + len); break;
    case CORE_RULE_COLON_EQ: PGSC_EMIT(PGT_COLON_EQUALS, at, at + len); break;
    case CORE_RULE_DECINTEGER: PGSC_EMIT(PGT_ICONST_DEC, at, at + len); break;
    case CORE_RULE_HEXINTEGER: PGSC_EMIT(PGT_ICONST_HEX, at, at + len); break;
    case CORE_RULE_OCTINTEGER: PGSC_EMIT(PGT_ICONST_OCT, at, at + len); break;
    case CORE_RULE_BININTEGER: PGSC_EMIT(PGT_ICONST_BIN, at, at + len); break;
    case CORE_RULE_HEXFAIL: PGSC_EMIT(PGT_HEXFAIL, at, at + len); break;
    case CORE_RULE_OCTFAIL: PGSC_EMIT(PGT_OCTFAIL, at, at + len); break;
    case CORE_RULE_BINFAIL: PGSC_EMIT(PGT_BINFAIL, at, at + len); break;
    case CORE_RULE_NUMERIC: PGSC_EMIT(PGT_FCONST_NUMERIC, at, at + len); break;
    case CORE_RULE_NUMERICFAIL: o->keep = len - 2; PGSC_EMIT(PGT_ICONST_DEC, at, at + len - 2); break;
    case CORE_RULE_REAL: PGSC_EMIT(PGT_FCONST_REAL, at, at + len); break;
    case CORE_RULE_REALFAIL:
    case CORE_RULE_INTEGER_JUNK:
    case CORE_RULE_NUMERIC_JUNK:
    case CORE_RULE_REAL_JUNK: PGSC_EMIT(PGT_TRAILING_JUNK_NUM, at, at + len); break;
    case CORE_RULE_PARAM_JUNK: PGSC_EMIT(PGT_PARAM_JUNK, at, at + len); break;
    case CORE_RULE_PARAM: PGSC_EMIT(PGT_PARAM, at, at + len); break;
    case CORE_RULE_OP:
        o->keep = pgsc_op_keep(text, len);
        PGSC_EMIT(PGT_OP, at, at + o->keep);
        break;
    case CORE_RULE_IDENT: PGSC_EMIT(PGT_IDENT_RAW, at, at + len); break;
    case CORE_RULE_SELF_CH:
    case CORE_RULE_OTHER: PGSC_EMIT(PGT_RAW_CHAR, at, at + len); break;

    case CORE_RULE_XC_NESTED: cx->xcdepth++; o->keep = 2; break;
    case CORE_RULE_XC_CLOSE:
        if (cx->xcdepth <= 0) o->new_state = CORE_STATE_INITIAL;
        else cx->xcdepth--;
        break;
    case CORE_RULE_XC_INSIDE:
    case CORE_RULE_XC_OP_CHARS:
    case CORE_RULE_XC_STARS: break;

    case CORE_RULE_XB_CLOSE: cx->prev_state = CORE_STATE_XB; o->new_state = CORE_STATE_XQS; break;
    case CORE_RULE_XH_CLOSE: cx->prev_state = CORE_STATE_XH; o->new_state = CORE_STATE_XQS; break;
    case CORE_RULE_XQ_CLOSE: cx->prev_state = CORE_STATE_XQ; o->new_state = CORE_STATE_XQS; break;
    case CORE_RULE_XE_CLOSE: cx->prev_state = CORE_STATE_XE; o->new_state = CORE_STATE_XQS; break;
    case CORE_RULE_XUS_CLOSE: cx->prev_state = CORE_STATE_XUS; o->new_state = CORE_STATE_XQS; break;
    case CORE_RULE_XE_UNICODE:
        if (pgsc_is_surrogate_first(pgsc_hex(text + 2, len - 2))) o->new_state = CORE_STATE_XEU;
        break;
    case CORE_RULE_XE_UNICODE_FAIL:
    case CORE_RULE_XEU_UNICODE_FAIL: PGSC_EMIT(PGT_BAD_UNICODE_ESCAPE, at, at + len); break;
    case CORE_RULE_XE_HEXFAIL: PGSC_EMIT(PGT_BAD_HEX_ESCAPE, at, at + len); break;
    case CORE_RULE_XEU_UNICODE: o->new_state = CORE_STATE_XE; break;
    case CORE_RULE_XEU_OTHER: PGSC_EMIT(PGT_BAD_SURROGATE, at, at + len); break;

    case CORE_RULE_XQS_CONTINUE: o->new_state = cx->prev_state; break;
    case CORE_RULE_XQS_OTHER:
    case CORE_RULE_XQS_EOF: {
        int code = cx->prev_state == CORE_STATE_XB    ? PGT_BCONST
                   : cx->prev_state == CORE_STATE_XH  ? PGT_XCONST
                   : cx->prev_state == CORE_STATE_XUS ? PGT_USCONST
                                                      : PGT_SCONST;
        PGSC_EMIT(code, cx->litstart, at);
        o->keep = 0; /* xqs_other pushes its whole match back */
        o->new_state = CORE_STATE_INITIAL;
        break;
    }
    case CORE_RULE_XDOLQ_CLOSE:
        if (cx->dolq && cx->dolq_len == len && memcmp(cx->dolq, text, len) == 0) {
            PGSC_EMIT(PGT_SCONST, cx->litstart, at + len);
            cx->dolq = NULL;
            o->new_state = CORE_STATE_INITIAL;
        } else {
            o->keep = len - 1;
        }
        break;
    case CORE_RULE_XD_CLOSE:
        PGSC_EMIT(PGT_IDENT_QUOTED, cx->litstart, at + 1);
        o->new_state = CORE_STATE_INITIAL;
        break;
    case CORE_RULE_XUI_CLOSE:
        PGSC_EMIT(PGT_UIDENT, cx->litstart, at + 1);
        o->new_state = CORE_STATE_INITIAL;
        break;

    case CORE_RULE_XC_EOF:
    case CORE_RULE_XB_EOF:
    case CORE_RULE_XH_EOF:
    case CORE_RULE_XQ_EOF:
    case CORE_RULE_XE_EOF:
    case CORE_RULE_XEU_EOF:
    case CORE_RULE_XUS_EOF:
    case CORE_RULE_XDOLQ_EOF:
    case CORE_RULE_XD_EOF:
    case CORE_RULE_XUI_EOF:
        PGSC_EMIT(PGT_EOF_ERROR, at, at);
        cx->error = 1;
        break;

    default: break; /* literal-body rules: consume, no token */
    }
}

/* The generated scanner's action body: run pgsc_action and apply its
** effects with the LEX_* primitives.  `user` is the PgscCtx. */
#define PGSC(rule)                                                                 \
    do {                                                                           \
        PgscOut _o;                                                                \
        pgsc_action((PgscCtx *)user, (rule), matched, matched_len, state, &_o);    \
        if (_o.keep < matched_len) LEX_PUSHBACK(matched_len - _o.keep);            \
        if (_o.new_state != state) LEX_TRANSITION(_o.new_state);                   \
        if (((PgscCtx *)user)->error) LEX_TERMINATE();                             \
        LEX_SKIP();                                                                \
    } while (0)

#endif /* PG_SCAN_CORE_ACT_H */
