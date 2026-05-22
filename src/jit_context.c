/*
** JIT context management for the extensible SQL parser.
**
** This file implements the JITContext lifecycle: creation, destruction,
** and snapshot integration using LLVM's OrcJIT (LLJIT) infrastructure.
** The actual LLVM IR generation lives in jit_codegen.c.
**
** OrcJIT replaces the deprecated MCJIT engine and provides:
**   - Thread-safe contexts for concurrent compilation
**   - Lazy compilation support (not yet used)
**   - Better resource management and error handling
**
** When compiled without LLVM (LIME_NO_JIT defined), all functions
** degrade to stubs that return appropriate error codes or no-ops.
*/
#include "jit_context.h"
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LIME_NO_JIT

#include <llvm-c/Core.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Orc.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include "jit_llvm_compat.h"

/* ------------------------------------------------------------------ */
/*  JIT context internal structure                                     */
/* ------------------------------------------------------------------ */

struct JITContext {
    LLVMOrcLLJITRef lljit;              /* OrcJIT LLJIT instance           */
    LLVMContextRef llvm_ctx;            /* Bare LLVM context (for IR gen)  */
    LLVMOrcThreadSafeContextRef ts_ctx; /* Thread-safe wrapper of llvm_ctx */

    void *parse_sequence_fn;    /* Monolithic jit_parse_sequence fn */
    uint32_t nstates;           /* Number of states                 */
    uint32_t *state_hit_counts; /* Per-state hit counting           */
    uint32_t nstate_functions;  /* Number of per-state functions    */

    JITStats stats; /* Compilation statistics           */
};

/* One-time LLVM initialization flag */
static int llvm_initialized = 0;

static JITStatus ensure_llvm_initialized(void) {
    if (llvm_initialized) return JIT_OK;

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    llvm_initialized = 1;
    return JIT_OK;
}

/* Helper to convert LLVMErrorRef to a status code, disposing the error */
static JITStatus consume_llvm_error(LLVMErrorRef err, JITStatus fallback) {
    if (err == LLVMErrorSuccess) return JIT_OK;
    char *msg = LLVMGetErrorMessage(err);
    (void)msg; /* Could log this in debug builds */
    LLVMDisposeErrorMessage(msg);
    return fallback;
}

/* ------------------------------------------------------------------ */
/*  Public API (LLVM-enabled build)                                    */
/* ------------------------------------------------------------------ */

JITStatus jit_create(JITContext **ctx_out) {
    if (ctx_out == NULL) return JIT_ERR_INVALID_ARG;
    *ctx_out = NULL;

    JITStatus st = ensure_llvm_initialized();
    if (st != JIT_OK) return st;

    JITContext *ctx = calloc(1, sizeof(JITContext));
    if (ctx == NULL) return JIT_ERR_INIT_FAILED;

    /* Create a ThreadSafeContext and fetch its internal LLVMContext.
    ** The compat shim papers over the LLVM 14 (ts_ctx-first) vs
    ** LLVM 15+ (externally-created context) API split; in both cases
    ** ts_ctx owns llvm_ctx, so only ts_ctx is disposed at teardown. */
    if (!lime_jit_create_ts_ctx(&ctx->ts_ctx, &ctx->llvm_ctx)) {
        free(ctx);
        return JIT_ERR_INIT_FAILED;
    }

    /* Build the LLJIT instance */
    LLVMOrcLLJITBuilderRef builder = LLVMOrcCreateLLJITBuilder();
    LLVMErrorRef err = LLVMOrcCreateLLJIT(&ctx->lljit, builder);
    if (err != LLVMErrorSuccess) {
        consume_llvm_error(err, JIT_ERR_INIT_FAILED);
        LLVMOrcDisposeThreadSafeContext(ctx->ts_ctx);
        free(ctx);
        return JIT_ERR_INIT_FAILED;
    }

    ctx->stats.available = true;
    *ctx_out = ctx;
    return JIT_OK;
}

