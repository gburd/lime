/*
** Disambiguation framework implementation.
**
** Manages the lifecycle of disambiguation strategies and dispatches
** conflict resolution requests to the active strategy's vtable.
**
** The framework is strategy-agnostic: it creates the appropriate
** strategy context via the vtable's init() callback and delegates
** all resolution decisions to the strategy implementation.
*/
#include "disambiguation.h"
#include "extension.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal context structure                                          */
/* ------------------------------------------------------------------ */

struct DisambiguationContext {
    LimeStrategy strategy_type;
    DisambiguationStrategyVTable vtable;
    void *strategy_context;             /* Opaque state from vtable.init() */
    struct ExtensionRegistry *registry; /* Borrowed reference              */
};

/* ------------------------------------------------------------------ */
/*  Built-in strategy vtable declarations                              */
/* ------------------------------------------------------------------ */

/* Defined in strategy_priority.c */
extern const DisambiguationStrategyVTable strategy_priority_vtable;

/* Defined in strategy_fork_resolve.c */
extern const DisambiguationStrategyVTable strategy_fork_resolve_vtable;

/* Placeholder vtables for strategies not yet implemented.
** These return "unresolved" for every conflict. */
static void *stub_init(const Extension *const *extensions, uint32_t n) {
    (void)extensions;
    (void)n;
    return (void *)(uintptr_t)1; /* Non-NULL sentinel */
}

static bool stub_resolve(void *ctx, const ConflictPoint *conflict, struct ParseContext *parse_ctx,
                         int lookahead, StrategyResult *result) {
    (void)ctx;
    (void)conflict;
    (void)parse_ctx;
    (void)lookahead;
    strategy_result_init(result);
    return false; /* Could not resolve */
}

static void stub_update(void *ctx, struct ExtensionRegistry *reg, bool success) {
    (void)ctx;
    (void)reg;
    (void)success;
}

static void stub_destroy(void *ctx) {
    (void)ctx;
}

static const DisambiguationStrategyVTable stub_vtable = {
    .init = stub_init,
    .resolve = stub_resolve,
    .update = stub_update,
    .destroy = stub_destroy,
};

/* ------------------------------------------------------------------ */
/*  Strategy result helpers                                             */
/* ------------------------------------------------------------------ */

void strategy_result_init(StrategyResult *result) {
    if (result == NULL) return;
    result->winning_contexts = NULL;
    result->nwinners = 0;
    result->confidence = 0.0f;
    result->explanation = NULL;
}

void strategy_result_cleanup(StrategyResult *result) {
    if (result == NULL) return;
    free(result->winning_contexts);
    result->winning_contexts = NULL;
    result->nwinners = 0;
    free(result->explanation);
    result->explanation = NULL;
    result->confidence = 0.0f;
}

/* ------------------------------------------------------------------ */
/*  Vtable lookup for built-in strategies                              */
/* ------------------------------------------------------------------ */

