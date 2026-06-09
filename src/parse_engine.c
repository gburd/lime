/*
** parse_engine.c -- runtime LALR(1) push parser that drives a
** ParserSnapshot's action tables.
**
** This is the implementation behind parse_begin / parse_token /
** parse_end.  It mirrors the shift/reduce/accept/error dispatch that
** the generator emits into limpar.c, but it operates on the snapshot's
** dynamically-loaded tables instead of a static set, so the same
** runtime can drive any grammar at any time -- including grammars
** that have been mutated by an extension since they were loaded.
**
** The interpreter is intentionally minimal: it tracks state numbers
** on the parse stack and reports accept / syntax-error outcomes via
** parse_token's return value.  Semantic value propagation (the user's
** %type and %extra_argument plumbing) requires the generator's
** typed reduce dispatch, which is per-grammar and lives in the
** generated parser.  Applications that need semantic values should
** call the generated entry point directly; applications that just
** want to validate tokens against a grammar (e.g. a syntax-only
** validator, a fuzzer harness, or a runtime extension test) get a
** real working parser here.
*/
#include "parse_context.h"
#include "snapshot.h"
#include "lime_time.h"
#include "jit_context.h"
#include "grammar_context.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Parse stack                                                         */
/* ------------------------------------------------------------------ */

#define PARSE_STACK_INITIAL 64

typedef struct {
    uint16_t state; /* Snapshot state number */
    int symbol;     /* Symbol code that put us in this state */
} ParseStackEntry;

typedef struct {
    ParseStackEntry *base;
    ParseStackEntry *top; /* Points one past the deepest entry */
    uint32_t capacity;
} ParseStack;

static bool stack_init(ParseStack *s) {
    s->base = malloc(PARSE_STACK_INITIAL * sizeof(ParseStackEntry));
    if (s->base == NULL) return false;
    s->capacity = PARSE_STACK_INITIAL;
    s->top = s->base;
    return true;
}

static void stack_destroy(ParseStack *s) {
    free(s->base);
    s->base = s->top = NULL;
    s->capacity = 0;
}

static bool stack_push(ParseStack *s, uint16_t state, int symbol) {
    uint32_t depth = (uint32_t)(s->top - s->base);
    if (depth >= s->capacity) {
        uint32_t new_cap = s->capacity * 2;
        ParseStackEntry *nb = realloc(s->base, new_cap * sizeof(ParseStackEntry));
        if (nb == NULL) return false;
        s->base = nb;
        s->top = nb + depth;
        s->capacity = new_cap;
    }
    s->top->state = state;
    s->top->symbol = symbol;
    s->top++;
    return true;
}

static void stack_pop_n(ParseStack *s, uint32_t n) {
    uint32_t depth = (uint32_t)(s->top - s->base);
    if (n > depth) n = depth;
    s->top -= n;
}

/* ------------------------------------------------------------------ */
/*  Action lookup                                                       */
/*                                                                      */
/*  Mirrors yy_find_shift_action / yy_find_reduce_action in limpar.c    */
/*  but operates on the snapshot's action arrays.                       */
/* ------------------------------------------------------------------ */

/*
** find_shift_action -- the per-token action lookup hot path.
**
** When the snapshot has JIT code attached (snap->jit_ctx != NULL,
** populated by lime_jit_compile), we dispatch through the JIT'd
** function pointer instead of doing the table-driven walk.  The JIT
** function (generate_find_shift_action in src/jit_codegen.c) is a
** fully-inlined nested switch lowered to native jump tables, which
** avoids the indirect-load chain through yy_shift_ofst / yy_lookahead
** / yy_action / yy_default.
**
** PERFORMANCE NOTE -- LOAD-BEARING: This is the dispatch site that
** turns lime_jit_compile() from a batch-only speedup into a real
** per-token JIT acceleration.  bench/bench_jit_real_parser shows the
** delta (with JIT vs without) on a real Lime-generated parser.  If a
** future refactor moves this dispatch elsewhere, please verify the
** bench still shows the JIT-armed path beating the table-driven
** path.
*/
/*
** find_shift_action -- per-token hot path.
**
** Optimisations:
**   1. Hoisted JIT entry-point cache: snap->jit_find_shift_fn is
**      written once in lime_jit_compile and read once per token.
**      Avoids the previous ctx->fn chase that was two loads + one
**      function call.
**   2. Branch hints: __builtin_expect tells the compiler the JIT
**      branch is unlikely (most callers don't enable JIT), so the
**      table-driven path stays in the straight-line fallthrough
**      that branch predictors handle best.
**   3. `static inline`: explicit inline hint so the engine's
**      step function doesn't pay function-call overhead per token.
**      The compiler at -O2 already inlines this most of the time,
**      but the explicit hint makes it consistent across compilers.
*/
typedef uint32_t (*lime_jit_shift_fn)(uint32_t state, uint32_t lookahead);