void jit_destroy(JITContext *ctx) {
    if (ctx == NULL) return;

    /* parse_sequence_fn is a function pointer inside JIT memory - don't free it */

    free(ctx->state_hit_counts);

    /* Dispose the LLJIT instance (owns all JIT'd code and modules) */
    if (ctx->lljit != NULL) {
        LLVMErrorRef err = LLVMOrcDisposeLLJIT(ctx->lljit);
        if (err != LLVMErrorSuccess) {
            consume_llvm_error(err, JIT_ERR_COMPILE_FAILED);
        }
    }

    /* Dispose the thread-safe context after LLJIT (it may reference it) */
    if (ctx->ts_ctx != NULL) {
        LLVMOrcDisposeThreadSafeContext(ctx->ts_ctx);
    }

    free(ctx);
}

JITStats jit_get_stats(const JITContext *ctx) {
    if (ctx == NULL) {
        JITStats empty = { 0 };
        return empty;
    }
    return ctx->stats;
}

bool jit_is_available(void) {
    return ensure_llvm_initialized() == JIT_OK;
}

/* Defined in jit_codegen.c -- OrcJIT-compatible entry point that
** generates IR into an externally-provided module rather than
** relying on the JITContext's internal module pointer. */
extern JITStatus jit_codegen_generate_into(LLVMContextRef llvm_ctx, LLVMModuleRef module,
                                           const ParserSnapshot *snap, JITStats *stats_out);

JITStatus jit_compile_snapshot(JITContext *ctx, const ParserSnapshot *snap) {
    if (ctx == NULL || snap == NULL) return JIT_ERR_INVALID_ARG;
    if (snap->yy_action == NULL || snap->nstate == 0) return JIT_ERR_INVALID_ARG;

    /* Use the bare LLVM context (stored alongside ts_ctx) for IR generation */
    LLVMContextRef llvm_ctx = ctx->llvm_ctx;

    /* Create a module in the thread-safe context */
    LLVMModuleRef module = LLVMModuleCreateWithNameInContext("lime_jit", llvm_ctx);
    if (module == NULL) return JIT_ERR_CODEGEN_FAILED;

    /* Generate LLVM IR for all states using the codegen module.
    ** jit_codegen_generate needs access to ctx fields, so we temporarily
    ** stash the module pointer. The codegen uses ctx->llvm_ctx and ctx->module
    ** through its own copy of the JITContext struct layout. We use the
    ** OrcJIT-aware codegen entry point instead. */
    JITStatus st = jit_codegen_generate_into(llvm_ctx, module, snap, &ctx->stats);
    if (st != JIT_OK) {
        LLVMDisposeModule(module);
        return st;
    }

    /* Verify the module */
    char *error = NULL;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &error)) {
        LLVMDisposeMessage(error);
        LLVMDisposeModule(module);
        return JIT_ERR_CODEGEN_FAILED;
    }
    LLVMDisposeMessage(error);

    /* Run optimization passes (compat shim picks PassBuilder on
    ** LLVM 16+ or legacy PassManagerBuilder on LLVM 14-15). */
    LIME_JIT_RUN_O2_PASSES(module);

    /* Wrap module in a thread-safe module for OrcJIT submission */
    LLVMOrcThreadSafeModuleRef ts_mod = LLVMOrcCreateNewThreadSafeModule(module, ctx->ts_ctx);
    /* Note: ts_mod now owns module; do not dispose module separately */

    /* Add the module to the LLJIT main JITDylib */
    LLVMOrcJITDylibRef jd = LLVMOrcLLJITGetMainJITDylib(ctx->lljit);
    LLVMErrorRef err = LLVMOrcLLJITAddLLVMIRModule(ctx->lljit, jd, ts_mod);
    if (err != LLVMErrorSuccess) {
        /* ts_mod is consumed even on error by some LLVM versions,
        ** but we attempt cleanup just in case */
        return consume_llvm_error(err, JIT_ERR_COMPILE_FAILED);
    }

    /* Look up the monolithic parse function via OrcJIT.  LimeJitAddress
    ** resolves to LLVMOrcExecutorAddress on LLVM 15+ and to
    ** LLVMOrcJITTargetAddress on LLVM 14 -- both are uint64_t. */
    LimeJitAddress addr = 0;
    err = LLVMOrcLLJITLookup(ctx->lljit, &addr, "jit_parse_sequence");
    if (err != LLVMErrorSuccess || addr == 0) {
        if (err != LLVMErrorSuccess) {
            consume_llvm_error(err, JIT_ERR_LOOKUP_FAILED);
        }
        return JIT_ERR_LOOKUP_FAILED;
    }

    /* Store function pointer in context */
    ctx->parse_sequence_fn = (void *)(uintptr_t)addr;
    ctx->nstates = snap->nstate;
    ctx->stats.states_compiled = snap->nstate;
    ctx->stats.states_total = snap->nstate;

    /* Allocate per-state hit counters for warmup tracking */
    ctx->state_hit_counts = calloc(snap->nstate, sizeof(uint32_t));

    return JIT_OK;
}

