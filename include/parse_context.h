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

/*
** Feed a token to the push parser.
**
**   ctx         Active parse context.
**   token_code  Token code (0 for end-of-input).
**   token_value Semantic value (may be NULL).
**   location    Byte offset of the token in the source, or
**               LIME_LOC_UNKNOWN if the grammar does not declare
**               %locations or the caller does not track positions.
**
** Returns 0 on success, non-zero on parse error.
*/
int parse_token(ParseContext *ctx,
                int token_code,
                void *token_value,
                int location);

/*
** Sentinel value for `location` callers who do not track positions
** (or who cannot attribute a position to a given token, e.g. an
** injected end-of-input marker).  Guaranteed to be -1 so that
** existing code that passed an integer offset happens to be
** forward-compatible when offsets are always >= 0.
*/
#define LIME_LOC_UNKNOWN (-1)

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
