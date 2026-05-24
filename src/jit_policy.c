/*
** JIT compilation policy implementation.
**
** Tracks per-snapshot runtime metrics and decides when JIT compilation
** is cost-effective. When triggered, compilation can run on a detached
** background thread so that parser throughput is not interrupted.
**
** The background thread atomically publishes the compiled JIT context
** to the snapshot's jit_ctx pointer, so parsers using the snapshot
** will transparently start using the JIT path on their next action
** lookup without any synchronization on the reader side.
*/
#include "jit_policy.h"
#include "jit_context.h"
#include "snapshot.h"

#include <stdlib.h>
#include <string.h>
#include "lime_threads.h"

/* ------------------------------------------------------------------ */
/*  Default configuration                                              */
/* ------------------------------------------------------------------ */

JITPolicyConfig jit_policy_default_config(void) {
    JITPolicyConfig cfg;
    cfg.min_parse_count = 200;
    cfg.min_total_parse_time_ns = 10000000; /* 10 ms */
    cfg.min_avg_lookups_per_parse = 100;
    cfg.background_compile = true;
    cfg.min_grammar_states = 500;
    cfg.min_avg_tokens_per_parse = 200;
    cfg.enabled = true;
    cfg.tokenizer_jit_enabled = true;
    return cfg;
}

/* ------------------------------------------------------------------ */
/*  Metrics                                                            */
/* ------------------------------------------------------------------ */

void jit_metrics_init(JITMetrics *m) {
    if (m == NULL) return;
    atomic_init(&m->parse_count, 0);
    atomic_init(&m->total_parse_time_ns, 0);
    atomic_init(&m->action_lookup_count, 0);
    atomic_init(&m->total_tokens_parsed, 0);
    atomic_init(&m->is_jitted, 0);
    atomic_init(&m->jit_in_progress, 0);
}

void jit_metrics_record_parse(JITMetrics *m, uint64_t parse_time_ns, uint64_t action_lookups) {
    if (m == NULL) return;
    atomic_fetch_add_explicit(&m->parse_count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&m->total_parse_time_ns, parse_time_ns, memory_order_relaxed);
    atomic_fetch_add_explicit(&m->action_lookup_count, action_lookups, memory_order_relaxed);
}

void jit_metrics_record_tokens(JITMetrics *m, uint64_t token_count) {
    if (m == NULL) return;
    atomic_fetch_add_explicit(&m->total_tokens_parsed, token_count, memory_order_relaxed);
}

/* ------------------------------------------------------------------ */
/*  Policy evaluation                                                  */
/* ------------------------------------------------------------------ */

bool jit_should_compile(const JITMetrics *m, const JITPolicyConfig *config) {
    if (m == NULL || config == NULL) return false;

    /* Master switch */
    if (!config->enabled) return false;

    /* Already compiled or compilation in progress */
    if (atomic_load_explicit(&m->is_jitted, memory_order_acquire)) return false;
    if (atomic_load_explicit(&m->jit_in_progress, memory_order_acquire)) return false;

    /* Check JIT runtime availability */
    if (!jit_is_available()) return false;

    uint64_t parses = atomic_load_explicit(&m->parse_count, memory_order_relaxed);
    uint64_t total_time = atomic_load_explicit(&m->total_parse_time_ns, memory_order_relaxed);
    uint64_t lookups = atomic_load_explicit(&m->action_lookup_count, memory_order_relaxed);

    if (parses < config->min_parse_count) return false;
    if (total_time < config->min_total_parse_time_ns) return false;

    /* Average lookups per parse */
    uint64_t avg_lookups = lookups / (parses > 0 ? parses : 1);
    if (avg_lookups < config->min_avg_lookups_per_parse) return false;

    /* Average tokens per parse session */
    uint64_t total_tokens = atomic_load_explicit(&m->total_tokens_parsed, memory_order_relaxed);
    uint64_t avg_tokens = total_tokens / (parses > 0 ? parses : 1);
    if (avg_tokens < config->min_avg_tokens_per_parse) return false;

    /*
    ** NOTE: min_grammar_states cannot be checked here because grammar
    ** state count is not available in JITMetrics -- it lives in the
    ** ParserSnapshot. Callers should verify that the snapshot's state
    ** count meets config->min_grammar_states before calling
    ** jit_should_compile() or jit_maybe_compile().
    */

    return true;
}

