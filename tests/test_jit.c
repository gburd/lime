/*
** Unit tests for the JIT compilation subsystem.
**
** Tests cover:
**   1. JIT availability detection
**   2. Status string mapping
**   3. JIT context lifecycle (create / destroy)
**   4. Compile with NULL / invalid arguments
**   5. Snapshot integration (attach / detach)
**   6. Runtime dispatch (jit_find_shift_action fallback)
**   7. JIT policy default config
**   8. Metrics initialization and recording
**   9. Policy evaluation (should_jit_compile thresholds)
**  10. Policy-triggered compilation (jit_maybe_compile)
**  11. Double-compile guard (JIT_ERR_ALREADY_COMPILED)
**  12. Idempotent detach
**
** When LLVM is not available (stub mode), the tests verify that all
** functions degrade gracefully and return appropriate error codes.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "jit_context.h"
#include "jit_policy.h"
#include "snapshot.h"

/* ------------------------------------------------------------------ */
/*  Test helpers                                                       */
/* ------------------------------------------------------------------ */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  test %-50s ", #name); \
        if (test_##name()) { \
            tests_passed++; \
            printf("PASS\n"); \
        } else { \
            printf("FAIL\n"); \
        } \
    } while (0)

#define ASSERT(cond) \
    do { if (!(cond)) { \
        fprintf(stderr, "    ASSERT failed: %s (%s:%d)\n", \
                #cond, __FILE__, __LINE__); \
        return 0; \
    }} while (0)

/*
** Create a minimal fake snapshot with small action tables for testing.
** The caller must free the snapshot with snapshot_release().
*/
static ParserSnapshot *make_test_snapshot(void) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (!snap) return NULL;

    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    snap->nstate = 4;
    snap->nterminal = 3;

    /* Allocate action tables */
    snap->action_count = 12;
    snap->lookahead_count = 12;

    snap->yy_action = calloc(snap->action_count, sizeof(uint16_t));
    snap->yy_lookahead = calloc(snap->lookahead_count, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(snap->nstate, sizeof(int16_t));
    snap->yy_reduce_ofst = calloc(snap->nstate, sizeof(int16_t));
    snap->yy_default = calloc(snap->nstate, sizeof(uint16_t));

    if (!snap->yy_action || !snap->yy_lookahead ||
        !snap->yy_shift_ofst || !snap->yy_reduce_ofst ||
        !snap->yy_default) {
        snapshot_release(snap);
        return NULL;
    }

    /*
    ** Set up a simple action table.
    ** State 0: shift_ofst=0, tokens 0,1,2 -> actions 10,11,12
    ** State 1: shift_ofst=3, tokens 0,1,2 -> actions 20,21,22
    ** State 2: shift_ofst=6, tokens 0,1,2 -> actions 30,31,32
    ** State 3: default action = 99
    */
    for (uint32_t s = 0; s < 3; s++) {
        snap->yy_shift_ofst[s] = (int16_t)(s * 3);
        snap->yy_default[s] = 0;
        for (uint32_t t = 0; t < 3; t++) {
            uint32_t idx = s * 3 + t;
            snap->yy_lookahead[idx] = (uint16_t)t;
            snap->yy_action[idx] = (uint16_t)((s + 1) * 10 + t);
        }
    }
    /* State 3: all lookups miss, use default */
    snap->yy_shift_ofst[3] = 9;
    snap->yy_default[3] = 99;
    /* Remaining lookahead entries are 0 from calloc, which is valid
    ** for token 0 only at offset 9. Fill with NOCODE-like values
    ** so all lookups for state 3 miss. */
    snap->yy_lookahead[9] = 0xFFFF;
    snap->yy_lookahead[10] = 0xFFFF;
    snap->yy_lookahead[11] = 0xFFFF;

    snap->jit_ctx = NULL;

    return snap;
}

/* ------------------------------------------------------------------ */
/*  JIT context tests                                                  */
/* ------------------------------------------------------------------ */

static int test_jit_is_available(void) {
    /* Just call it -- should not crash. Returns true or false depending
    ** on whether LLVM is linked. */
    bool avail = jit_is_available();
    (void)avail;
    return 1;
}

static int test_jit_status_strings(void) {
    ASSERT(strcmp(jit_status_string(JIT_OK), "OK") == 0);
    ASSERT(strcmp(jit_status_string(JIT_ERR_NO_LLVM), "LLVM not available") == 0);
    ASSERT(strcmp(jit_status_string(JIT_ERR_INIT_FAILED), "LLVM initialization failed") == 0);
    ASSERT(strcmp(jit_status_string(JIT_ERR_CODEGEN_FAILED), "code generation failed") == 0);
    ASSERT(strcmp(jit_status_string(JIT_ERR_COMPILE_FAILED), "JIT compilation failed") == 0);
    ASSERT(strcmp(jit_status_string(JIT_ERR_LOOKUP_FAILED), "symbol lookup failed") == 0);
    ASSERT(strcmp(jit_status_string(JIT_ERR_INVALID_ARG), "invalid argument") == 0);
    ASSERT(strcmp(jit_status_string(JIT_ERR_ALREADY_COMPILED), "already compiled") == 0);
    return 1;
}

static int test_jit_create_null_arg(void) {
    JITStatus st = jit_create(NULL);
    ASSERT(st == JIT_ERR_INVALID_ARG);
    return 1;
}

static int test_jit_create_destroy(void) {
    JITContext *ctx = NULL;
    JITStatus st = jit_create(&ctx);

    if (jit_is_available()) {
        ASSERT(st == JIT_OK);
        ASSERT(ctx != NULL);
        jit_destroy(ctx);
    } else {
        ASSERT(st == JIT_ERR_NO_LLVM);
        ASSERT(ctx == NULL);
    }
    return 1;
}

static int test_jit_destroy_null(void) {
    /* Should be a safe no-op */
    jit_destroy(NULL);
    return 1;
}

static int test_jit_compile_null_args(void) {
    JITContext *ctx = NULL;
    JITStatus st;

    st = jit_compile_snapshot(NULL, NULL);
    ASSERT(st == JIT_ERR_INVALID_ARG || st == JIT_ERR_NO_LLVM);

    ParserSnapshot *snap = make_test_snapshot();
    ASSERT(snap != NULL);

    st = jit_compile_snapshot(NULL, snap);
    ASSERT(st == JIT_ERR_INVALID_ARG || st == JIT_ERR_NO_LLVM);

    if (jit_is_available()) {
        jit_create(&ctx);
        st = jit_compile_snapshot(ctx, NULL);
        ASSERT(st == JIT_ERR_INVALID_ARG);
        jit_destroy(ctx);
    }

    snapshot_release(snap);
    return 1;
}

static int test_jit_get_shift_action_null(void) {
    JITShiftActionFn fn = jit_get_shift_action(NULL, 0);
    ASSERT(fn == NULL);
    return 1;
}

static int test_jit_stats_null(void) {
    JITStats stats = jit_get_stats(NULL);
    ASSERT(stats.states_compiled == 0);
    ASSERT(stats.states_total == 0);
    ASSERT(stats.available == false);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Snapshot integration tests                                         */
/* ------------------------------------------------------------------ */

static int test_jit_attach_null(void) {
    JITStatus st = jit_attach_to_snapshot(NULL);
    ASSERT(st == JIT_ERR_INVALID_ARG);
    return 1;
}

static int test_jit_attach_to_snapshot(void) {
    ParserSnapshot *snap = make_test_snapshot();
    ASSERT(snap != NULL);
    ASSERT(snap->jit_ctx == NULL);

    JITStatus st = jit_attach_to_snapshot(snap);

    if (jit_is_available()) {
        ASSERT(st == JIT_OK);
        ASSERT(snap->jit_ctx != NULL);

        /* Attaching again should return ALREADY_COMPILED */
        st = jit_attach_to_snapshot(snap);
        ASSERT(st == JIT_ERR_ALREADY_COMPILED);
    } else {
        /* Without LLVM, attach should fail gracefully */
        ASSERT(st == JIT_ERR_NO_LLVM);
        ASSERT(snap->jit_ctx == NULL);
    }

    snapshot_release(snap);
    return 1;
}

static int test_jit_detach_null(void) {
    /* Should be a safe no-op */
    jit_detach_from_snapshot(NULL);
    return 1;
}

static int test_jit_detach_idempotent(void) {
    ParserSnapshot *snap = make_test_snapshot();
    ASSERT(snap != NULL);

    /* Detach from snapshot with no JIT context */
    jit_detach_from_snapshot(snap);
    ASSERT(snap->jit_ctx == NULL);

    /* Detach again -- should be safe */
    jit_detach_from_snapshot(snap);
    ASSERT(snap->jit_ctx == NULL);

    snapshot_release(snap);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Runtime dispatch tests                                             */
/* ------------------------------------------------------------------ */

static int test_jit_find_shift_action_null(void) {
    uint16_t result = jit_find_shift_action(NULL, 0, 0);
    ASSERT(result == 0);
    return 1;
}

static int test_jit_find_shift_action_table_fallback(void) {
    ParserSnapshot *snap = make_test_snapshot();
    ASSERT(snap != NULL);

    /* No JIT attached -- should use table-driven path */
    ASSERT(snap->jit_ctx == NULL);

    /* State 0, token 0 -> action 10 */
    ASSERT(jit_find_shift_action(snap, 0, 0) == 10);
    /* State 0, token 1 -> action 11 */
    ASSERT(jit_find_shift_action(snap, 0, 1) == 11);
    /* State 0, token 2 -> action 12 */
    ASSERT(jit_find_shift_action(snap, 0, 2) == 12);

    /* State 1, token 0 -> action 20 */
    ASSERT(jit_find_shift_action(snap, 1, 0) == 20);
    /* State 1, token 2 -> action 22 */
    ASSERT(jit_find_shift_action(snap, 1, 2) == 22);

    /* State 2, token 1 -> action 31 */
    ASSERT(jit_find_shift_action(snap, 2, 1) == 31);

    /* State 3 -- all lookups miss, should return default (99) */
    ASSERT(jit_find_shift_action(snap, 3, 0) == 99);
    ASSERT(jit_find_shift_action(snap, 3, 1) == 99);
    ASSERT(jit_find_shift_action(snap, 3, 2) == 99);

    snapshot_release(snap);
    return 1;
}

static int test_jit_find_shift_action_with_jit(void) {
    if (!jit_is_available()) {
        /* Skip: LLVM not available */
        return 1;
    }

    ParserSnapshot *snap = make_test_snapshot();
    ASSERT(snap != NULL);

    JITStatus st = jit_attach_to_snapshot(snap);
    ASSERT(st == JIT_OK);
    ASSERT(snap->jit_ctx != NULL);

    /* JIT path should produce same results as table-driven */
    ASSERT(jit_find_shift_action(snap, 0, 0) == 10);
    ASSERT(jit_find_shift_action(snap, 0, 1) == 11);
    ASSERT(jit_find_shift_action(snap, 0, 2) == 12);
    ASSERT(jit_find_shift_action(snap, 1, 0) == 20);
    ASSERT(jit_find_shift_action(snap, 1, 2) == 22);
    ASSERT(jit_find_shift_action(snap, 2, 1) == 31);
    ASSERT(jit_find_shift_action(snap, 3, 0) == 99);
    ASSERT(jit_find_shift_action(snap, 3, 1) == 99);
    ASSERT(jit_find_shift_action(snap, 3, 2) == 99);

    snapshot_release(snap);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  JIT policy tests                                                   */
/* ------------------------------------------------------------------ */

static int test_policy_default_config(void) {
    JITPolicyConfig cfg = jit_policy_default_config();
    ASSERT(cfg.min_parse_count == 50);
    ASSERT(cfg.min_total_parse_time_ns == 10000000);
    ASSERT(cfg.min_avg_lookups_per_parse == 100);
    ASSERT(cfg.background_compile == true);
    return 1;
}

static int test_metrics_init(void) {
    JITMetrics m;
    jit_metrics_init(&m);
    ASSERT(atomic_load(&m.parse_count) == 0);
    ASSERT(atomic_load(&m.total_parse_time_ns) == 0);
    ASSERT(atomic_load(&m.action_lookup_count) == 0);
    ASSERT(atomic_load(&m.is_jitted) == 0);
    ASSERT(atomic_load(&m.jit_in_progress) == 0);
    return 1;
}

static int test_metrics_init_null(void) {
    /* Should be a safe no-op */
    jit_metrics_init(NULL);
    return 1;
}

static int test_metrics_record_parse(void) {
    JITMetrics m;
    jit_metrics_init(&m);

    jit_metrics_record_parse(&m, 1000, 50);
    ASSERT(atomic_load(&m.parse_count) == 1);
    ASSERT(atomic_load(&m.total_parse_time_ns) == 1000);
    ASSERT(atomic_load(&m.action_lookup_count) == 50);

    jit_metrics_record_parse(&m, 2000, 75);
    ASSERT(atomic_load(&m.parse_count) == 2);
    ASSERT(atomic_load(&m.total_parse_time_ns) == 3000);
    ASSERT(atomic_load(&m.action_lookup_count) == 125);
    return 1;
}

static int test_metrics_record_null(void) {
    /* Should be a safe no-op */
    jit_metrics_record_parse(NULL, 1000, 50);
    return 1;
}

static int test_should_compile_null_args(void) {
    JITPolicyConfig cfg = jit_policy_default_config();
    JITMetrics m;
    jit_metrics_init(&m);

    ASSERT(jit_should_compile(NULL, &cfg) == false);
    ASSERT(jit_should_compile(&m, NULL) == false);
    ASSERT(jit_should_compile(NULL, NULL) == false);
    return 1;
}

static int test_should_compile_below_thresholds(void) {
    JITMetrics m;
    jit_metrics_init(&m);
    JITPolicyConfig cfg = jit_policy_default_config();

    /* No parses yet -- should not compile */
    ASSERT(jit_should_compile(&m, &cfg) == false);

    /* A few parses, not enough */
    for (int i = 0; i < 10; i++) {
        jit_metrics_record_parse(&m, 100000, 200);
    }
    ASSERT(jit_should_compile(&m, &cfg) == false);

    return 1;
}

static int test_should_compile_above_thresholds(void) {
    JITMetrics m;
    jit_metrics_init(&m);

    /* Use very low thresholds to trigger easily */
    JITPolicyConfig cfg;
    cfg.min_parse_count = 5;
    cfg.min_total_parse_time_ns = 1000;
    cfg.min_avg_lookups_per_parse = 10;
    cfg.background_compile = false;

    /* Record enough parses to exceed all thresholds */
    for (int i = 0; i < 10; i++) {
        jit_metrics_record_parse(&m, 500, 50);
    }

    if (jit_is_available()) {
        ASSERT(jit_should_compile(&m, &cfg) == true);
    } else {
        /* Without LLVM, jit_should_compile checks jit_is_available() */
        ASSERT(jit_should_compile(&m, &cfg) == false);
    }

    return 1;
}

static int test_should_compile_already_jitted(void) {
    JITMetrics m;
    jit_metrics_init(&m);

    JITPolicyConfig cfg;
    cfg.min_parse_count = 1;
    cfg.min_total_parse_time_ns = 1;
    cfg.min_avg_lookups_per_parse = 1;
    cfg.background_compile = false;

    jit_metrics_record_parse(&m, 1000000, 500);

    /* Mark as already JIT compiled */
    atomic_store(&m.is_jitted, 1);
    ASSERT(jit_should_compile(&m, &cfg) == false);

    return 1;
}

static int test_should_compile_in_progress(void) {
    JITMetrics m;
    jit_metrics_init(&m);

    JITPolicyConfig cfg;
    cfg.min_parse_count = 1;
    cfg.min_total_parse_time_ns = 1;
    cfg.min_avg_lookups_per_parse = 1;
    cfg.background_compile = false;

    jit_metrics_record_parse(&m, 1000000, 500);

    /* Mark compilation in progress */
    atomic_store(&m.jit_in_progress, 1);
    ASSERT(jit_should_compile(&m, &cfg) == false);

    return 1;
}

static int test_should_compile_low_lookups(void) {
    JITMetrics m;
    jit_metrics_init(&m);

    JITPolicyConfig cfg = jit_policy_default_config();

    /* Many parses with enough time but very few lookups per parse */
    for (int i = 0; i < 100; i++) {
        jit_metrics_record_parse(&m, 200000, 5);
    }
    /* avg lookups = 5, threshold = 100 */
    ASSERT(jit_should_compile(&m, &cfg) == false);

    return 1;
}

/* ------------------------------------------------------------------ */
/*  jit_maybe_compile tests                                            */
/* ------------------------------------------------------------------ */

static int test_maybe_compile_null_args(void) {
    JITMetrics m;
    jit_metrics_init(&m);
    JITPolicyConfig cfg = jit_policy_default_config();
    ParserSnapshot *snap = make_test_snapshot();
    ASSERT(snap != NULL);

    ASSERT(jit_maybe_compile(NULL, &m, &cfg) == -1);
    ASSERT(jit_maybe_compile(snap, NULL, &cfg) == -1);
    ASSERT(jit_maybe_compile(snap, &m, NULL) == -1);

    snapshot_release(snap);
    return 1;
}

static int test_maybe_compile_not_ready(void) {
    JITMetrics m;
    jit_metrics_init(&m);
    JITPolicyConfig cfg = jit_policy_default_config();
    ParserSnapshot *snap = make_test_snapshot();
    ASSERT(snap != NULL);

    /* Metrics below thresholds -- should return 1 (not ready) */
    int result = jit_maybe_compile(snap, &m, &cfg);
    ASSERT(result == 1);

    snapshot_release(snap);
    return 1;
}

static int test_maybe_compile_sync(void) {
    JITMetrics m;
    jit_metrics_init(&m);

    /* Low thresholds, synchronous mode */
    JITPolicyConfig cfg;
    cfg.min_parse_count = 2;
    cfg.min_total_parse_time_ns = 100;
    cfg.min_avg_lookups_per_parse = 5;
    cfg.background_compile = false;

    ParserSnapshot *snap = make_test_snapshot();
    ASSERT(snap != NULL);

    /* Record enough parses */
    for (int i = 0; i < 5; i++) {
        jit_metrics_record_parse(&m, 1000, 50);
    }

    int result = jit_maybe_compile(snap, &m, &cfg);

    if (jit_is_available()) {
        ASSERT(result == 0);
        ASSERT(atomic_load(&m.is_jitted) == 1);
        ASSERT(snap->jit_ctx != NULL);

        /* Verify JIT produces correct results */
        ASSERT(jit_find_shift_action(snap, 0, 0) == 10);
        ASSERT(jit_find_shift_action(snap, 0, 1) == 11);
    } else {
        /* Without LLVM, should return 1 (not ready, since
        ** jit_should_compile returns false when LLVM unavailable) */
        ASSERT(result == 1);
        ASSERT(atomic_load(&m.is_jitted) == 0);
    }

    snapshot_release(snap);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Public API wrapper tests                                           */
/* ------------------------------------------------------------------ */

static int test_lime_jit_available(void) {
    /* Just call it -- should match jit_is_available() */
    bool pub = lime_jit_available();
    bool internal = jit_is_available();
    ASSERT(pub == internal);
    return 1;
}

static int test_lime_jit_compile(void) {
    ParserSnapshot *snap = make_test_snapshot();
    ASSERT(snap != NULL);

    int result = lime_jit_compile(snap);

    if (jit_is_available()) {
        ASSERT(result == 0);
        ASSERT(snap->jit_ctx != NULL);

        /* Calling again should succeed (already compiled is OK) */
        result = lime_jit_compile(snap);
        ASSERT(result == 0);
    } else {
        ASSERT(result != 0);
    }

    snapshot_release(snap);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Policy shutdown test                                               */
/* ------------------------------------------------------------------ */

static int test_policy_shutdown(void) {
    /* Should be a safe no-op */
    jit_policy_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("JIT compilation tests\n");
    printf("  LLVM available: %s\n\n", jit_is_available() ? "yes" : "no (stub mode)");

    /* JIT context */
    TEST(jit_is_available);
    TEST(jit_status_strings);
    TEST(jit_create_null_arg);
    TEST(jit_create_destroy);
    TEST(jit_destroy_null);
    TEST(jit_compile_null_args);
    TEST(jit_get_shift_action_null);
    TEST(jit_stats_null);

    /* Snapshot integration */
    TEST(jit_attach_null);
    TEST(jit_attach_to_snapshot);
    TEST(jit_detach_null);
    TEST(jit_detach_idempotent);

    /* Runtime dispatch */
    TEST(jit_find_shift_action_null);
    TEST(jit_find_shift_action_table_fallback);
    TEST(jit_find_shift_action_with_jit);

    /* Policy defaults */
    TEST(policy_default_config);

    /* Metrics */
    TEST(metrics_init);
    TEST(metrics_init_null);
    TEST(metrics_record_parse);
    TEST(metrics_record_null);

    /* Policy evaluation */
    TEST(should_compile_null_args);
    TEST(should_compile_below_thresholds);
    TEST(should_compile_above_thresholds);
    TEST(should_compile_already_jitted);
    TEST(should_compile_in_progress);
    TEST(should_compile_low_lookups);

    /* jit_maybe_compile */
    TEST(maybe_compile_null_args);
    TEST(maybe_compile_not_ready);
    TEST(maybe_compile_sync);

    /* Public API wrappers */
    TEST(lime_jit_available);
    TEST(lime_jit_compile);

    /* Shutdown */
    TEST(policy_shutdown);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
