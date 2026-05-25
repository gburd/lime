/*
** Parse context implementation
**
** ParseContext manages a parse session, pinning a snapshot for the
** duration of parsing. This ensures the grammar remains stable even
** if extensions are loaded/unloaded by other threads.
*/
#include "parse_context.h"
#include "grammar_context.h"
#include "snapshot.h"

#include <stdlib.h>

/*
** Create a new parse context, acquiring a reference to the given snapshot.
*/
ParseContext *parse_context_create(ParserSnapshot *snap) {
    if (snap == NULL) {
        return NULL;
    }

    ParseContext *ctx = malloc(sizeof(ParseContext));
    if (ctx == NULL) {
        return NULL;
    }

    ctx->snapshot = snapshot_acquire(snap);
    ctx->engine = NULL;  /* Allocated lazily by parse_engine_step. */
    ctx->context_stack = NULL; /* No grammar boundary detection by default. */
    if (ctx->snapshot == NULL) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

/*
** Destroy a parse context, releasing its snapshot reference.
*/
void parse_context_destroy(ParseContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    /* Drop any parse-engine state attached to this context. */
    extern void parse_engine_drop(ParseContext *);
    parse_engine_drop(ctx);

    if (ctx->snapshot != NULL) {
        snapshot_release(ctx->snapshot);
    }

    free(ctx);
}

/* ------------------------------------------------------------------ */
/*  parser.h wrappers: parse_begin / parse_end / parse_get_snapshot   */
/* ------------------------------------------------------------------ */

ParseContext *parse_begin(ParserSnapshot *snap) {
    return parse_context_create(snap);
}

void parse_end(ParseContext *ctx) {
    parse_context_destroy(ctx);
}

ParserSnapshot *parse_get_snapshot(ParseContext *ctx) {
    if (ctx == NULL) return NULL;
    return ctx->snapshot;
}

int parse_token(ParseContext *ctx, int token_code, void *token_value, int location) {
    /* Drive the runtime LALR(1) push-parser engine.  The engine
    ** operates on the snapshot's action tables and rule metadata
    ** (populated either by snapshot_build_from_tables() from a
    ** generated parser or by create_modified_snapshot() after an
    ** extension applies modifications). */
    extern int parse_engine_step(ParseContext *, int, void *, int);
    return parse_engine_step(ctx, token_code, token_value, location);
}

/* ------------------------------------------------------------------ */
/*  Grammar-context boundary detection (optional)                      */
/* ------------------------------------------------------------------ */

void parse_attach_context_stack(ParseContext *ctx, GrammarContextStack *stack) {
    if (ctx == NULL) return;
    ctx->context_stack = stack;
    /* If a stack is attached and the current snapshot doesn't match
    ** the stack's current top, sync them so the engine sees a
    ** consistent view.  We don't reset the engine state here -- the
    ** caller is responsible for ordering the attach with respect to
    ** any in-flight parse. */
    if (stack != NULL) {
        ParserSnapshot *top = grammar_context_current_snapshot(stack);
        if (top != NULL && top != ctx->snapshot) {
            ParserSnapshot *prev = ctx->snapshot;
            ctx->snapshot = snapshot_acquire(top);
            if (prev != NULL) snapshot_release(prev);
        }
    }
}

int parse_token_lex(ParseContext *ctx, int token_code, void *token_value, const char *lexeme,
                    int location) {
    if (ctx == NULL) return -1;

    /* Host-side hook: when a context stack is attached, run the
    ** trigger detection BEFORE stepping the engine.  When the engine
    ** is mid-parse on the current grammar this swaps ctx->snapshot
    ** to the embedded grammar; the caller is responsible for
    ** orchestrating a fresh sub-parse for the embedded region. */
    if (ctx->context_stack != NULL &&
        context_switch_needed(ctx->context_stack, token_code, lexeme)) {
        if (grammar_context_is_root_only(ctx->context_stack)) {
            /* Try a switch-into-embedded.  The 0 offset is a
            ** placeholder; callers that need offsets should reach
            ** for grammar_context_detect_switch directly. */
            (void)grammar_context_detect_switch(ctx->context_stack, token_code, lexeme, 0);
        } else {
            (void)context_switch_detect_exit(ctx->context_stack, token_code, lexeme);
        }
        ParserSnapshot *top = grammar_context_current_snapshot(ctx->context_stack);
        if (top != NULL && top != ctx->snapshot) {
            ParserSnapshot *prev = ctx->snapshot;
            ctx->snapshot = snapshot_acquire(top);
            if (prev != NULL) snapshot_release(prev);
        }
    }

    extern int parse_engine_step(ParseContext *, int, void *, int);
    return parse_engine_step(ctx, token_code, token_value, location);
}

/* ------------------------------------------------------------------ */
/*  Snapshot action table lookup helpers                               */
/* ------------------------------------------------------------------ */

/*
** Look up a shift action from the snapshot's compact action tables.
** Mirrors the logic of yy_find_shift_action() in limpar.c but
** operates on the snapshot's dynamically-allocated arrays.
*/
uint16_t snap_find_shift_action(const ParserSnapshot *snap, uint16_t stateno, uint16_t iLookAhead) {
    if (snap == NULL || snap->yy_shift_ofst == NULL) return 0;
    if (stateno >= snap->nstate) return 0;

    int32_t ofst = snap->yy_shift_ofst[stateno];
    uint32_t idx = (uint32_t)(ofst + (int32_t)iLookAhead);

    if (idx < snap->lookahead_count && snap->yy_lookahead[idx] == iLookAhead) {
        return snap->yy_action[idx];
    }

    return snap->yy_default[stateno];
}

/*
** Look up a reduce action from the snapshot's compact action tables.
** Uses the reduce offset table; falls back to the default action when
** the offset is negative or the lookahead doesn't match.
*/
uint16_t snap_find_reduce_action(const ParserSnapshot *snap, uint16_t stateno,
                                 uint16_t iLookAhead) {
    if (snap == NULL || snap->yy_reduce_ofst == NULL) return 0;
    if (stateno >= snap->nstate) return 0;

    int32_t ofst = snap->yy_reduce_ofst[stateno];
    if (ofst < 0) {
        return snap->yy_default[stateno];
    }

    uint32_t idx = (uint32_t)(ofst + (int32_t)iLookAhead);

    if (idx < snap->lookahead_count && snap->yy_lookahead[idx] == iLookAhead) {
        return snap->yy_action[idx];
    }

    return snap->yy_default[stateno];
}