/* ------------------------------------------------------------------ */
/*  Background compilation                                             */
/* ------------------------------------------------------------------ */

typedef struct JITCompileArgs {
    ParserSnapshot *snap;
    JITMetrics *metrics;
} JITCompileArgs;

/*
** Background thread entry point. Compiles JIT code for the snapshot
** and atomically publishes it. On failure, clears the in_progress
** flag so a retry can happen later.
*/
static void *jit_compile_thread(void *arg) {
    JITCompileArgs *args = (JITCompileArgs *)arg;
    ParserSnapshot *snap = args->snap;
    JITMetrics *metrics = args->metrics;
    free(args);

    JITStatus st = jit_attach_to_snapshot(snap);

    if (st == JIT_OK || st == JIT_ERR_ALREADY_COMPILED) {
        atomic_store_explicit(&metrics->is_jitted, 1, memory_order_release);
    }

    /* Clear in_progress regardless of outcome, allowing retries on failure */
    atomic_store_explicit(&metrics->jit_in_progress, 0, memory_order_release);

    /* Release the snapshot reference we acquired for this thread */
    snapshot_release(snap);

    return NULL;
}

/*
** Compile synchronously (blocking the caller).
*/
static int jit_compile_sync(ParserSnapshot *snap, JITMetrics *metrics) {
    JITStatus st = jit_attach_to_snapshot(snap);

    if (st == JIT_OK || st == JIT_ERR_ALREADY_COMPILED) {
        atomic_store_explicit(&metrics->is_jitted, 1, memory_order_release);
    }

    atomic_store_explicit(&metrics->jit_in_progress, 0, memory_order_release);

    return (st == JIT_OK || st == JIT_ERR_ALREADY_COMPILED) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int jit_maybe_compile(ParserSnapshot *snap, JITMetrics *m, const JITPolicyConfig *config) {
    if (snap == NULL || m == NULL || config == NULL) return -1;

    /* Check if compilation is warranted */
    if (!jit_should_compile(m, config)) {
        /* Check if already compiled (success) vs not yet ready */
        if (atomic_load_explicit(&m->is_jitted, memory_order_acquire)) return 0;
        return 1;
    }

    /* Try to claim the in_progress flag (CAS prevents double-compile) */
    int expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&m->jit_in_progress, &expected, 1,
                                                 memory_order_acq_rel, memory_order_relaxed)) {
        /* Another thread is already compiling */
        return 0;
    }

    if (config->background_compile) {
        /* Acquire a snapshot reference for the background thread */
        snapshot_acquire(snap);

        JITCompileArgs *args = malloc(sizeof(JITCompileArgs));
        if (args == NULL) {
            snapshot_release(snap);
            atomic_store_explicit(&m->jit_in_progress, 0, memory_order_release);
            return -1;
        }
        args->snap = snap;
        args->metrics = m;

        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        int err = pthread_create(&thread, &attr, jit_compile_thread, args);
        pthread_attr_destroy(&attr);

        if (err != 0) {
            snapshot_release(snap);
            free(args);
            atomic_store_explicit(&m->jit_in_progress, 0, memory_order_release);
            return -1;
        }

        return 0;
    } else {
        return jit_compile_sync(snap, m);
    }
}

void jit_policy_shutdown(void) {
    /*
    ** Background compilation threads are detached, so we cannot
    ** join them. In practice, callers should ensure all snapshots
    ** are released (which destroys JIT contexts) before calling
    ** this. This function exists as a hook for future cleanup if
    ** we switch to a thread pool model.
    */
}
