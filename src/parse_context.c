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
