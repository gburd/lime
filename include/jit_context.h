/*
** LLVM OrcJIT compilation context for the extensible SQL parser.
**
** Provides runtime compilation of parser hot paths (action table lookups)
** using LLVM's OrcJIT (LLJIT) infrastructure. The JIT compiles a
** monolithic parse function that processes entire token sequences with
** fully-inlined state dispatch, eliminating per-token function call
** overhead.
**
** OrcJIT replaces the deprecated MCJIT engine and provides:
**   - Thread-safe contexts for concurrent compilation
**   - Lazy compilation support (for future tiered compilation)
**   - Better resource management and error handling via LLVMErrorRef
**
** When LLVM is not available at compile time (LIME_NO_JIT is defined),
** all JIT functions degrade to no-ops and the parser falls back to the
** standard interpreted table-driven approach.
*/
#ifndef JIT_CONTEXT_H
#define JIT_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct ParserSnapshot ParserSnapshot;

/* ------------------------------------------------------------------ */
/*  JIT compilation status codes                                       */
/* ------------------------------------------------------------------ */

typedef enum JITStatus {
    JIT_OK = 0,              /* Operation succeeded                    */
    JIT_ERR_NO_LLVM,         /* LLVM not available (compiled without)  */
    JIT_ERR_INIT_FAILED,     /* LLVM initialization failed             */
    JIT_ERR_CODEGEN_FAILED,  /* Code generation failed                 */
    JIT_ERR_COMPILE_FAILED,  /* JIT compilation failed                 */
    JIT_ERR_LOOKUP_FAILED,   /* Symbol lookup in JIT module failed     */
    JIT_ERR_INVALID_ARG,     /* NULL or invalid argument               */
    JIT_ERR_ALREADY_COMPILED /* Snapshot already has JIT code          */
} JITStatus;

/* ------------------------------------------------------------------ */
/*  JIT statistics                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief JIT compilation statistics for a snapshot.
 */
typedef struct JITStats {
    uint32_t states_compiled;    /**< Number of states with JIT code attached */
    uint32_t states_total;       /**< Total number of states in the snapshot */
    uint64_t compile_time_ns;    /**< Wall-clock nanoseconds spent compiling */
    uint64_t code_size_bytes;    /**< Approximate generated code size in bytes */
    bool     available;          /**< True if JIT support is available at runtime */
} JITStats;

/* ------------------------------------------------------------------ */
/*  Opaque JIT context handle                                          */
/* ------------------------------------------------------------------ */

/*
** The JITContext is an opaque handle that holds LLVM state (module,
** execution engine, compiled function pointers). It is stored in
** ParserSnapshot.jit_ctx and freed when the snapshot is destroyed.
*/
typedef struct JITContext JITContext;

/*
** Function pointer type for JIT-compiled shift action lookup.
**
** Given a lookahead token code, returns the action code (shift, reduce,
** or error). Each compiled state has its own function with the action
** table logic baked into the instruction stream.
*/
typedef uint16_t (*JITShiftActionFn)(uint16_t iLookAhead);

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/*
** Create a new JIT context. Returns JIT_OK on success or an error
** code if LLVM is unavailable or initialization fails.
**
** The context is heap-allocated and must be freed with jit_destroy().
** On success *ctx_out is set to the new context; on failure it is NULL.
*/
JITStatus jit_create(JITContext **ctx_out);

/*
** Destroy a JIT context and free all associated LLVM resources.
** Passing NULL is safe and does nothing.
*/
void jit_destroy(JITContext *ctx);

/*
** Compile JIT code for a parser snapshot's action tables.
**
** Generates optimized machine code for find_shift_action across all
** parser states. On success the compiled function pointers are stored
** inside the JITContext and can be queried with jit_get_shift_action().
**
** The snapshot must have valid action tables (yy_action, yy_lookahead,
** yy_shift_ofst, yy_default arrays and nstate/action_count set).
**
** Returns JIT_OK on success, or an error code on failure. On failure
** the context remains valid but contains no compiled code.
*/
JITStatus jit_compile_snapshot(JITContext *ctx, const ParserSnapshot *snap);

/*
** Look up the JIT-compiled shift action function for a given state.
**
** Returns the function pointer if state_id has been compiled, or NULL
** if the state has no JIT code (caller should fall back to the
** table-driven path).
*/
JITShiftActionFn jit_get_shift_action(const JITContext *ctx, uint32_t state_id);

/*
** Pre-warm the JIT for a set of hot parser states.
**
** Records which states are frequently visited so that future tiered
** compilation can apply extra optimization to those states. Currently
** the monolithic JIT function covers all states equally, so this is
** a no-op beyond bookkeeping, but the API is provided for forward
** compatibility.
**
** Parameters:
**   ctx - JIT context (must have compiled a snapshot)
**   hot_states - Array of state IDs to mark as hot
**   n - Number of entries in hot_states
**
** Returns JIT_OK on success, JIT_ERR_INVALID_ARG if ctx is NULL,
** or JIT_ERR_NO_LLVM if compiled without LLVM.
*/
JITStatus jit_warmup(JITContext *ctx, const uint32_t *hot_states, uint32_t n);

/*
** Get JIT compilation statistics.
*/
JITStats jit_get_stats(const JITContext *ctx);

/*
** Return a human-readable string for a JIT status code.
** The returned pointer is to static storage and must not be freed.
*/
const char *jit_status_string(JITStatus status);

/*
** Check whether JIT compilation is available at runtime.
** Returns true if LLVM support was compiled in and initialization
** succeeds, false otherwise.
*/
bool jit_is_available(void);

/* ------------------------------------------------------------------ */
/*  Snapshot integration helpers                                       */
/* ------------------------------------------------------------------ */

/*
** Compile and attach JIT code to a snapshot.
**
** Convenience function that creates a JIT context, compiles code for
** the snapshot's action tables, and stores the context in snap->jit_ctx.
** If the snapshot already has a JIT context this is a no-op returning
** JIT_ERR_ALREADY_COMPILED.
**
** Returns JIT_OK on success. On failure snap->jit_ctx is left unchanged.
*/
JITStatus jit_attach_to_snapshot(ParserSnapshot *snap);

/*
** Detach and destroy the JIT context from a snapshot.
** This is called automatically by snapshot_release() when the refcount
** reaches zero, but can also be called manually to free JIT resources
** earlier.
*/
void jit_detach_from_snapshot(ParserSnapshot *snap);

/*
** Runtime dispatch: look up the shift action for a state+lookahead pair.
**
** If the snapshot has JIT code for the given state, uses the compiled
** path. Otherwise falls back to the table-driven lookup using the
** snapshot's action table arrays.
**
** This is the primary entry point for the parser runtime to query
** shift actions when JIT is enabled.
*/
uint16_t jit_find_shift_action(const ParserSnapshot *snap,
                               uint16_t stateno,
                               uint16_t iLookAhead);

/*
** Batch parse a sequence of tokens using the JIT-compiled monolithic function.
**
** Processes all tokens in one call to avoid per-token function call overhead.
** If JIT is available, calls the compiled jit_parse_sequence function.
** Otherwise, falls back to calling jit_find_shift_action in a loop.
**
** Parameters:
**   snap - Parser snapshot (may or may not have JIT compiled)
**   tokens - Array of lookahead tokens to process
**   count - Number of tokens in the array
**   state_inout - Pointer to current parser state (updated after processing)
*/
void jit_parse_batch(const ParserSnapshot *snap,
                     uint16_t *tokens,
                     uint32_t count,
                     uint16_t *state_inout);

#ifdef __cplusplus
}
#endif

#endif /* JIT_CONTEXT_H */