static inline uint16_t find_shift_action(const ParserSnapshot * LIME_RESTRICT snap, uint16_t stateno,
                                         uint16_t lookahead) {
    lime_jit_shift_fn jit_fn = (lime_jit_shift_fn)snap->jit_find_shift_fn;
    if (LIME_UNLIKELY(jit_fn != NULL)) {
        return (uint16_t)jit_fn((uint32_t)stateno, (uint32_t)lookahead);
    }

    if (LIME_UNLIKELY(stateno > snap->yy_max_shift)) return stateno;
    if (LIME_UNLIKELY(snap->yy_shift_ofst == NULL)) return snap->yy_no_action;

    int32_t ofst = snap->yy_shift_ofst[stateno];
    int32_t idx = ofst + (int32_t)lookahead;

    if (LIME_LIKELY(idx >= 0 && (uint32_t)idx < snap->lookahead_count
                             && snap->yy_lookahead[idx] == lookahead)) {
        return snap->yy_action[idx];
    }
    return snap->yy_default[stateno];
}

static inline uint16_t find_reduce_action(const ParserSnapshot * LIME_RESTRICT snap, uint16_t stateno,
                                          uint16_t lookahead) {
    if (LIME_UNLIKELY(snap->yy_reduce_ofst == NULL)) return snap->yy_no_action;

    int32_t ofst = snap->yy_reduce_ofst[stateno];
    int32_t idx = ofst + (int32_t)lookahead;

    if (LIME_LIKELY(idx >= 0 && (uint32_t)idx < snap->lookahead_count
                             && snap->yy_lookahead[idx] == lookahead)) {
        return snap->yy_action[idx];
    }
    return snap->yy_default[stateno];
}

/* ------------------------------------------------------------------ */
/*  Reduce                                                              */
/* ------------------------------------------------------------------ */

