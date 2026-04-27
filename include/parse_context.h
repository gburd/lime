/*
** Parse context - manages parser state for a single parse session
**
** The parse context pins a snapshot for the duration of parsing,
** ensuring grammar stability even if extensions are modified.
*/
#ifndef PARSE_CONTEXT_H
#define PARSE_CONTEXT_H

#include "snapshot.h"

/* Forward declaration (full definition in parser.h) */
typedef struct ParseContext ParseContext;

/*
** Parse context structure
*/
struct ParseContext {
    ParserSnapshot *snapshot;   /* Pinned snapshot for this parse */
};

/*
** Create a parse context with the given snapshot.
** Acquires a reference to the snapshot.
*/
ParseContext *parse_context_create(ParserSnapshot *snap);

/*
** Destroy a parse context, releasing the snapshot.
*/
void parse_context_destroy(ParseContext *ctx);

/* ------------------------------------------------------------------ */
/*  parser.h wrappers                                                  */
/* ------------------------------------------------------------------ */

ParseContext *parse_begin(ParserSnapshot *snap);
void parse_end(ParseContext *ctx);
ParserSnapshot *parse_get_snapshot(ParseContext *ctx);
int parse_token(ParseContext *ctx, int token_code, void *token_value);

/* ------------------------------------------------------------------ */
/*  Snapshot action table lookup helpers                               */
/* ------------------------------------------------------------------ */

uint16_t snap_find_shift_action(const ParserSnapshot *snap,
                                uint16_t stateno,
                                uint16_t iLookAhead);
uint16_t snap_find_reduce_action(const ParserSnapshot *snap,
                                 uint16_t stateno,
                                 uint16_t iLookAhead);

#endif /* PARSE_CONTEXT_H */
