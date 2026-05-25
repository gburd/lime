/*
** parse_glr.c -- public entry points for GLR parsing.
**
** This is the user-facing API surface that lifts the GLR engine in
** src/glr.c into the same conceptual world as the LALR push parser
** (src/parse_engine.c, src/parse_context.c).  The LALR fast path is
** intentionally untouched: parse_token() does not know about GLR at
** all and pays no overhead for callers that never enter GLR mode.
**
** Lifecycle:
**
**    ParseContext *ctx = parse_begin(snap);
**    if (lime_parse_glr(ctx, my_disambig, my_data) != 0) ...
**    while (have_more_tokens) {
**        int rc = lime_parse_glr_feed(ctx, token);
**        if (rc < 0) break;            // -1: ambiguity, -2: dead
**    }
**    bool ok = lime_parse_glr_accepted(ctx);
**    parse_end(ctx);
**
** Internally the GLR parser lives in a side struct hung off the
** ParseContext via a small registry.  We deliberately do NOT extend
** the ParseContext layout itself, because that would force the
** struct's public layout (used by parse_engine.c hot paths) to
** churn on a feature most users will never enable.
*/

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"
#include "glr.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* GLR side-band storage                                              */
/* ------------------------------------------------------------------ */

/*
** Each ParseContext that has been switched into GLR mode owns a
** GLRBinding.  We keep these in a small lock-protected linked list
** rather than embedding a pointer in ParseContext, so the LALR fast
** path's struct layout stays byte-identical to before this feature
** existed.  Lookup is O(N_active_glr_sessions); in practice that
** number is tiny (one per concurrent GLR parse) and the lookup only
** happens on the GLR path, never in parse_token.
*/
typedef struct GLRBinding {
    ParseContext *ctx;
    GLRParser *parser;
    struct GLRBinding *next;
} GLRBinding;

static pthread_mutex_t g_glr_mu = PTHREAD_MUTEX_INITIALIZER;
static GLRBinding *g_glr_bindings = NULL;

static GLRBinding *glr_find_locked(ParseContext *ctx) {
    for (GLRBinding *b = g_glr_bindings; b; b = b->next) {
        if (b->ctx == ctx) return b;
    }
    return NULL;
}

static GLRBinding *glr_find(ParseContext *ctx) {
    pthread_mutex_lock(&g_glr_mu);
    GLRBinding *b = glr_find_locked(ctx);
    pthread_mutex_unlock(&g_glr_mu);
    return b;
}

/*
** parse_glr_drop_for_context
** Internal hook used by parse_end()/parse_context_destroy() to clean
** up any GLR binding when the underlying context is torn down.  The
** linkage is by weak symbol: parse_context.c calls this through a
** lookup-or-noop pointer so the runtime library can be linked
** without parse_glr.o (see src/meson.build comment).
**
** Currently we rely on the user calling lime_parse_glr_end() before
** parse_end(); this entry point exists so a future change can wire
** parse_context_destroy() to it without API churn.
*/
void parse_glr_drop_for_context(ParseContext *ctx) {
    if (!ctx) return;
    pthread_mutex_lock(&g_glr_mu);
    GLRBinding **link = &g_glr_bindings;
    while (*link) {
        if ((*link)->ctx == ctx) {
            GLRBinding *gone = *link;
            *link = gone->next;
            pthread_mutex_unlock(&g_glr_mu);
            glr_parser_destroy(gone->parser);
            free(gone);
            return;
        }
        link = &(*link)->next;
    }
    pthread_mutex_unlock(&g_glr_mu);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int lime_parse_glr(ParseContext *ctx, GLRDisambiguateFn disambig, void *user_data) {
    if (!ctx || !ctx->snapshot) return -2;

    pthread_mutex_lock(&g_glr_mu);
    GLRBinding *existing = glr_find_locked(ctx);
    if (existing) {
        /* Already in GLR mode: just update the callback. */
        glr_parser_set_disambiguate(existing->parser, disambig, user_data);
        pthread_mutex_unlock(&g_glr_mu);
        return 0;
    }
    pthread_mutex_unlock(&g_glr_mu);

    GLRParser *parser = glr_parser_create(0, 0);
    if (!parser) return -2;

    glr_parser_set_disambiguate(parser, disambig, user_data);

    GLRBinding *b = (GLRBinding *)calloc(1, sizeof(GLRBinding));
    if (!b) {
        glr_parser_destroy(parser);
        return -2;
    }
    b->ctx = ctx;
    b->parser = parser;

    pthread_mutex_lock(&g_glr_mu);
    /* Re-check under lock in case another thread raced us. */
    if (glr_find_locked(ctx) != NULL) {
        pthread_mutex_unlock(&g_glr_mu);
        glr_parser_destroy(parser);
        free(b);
        return 0;
    }
    b->next = g_glr_bindings;
    g_glr_bindings = b;
    pthread_mutex_unlock(&g_glr_mu);

    return 0;
}

int lime_parse_glr_feed(ParseContext *ctx, uint16_t token) {
    if (!ctx) return -2;
    GLRBinding *b = glr_find(ctx);
    if (!b) return -2;
    return glr_parser_feed(b->parser, ctx->snapshot, token);
}

bool lime_parse_glr_accepted(ParseContext *ctx) {
    if (!ctx || !ctx->snapshot) return false;
    GLRBinding *b = glr_find(ctx);
    if (!b) return false;
    return glr_parser_accepted(b->parser, ctx->snapshot->yy_accept_action);
}

uint32_t lime_parse_glr_head_count(ParseContext *ctx) {
    if (!ctx) return 0;
    GLRBinding *b = glr_find(ctx);
    if (!b) return 0;
    return glr_parser_head_count(b->parser);
}

void lime_parse_glr_end(ParseContext *ctx) {
    parse_glr_drop_for_context(ctx);
}
