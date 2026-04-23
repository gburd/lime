/*
** JIT context management for the extensible SQL parser.
**
** This file implements the JITContext lifecycle: creation, destruction,
** and snapshot integration. The actual LLVM IR generation and compilation
** lives in jit_codegen.c.
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
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassBuilder.h>

/* ------------------------------------------------------------------ */
/*  JIT context internal structure                                     */
/* ------------------------------------------------------------------ */

struct JITContext {
    LLVMModuleRef module;             /* LLVM module for generated IR     */
    LLVMExecutionEngineRef engine;    /* MCJIT execution engine           */
    LLVMContextRef llvm_ctx;          /* LLVM thread-local context        */

    void *parse_sequence_fn;          /* Monolithic jit_parse_sequence fn */
    uint32_t nstates;                 /* Number of states                 */

    JITStats stats;                   /* Compilation statistics           */
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

    ctx->llvm_ctx = LLVMContextCreate();
    if (ctx->llvm_ctx == NULL) {
        free(ctx);
        return JIT_ERR_INIT_FAILED;
    }

    ctx->module = LLVMModuleCreateWithNameInContext("lime_jit", ctx->llvm_ctx);
    if (ctx->module == NULL) {
        LLVMContextDispose(ctx->llvm_ctx);
        free(ctx);
        return JIT_ERR_INIT_FAILED;
    }

    ctx->stats.available = true;
    *ctx_out = ctx;
    return JIT_OK;
}

void jit_destroy(JITContext *ctx) {
    if (ctx == NULL) return;

    /* parse_sequence_fn is a function pointer, not allocated memory - don't free it */

    /* The execution engine owns the module, so only dispose the engine.
    ** If the engine was never created we must dispose the module manually. */
    if (ctx->engine != NULL) {
        LLVMDisposeExecutionEngine(ctx->engine);
    } else if (ctx->module != NULL) {
        LLVMDisposeModule(ctx->module);
    }

    if (ctx->llvm_ctx != NULL) {
        LLVMContextDispose(ctx->llvm_ctx);
    }

    free(ctx);
}

JITStats jit_get_stats(const JITContext *ctx) {
    if (ctx == NULL) {
        JITStats empty = {0};
        return empty;
    }
    return ctx->stats;
}

bool jit_is_available(void) {
    return ensure_llvm_initialized() == JIT_OK;
}

/* Defined in jit_codegen.c */
extern JITStatus jit_codegen_generate(JITContext *ctx,
                                      const ParserSnapshot *snap);

JITStatus jit_compile_snapshot(JITContext *ctx, const ParserSnapshot *snap) {
    if (ctx == NULL || snap == NULL) return JIT_ERR_INVALID_ARG;
    if (snap->yy_action == NULL || snap->nstate == 0) return JIT_ERR_INVALID_ARG;

    /* Generate LLVM IR for all states */
    JITStatus st = jit_codegen_generate(ctx, snap);
    if (st != JIT_OK) return st;

    /* Verify the module */
    char *error = NULL;
    if (LLVMVerifyModule(ctx->module, LLVMReturnStatusAction, &error)) {
        LLVMDisposeMessage(error);
        return JIT_ERR_CODEGEN_FAILED;
    }
    LLVMDisposeMessage(error);

    /* Run optimization passes */
    LLVMPassBuilderOptionsRef pass_opts = LLVMCreatePassBuilderOptions();
    LLVMRunPasses(ctx->module, "default<O2>", NULL, pass_opts);
    LLVMDisposePassBuilderOptions(pass_opts);

    /* Create execution engine (MCJIT) */
    struct LLVMMCJITCompilerOptions options;
    LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
    options.OptLevel = 2;

    error = NULL;
    if (LLVMCreateMCJITCompilerForModule(&ctx->engine, ctx->module,
                                         &options, sizeof(options), &error)) {
        if (error) LLVMDisposeMessage(error);
        return JIT_ERR_COMPILE_FAILED;
    }

    /* Look up the monolithic parse function */
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "jit_parse_sequence");
    if (fn == NULL) return JIT_ERR_LOOKUP_FAILED;

    uint64_t addr = LLVMGetFunctionAddress(ctx->engine, "jit_parse_sequence");
    if (addr == 0) return JIT_ERR_LOOKUP_FAILED;

    /* Store function pointer in context */
    ctx->parse_sequence_fn = (void *)(uintptr_t)addr;
    ctx->nstates = snap->nstate;
    ctx->stats.states_compiled = snap->nstate;
    ctx->stats.states_total = snap->nstate;

    /* Code size from jit_codegen */
    /* stats.code_size_bytes already set by jit_codegen_generate */

    return JIT_OK;
}

JITShiftActionFn jit_get_shift_action(const JITContext *ctx, uint32_t state_id) {
    /* No longer used in monolithic JIT approach - per-state functions not generated */
    (void)ctx; (void)state_id;
    return NULL;
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
    (void)ctx; (void)snap;
    return JIT_ERR_NO_LLVM;
}

JITShiftActionFn jit_get_shift_action(const JITContext *ctx,
                                      uint32_t state_id) {
    (void)ctx; (void)state_id;
    return NULL;
}

JITStats jit_get_stats(const JITContext *ctx) {
    (void)ctx;
    JITStats empty = {0};
    return empty;
}

bool jit_is_available(void) {
    return false;
}

#endif /* LIME_NO_JIT */

/* ------------------------------------------------------------------ */
/*  Status string (always available)                                   */
/* ------------------------------------------------------------------ */

const char *jit_status_string(JITStatus status) {
    switch (status) {
    case JIT_OK:                 return "OK";
    case JIT_ERR_NO_LLVM:        return "LLVM not available";
    case JIT_ERR_INIT_FAILED:    return "LLVM initialization failed";
    case JIT_ERR_CODEGEN_FAILED: return "code generation failed";
    case JIT_ERR_COMPILE_FAILED: return "JIT compilation failed";
    case JIT_ERR_LOOKUP_FAILED:  return "symbol lookup failed";
    case JIT_ERR_INVALID_ARG:    return "invalid argument";
    case JIT_ERR_ALREADY_COMPILED: return "already compiled";
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
static uint16_t table_find_shift_action(const ParserSnapshot *snap,
                                        uint16_t stateno,
                                        uint16_t iLookAhead) {
    if (snap->yy_shift_ofst == NULL) return 0;

    int16_t ofst = snap->yy_shift_ofst[stateno];
    uint32_t idx = (uint32_t)((int32_t)ofst + (int32_t)iLookAhead);

    if (idx < snap->lookahead_count &&
        snap->yy_lookahead[idx] == iLookAhead) {
        return snap->yy_action[idx];
    }

    return snap->yy_default[stateno];
}

uint16_t jit_find_shift_action(const ParserSnapshot *snap,
                               uint16_t stateno,
                               uint16_t iLookAhead) {
    if (snap == NULL) return 0;

    /* JIT path disabled for per-token lookups (too much overhead).
    ** Use jit_parse_batch() instead for batch processing. */

    /* Fall back to table-driven lookup */
    return table_find_shift_action(snap, stateno, iLookAhead);
}

void jit_parse_batch(const ParserSnapshot *snap,
                     uint16_t *tokens,
                     uint32_t count,
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
            typedef void (*JITParseSequenceFn)(uint16_t*, uint32_t, uint16_t*);
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