/*
** Apply rule `ruleno`: pop |RHS| stack entries, then perform a goto
** on the LHS non-terminal from the new top state.  Mirrors yy_reduce
** in limpar.c.
*/
static int reduce(const ParserSnapshot * LIME_RESTRICT snap, ParseStack * LIME_RESTRICT stk, uint32_t ruleno) {
    if (snap->yy_rule_info_nrhs == NULL || snap->yy_rule_info_lhs == NULL) {
        return -1;
    }
    if (ruleno >= snap->nrule) return -1;

    /* The generator stores nrhs as a non-negative count or as a
    ** negative number per limpar.c's convention; we treat magnitude. */
    int nrhs = snap->yy_rule_info_nrhs[ruleno];
    if (nrhs < 0) nrhs = -nrhs;

    int lhs = snap->yy_rule_info_lhs[ruleno];

    stack_pop_n(stk, (uint32_t)nrhs);

    if (stk->top == stk->base) {
        /* Reduced past the bottom -- start rule completed. */
        return 1;
    }

    uint16_t goto_state = stk->top[-1].state;
    uint16_t next = find_reduce_action(snap, goto_state, (uint16_t)lhs);

    if (next == snap->yy_accept_action) return 1;
    if (next == snap->yy_error_action || next == snap->yy_no_action) return -1;

    /* yy_shift(yyact, lhs): if yyact is in the shift-reduce range
    ** (action > MAX_SHIFT && action <= MAX_SHIFTREDUCE), push
    ** yyact + (MIN_REDUCE - MIN_SHIFTREDUCE) -- encoded as a pending
    ** reduce.  Otherwise push yyact directly. */
    uint16_t pushed = next;
    if (pushed > snap->yy_max_shift && pushed <= snap->yy_max_shiftreduce) {
        pushed = (uint16_t)(pushed + (snap->yy_min_reduce - snap->yy_min_shiftreduce));
    }
    if (!stack_push(stk, pushed, lhs)) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public push-parser entry point                                      */
/* ------------------------------------------------------------------ */

/*
** Per-context engine state.  Embedded in ParseContext via the
** opaque `engine` field; allocated lazily on the first
** parse_token() call and freed by parse_end() / parse_engine_drop().
**
** PERFORMANCE NOTE -- LOAD-BEARING: This used to live in a global
** side-table keyed by ParseContext *, with a pthread_mutex around
** every lookup.  Per-call mutex acquisition shows up clearly in
** bench/bench_flex_bison_compare; on aarch64 it makes Lime ~3-4%
** slower than Bison for short inputs.  The embedded layout below
** removes that overhead entirely.  If a future refactor needs to
** detach engine state from ParseContext (e.g. to support fork +
** snapshot per fork), please rerun the comparison bench and confirm
** Lime does not regress past the 1.0x mark.
*/
typedef struct ParseEngine {
    ParseStack stack;
    bool initialised;
    bool accepted;
    bool errored;
} ParseEngine;

/* Hook called from parse_end (in parse_context.c) to free our engine. */
LIME_COLD
void parse_engine_drop(struct ParseContext *ctx) {
    if (ctx == NULL || ctx->engine == NULL) return;
    ParseEngine *eng = (ParseEngine *)ctx->engine;
    stack_destroy(&eng->stack);
    free(eng);
    ctx->engine = NULL;
}

/*
** parse_engine_reset -- drop per-parse state but keep the engine
** allocation and stack buffer for reuse.
**
** Used by the thread-local ParseContext pool in parse_context.c to
** recycle ParseContexts across many parse_begin / parse_end pairs
** without paying malloc/free + stack alloc/destroy on every parse.
** Saves ~3 mallocs + 3 frees per parse on bench/bench_parse_fanout.
**
** Important: do NOT clear `initialised` or call stack_init -- the
** stack buffer is reused.  Just rewind the stack pointer, push the
** initial state 0, and clear the accept/error flags.  The engine
** is then in the same observable state as right after the first
** parse_token call's lazy init block, ready to consume tokens.
**
** If the previous parse grew the stack to its capacity limit,
** that capacity is preserved on reset -- subsequent parses skip
** the realloc churn entirely.
*/
void parse_engine_reset(struct ParseContext *ctx) {
    if (ctx == NULL || ctx->engine == NULL) return;
    ParseEngine *eng = (ParseEngine *)ctx->engine;
    /* Engine never made it past lazy init -- treat as cold path. */
    if (!eng->initialised || eng->stack.base == NULL) {
        eng->initialised = false;
        eng->accepted = false;
        eng->errored = false;
        return;
    }
    /* Rewind stack and push state 0 so the next parse_engine_step
    ** finds the same shape stack_init + stack_push would have left. */
    eng->stack.top = eng->stack.base;
    if (!stack_push(&eng->stack, 0, 0)) {
        /* Should never happen -- capacity is unchanged from the
        ** previous successful init.  Defensive: mark errored so the
        ** next parse fails cleanly rather than silently misbehaving. */
        eng->errored = true;
        return;
    }
    eng->accepted = false;
    eng->errored = false;
    /* eng->initialised stays true -- stack is logically initialised. */
}

/*
** parse_context_current_state -- expose the parser's current LR state
** (the raw top of the state stack) for introspection / logging.
**
** Returns LIME_NO_STATE when no parse is in progress.  NOTE: between
** tokens the raw top may be a pending shift-reduce encoding
** (value >= yy_min_reduce), not a settled LR state.  Callers that
** want a context-sensitive admissibility decision should NOT try to
** interpret this value themselves -- use
** parse_context_token_admissible(), which simulates the engine's
** shift/reduce/goto loop read-only and is lookahead-correct.
*/
uint16_t parse_context_current_state(const struct ParseContext *ctx) {
    if (ctx == NULL || ctx->engine == NULL) return LIME_NO_STATE;
    const ParseEngine *eng = (const ParseEngine *)ctx->engine;
    if (!eng->initialised || eng->stack.base == NULL
        || eng->stack.top == eng->stack.base) {
        return LIME_NO_STATE;
    }
    return eng->stack.top[-1].state;
}

/*
** parse_context_token_admissible -- would the parser, in its current
** state, make progress on `external_token_code` (shift, shift-reduce,
** reduce, or accept) rather than report a syntax error?
**
** This is the lookahead-correct oracle that backs context-sensitive
** keyword disambiguation for composed grammars (docs/MULTI_GRAMMAR.md).
** A scanner resolving a lexeme that collides between a base grammar
** and a loaded extension calls this with each candidate token code
** and emits the one that is admissible in the current parse state.
** When BOTH a base and an extension code are admissible the collision
** is genuinely ambiguous and must be settled by the disambiguation
** strategy (fork-resolve), not here.
**
** It replays the engine's parse_token loop READ-ONLY on a scratch
** copy of the state-number stack: resolve any pending shift-reduce,
** probe the shift action, and on a default reduce follow the goto and
** re-probe -- exactly what parse_engine_step would do with this
** lookahead, but without mutating the live parse or running user
** actions.  Returns the same LimeTokenAdmissibility classification as
** lime_token_admissible_in_state.
**
** Returns LIME_TOK_SHIFT (treat-as-admissible) before the first
** token (no live state to constrain the token) and on any internal
** limit (stack deeper than scratch, malformed snapshot) so the
** oracle never wrongly vetoes.
*/
LimeTokenAdmissibility parse_context_token_admissible(
    const struct ParseContext *ctx, int external_token_code) {
    if (ctx == NULL || ctx->engine == NULL) return LIME_TOK_SHIFT;
    const ParseEngine *eng = (const ParseEngine *)ctx->engine;
    const ParserSnapshot *snap = ctx->snapshot;
    if (snap == NULL) return LIME_TOK_SHIFT;
    if (!eng->initialised || eng->stack.base == NULL
        || eng->stack.top == eng->stack.base || eng->errored) {
        return LIME_TOK_SHIFT;
    }
    if (snap->yy_rule_info_nrhs == NULL || snap->yy_rule_info_lhs == NULL) {
        return LIME_TOK_SHIFT;
    }

    /* External -> internal index (mirrors parse_engine_step). */
    int major = (external_token_code == 0)
                    ? 0
                    : external_token_code - (int)snap->yy_first_token;
    if (major < 0 || major >= (int)snap->yy_ntoken) {
        return LIME_TOK_NONE;
    }

    /* Scratch copy of the state-number stack (states only -- goto and
    ** shift probing need nothing else). */
    uint32_t depth = (uint32_t)(eng->stack.top - eng->stack.base);
    enum { SCRATCH_MAX = 512 };
    uint16_t scratch[SCRATCH_MAX];
    if (depth > SCRATCH_MAX) return LIME_TOK_SHIFT; /* don't risk a veto */
    for (uint32_t i = 0; i < depth; i++) {
        scratch[i] = eng->stack.base[i].state;
    }
    uint32_t sp = depth; /* one past top */

    for (int guard = 0; guard < 8192; guard++) {
        uint16_t cur = scratch[sp - 1];

        /* Unpack a pending shift-reduce sitting on top first. */
        if (cur >= snap->yy_min_reduce) {
            uint32_t ruleno = (uint32_t)(cur - snap->yy_min_reduce);
            if (ruleno >= snap->nrule) return LIME_TOK_SHIFT;
            int nrhs = snap->yy_rule_info_nrhs[ruleno];
            if (nrhs < 0) nrhs = -nrhs;
            int lhs = snap->yy_rule_info_lhs[ruleno];
            if ((uint32_t)nrhs >= sp) return LIME_TOK_ACCEPT; /* past bottom */
            sp -= (uint32_t)nrhs;
            uint16_t gstate = scratch[sp - 1];
            uint16_t next = snap_find_reduce_action(snap, gstate, (uint16_t)lhs);
            if (next == snap->yy_accept_action) return LIME_TOK_ACCEPT;
            if (next == snap->yy_error_action || next == snap->yy_no_action) {
                return LIME_TOK_NONE;
            }
            uint16_t pushed = next;
            if (pushed > snap->yy_max_shift && pushed <= snap->yy_max_shiftreduce) {
                pushed = (uint16_t)(pushed
                            + (snap->yy_min_reduce - snap->yy_min_shiftreduce));
            }
            if (sp >= SCRATCH_MAX) return LIME_TOK_SHIFT;
            scratch[sp++] = pushed;
            continue;
        }

        /* Real state: probe the shift action for the lookahead. */
        uint16_t act = snap_find_shift_action(snap, cur, (uint16_t)major);
        if (act <= snap->yy_max_shift) {
            return (act == snap->yy_no_action) ? LIME_TOK_NONE : LIME_TOK_SHIFT;
        }
        if (act >= snap->yy_min_shiftreduce && act <= snap->yy_max_shiftreduce) {
            return LIME_TOK_SHIFTREDUCE;
        }
        if (act == snap->yy_accept_action) return LIME_TOK_ACCEPT;
        if (act == snap->yy_error_action || act == snap->yy_no_action) {
            return LIME_TOK_NONE;
        }
        /* Reduce by a rule (default action gated on this lookahead):
        ** apply it and re-probe, exactly as the engine would. */
        if (act >= snap->yy_min_reduce) {
            uint32_t ruleno = (uint32_t)(act - snap->yy_min_reduce);
            if (ruleno >= snap->nrule) return LIME_TOK_SHIFT;
            int nrhs = snap->yy_rule_info_nrhs[ruleno];
            if (nrhs < 0) nrhs = -nrhs;
            int lhs = snap->yy_rule_info_lhs[ruleno];
            if ((uint32_t)nrhs >= sp) return LIME_TOK_ACCEPT;
            sp -= (uint32_t)nrhs;
            uint16_t gstate = scratch[sp - 1];
            uint16_t next = snap_find_reduce_action(snap, gstate, (uint16_t)lhs);
            if (next == snap->yy_accept_action) return LIME_TOK_ACCEPT;
            if (next == snap->yy_error_action || next == snap->yy_no_action) {
                return LIME_TOK_NONE;
            }
            uint16_t pushed = next;
            if (pushed > snap->yy_max_shift && pushed <= snap->yy_max_shiftreduce) {
                pushed = (uint16_t)(pushed
                            + (snap->yy_min_reduce - snap->yy_min_shiftreduce));
            }
            if (sp >= SCRATCH_MAX) return LIME_TOK_SHIFT;
            scratch[sp++] = pushed;
            continue;
        }
        return LIME_TOK_NONE;
    }
    return LIME_TOK_SHIFT; /* runaway guard -- never veto */
}

/*
** parse_token implementation.  Returns:
**   0  -- token consumed, parse continues
**   1  -- end-of-input token (0) accepted, parse complete
**  -1  -- syntax error / parse failure
*/
LIME_HOT
int parse_engine_step(struct ParseContext *ctx, int token_code, void *token_value, int location) {
    (void)token_value; /* The runtime engine is value-free; the */
    (void)location;    /* generator's typed entry handles those. */

    if (ctx == NULL || ctx->snapshot == NULL) return -1;

    /* Optional hook: when a grammar-context stack is attached, give
    ** it a chance to swap the bound snapshot before we look up the
    ** action.  The fast path is the LIME_UNLIKELY branch -- a single
    ** load + statically-predicted-not-taken branch when no stack is
    ** attached, which is the common case for single-grammar parsers.
    ** Cost on the no-stack path: ~1 ns / token. */
    if (LIME_UNLIKELY(ctx->context_stack != NULL)) {
        if (context_switch_needed(ctx->context_stack, token_code, NULL)) {
            if (grammar_context_is_root_only(ctx->context_stack)) {
                /* Token-code-based triggers only here; lexeme-based
                ** triggers fire from parse_token_lex(). */
                (void)grammar_context_detect_switch(ctx->context_stack, token_code, NULL, 0);
            } else {
                (void)context_switch_detect_exit(ctx->context_stack, token_code, NULL);
            }
            ParserSnapshot *top = grammar_context_current_snapshot(ctx->context_stack);
            if (top != NULL && top != ctx->snapshot) {
                ParserSnapshot *prev = ctx->snapshot;
                ctx->snapshot = snapshot_acquire(top);
                if (prev != NULL) snapshot_release(prev);
            }
        }
    }

    ParserSnapshot *snap = ctx->snapshot;

    if (snap->yy_action == NULL || snap->yy_default == NULL || snap->yy_rule_info_nrhs == NULL) {
        /* Snapshot wasn't built from a real parser. */
        return -1;
    }

    ParseEngine *eng = (ParseEngine *)ctx->engine;
    if (eng == NULL) {
        eng = calloc(1, sizeof(ParseEngine));
        if (eng == NULL) return -1;
        ctx->engine = eng;
    }

    if (!eng->initialised) {
        if (!stack_init(&eng->stack)) return -1;
        if (!stack_push(&eng->stack, 0, 0)) return -1; /* state 0 */
        eng->initialised = true;
    }

    if (eng->accepted) return 1;
    if (eng->errored) return -1;

    /* Convert external token code to internal action-table index.
    ** External and internal codes are paired: external = internal
    ** + yy_first_token.  When yy_first_token == 0 the subtraction
    ** is identity and the contract collapses to "callers pass
    ** internal codes" (the trivial case for grammars without
    ** %first_token).  EOF (0) is preserved.
    **
    ** Range check rejects out-of-bounds external codes (e.g. an
    ** ASCII character sneaking through to a parser declared with
    ** %first_token 258) cleanly with -1, instead of letting the
    ** action-table index go negative or run off the end.  v0.7.0
    ** simplification of the conditional dispatch added in v0.6.4
    ** for Lime-Letter-25. */
    int major = (token_code == 0)
                ? 0
                : token_code - (int)snap->yy_first_token;
    if (major < 0 || major >= (int)snap->yy_ntoken) {
        eng->errored = true;
        return -1;
    }
    bool retried_with_fallback = false;

    for (;;) {
        uint16_t cur_state = eng->stack.top[-1].state;

        /* If the top of stack is encoded as a pending reduce
        ** (shift-reduce), unpack and apply it before consuming the
        ** current lookahead.  This mirrors the early-return in
        ** yy_find_shift_action(). */
        if (cur_state >= snap->yy_min_reduce) {
            uint32_t ruleno = (uint32_t)(cur_state - snap->yy_min_reduce);
            int r = reduce(snap, &eng->stack, ruleno);
            if (r == 1) {
                eng->accepted = true;
                return token_code == 0 ? 1 : 0;
            }
            if (r < 0) {
                eng->errored = true;
                return -1;
            }
            continue;
        }

        uint16_t action = find_shift_action(snap, cur_state, (uint16_t)major);

        /* Plain shift: push (action, major), wait for next token. */
        if (action <= snap->yy_max_shift) {
            if (token_code == 0) {
                /* End-of-input shift means we're done if next reduce
                ** is the accept rule.  Treat as accept. */
                eng->accepted = true;
                return 1;
            }
            if (!stack_push(&eng->stack, action, major)) {
                eng->errored = true;
                return -1;
            }
            return 0;
        }

        /* Shift-reduce: push the action encoded as a pending reduce
        ** state (shift+reduce).  The next call to parse_token will
        ** see the encoded state, fall into the reduce branch above,
        ** and apply the reduction at that time -- consistent with
        ** the limpar.c convention where yy_shift adds
        ** (MIN_REDUCE - MIN_SHIFTREDUCE) to the new state when
        ** action > MAX_SHIFT. */
        if (action <= snap->yy_max_shiftreduce) {
            uint16_t encoded =
                (uint16_t)(action + (snap->yy_min_reduce - snap->yy_min_shiftreduce));
            if (!stack_push(&eng->stack, encoded, major)) {
                eng->errored = true;
                return -1;
            }
            return 0;
        }

        if (action == snap->yy_accept_action) {
            eng->accepted = true;
            return 1;
        }

        /* Reduce */
        if (action >= snap->yy_min_reduce && action != snap->yy_error_action &&
            action != snap->yy_no_action) {
            uint32_t ruleno = (uint32_t)(action - snap->yy_min_reduce);
            int r = reduce(snap, &eng->stack, ruleno);
            if (r == 1) {
                eng->accepted = true;
                return token_code == 0 ? 1 : 0;
            }
            if (r < 0) {
                eng->errored = true;
                return -1;
            }
            continue;
        }

        /* Error path -- try the fallback table once. */
        if (!retried_with_fallback && snap->yy_fallback != NULL && major >= 0 &&
            (uint32_t)major < snap->nfallback && snap->yy_fallback[major] != 0) {
            major = snap->yy_fallback[major];
            retried_with_fallback = true;
            continue;
        }

        eng->errored = true;
        return -1;
    }
}