JITShiftActionFn jit_get_shift_action(const JITContext *ctx, uint32_t state_id) {
    /* No longer used in monolithic JIT approach - per-state functions not generated */
    (void)ctx;
    (void)state_id;
    return NULL;
}

JITStatus jit_warmup(JITContext *ctx, const uint32_t *hot_states, uint32_t n) {
    if (ctx == NULL) return JIT_ERR_INVALID_ARG;
    if (hot_states == NULL || n == 0) return JIT_OK;

    /* Record hot state hits for future per-state compilation.
    ** Currently the monolithic function covers all states, so warmup
    ** just records which states are hot for potential future use
    ** (e.g., tiered compilation where only hot states get extra
    ** optimization). */
    if (ctx->state_hit_counts == NULL || ctx->nstates == 0) {
        return JIT_OK; /* No snapshot compiled yet */
    }

    for (uint32_t i = 0; i < n; i++) {
        if (hot_states[i] < ctx->nstates) {
            ctx->state_hit_counts[hot_states[i]]++;
        }
    }

    return JIT_OK;
}

#else /* LIME_NO_JIT -- stub implementations */

struct JITContext {
    JITStats stats;
};

JITStatus jit_create(JITContext **ctx_out) {
    if (ctx_out == NULL) return JIT_ERR_INVALID_ARG;
    *ctx_out = NULL;
    return JIT_ERR_NO_LLVM;
}

void jit_destroy(JITContext *ctx) {
    free(ctx);
}

JITStatus jit_compile_snapshot(JITContext *ctx, const ParserSnapshot *snap) {
    (void)ctx;
    (void)snap;
    return JIT_ERR_NO_LLVM;
}

JITShiftActionFn jit_get_shift_action(const JITContext *ctx, uint32_t state_id) {
    (void)ctx;
    (void)state_id;
    return NULL;
}

JITStats jit_get_stats(const JITContext *ctx) {
    (void)ctx;
    JITStats empty = { 0 };
    return empty;
}

bool jit_is_available(void) {
    return false;
}

JITStatus jit_warmup(JITContext *ctx, const uint32_t *hot_states, uint32_t n) {
    (void)ctx;
    (void)hot_states;
    (void)n;
    return JIT_ERR_NO_LLVM;
}

#endif /* LIME_NO_JIT */

/* ------------------------------------------------------------------ */
/*  Status string (always available)                                   */
/* ------------------------------------------------------------------ */

const char *jit_status_string(JITStatus status) {
    switch (status) {
    case JIT_OK:
        return "OK";
    case JIT_ERR_NO_LLVM:
        return "LLVM not available";
    case JIT_ERR_INIT_FAILED:
        return "LLVM initialization failed";
    case JIT_ERR_CODEGEN_FAILED:
        return "code generation failed";
    case JIT_ERR_COMPILE_FAILED:
        return "JIT compilation failed";
    case JIT_ERR_LOOKUP_FAILED:
        return "symbol lookup failed";
    case JIT_ERR_INVALID_ARG:
        return "invalid argument";
    case JIT_ERR_ALREADY_COMPILED:
        return "already compiled";
    }
    return "unknown JIT error";
}

