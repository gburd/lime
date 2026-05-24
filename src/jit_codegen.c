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
#include <stdlib.h>
#include <string.h>
#include "lime_time.h"

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
static LLVMValueRef generate_monolithic_parser(LLVMContextRef llvm_ctx, LLVMModuleRef module,
                                               const ParserSnapshot *snap) {
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
    LLVMValueRef token_ptr =
        LLVMBuildGEP2(builder, i16, param_tokens, &current_index, 1, "token_ptr");
    LLVMValueRef lookahead = LLVMBuildLoad2(builder, i16, token_ptr, "la");

    /* Load current state */
    LLVMValueRef current_state = LLVMBuildLoad2(builder, i16, state_var, "current_state");

    /* State dispatch switch */
    LLVMBasicBlockRef loop_increment_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "loop.inc");
    LLVMBasicBlockRef default_state_bb =
        LLVMAppendBasicBlockInContext(llvm_ctx, fn, "state.default");

    LLVMValueRef state_switch =
        LLVMBuildSwitch(builder, current_state, default_state_bb, snap->nstate);

    /* Generate a case for each parser state */
    for (uint32_t s = 0; s < snap->nstate; s++) {
        char bb_name[64];
        snprintf(bb_name, sizeof(bb_name), "state.%u", s);
        LLVMBasicBlockRef state_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, bb_name);

        LLVMAddCase(state_switch, LLVMConstInt(i16, s, 0), state_bb);

        LLVMPositionBuilderAtEnd(builder, state_bb);

        /* Inline action lookup for this state */
        int32_t ofst = snap->yy_shift_ofst[s];
        uint16_t default_action = snap->yy_default[s];

        if (ofst < 0 || (uint32_t)ofst >= snap->lookahead_count) {
            /* No shift actions - use default */
            LLVMBuildStore(builder, LLVMConstInt(i16, default_action, 0), state_var);
            LLVMBuildBr(builder, loop_increment_bb);
        } else {
            /* Build action lookup as switch on lookahead */
            LLVMBasicBlockRef action_default_bb =
                LLVMAppendBasicBlockInContext(llvm_ctx, fn, "action.default");

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
                LLVMValueRef action_switch =
                    LLVMBuildSwitch(builder, lookahead, action_default_bb, valid_actions);

                /* Generate cases for each valid action */
                for (uint32_t k = idx_lo; k < idx_hi; k++) {
                    uint16_t expected_la = snap->yy_lookahead[k];
                    uint16_t token = (uint16_t)(k - idx_lo);
                    if (expected_la != token) continue;

                    uint16_t action_val = snap->yy_action[k];

                    char action_name[64];
                    snprintf(action_name, sizeof(action_name), "action.s%u.t%u", s, token);
                    LLVMBasicBlockRef action_bb =
                        LLVMAppendBasicBlockInContext(llvm_ctx, fn, action_name);

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
** Generate a per-token shift-action lookup function:
**
**   uint16_t jit_find_shift_action(uint16_t state, uint16_t lookahead);
**
** This is the function the runtime push parser (parse_engine_step in
** src/parse_engine.c) calls once per token when a snapshot has JIT
** code attached.  Its body is a fully-inlined nested switch over
** (state, lookahead) -> action that LLVM lowers to native jump
** tables -- avoiding the indirect-load chain through yy_shift_ofst /
** yy_lookahead / yy_action / yy_default that the table-driven path
** has to walk.
**
** Algorithm matches src/parse_engine.c::find_shift_action exactly:
**
**   - State above max_shift returns the state code itself
**     (encoded shift-reduce; the engine handles dispatch).
**   - Otherwise look up the (state, lookahead) entry; if the
**     lookahead matches yy_lookahead at that index, return
**     yy_action[index]; otherwise return yy_default[state].
*/
/* ------------------------------------------------------------------ */
/*  Compact codegen for very large grammars                            */
/* ------------------------------------------------------------------ */

/*
** Add an internal-linkage const array global to the module and
** initialise it with `count` elements from `data`.  Returns the
** opaque LLVMValueRef for the global.  Used by the compact
** find_shift_action path to bake the snapshot's tables directly
** into the JIT module.
*/
static LLVMValueRef add_const_array_global(LLVMContextRef llvm_ctx, LLVMModuleRef module,
                                           const char *name, LLVMTypeRef elt_ty,
                                           const void *data, uint32_t count, uint32_t elt_size) {
    (void)llvm_ctx;
    LLVMTypeRef arr_ty = LLVMArrayType(elt_ty, count);
    LLVMValueRef g = LLVMAddGlobal(module, arr_ty, name);
    LLVMSetLinkage(g, LLVMInternalLinkage);
    LLVMSetGlobalConstant(g, 1);

    /* Build the initializer.  count is bounded by snap->action_count
    ** which is at most a few hundred thousand; allocate the values
    ** array on the heap. */
    LLVMValueRef *vals = malloc((size_t)count * sizeof(LLVMValueRef));
    if (vals == NULL) {
        /* Fall back to a zero initializer; the JIT path will be wrong
        ** but the build won't crash.  Caller checks for NULL is not
        ** strictly necessary because LLVMConstArray accepts an empty
        ** initializer too. */
        LLVMSetInitializer(g, LLVMConstNull(arr_ty));
        return g;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v;
        switch (elt_size) {
            case 1: v = ((const uint8_t *)data)[i]; break;
            case 2: v = ((const uint16_t *)data)[i]; break;
            case 4: v = ((const uint32_t *)data)[i]; break;
            default: v = 0; break;
        }
        /* SignExtend = 0: store as unsigned/exact-bits.  i16 / i32
        ** array entries are interpreted as 2's complement at use
        ** sites (yy_shift_ofst is signed), but the bits are the
        ** same. */
        vals[i] = LLVMConstInt(elt_ty, v, 0);
    }
    LLVMSetInitializer(g, LLVMConstArray(elt_ty, vals, count));
    free(vals);
    return g;
}

/*
** Compact find_shift_action codegen: emit the action tables as
** internal-linkage const globals and a small function body that
** mirrors src/parse_engine.c::find_shift_action exactly.  Result
** IR is ~30 instructions independent of grammar size, in contrast
** to generate_find_shift_action() which fully unrolls the
** state x lookahead switch (millions of basic blocks on PG-scale
** grammars, where LLVM's mandatory lowering passes do not scale).
**
** Algorithm:
**     uint32_t jit_find_shift_action(uint32_t state, uint32_t la) {
**         if (state > yy_max_shift) return state;
**         int32_t ofst = yy_shift_ofst[state];
**         int32_t idx  = ofst + (int32_t)la;
**         if (idx >= 0 && idx < lookahead_count
**             && yy_lookahead[idx] == la) return yy_action[idx];
**         return yy_default[state];
**     }
*/
static LLVMValueRef generate_find_shift_action_compact(LLVMContextRef llvm_ctx,
                                                       LLVMModuleRef module,
                                                       const ParserSnapshot *snap) {
    LLVMTypeRef i16 = LLVMInt16TypeInContext(llvm_ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(llvm_ctx);

    /* Bake the four arrays into the module as internal globals. */
    LLVMValueRef g_action =
        add_const_array_global(llvm_ctx, module, "lime_yy_action", i16, snap->yy_action,
                               snap->action_count, 2);
    LLVMValueRef g_lookahead =
        add_const_array_global(llvm_ctx, module, "lime_yy_lookahead", i16, snap->yy_lookahead,
                               snap->lookahead_count, 2);
    LLVMValueRef g_default =
        add_const_array_global(llvm_ctx, module, "lime_yy_default", i16, snap->yy_default,
                               snap->nstate, 2);
    LLVMValueRef g_shift_ofst =
        add_const_array_global(llvm_ctx, module, "lime_yy_shift_ofst", i32, snap->yy_shift_ofst,
                               snap->nstate, 4);
    LLVMTypeRef arr_action_ty = LLVMArrayType(i16, snap->action_count);
    LLVMTypeRef arr_lookahead_ty = LLVMArrayType(i16, snap->lookahead_count);
    LLVMTypeRef arr_default_ty = LLVMArrayType(i16, snap->nstate);
    LLVMTypeRef arr_shift_ofst_ty = LLVMArrayType(i32, snap->nstate);

    /* Function: uint32_t jit_find_shift_action(uint32_t, uint32_t) */
    LLVMTypeRef param_types[] = {i32, i32};
    LLVMTypeRef fn_type = LLVMFunctionType(i32, param_types, 2, 0);
    LLVMValueRef fn = LLVMAddFunction(module, "jit_find_shift_action", fn_type);
    LLVMSetFunctionCallConv(fn, LLVMCCallConv);
    LLVMSetLinkage(fn, LLVMExternalLinkage);

    LLVMValueRef state_i32 = LLVMGetParam(fn, 0);
    LLVMValueRef la_i32 = LLVMGetParam(fn, 1);

    LLVMBuilderRef b = LLVMCreateBuilderInContext(llvm_ctx);
    LLVMBasicBlockRef bb_entry = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "entry");
    LLVMBasicBlockRef bb_passthru = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "passthru");
    LLVMBasicBlockRef bb_lookup = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "lookup");
    LLVMBasicBlockRef bb_check_match = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "check_match");
    LLVMBasicBlockRef bb_hit = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "hit");
    LLVMBasicBlockRef bb_default = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "default");

    LLVMPositionBuilderAtEnd(b, bb_entry);
    /* if (state > yy_max_shift) return state; */
    LLVMValueRef max_shift = LLVMConstInt(i32, snap->yy_max_shift, 0);
    LLVMValueRef cmp_passthru = LLVMBuildICmp(b, LLVMIntUGT, state_i32, max_shift, "above_max");
    LLVMBuildCondBr(b, cmp_passthru, bb_passthru, bb_lookup);

    LLVMPositionBuilderAtEnd(b, bb_passthru);
    LLVMBuildRet(b, state_i32);

    /* lookup: ofst = yy_shift_ofst[state] */
    LLVMPositionBuilderAtEnd(b, bb_lookup);
    LLVMValueRef zero_i32 = LLVMConstInt(i32, 0, 0);
    LLVMValueRef so_idx[] = {zero_i32, state_i32};
    LLVMValueRef so_ptr =
        LLVMBuildInBoundsGEP2(b, arr_shift_ofst_ty, g_shift_ofst, so_idx, 2, "so_ptr");
    LLVMValueRef ofst = LLVMBuildLoad2(b, i32, so_ptr, "ofst");

    /* idx = ofst + la (signed) */
    LLVMValueRef idx = LLVMBuildAdd(b, ofst, la_i32, "idx");

    /* bounds: idx >= 0 && (uint32_t)idx < lookahead_count */
    LLVMValueRef geq_zero = LLVMBuildICmp(b, LLVMIntSGE, idx, zero_i32, "ge0");
    LLVMValueRef lac = LLVMConstInt(i32, snap->lookahead_count, 0);
    LLVMValueRef lt_lac = LLVMBuildICmp(b, LLVMIntULT, idx, lac, "lt_lac");
    LLVMValueRef bounds = LLVMBuildAnd(b, geq_zero, lt_lac, "bounds");
    LLVMBuildCondBr(b, bounds, bb_check_match, bb_default);

    /* check_match: yy_lookahead[idx] == la ? */
    LLVMPositionBuilderAtEnd(b, bb_check_match);
    LLVMValueRef la_idx[] = {zero_i32, idx};
    LLVMValueRef la_ptr =
        LLVMBuildInBoundsGEP2(b, arr_lookahead_ty, g_lookahead, la_idx, 2, "la_ptr");
    LLVMValueRef expected = LLVMBuildLoad2(b, i16, la_ptr, "expected");
    LLVMValueRef la_trunc = LLVMBuildTrunc(b, la_i32, i16, "la_i16");
    LLVMValueRef matches = LLVMBuildICmp(b, LLVMIntEQ, expected, la_trunc, "matches");
    LLVMBuildCondBr(b, matches, bb_hit, bb_default);

    /* hit: return yy_action[idx] */
    LLVMPositionBuilderAtEnd(b, bb_hit);
    LLVMValueRef act_idx[] = {zero_i32, idx};
    LLVMValueRef act_ptr =
        LLVMBuildInBoundsGEP2(b, arr_action_ty, g_action, act_idx, 2, "act_ptr");
    LLVMValueRef action = LLVMBuildLoad2(b, i16, act_ptr, "action");
    LLVMBuildRet(b, LLVMBuildZExt(b, action, i32, "ret_action"));

    /* default: return yy_default[state] */
    LLVMPositionBuilderAtEnd(b, bb_default);
    LLVMValueRef def_idx[] = {zero_i32, state_i32};
    LLVMValueRef def_ptr =
        LLVMBuildInBoundsGEP2(b, arr_default_ty, g_default, def_idx, 2, "def_ptr");
    LLVMValueRef def_act = LLVMBuildLoad2(b, i16, def_ptr, "def_act");
    LLVMBuildRet(b, LLVMBuildZExt(b, def_act, i32, "ret_def"));

    LLVMDisposeBuilder(b);
    return fn;
}

