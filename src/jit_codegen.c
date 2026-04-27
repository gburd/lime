/*
** LLVM IR code generation for JIT-compiled parser.
**
** Generates a SINGLE monolithic parse function that processes an entire
** token sequence. The state machine is fully inlined with no function call
** overhead per token - similar to HotSpot's method JIT or V8's TurboFan.
**
** Generated function signature:
**   void jit_parse_sequence(uint16_t *tokens, uint32_t count, uint16_t *state_inout)
**
** The function contains a main loop with a switch statement on the current
** parser state. For each state, action lookups are fully inlined as nested
** switches on the lookahead token. LLVM optimizes this into efficient jump
** tables, keeping the state in a register and eliminating redundant bounds
** checks.
*/

#ifndef LIME_NO_JIT

#include "jit_context.h"
#include "snapshot.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <llvm-c/Core.h>

/* Access JITStats from jit_context.h */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
** Generate the monolithic parse function using stack variables for simplicity.
** LLVM's mem2reg optimization pass will promote them to registers/SSA.
*/
static LLVMValueRef generate_monolithic_parser(
    LLVMContextRef llvm_ctx,
    LLVMModuleRef module,
    const ParserSnapshot *snap)
{
    LLVMTypeRef i16 = LLVMInt16TypeInContext(llvm_ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(llvm_ctx);
    LLVMTypeRef i16_ptr = LLVMPointerType(i16, 0);
    LLVMTypeRef void_type = LLVMVoidTypeInContext(llvm_ctx);

    /* Function signature: void (uint16_t *tokens, uint32_t count, uint16_t *state_inout) */
    LLVMTypeRef param_types[] = { i16_ptr, i32, i16_ptr };
    LLVMTypeRef fn_type = LLVMFunctionType(void_type, param_types, 3, 0);

    LLVMValueRef fn = LLVMAddFunction(module, "jit_parse_sequence", fn_type);
    LLVMSetFunctionCallConv(fn, LLVMCCallConv);
    LLVMSetLinkage(fn, LLVMExternalLinkage);

    LLVMValueRef param_tokens = LLVMGetParam(fn, 0);
    LLVMValueRef param_count = LLVMGetParam(fn, 1);
    LLVMValueRef param_state_ptr = LLVMGetParam(fn, 2);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(llvm_ctx);

    /* Entry block */
    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry_bb);

    /* Allocate local variables: state and loop index */
    LLVMValueRef state_var = LLVMBuildAlloca(builder, i16, "state");
    LLVMValueRef index_var = LLVMBuildAlloca(builder, i32, "i");

    /* Load initial state from parameter */
    LLVMValueRef initial_state = LLVMBuildLoad2(builder, i16, param_state_ptr, "initial_state");
    LLVMBuildStore(builder, initial_state, state_var);

    /* Initialize loop counter to 0 */
    LLVMValueRef zero = LLVMConstInt(i32, 0, 0);
    LLVMBuildStore(builder, zero, index_var);

    /* Loop blocks */
    LLVMBasicBlockRef loop_cond_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "loop.cond");
    LLVMBasicBlockRef loop_body_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "loop.body");
    LLVMBasicBlockRef loop_exit_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "loop.exit");

    LLVMBuildBr(builder, loop_cond_bb);

    /* --- Loop condition: while (i < count) --- */
    LLVMPositionBuilderAtEnd(builder, loop_cond_bb);
    LLVMValueRef current_index = LLVMBuildLoad2(builder, i32, index_var, "i");
    LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntULT, current_index, param_count, "cmp");
    LLVMBuildCondBr(builder, cmp, loop_body_bb, loop_exit_bb);

    /* --- Loop body --- */
    LLVMPositionBuilderAtEnd(builder, loop_body_bb);

    /* Load current token: la = tokens[i] */
    LLVMValueRef token_ptr = LLVMBuildGEP2(builder, i16, param_tokens, &current_index, 1, "token_ptr");
    LLVMValueRef lookahead = LLVMBuildLoad2(builder, i16, token_ptr, "la");

    /* Load current state */
    LLVMValueRef current_state = LLVMBuildLoad2(builder, i16, state_var, "current_state");

    /* State dispatch switch */
    LLVMBasicBlockRef loop_increment_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "loop.inc");
    LLVMBasicBlockRef default_state_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "state.default");

    LLVMValueRef state_switch = LLVMBuildSwitch(builder, current_state, default_state_bb, snap->nstate);

    /* Generate a case for each parser state */
    for (uint32_t s = 0; s < snap->nstate; s++) {
        char bb_name[64];
        snprintf(bb_name, sizeof(bb_name), "state.%u", s);
        LLVMBasicBlockRef state_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, bb_name);

        LLVMAddCase(state_switch, LLVMConstInt(i16, s, 0), state_bb);

        LLVMPositionBuilderAtEnd(builder, state_bb);

        /* Inline action lookup for this state */
        int16_t ofst = snap->yy_shift_ofst[s];
        uint16_t default_action = snap->yy_default[s];

        if (ofst < 0 || ofst >= (int16_t)snap->lookahead_count) {
            /* No shift actions - use default */
            LLVMBuildStore(builder, LLVMConstInt(i16, default_action, 0), state_var);
            LLVMBuildBr(builder, loop_increment_bb);
        } else {
            /* Build action lookup as switch on lookahead */
            LLVMBasicBlockRef action_default_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "action.default");

            /* Count valid actions */
            uint32_t valid_actions = 0;
            uint32_t idx_lo = (uint32_t)ofst;
            uint32_t idx_hi = idx_lo + snap->nterminal;
            if (idx_hi > snap->lookahead_count) idx_hi = snap->lookahead_count;

            for (uint32_t k = idx_lo; k < idx_hi; k++) {
                uint16_t expected_la = snap->yy_lookahead[k];
                uint16_t token = (uint16_t)(k - idx_lo);
                if (expected_la == token) {
                    valid_actions++;
                }
            }

            if (valid_actions > 0) {
                LLVMValueRef action_switch = LLVMBuildSwitch(builder, lookahead, action_default_bb, valid_actions);

                /* Generate cases for each valid action */
                for (uint32_t k = idx_lo; k < idx_hi; k++) {
                    uint16_t expected_la = snap->yy_lookahead[k];
                    uint16_t token = (uint16_t)(k - idx_lo);
                    if (expected_la != token) continue;

                    uint16_t action_val = snap->yy_action[k];

                    char action_name[64];
                    snprintf(action_name, sizeof(action_name), "action.s%u.t%u", s, token);
                    LLVMBasicBlockRef action_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, action_name);

                    LLVMAddCase(action_switch, LLVMConstInt(i16, token, 0), action_bb);

                    LLVMPositionBuilderAtEnd(builder, action_bb);
                    LLVMBuildStore(builder, LLVMConstInt(i16, action_val, 0), state_var);
                    LLVMBuildBr(builder, loop_increment_bb);
                }
            } else {
                /* No valid actions, jump to default */
                LLVMBuildBr(builder, action_default_bb);
            }

            /* Default action case */
            LLVMPositionBuilderAtEnd(builder, action_default_bb);
            LLVMBuildStore(builder, LLVMConstInt(i16, default_action, 0), state_var);
            LLVMBuildBr(builder, loop_increment_bb);
        }
    }

    /* Default state case (should never happen) */
    LLVMPositionBuilderAtEnd(builder, default_state_bb);
    LLVMBuildBr(builder, loop_increment_bb);

    /* --- Loop increment: i++ --- */
    LLVMPositionBuilderAtEnd(builder, loop_increment_bb);
    LLVMValueRef index_for_inc = LLVMBuildLoad2(builder, i32, index_var, "i_for_inc");
    LLVMValueRef one = LLVMConstInt(i32, 1, 0);
    LLVMValueRef next_index = LLVMBuildAdd(builder, index_for_inc, one, "next_i");
    LLVMBuildStore(builder, next_index, index_var);
    LLVMBuildBr(builder, loop_cond_bb);

    /* --- Loop exit --- */
    LLVMPositionBuilderAtEnd(builder, loop_exit_bb);

    /* Store final state back to *state_inout */
    LLVMValueRef final_state = LLVMBuildLoad2(builder, i16, state_var, "final_state");
    LLVMBuildStore(builder, final_state, param_state_ptr);
    LLVMBuildRetVoid(builder);

    LLVMDisposeBuilder(builder);

    return fn;
}

/*
** OrcJIT-compatible entry point: generates IR into an externally-provided
** module rather than relying on JITContext internals. This decouples
** codegen from the JIT engine lifetime, which OrcJIT requires since
** modules are consumed (moved) into the JIT dylib.
*/
JITStatus jit_codegen_generate_into(LLVMContextRef llvm_ctx,
                                     LLVMModuleRef module,
                                     const ParserSnapshot *snap,
                                     JITStats *stats_out) {
    if (llvm_ctx == NULL || module == NULL || snap == NULL) {
        return JIT_ERR_INVALID_ARG;
    }

    uint64_t start = now_ns();

    /* Generate single monolithic parse function */
    LLVMValueRef fn = generate_monolithic_parser(llvm_ctx, module, snap);

    if (fn == NULL) return JIT_ERR_CODEGEN_FAILED;

    uint64_t end = now_ns();

    if (stats_out != NULL) {
        stats_out->compile_time_ns = end - start;
        stats_out->states_compiled = snap->nstate;
        stats_out->states_total = snap->nstate;
        /* Estimate code size: ~200 bytes per state on average */
        stats_out->code_size_bytes = snap->nstate * 200;
    }

    return JIT_OK;
}

#endif /* LIME_NO_JIT */
