/*
** Parse context implementation
**
** ParseContext manages a parse session, pinning a snapshot for the
** duration of parsing. This ensures the grammar remains stable even
** if extensions are loaded/unloaded by other threads.
*/
#include "parse_context.h"
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

int parse_token(ParseContext *ctx, int token_code, void *token_value) {
    (void)ctx;
    (void)token_code;
    (void)token_value;
    /* Stub: full implementation requires generated parser tables. */
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Snapshot action table lookup helpers                               */
/* ------------------------------------------------------------------ */

/*
** Look up a shift action from the snapshot's compact action tables.
** Mirrors the logic of yy_find_shift_action() in limpar.c but
** operates on the snapshot's dynamically-allocated arrays.
*/
uint16_t snap_find_shift_action(const ParserSnapshot *snap,
                                uint16_t stateno,
                                uint16_t iLookAhead) {
    if (snap == NULL || snap->yy_shift_ofst == NULL) return 0;
    if (stateno >= snap->nstate) return 0;

    int16_t ofst = snap->yy_shift_ofst[stateno];
    uint32_t idx = (uint32_t)((int32_t)ofst + (int32_t)iLookAhead);

    if (idx < snap->lookahead_count &&
        snap->yy_lookahead[idx] == iLookAhead) {
        return snap->yy_action[idx];
    }

    return snap->yy_default[stateno];
}

/*
** Look up a reduce action from the snapshot's compact action tables.
** Uses the reduce offset table; falls back to the default action when
** the offset is negative or the lookahead doesn't match.
*/
uint16_t snap_find_reduce_action(const ParserSnapshot *snap,
                                 uint16_t stateno,
                                 uint16_t iLookAhead) {
    if (snap == NULL || snap->yy_reduce_ofst == NULL) return 0;
    if (stateno >= snap->nstate) return 0;

    int16_t ofst = snap->yy_reduce_ofst[stateno];
    if (ofst < 0) {
        return snap->yy_default[stateno];
    }

    uint32_t idx = (uint32_t)((int32_t)ofst + (int32_t)iLookAhead);

    if (idx < snap->lookahead_count &&
        snap->yy_lookahead[idx] == iLookAhead) {
        return snap->yy_action[idx];
    }

    return snap->yy_default[stateno];
}
