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

#endif /* PARSE_CONTEXT_H */