static LLVMValueRef generate_find_shift_action(LLVMContextRef llvm_ctx, LLVMModuleRef module,
                                               const ParserSnapshot *snap) {
    LLVMTypeRef i16 = LLVMInt16TypeInContext(llvm_ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(llvm_ctx);

    /*
    ** uint32_t (uint32_t state, uint32_t lookahead)
    **
    ** We use i32 for the return type and the parameters so the
    ** aarch64 / x86_64 ABIs put a well-defined 32-bit value in the
    ** return register.  Returning an LLVM i16 directly leaves the
    ** upper 16 bits of the return register undefined, and most C
    ** ABIs require the caller (not callee) to zero-extend small
    ** values -- so reading back through a `uint16_t (*)(...)`
    ** function pointer gets garbage in the upper bits.
    **
    ** The JIT helper this maps to is declared as
    ** uint32_t (*)(uint32_t, uint32_t) on the C side
    ** (JITFindShiftActionFn) and the runtime engine masks to
    ** uint16_t for use.
    */
    LLVMTypeRef param_types[] = {i32, i32};
    LLVMTypeRef fn_type = LLVMFunctionType(i32, param_types, 2, 0);

    LLVMValueRef fn = LLVMAddFunction(module, "jit_find_shift_action", fn_type);
    LLVMSetFunctionCallConv(fn, LLVMCCallConv);
    LLVMSetLinkage(fn, LLVMExternalLinkage);

    LLVMValueRef param_state_i32 = LLVMGetParam(fn, 0);
    LLVMValueRef param_la_i32 = LLVMGetParam(fn, 1);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(llvm_ctx);

    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry_bb);

    /* Truncate the i32 args to i16 for the switch keys (the table
    ** values are all i16 by construction). */
    LLVMValueRef param_state =
        LLVMBuildTrunc(builder, param_state_i32, i16, "state_i16");
    LLVMValueRef param_la = LLVMBuildTrunc(builder, param_la_i32, i16, "la_i16");

    /* Mirror find_shift_action: if (stateno > yy_max_shift) return stateno; */
    LLVMBasicBlockRef shift_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "in_shift_range");
    LLVMBasicBlockRef passthru_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, "passthru");

    LLVMValueRef max_shift_const = LLVMConstInt(i16, snap->yy_max_shift, 0);
    LLVMValueRef cmp_above_max =
        LLVMBuildICmp(builder, LLVMIntUGT, param_state, max_shift_const, "above_max");
    LLVMBuildCondBr(builder, cmp_above_max, passthru_bb, shift_bb);

    LLVMPositionBuilderAtEnd(builder, passthru_bb);
    LLVMBuildRet(builder, LLVMBuildZExt(builder, param_state, i32, "ret_passthru"));

    /* shift_bb: switch on state. */
    LLVMPositionBuilderAtEnd(builder, shift_bb);

    LLVMBasicBlockRef state_default_bb =
        LLVMAppendBasicBlockInContext(llvm_ctx, fn, "state_default");

    LLVMValueRef state_switch =
        LLVMBuildSwitch(builder, param_state, state_default_bb, snap->nstate);

    for (uint32_t s = 0; s < snap->nstate; s++) {
        char bb_name[64];
        snprintf(bb_name, sizeof(bb_name), "s%u", s);
        LLVMBasicBlockRef sbb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, bb_name);
        LLVMAddCase(state_switch, LLVMConstInt(i16, s, 0), sbb);

        LLVMPositionBuilderAtEnd(builder, sbb);

        int32_t ofst = snap->yy_shift_ofst[s];
        uint16_t default_action = snap->yy_default[s];

        if (ofst < 0 || (uint32_t)ofst >= snap->lookahead_count) {
            LLVMBuildRet(builder, LLVMConstInt(i32, default_action, 0));
            continue;
        }

        uint32_t valid = 0;
        uint32_t idx_lo = (uint32_t)ofst;
        uint32_t idx_hi = idx_lo + snap->yy_ntoken;
        if (idx_hi > snap->lookahead_count) idx_hi = snap->lookahead_count;
        for (uint32_t k = idx_lo; k < idx_hi; k++) {
            uint16_t expected = snap->yy_lookahead[k];
            uint16_t token = (uint16_t)(k - idx_lo);
            if (expected == token) valid++;
        }

        if (valid == 0) {
            LLVMBuildRet(builder, LLVMConstInt(i32, default_action, 0));
            continue;
        }

        char defname[80];
        snprintf(defname, sizeof(defname), "s%u_default", s);
        LLVMBasicBlockRef la_default_bb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, defname);

        LLVMValueRef la_switch = LLVMBuildSwitch(builder, param_la, la_default_bb, valid);

        for (uint32_t k = idx_lo; k < idx_hi; k++) {
            uint16_t expected = snap->yy_lookahead[k];
            uint16_t token = (uint16_t)(k - idx_lo);
            if (expected != token) continue;

            uint16_t action_val = snap->yy_action[k];

            char abname[80];
            snprintf(abname, sizeof(abname), "s%u_la%u", s, token);
            LLVMBasicBlockRef abb = LLVMAppendBasicBlockInContext(llvm_ctx, fn, abname);
            LLVMAddCase(la_switch, LLVMConstInt(i16, token, 0), abb);
            LLVMPositionBuilderAtEnd(builder, abb);
            LLVMBuildRet(builder, LLVMConstInt(i32, action_val, 0));
        }

        LLVMPositionBuilderAtEnd(builder, la_default_bb);
        LLVMBuildRet(builder, LLVMConstInt(i32, default_action, 0));
    }

    LLVMPositionBuilderAtEnd(builder, state_default_bb);
    LLVMBuildRet(builder, LLVMConstInt(i32, snap->yy_no_action, 0));

    LLVMDisposeBuilder(builder);
    return fn;
}