/* ------------------------------------------------------------------ */
/*  Snapshot integration helpers                                       */
/* ------------------------------------------------------------------ */

JITStatus jit_attach_to_snapshot(ParserSnapshot *snap) {
    if (snap == NULL) return JIT_ERR_INVALID_ARG;
    if (snap->jit_ctx != NULL) return JIT_ERR_ALREADY_COMPILED;

    JITContext *ctx = NULL;
    JITStatus st = jit_create(&ctx);
    if (st != JIT_OK) return st;

    st = jit_compile_snapshot(ctx, snap);
    if (st != JIT_OK) {
        jit_destroy(ctx);
        return st;
    }

    snap->jit_ctx = ctx;

    return JIT_OK;
}

void jit_detach_from_snapshot(ParserSnapshot *snap) {
    if (snap == NULL) return;
    if (snap->jit_ctx != NULL) {
        jit_destroy((JITContext *)snap->jit_ctx);
        snap->jit_ctx = NULL;
    }
}

/*
** Table-driven fallback for find_shift_action.
**
** This mirrors the logic in lempar.c's yy_find_shift_action() but
** operates on the snapshot's dynamically-allocated arrays instead
** of static globals.
*/
static uint16_t table_find_shift_action(const ParserSnapshot *snap, uint16_t stateno,
                                        uint16_t iLookAhead) {
    if (snap->yy_shift_ofst == NULL) return 0;

    int16_t ofst = snap->yy_shift_ofst[stateno];
    uint32_t idx = (uint32_t)((int32_t)ofst + (int32_t)iLookAhead);

    if (idx < snap->lookahead_count && snap->yy_lookahead[idx] == iLookAhead) {
        return snap->yy_action[idx];
    }

    return snap->yy_default[stateno];
}

uint16_t jit_find_shift_action(const ParserSnapshot *snap, uint16_t stateno, uint16_t iLookAhead) {
    if (snap == NULL) return 0;

    /* JIT path disabled for per-token lookups (too much overhead).
    ** Use jit_parse_batch() instead for batch processing. */

    /* Fall back to table-driven lookup */
    return table_find_shift_action(snap, stateno, iLookAhead);
}

void jit_parse_batch(const ParserSnapshot *snap, uint16_t *tokens, uint32_t count,
                     uint16_t *state_inout) {
    if (snap == NULL || tokens == NULL || count == 0 || state_inout == NULL) {
        return;
    }

#ifndef LIME_NO_JIT
    /* Try JIT batch path */
    if (snap->jit_ctx != NULL) {
        JITContext *ctx = (JITContext *)snap->jit_ctx;
        if (ctx->parse_sequence_fn != NULL) {
            /* Call monolithic JIT function: void (*)(uint16_t*, uint32_t, uint16_t*) */
            typedef void (*JITParseSequenceFn)(uint16_t *, uint32_t, uint16_t *);
            JITParseSequenceFn jit_fn = (JITParseSequenceFn)ctx->parse_sequence_fn;
            jit_fn(tokens, count, state_inout);
            return;
        }
    }
#endif

    /* Fallback: interpreted loop */
    uint16_t state = *state_inout;
    for (uint32_t i = 0; i < count; i++) {
        uint16_t action = table_find_shift_action(snap, state, tokens[i]);
        /* Update state based on action (simplified parse logic) */
        if (action < 1000) {
            state = action % snap->nstate;
        }
    }
    *state_inout = state;
}

/* ------------------------------------------------------------------ */
/*  Public lime_jit_* wrappers (declared in parser.h)                 */
/* ------------------------------------------------------------------ */

bool lime_jit_available(void) {
    return jit_is_available();
}

int lime_jit_compile(ParserSnapshot *snap) {
    JITStatus st = jit_attach_to_snapshot(snap);
    return (st == JIT_OK || st == JIT_ERR_ALREADY_COMPILED) ? 0 : (int)st;
}
