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

/* Reset hook implemented in parse_engine.c -- preserves the engine's
** stack buffer while clearing per-parse state, so the thread-local
** pool can recycle a ParseContext without paying malloc/free on the
** stack region. */
extern void parse_engine_reset(ParseContext *ctx);

/*
** Thread-local ParseContext pool.
**
** Every parse_begin / parse_end pair previously did:
**
**   parse_begin:  malloc(ParseContext)              [24 bytes]
**                 snapshot_acquire (atomic_fetch_add)
**                 -- first parse_token --
**                 calloc(ParseEngine)                 [~50 bytes]
**                 malloc(ParseStack base buffer)      [~512 bytes]
**
**   parse_end:    free(ParseStack base buffer)
**                 free(ParseEngine)
**                 snapshot_release (atomic_fetch_sub)
**                 free(ParseContext)
**
** That is 3 malloc + 3 free + 2 atomics PER PARSE.  At ~50 ns per
** alloc/free and ~30 ns per atomic on x86_64, allocator+atomic overhead
** dominates short-parse workloads -- bench/bench_parse_fanout shows
** ~380 ns/parse single-threaded for a 21-token input where the actual
** parser-engine loop is ~120 ns.
**
** The pool is one ParseContext slot per OS thread.  The first parse on
** a thread allocates fresh; subsequent parses reuse the pooled context.
** Engine stack buffer grows monotonically -- the reset path preserves
** the largest-seen capacity so deep grammars don't pay realloc churn.
**
** Cleanup: pthread_key_t with a destructor frees the pool on thread
** exit (ASAN/leak-clean), without requiring callers to call
** parse_context_pool_drain() explicitly.  The pthread_key is
** lazily initialised on the first parse_begin via pthread_once().
*/
#include <pthread.h>

static pthread_key_t g_pool_key;
static pthread_once_t g_pool_key_once = PTHREAD_ONCE_INIT;

static void pool_slot_destructor(void *p) {
    ParseContext *ctx = (ParseContext *)p;
    if (ctx == NULL) return;
    extern void parse_engine_drop(ParseContext *);
    parse_engine_drop(ctx);
    if (ctx->snapshot != NULL) {
        snapshot_release(ctx->snapshot);
    }
    free(ctx);
}

static void pool_key_init(void) {
    pthread_key_create(&g_pool_key, pool_slot_destructor);
}

/*
** Public: drop the calling thread's pooled ParseContext.
*/
void parse_context_pool_drain(void) {
    pthread_once(&g_pool_key_once, pool_key_init);
    ParseContext *ctx = (ParseContext *)pthread_getspecific(g_pool_key);
    if (ctx != NULL) {
        pool_slot_destructor(ctx);
        pthread_setspecific(g_pool_key, NULL);
    }
}

/*
** Create a new parse context, acquiring a reference to the given snapshot.
**
** Hot path: pop a recycled context from the thread-local pool, replace
** its snapshot ref, return.  Cold path (first call on this thread, or
** after a drain): malloc fresh.
*/
ParseContext *parse_context_create(ParserSnapshot *snap) {
    if (snap == NULL) {
        return NULL;
    }

    ParseContext *ctx = NULL;
    pthread_once(&g_pool_key_once, pool_key_init);
    ctx = (ParseContext *)pthread_getspecific(g_pool_key);
    if (ctx != NULL) {
        /* Hot path: reuse the pooled context.  Engine + stack are
        ** already allocated; reset them to a fresh-parse state. */
        pthread_setspecific(g_pool_key, NULL);
        ctx->snapshot = snapshot_acquire(snap);
        if (ctx->snapshot == NULL) {
            /* Snapshot acquire failed (NULL passed in earlier path,
            ** but defensive).  Free and bail. */
            extern void parse_engine_drop(ParseContext *);
            parse_engine_drop(ctx);
            free(ctx);
            return NULL;
        }
        ctx->context_stack = NULL;
        parse_engine_reset(ctx);
        return ctx;
    }

    /* Cold path: first parse on this thread.  Allocate fresh. */
    ctx = malloc(sizeof(ParseContext));
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
**
** Hot path: release the snapshot, push the context back to the
** thread-local pool (preserving its engine + stack buffer for the
** next parse_begin).  Cold path: if the pool slot is occupied,
** actually free this context.
*/
void parse_context_destroy(ParseContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    if (ctx->snapshot != NULL) {
        snapshot_release(ctx->snapshot);
        ctx->snapshot = NULL;
    }

    /* Don't free attached grammar-context stack -- it's borrowed.
    ** Just drop our pointer so the pool slot doesn't leak it. */
    ctx->context_stack = NULL;

    if (pthread_getspecific(g_pool_key) == NULL) {
        /* Hot path: hand off to the pool for reuse. */
        pthread_setspecific(g_pool_key, ctx);
        return;
    }

    /* Cold path: pool slot already filled (rare -- only happens if
    ** caller nests parse_begin/end somehow).  Actually free this one. */
    extern void parse_engine_drop(ParseContext *);
    parse_engine_drop(ctx);
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