static const DisambiguationStrategyVTable *get_builtin_vtable(LimeStrategy strategy) {
    switch (strategy) {
    case STRAT_PRIORITY:
        return &strategy_priority_vtable;
    case STRAT_FORK_RESOLVE:
        return &strategy_fork_resolve_vtable;
    case STRAT_BAYESIAN:
    case STRAT_LLM:
        /* Not yet implemented -- return stub */
        return &stub_vtable;
    case STRAT_CUSTOM:
        /* STRAT_CUSTOM requires user vtable via disambiguation_create_custom */
        return NULL;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Gather loaded extensions from registry                             */
/* ------------------------------------------------------------------ */

/*
** Collect pointers to all loaded extensions into a temporary array.
** Caller must free the returned array.  Sets *count_out.
** Returns NULL on allocation failure or if no extensions are loaded.
*/
static const Extension **gather_loaded_extensions(ExtensionRegistry *reg, uint32_t *count_out) {
    *count_out = 0;
    if (reg == NULL) return NULL;

    uint32_t loaded = get_loaded_extension_count(reg);
    if (loaded == 0) return NULL;

    const Extension **arr = malloc(loaded * sizeof(const Extension *));
    if (arr == NULL) return NULL;

    /* Walk the registry to collect loaded extensions.
    ** We iterate through IDs starting from 1. This is a simplification
    ** that works because IDs are assigned sequentially. */
    uint32_t found = 0;
    for (uint32_t id = 1; found < loaded && id < 10000; id++) {
        const Extension *ext = find_extension(reg, id);
        if (ext != NULL && ext->state == EXT_LOADED) {
            arr[found++] = ext;
        }
    }
    *count_out = found;
    return arr;
}

/* ------------------------------------------------------------------ */
/*  Public API: creation                                               */
/* ------------------------------------------------------------------ */

DisambiguationContext *disambiguation_create(LimeStrategy strategy, ExtensionRegistry *reg) {
    const DisambiguationStrategyVTable *vt = get_builtin_vtable(strategy);
    if (vt == NULL) return NULL;

    DisambiguationContext *ctx = calloc(1, sizeof(DisambiguationContext));
    if (ctx == NULL) return NULL;

    ctx->strategy_type = strategy;
    ctx->vtable = *vt;
    ctx->registry = reg;

    /* Gather loaded extensions and call strategy init */
    uint32_t nextensions = 0;
    const Extension **extensions = gather_loaded_extensions(reg, &nextensions);

    ctx->strategy_context = ctx->vtable.init(extensions, nextensions);
    free(extensions);

    if (ctx->strategy_context == NULL) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

DisambiguationContext *disambiguation_create_custom(const DisambiguationStrategyVTable *vtable,
                                                    ExtensionRegistry *reg) {
    if (vtable == NULL) return NULL;
    if (vtable->init == NULL || vtable->resolve == NULL || vtable->destroy == NULL) {
        return NULL;
    }

    DisambiguationContext *ctx = calloc(1, sizeof(DisambiguationContext));
    if (ctx == NULL) return NULL;

    ctx->strategy_type = STRAT_CUSTOM;
    ctx->vtable = *vtable;
    ctx->registry = reg;

    /* Gather loaded extensions and call strategy init */
    uint32_t nextensions = 0;
    const Extension **extensions = gather_loaded_extensions(reg, &nextensions);

    ctx->strategy_context = ctx->vtable.init(extensions, nextensions);
    free(extensions);

    if (ctx->strategy_context == NULL) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

/* ------------------------------------------------------------------ */
/*  Public API: resolution                                             */
/* ------------------------------------------------------------------ */

StrategyResult disambiguation_resolve(DisambiguationContext *ctx, const ConflictPoint *conflict,
                                      struct ParseContext *parse_ctx) {
    StrategyResult result;
    strategy_result_init(&result);

    if (ctx == NULL || conflict == NULL) return result;

    int lookahead = (int)conflict->token;
    ctx->vtable.resolve(ctx->strategy_context, conflict, parse_ctx, lookahead, &result);
    return result;
}

void disambiguation_update(DisambiguationContext *ctx, bool success) {
    if (ctx == NULL) return;
    if (ctx->vtable.update != NULL) {
        ctx->vtable.update(ctx->strategy_context, ctx->registry, success);
    }
}

/* ------------------------------------------------------------------ */
/*  Public API: introspection                                          */
/* ------------------------------------------------------------------ */

LimeStrategy disambiguation_get_strategy(const DisambiguationContext *ctx) {
    if (ctx == NULL) return STRAT_PRIORITY;
    return ctx->strategy_type;
}

const char *disambiguation_strategy_name(LimeStrategy strategy) {
    switch (strategy) {
    case STRAT_PRIORITY:
        return "priority";
    case STRAT_FORK_RESOLVE:
        return "fork-resolve";
    case STRAT_BAYESIAN:
        return "bayesian";
    case STRAT_LLM:
        return "llm";
    case STRAT_CUSTOM:
        return "custom";
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/*  Public API: destruction                                            */
/* ------------------------------------------------------------------ */

void disambiguation_destroy(DisambiguationContext *ctx) {
    if (ctx == NULL) return;
    if (ctx->vtable.destroy != NULL) {
        ctx->vtable.destroy(ctx->strategy_context);
    }
    free(ctx);
}