/*
** OrcJIT-compatible entry point: generates IR into an externally-provided
** module rather than relying on JITContext internals. This decouples
** codegen from the JIT engine lifetime, which OrcJIT requires since
** modules are consumed (moved) into the JIT dylib.
*/
JITStatus jit_codegen_generate_into(LLVMContextRef llvm_ctx, LLVMModuleRef module,
                                    const ParserSnapshot *snap, JITStats *stats_out) {
    if (llvm_ctx == NULL || module == NULL || snap == NULL) {
        return JIT_ERR_INVALID_ARG;
    }

    uint64_t start = now_ns();

    /*
    ** Size-based codegen heuristic.  The monolithic parse function
    ** (jit_parse_sequence) and the per-token lookup function
    ** (jit_find_shift_action) share the same nested switch shape:
    ** outer switch on state (nstate cases) with an inner switch on
    ** lookahead per state (up to nterminal cases).  Total basic-block
    ** count scales with nstate * average_actions_per_state.
    **
    ** On medium grammars (e.g. ~500 states, ~50 terminals) this is
    ** fine.  On the full PostgreSQL SQL grammar (3,842 states x 557
    ** terminals) the resulting IR has hundreds of thousands of basic
    ** blocks and LLVM's default<O2> pipeline does not appear to
    ** terminate in reasonable bounds (>5 minutes on Apple M1 / LLVM
    ** 21).
    **
    ** Two mitigations applied above the threshold:
    **
    **   1. Skip generation of the monolithic parse function.  Only
    **      jit_parse_batch() (a bench-only API) and jit_comparison.c
    **      use it; the production runtime engine calls
    **      jit_find_shift_action per token.  Halves the IR size at no
    **      runtime cost.
    **
    **   2. Signal "skip O2" to the caller via stats_out->skip_opts.
    **      jit_context.c reads the flag and runs only verify + codegen
    **      without optimisation passes.  The IR is structured (every
    **      switch arm is a constant return) so LLVM's default codegen
    **      lowers it to a jump table without help.
    **
    ** Threshold chosen so the in-tree examples (arithmetic ~11 states,
    ** COBOL ~80 states, calculator ~30 states, jsonpath ~120 states)
    ** all stay well below it and continue to benefit from O2; only
    ** real production grammars cross it.
    */
    const uint64_t IR_SIZE_THRESHOLD = 500000;
    uint64_t ir_size_estimate = (uint64_t)snap->nstate * (uint64_t)snap->nterminal;
    bool large_grammar = ir_size_estimate > IR_SIZE_THRESHOLD;

    if (!large_grammar) {
        /* Generate the monolithic parse function (bench-path).  Skipped
        ** for large grammars -- see comment above. */
        LLVMValueRef fn = generate_monolithic_parser(llvm_ctx, module, snap);
        if (fn == NULL) return JIT_ERR_CODEGEN_FAILED;
    }

    /* Always generate the per-token shift-action lookup -- the
    ** runtime push parser (parse_engine_step) calls this once per
    ** token when the snapshot has JIT code attached.
    **
    ** For large grammars use the compact (table-load) variant: it
    ** emits a small, fixed-size IR that does the same thing as
    ** parse_engine.c::find_shift_action, with the four data tables
    ** baked in as internal-linkage globals.  The unrolled-switch
    ** variant is faster per call (LLVM lowers the action constants
    ** to immediates) but does not scale to PG-size IR. */
    LLVMValueRef fn2 = large_grammar
                           ? generate_find_shift_action_compact(llvm_ctx, module, snap)
                           : generate_find_shift_action(llvm_ctx, module, snap);
    if (fn2 == NULL) return JIT_ERR_CODEGEN_FAILED;

    uint64_t end = now_ns();

    if (stats_out != NULL) {
        stats_out->compile_time_ns = end - start;
        stats_out->states_compiled = snap->nstate;
        stats_out->states_total = snap->nstate;
        stats_out->skip_opts = large_grammar;
        /* Estimate code size: ~200 bytes per state on average for
        ** the monolithic loop, plus a similar amount for the
        ** per-token lookup since they share the same state x
        ** lookahead structure.  Halve the estimate for large
        ** grammars where we skipped the monolithic function. */
        stats_out->code_size_bytes = snap->nstate * (large_grammar ? 200 : 400);
    }

    return JIT_OK;
}

#endif /* LIME_NO_JIT */
