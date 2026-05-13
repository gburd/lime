/*
** JIT compilation policy for the extensible SQL parser.
**
** Decides when to JIT compile a snapshot based on runtime metrics.
** The policy tracks per-snapshot usage statistics and triggers
** background JIT compilation when the expected benefit (faster action
** table lookups) exceeds the compilation cost.
**
** Thread safety: JITMetrics use atomic counters so multiple parser
** threads can update them concurrently. The background compilation
** thread is managed internally and publishes the JIT context to the
** snapshot via an atomic pointer store.
*/
#ifndef JIT_POLICY_H
#define JIT_POLICY_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct ParserSnapshot ParserSnapshot;

/* ------------------------------------------------------------------ */
/*  JIT metrics                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Per-snapshot runtime metrics used by the JIT policy.
 *
 * All counters are atomic so they can be updated from any parser
 * thread without locking.
 */
typedef struct JITMetrics {
    atomic_uint_fast64_t parse_count;         /**< Number of parse sessions */
    atomic_uint_fast64_t total_parse_time_ns; /**< Cumulative parse wall-clock (ns) */
    atomic_uint_fast64_t action_lookup_count; /**< Total action table lookups */
    atomic_uint_fast64_t total_tokens_parsed; /**< Cumulative tokens across all parses */
    atomic_int           is_jitted;           /**< 1 if JIT code is attached */
    atomic_int           jit_in_progress;     /**< 1 if background compile active */
} JITMetrics;

/* ------------------------------------------------------------------ */
/*  Policy configuration                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Tunable thresholds for the JIT compilation policy.
 *
 * These can be adjusted per-application to match workload patterns.
 * See jit_policy_default_config() for the shipping defaults.
 */
typedef struct JITPolicyConfig {
    /** Minimum number of parse sessions before considering JIT.
    ** Prevents wasting compilation effort on rarely-used grammars. */
    uint64_t min_parse_count;

    /** Minimum cumulative parse time (nanoseconds) before JIT.
    ** Ensures the grammar is spending enough time in parsing to
    ** justify the compilation overhead. */
    uint64_t min_total_parse_time_ns;

    /** Minimum average action lookups per parse session.
    ** Grammars with few lookups per parse won't benefit from JIT. */
    uint64_t min_avg_lookups_per_parse;

    /** If true, JIT compilation happens on a background thread.
    ** If false, compilation is synchronous (blocks the caller). */
    bool background_compile;

    /** Minimum number of parser states before considering JIT.
    ** Small grammars don't benefit enough to justify compilation. */
    uint32_t min_grammar_states;

    /** Minimum average tokens per parse session before JIT.
    ** Short inputs don't benefit from JIT compilation. */
    uint32_t min_avg_tokens_per_parse;

    /** Master switch for JIT compilation.
    ** Set to false to disable JIT entirely regardless of metrics. */
    bool enabled;

    /** Enable JIT compilation of the keyword tokenizer.
    ** When true, the JIT policy compiles a trie-based keyword
    ** classifier from the TokenTable when the parser JIT triggers.
    ** When false, keyword lookups always use the hash-based path. */
    bool tokenizer_jit_enabled;
} JITPolicyConfig;

/* ------------------------------------------------------------------ */
/*  Policy API                                                         */
/* ------------------------------------------------------------------ */

/*
** Return the default policy configuration.
** Defaults:
**   min_parse_count           = 200
**   min_total_parse_time_ns   = 10,000,000  (10 ms cumulative)
**   min_avg_lookups_per_parse = 100
**   background_compile        = true
**   min_grammar_states        = 500
**   min_avg_tokens_per_parse  = 200
**   enabled                   = true
**   tokenizer_jit_enabled     = true
*/
JITPolicyConfig jit_policy_default_config(void);

/*
** Initialize a JITMetrics struct to zero. Must be called before
** the metrics are used.
*/
void jit_metrics_init(JITMetrics *m);

/*
** Record the completion of a parse session.
** Updates parse_count, total_parse_time_ns, and action_lookup_count.
** Thread-safe (uses atomic operations).
*/
void jit_metrics_record_parse(JITMetrics *m,
                              uint64_t parse_time_ns,
                              uint64_t action_lookups);

/*
** Record the number of tokens processed in a parse session.
** Thread-safe (uses atomic operations).
*/
void jit_metrics_record_tokens(JITMetrics *m, uint64_t token_count);

/*
** Evaluate whether a snapshot should be JIT compiled based on its
** current metrics and the given policy configuration.
**
** Returns true if the metrics exceed all thresholds and the snapshot
** is not already JIT compiled or in the process of being compiled.
*/
bool jit_should_compile(const JITMetrics *m, const JITPolicyConfig *config);

/*
** Possibly trigger JIT compilation for a snapshot.
**
** Checks the metrics against the policy; if compilation is warranted:
**   - If config->background_compile is true, spawns a detached thread
**     that compiles and atomically publishes the JIT context.
**   - If config->background_compile is false, compiles synchronously.
**
** Returns:
**   0 -- compilation triggered (or already done)
**   1 -- metrics do not yet warrant compilation
**  -1 -- error (JIT unavailable, snapshot NULL, etc.)
**
** Thread-safe: uses the jit_in_progress flag to prevent concurrent
** compilation attempts on the same snapshot.
*/
int jit_maybe_compile(ParserSnapshot *snap,
                      JITMetrics *m,
                      const JITPolicyConfig *config);

/*
** Shut down the JIT policy subsystem.
** Waits for any in-flight background compilations to finish.
** Call once at application shutdown.
*/
void jit_policy_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* JIT_POLICY_H */
