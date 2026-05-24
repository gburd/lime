/*
** Comprehensive Extension Framework Overhead Benchmark
**
** Measures the overhead introduced by the extension framework compared
** to a baseline parser with no extensions.  Scenarios:
**
**   1. Baseline: snapshot operations, no extensions
**   2. Extension registry: register/unregister, lookup, dependency check
**   3. Conflict detection: token, rule, semantic conflict scanning
**   4. Disambiguation: priority strategy, fork-resolve strategy
**   5. Execution policy: FIRST_ONLY, ALL, CHAIN, CONDITIONAL
**   6. Parser fork: clone, fork set management
**   7. Grammar context: mode push/pop, switch detection
**   8. Combined: full pipeline (register + load + detect + resolve + execute)
**
** Output: CSV data on stdout, diagnostic info on stderr.
*/
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lime_time.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>

/*
** Include order matters: execution_policy.h defines its own
** GrammarExtensionMetadata (for the execution pipeline), while
** extension.h (internal) defines the Extension struct for the
** registry.  disambiguation.h and conflict.h are safe alongside
** both.  Do NOT include extension_registry.h here as it would
** re-define GrammarExtensionMetadata.
*/
#include "execution_policy.h"
#include "disambiguation.h"
#include "conflict.h"
#include "extension.h"
#include "parser_fork.h"
#include "grammar_context.h"
#include "snapshot.h"
#include "jit_context.h"

/* ------------------------------------------------------------------ */
/*  Timing infrastructure                                              */
/* ------------------------------------------------------------------ */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

typedef struct BenchResult {
    const char *category;
    const char *name;
    uint64_t iterations;
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    double ns_per_op;
    double ops_per_sec;
    double stddev_ns;
} BenchResult;

static void bench_header(void) {
    printf("category,benchmark,iterations,total_ns,min_ns,max_ns,"
           "ns_per_op,ops_per_sec,stddev_ns\n");
}

static void bench_print(const BenchResult *r) {
    printf("%s,%s,%lu,%lu,%lu,%lu,%.2f,%.0f,%.2f\n",
           r->category, r->name,
           (unsigned long)r->iterations,
           (unsigned long)r->total_ns,
           (unsigned long)r->min_ns,
           (unsigned long)r->max_ns,
           r->ns_per_op,
           r->ops_per_sec,
           r->stddev_ns);
}

typedef void (*BenchFn)(void *ctx);

static BenchResult run_bench(const char *category, const char *name,
                             BenchFn fn, void *ctx,
                             uint64_t warmup, uint64_t iters) {
    BenchResult r;
    r.category = category;
    r.name = name;
    r.iterations = iters;
    r.min_ns = UINT64_MAX;
    r.max_ns = 0;

    /* Warmup */
    for (uint64_t i = 0; i < warmup; i++) fn(ctx);

    /* Collect per-iteration times for stddev */
    double *times = malloc(iters * sizeof(double));
    uint64_t total = 0;

    for (uint64_t i = 0; i < iters; i++) {
        uint64_t start = now_ns();
        fn(ctx);
        uint64_t elapsed = now_ns() - start;

        if (times) times[i] = (double)elapsed;
        total += elapsed;
        if (elapsed < r.min_ns) r.min_ns = elapsed;
        if (elapsed > r.max_ns) r.max_ns = elapsed;
    }

    r.total_ns = total;
    r.ns_per_op = (double)total / (double)iters;
    r.ops_per_sec = iters > 0 ? (double)iters / ((double)total / 1e9) : 0.0;

    /* Stddev */
    r.stddev_ns = 0.0;
    if (times && iters > 1) {
        double variance = 0.0;
        for (uint64_t i = 0; i < iters; i++) {
            double diff = times[i] - r.ns_per_op;
            variance += diff * diff;
        }
        r.stddev_ns = sqrt(variance / (double)iters);
    }
    free(times);

    return r;
}

/* ------------------------------------------------------------------ */
/*  Helper: create a minimal snapshot for testing                       */
/* ------------------------------------------------------------------ */

static ParserSnapshot *create_test_snapshot(uint32_t nstates,
                                             uint32_t nterminals) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (!snap) return NULL;

    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    snap->nstate = nstates;
    snap->nterminal = nterminals;

    uint32_t table_size = nstates * nterminals;
    snap->action_count = table_size;
    snap->lookahead_count = table_size;

    snap->yy_action = calloc(table_size, sizeof(uint16_t));
    snap->yy_lookahead = calloc(table_size, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(nstates, sizeof(int16_t));
    snap->yy_reduce_ofst = calloc(nstates, sizeof(int16_t));
    snap->yy_default = calloc(nstates, sizeof(uint16_t));

    if (!snap->yy_action || !snap->yy_lookahead ||
        !snap->yy_shift_ofst || !snap->yy_reduce_ofst ||
        !snap->yy_default) {
        snapshot_release(snap);
        return NULL;
    }

    for (uint32_t s = 0; s < nstates; s++) {
        snap->yy_shift_ofst[s] = (int16_t)(s * nterminals);
        snap->yy_default[s] = (uint16_t)(1000 + s);
        for (uint32_t t = 0; t < nterminals; t++) {
            uint32_t idx = s * nterminals + t;
            snap->yy_lookahead[idx] = (uint16_t)t;
            snap->yy_action[idx] = (uint16_t)(s * 10 + t);
        }
    }

    snap->jit_ctx = NULL;
    return snap;
}

/* ================================================================== */
/*  1. BASELINE: Snapshot acquire/release and action lookup             */
/* ================================================================== */

typedef struct {
    ParserSnapshot *snap;
} SnapCtx;

static void bench_snap_acquire_release(void *arg) {
    SnapCtx *ctx = (SnapCtx *)arg;
    ParserSnapshot *ref = snapshot_acquire(ctx->snap);
    snapshot_release(ref);
}

static void bench_snap_action_lookup(void *arg) {
    SnapCtx *ctx = (SnapCtx *)arg;
    /* Look up 50 actions (simulating a moderate parse) */
    for (int i = 0; i < 50; i++) {
        uint16_t action = jit_find_shift_action(
            ctx->snap, (uint16_t)(i % 64), (uint16_t)(i % 32));
        (void)action;
    }
}

static void run_baseline_benchmarks(uint64_t warmup, uint64_t iters) {
    ParserSnapshot *snap = create_test_snapshot(64, 32);
    if (!snap) return;

    SnapCtx ctx = { .snap = snap };

    BenchResult r;
    r = run_bench("baseline", "snapshot_acquire_release",
                  bench_snap_acquire_release, &ctx, warmup, iters);
    bench_print(&r);

    r = run_bench("baseline", "action_lookup_50_tokens",
                  bench_snap_action_lookup, &ctx, warmup, iters);
    bench_print(&r);

    snapshot_release(snap);
}

/* ================================================================== */
/*  2. EXTENSION REGISTRY: register, lookup, load                      */
/* ================================================================== */

static bool mock_get_mods(void *user_data,
                          const struct ParserSnapshot *base,
                          GrammarModification **mods_out,
                          uint32_t *nmods_out) {
    (void)user_data; (void)base;
    *mods_out = NULL;
    *nmods_out = 0;
    return true;
}

typedef struct {
    ExtensionRegistry *reg;
    ExtensionID ids[16];
    int nids;
} RegCtx;

static void bench_registry_lookup(void *arg) {
    RegCtx *ctx = (RegCtx *)arg;
    for (int i = 0; i < ctx->nids; i++) {
        const Extension *ext = find_extension(ctx->reg, ctx->ids[i]);
        (void)ext;
    }
}

static void bench_registry_loaded_count(void *arg) {
    RegCtx *ctx = (RegCtx *)arg;
    uint32_t n = get_loaded_extension_count(ctx->reg);
    (void)n;
}

static void run_registry_benchmarks(uint64_t warmup, uint64_t iters) {
    RegCtx ctx;
    ctx.reg = create_extension_registry();
    if (!ctx.reg) return;
    ctx.nids = 0;

    /* Pre-register several extensions */
    const char *names[] = {
        "ext-jsonb", "ext-power", "ext-bitwise", "ext-xquery",
        "ext-xpath", "ext-edn", "ext-json", "ext-pgvector",
    };
    for (int i = 0; i < 8; i++) {
        ExtensionInfo info = {
            .name = names[i],
            .version = "1.0",
            .get_modifications = mock_get_mods,
        };
        register_extension(ctx.reg, &info, &ctx.ids[ctx.nids]);
        load_extension(ctx.reg, ctx.ids[ctx.nids], NULL, NULL);
        ctx.nids++;
    }

    BenchResult r;
    r = run_bench("registry", "lookup_8_extensions",
                  bench_registry_lookup, &ctx, warmup, iters);
    bench_print(&r);

    r = run_bench("registry", "get_loaded_count",
                  bench_registry_loaded_count, &ctx, warmup, iters);
    bench_print(&r);

    destroy_extension_registry(ctx.reg);
}

/* ================================================================== */
/*  3. CONFLICT DETECTION: token, rule, semantic                       */
/* ================================================================== */

typedef struct {
    ExtensionRegistry *reg;
} ConflictCtx;

static void bench_detect_token_conflicts(void *arg) {
    ConflictCtx *ctx = (ConflictCtx *)arg;
    MultiGrammarConflictResult *result = multi_conflict_result_create();
    if (!result) return;
    detect_token_conflicts(ctx->reg, result);
    multi_conflict_result_destroy(result);
}

static void bench_detect_rule_conflicts(void *arg) {
    ConflictCtx *ctx = (ConflictCtx *)arg;
    MultiGrammarConflictResult *result = multi_conflict_result_create();
    if (!result) return;
    detect_rule_conflicts(ctx->reg, 42, 10, result);
    multi_conflict_result_destroy(result);
}

static void bench_detect_all_conflicts(void *arg) {
    ConflictCtx *ctx = (ConflictCtx *)arg;
    MultiGrammarConflictResult *result = multi_conflict_result_create();
    if (!result) return;
    detect_all_multi_grammar_conflicts(ctx->reg, result);
    multi_conflict_result_destroy(result);
}

static void bench_detect_conflict_single(void *arg) {
    ConflictCtx *ctx = (ConflictCtx *)arg;
    ConflictPoint cp = detect_conflict(ctx->reg, 42, 10);
    conflict_point_destroy(&cp);
}

/*
** Create a registry with extensions that have conflicting token modifications.
*/
static ExtensionRegistry *create_conflicting_registry(void) {
    ExtensionRegistry *reg = create_extension_registry();
    if (!reg) return NULL;

    /* Create two extensions that both define "CARET" token */
    static GrammarModification mods_power[2];
    mods_power[0].type = MOD_ADD_TOKEN;
    mods_power[0].description = "add CARET for exponentiation";
    mods_power[0].u.add_token.name = "CARET";
    mods_power[0].u.add_token.lexeme = "^";
    mods_power[0].u.add_token.token_code = 42;

    mods_power[1].type = MOD_ADD_RULE;
    mods_power[1].description = "expr -> expr CARET expr";
    mods_power[1].u.add_rule.lhs = "expr";
    static const char *rhs_power[] = { "expr", "CARET", "expr", NULL };
    mods_power[1].u.add_rule.rhs = rhs_power;
    mods_power[1].u.add_rule.nrhs = 3;
    mods_power[1].u.add_rule.code = "{ A = pow(B, D); }";
    mods_power[1].u.add_rule.precedence = 60;

    static GrammarModification mods_bitwise[2];
    mods_bitwise[0].type = MOD_ADD_TOKEN;
    mods_bitwise[0].description = "add CARET for XOR";
    mods_bitwise[0].u.add_token.name = "CARET";
    mods_bitwise[0].u.add_token.lexeme = "^";
    mods_bitwise[0].u.add_token.token_code = 42;

    mods_bitwise[1].type = MOD_ADD_RULE;
    mods_bitwise[1].description = "expr -> expr CARET expr";
    mods_bitwise[1].u.add_rule.lhs = "expr";
    static const char *rhs_bitwise[] = { "expr", "CARET", "expr", NULL };
    mods_bitwise[1].u.add_rule.rhs = rhs_bitwise;
    mods_bitwise[1].u.add_rule.nrhs = 3;
    mods_bitwise[1].u.add_rule.code = "{ A = B ^ D; }";
    mods_bitwise[1].u.add_rule.precedence = 30;

    /* Register and load extension A (power) */
    ExtensionInfo info_a = {
        .name = "ext-power",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionID id_a;
    register_extension(reg, &info_a, &id_a);
    load_extension(reg, id_a, NULL, NULL);

    /* Manually inject modifications */
    pthread_rwlock_wrlock(&reg->lock);
    reg->extensions[0].modifications = mods_power;
    reg->extensions[0].nmodifications = 2;
    pthread_rwlock_unlock(&reg->lock);

    /* Register and load extension B (bitwise) */
    ExtensionInfo info_b = {
        .name = "ext-bitwise",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionID id_b;
    register_extension(reg, &info_b, &id_b);
    load_extension(reg, id_b, NULL, NULL);

    pthread_rwlock_wrlock(&reg->lock);
    reg->extensions[1].modifications = mods_bitwise;
    reg->extensions[1].nmodifications = 2;
    pthread_rwlock_unlock(&reg->lock);

    return reg;
}

static void cleanup_conflicting_registry(ExtensionRegistry *reg) {
    if (!reg) return;
    /* Reset modifications to NULL before destroying to avoid freeing statics */
    pthread_rwlock_wrlock(&reg->lock);
    for (uint32_t i = 0; i < reg->count; i++) {
        reg->extensions[i].modifications = NULL;
        reg->extensions[i].nmodifications = 0;
    }
    pthread_rwlock_unlock(&reg->lock);
    destroy_extension_registry(reg);
}

static void run_conflict_benchmarks(uint64_t warmup, uint64_t iters) {
    /* No-conflict scenario */
    ExtensionRegistry *reg_clean = create_extension_registry();
    if (!reg_clean) return;

    ExtensionInfo info = {
        .name = "ext-clean",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionID id;
    register_extension(reg_clean, &info, &id);
    load_extension(reg_clean, id, NULL, NULL);

    ConflictCtx ctx_clean = { .reg = reg_clean };

    BenchResult r;
    r = run_bench("conflict", "detect_token_no_conflict",
                  bench_detect_token_conflicts, &ctx_clean, warmup, iters);
    bench_print(&r);

    r = run_bench("conflict", "detect_single_no_conflict",
                  bench_detect_conflict_single, &ctx_clean, warmup, iters);
    bench_print(&r);

    destroy_extension_registry(reg_clean);

    /* With-conflict scenario */
    ExtensionRegistry *reg_conflict = create_conflicting_registry();
    if (!reg_conflict) return;

    ConflictCtx ctx_conflict = { .reg = reg_conflict };

    r = run_bench("conflict", "detect_token_with_conflict",
                  bench_detect_token_conflicts, &ctx_conflict, warmup, iters);
    bench_print(&r);

    r = run_bench("conflict", "detect_rule_with_conflict",
                  bench_detect_rule_conflicts, &ctx_conflict, warmup, iters);
    bench_print(&r);

    r = run_bench("conflict", "detect_all_with_conflict",
                  bench_detect_all_conflicts, &ctx_conflict, warmup, iters);
    bench_print(&r);

    r = run_bench("conflict", "detect_single_with_conflict",
                  bench_detect_conflict_single, &ctx_conflict, warmup, iters);
    bench_print(&r);

    cleanup_conflicting_registry(reg_conflict);
}

/* ================================================================== */
/*  4. DISAMBIGUATION: priority and fork-resolve strategies            */
/* ================================================================== */

typedef struct {
    DisambiguationContext *dctx;
    ConflictPoint cp;
} DisambigCtx;

static void bench_disambig_resolve(void *arg) {
    DisambigCtx *ctx = (DisambigCtx *)arg;
    StrategyResult result = disambiguation_resolve(ctx->dctx, &ctx->cp, NULL);
    strategy_result_cleanup(&result);
}

static void bench_disambig_update(void *arg) {
    DisambigCtx *ctx = (DisambigCtx *)arg;
    disambiguation_update(ctx->dctx, true);
}

static void run_disambiguation_benchmarks(uint64_t warmup, uint64_t iters) {
    /* Create a registry for disambiguation context */
    ExtensionRegistry *reg = create_extension_registry();
    if (!reg) return;

    ExtensionInfo info1 = {
        .name = "ext-high",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionInfo info2 = {
        .name = "ext-low",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionID id1, id2;
    register_extension(reg, &info1, &id1);
    register_extension(reg, &info2, &id2);
    load_extension(reg, id1, NULL, NULL);
    load_extension(reg, id2, NULL, NULL);

    /* Build a conflict point with 2 contexts */
    ConflictPoint cp;
    conflict_point_init(&cp, 42, 10, CONFLICT_LEVEL_TOKEN);
    LimeContext ctx1 = { .ext_id = id1, .token = 42, .state = 10,
                         .priority = 100, .grammar_name = "ext-high" };
    LimeContext ctx2 = { .ext_id = id2, .token = 42, .state = 10,
                         .priority = 50, .grammar_name = "ext-low" };
    conflict_point_add_context(&cp, &ctx1);
    conflict_point_add_context(&cp, &ctx2);

    BenchResult r;

    /* PRIORITY strategy */
    DisambiguationContext *dctx_prio =
        disambiguation_create(STRAT_PRIORITY, reg);
    if (dctx_prio) {
        DisambigCtx dctx = { .dctx = dctx_prio, .cp = cp };

        r = run_bench("disambiguation", "priority_resolve",
                      bench_disambig_resolve, &dctx, warmup, iters);
        bench_print(&r);

        r = run_bench("disambiguation", "priority_update",
                      bench_disambig_update, &dctx, warmup, iters);
        bench_print(&r);

        disambiguation_destroy(dctx_prio);
    }

    /* FORK_RESOLVE strategy */
    DisambiguationContext *dctx_fork =
        disambiguation_create(STRAT_FORK_RESOLVE, reg);
    if (dctx_fork) {
        DisambigCtx dctx = { .dctx = dctx_fork, .cp = cp };

        r = run_bench("disambiguation", "fork_resolve_resolve",
                      bench_disambig_resolve, &dctx, warmup, iters);
        bench_print(&r);

        r = run_bench("disambiguation", "fork_resolve_update",
                      bench_disambig_update, &dctx, warmup, iters);
        bench_print(&r);

        disambiguation_destroy(dctx_fork);
    }

    conflict_point_destroy(&cp);
    destroy_extension_registry(reg);
}

/* ================================================================== */
/*  5. EXECUTION POLICY                                                */
/* ================================================================== */

static int exec_parser_slots[8];

static bool mock_execute(LimeParserHandle *parser,
                         const GrammarExtensionMetadata *ext,
                         void *input,
                         void **result,
                         char **error) {
    (void)parser; (void)input;
    int *out = malloc(sizeof(int));
    if (out) *out = (int)ext->extension_id;
    *result = out;
    *error = NULL;
    return true;
}

typedef struct {
    ExecutionPolicyConfig config;
    StrategyResult strat_result;
    LimeParserHandle *parsers[4];
    GrammarExtensionMetadata *ext_ptrs[4];
    GrammarExtensionMetadata ext_storage[4];
} ExecCtx;

static void bench_exec_first_only(void *arg) {
    ExecCtx *ctx = (ExecCtx *)arg;
    ctx->config.policy = EXEC_FIRST_ONLY;
    int nresults = 0;
    ExecutionResult *results = execute_semantic_actions(
        &ctx->config, &ctx->strat_result,
        ctx->parsers, ctx->ext_ptrs, &nresults);
    if (results) {
        for (int i = 0; i < nresults; i++) {
            free(results[i].result);
            free(results[i].error);
        }
        execution_results_free(results, nresults);
    }
}

static void bench_exec_all(void *arg) {
    ExecCtx *ctx = (ExecCtx *)arg;
    ctx->config.policy = EXEC_ALL;
    int nresults = 0;
    ExecutionResult *results = execute_semantic_actions(
        &ctx->config, &ctx->strat_result,
        ctx->parsers, ctx->ext_ptrs, &nresults);
    if (results) {
        for (int i = 0; i < nresults; i++) {
            free(results[i].result);
            free(results[i].error);
        }
        execution_results_free(results, nresults);
    }
}

static void bench_exec_chain(void *arg) {
    ExecCtx *ctx = (ExecCtx *)arg;
    ctx->config.policy = EXEC_CHAIN;
    int nresults = 0;
    ExecutionResult *results = execute_semantic_actions(
        &ctx->config, &ctx->strat_result,
        ctx->parsers, ctx->ext_ptrs, &nresults);
    if (results) {
        for (int i = 0; i < nresults; i++) {
            free(results[i].result);
            free(results[i].error);
        }
        execution_results_free(results, nresults);
    }
}

static void run_execution_policy_benchmarks(uint64_t warmup, uint64_t iters) {
    ExecCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    execution_policy_config_init(&ctx.config);
    ctx.config.execute_fn = mock_execute;

    /* Set up a strategy result with 2 winners */
    strategy_result_init(&ctx.strat_result);
    ctx.strat_result.winning_contexts = malloc(2 * sizeof(LimeContext));
    if (!ctx.strat_result.winning_contexts) return;
    ctx.strat_result.nwinners = 2;
    ctx.strat_result.confidence = 0.9f;
    ctx.strat_result.winning_contexts[0] = (LimeContext){
        .ext_id = 1, .token = 42, .state = 10, .priority = 100 };
    ctx.strat_result.winning_contexts[1] = (LimeContext){
        .ext_id = 2, .token = 42, .state = 10, .priority = 50 };

    /* Set up parser handles and extension metadata */
    for (int i = 0; i < 2; i++) {
        ctx.parsers[i] = (LimeParserHandle *)&exec_parser_slots[i];
        ctx.ext_storage[i].extension_id = (uint32_t)(i + 1);
        ctx.ext_storage[i].name = (i == 0) ? "ext-high" : "ext-low";
        ctx.ext_storage[i].priority = (i == 0) ? 100 : 50;
        ctx.ext_storage[i].user_data = NULL;
        ctx.ext_storage[i].should_execute = NULL;
        ctx.ext_ptrs[i] = &ctx.ext_storage[i];
    }

    BenchResult r;
    r = run_bench("exec_policy", "first_only_2_winners",
                  bench_exec_first_only, &ctx, warmup, iters);
    bench_print(&r);

    r = run_bench("exec_policy", "all_2_winners",
                  bench_exec_all, &ctx, warmup, iters);
    bench_print(&r);

    r = run_bench("exec_policy", "chain_2_winners",
                  bench_exec_chain, &ctx, warmup, iters);
    bench_print(&r);

    strategy_result_cleanup(&ctx.strat_result);
}

/* ================================================================== */
/*  6. PARSER FORK                                                     */
/* ================================================================== */

/* Mock parser struct matching the layout expected by clone_parser_state() */
#define MOCK_STACK_DEPTH 8

typedef struct MockStackEntry {
    uint16_t stateno;
    uint16_t major;
    uint64_t minor;
} MockStackEntry;

typedef struct MockParser {
    MockStackEntry *yytos;
    int yyerrcnt;
    MockStackEntry *yystackEnd;
    MockStackEntry *yystack;
    MockStackEntry yystk0[MOCK_STACK_DEPTH];
} MockParser;

static void mock_parser_init(MockParser *p) {
    memset(p, 0, sizeof(*p));
    p->yystack = p->yystk0;
    p->yytos = &p->yystk0[3]; /* simulate 3 items on stack */
    p->yystackEnd = &p->yystk0[MOCK_STACK_DEPTH - 1];
    p->yyerrcnt = -1;
    for (int i = 0; i < 4; i++) {
        p->yystk0[i].stateno = (uint16_t)(i * 10);
        p->yystk0[i].major = (uint16_t)(i + 1);
        p->yystk0[i].minor = (uint64_t)(100 + i);
    }
}

typedef struct {
    MockParser parser;
    ParserSnapshot *snap;
} ForkCtx;

static void bench_clone_parser(void *arg) {
    ForkCtx *ctx = (ForkCtx *)arg;
    ClonedParserState cloned;
    bool ok = clone_parser_state(
        &ctx->parser,
        sizeof(MockParser),
        sizeof(MockStackEntry),
        offsetof(MockParser, yystk0),
        MOCK_STACK_DEPTH,
        offsetof(MockParser, yystack),
        offsetof(MockParser, yytos),
        offsetof(MockParser, yystackEnd),
        &cloned);
    if (ok) cloned_parser_state_destroy(&cloned);
}

static void bench_fork_parser(void *arg) {
    ForkCtx *ctx = (ForkCtx *)arg;
    ParseFork *fork = fork_parser(
        &ctx->parser,
        sizeof(MockParser),
        sizeof(MockStackEntry),
        offsetof(MockParser, yystk0),
        MOCK_STACK_DEPTH,
        offsetof(MockParser, yystack),
        offsetof(MockParser, yytos),
        offsetof(MockParser, yystackEnd),
        ctx->snap,
        0);
    if (fork) free_parse_fork(fork);
}

static void bench_fork_set_lifecycle(void *arg) {
    ForkCtx *ctx = (ForkCtx *)arg;
    ParseForkSet *set = parse_fork_set_create(4);
    if (!set) return;

    for (int i = 0; i < 2; i++) {
        ParseFork *fork = fork_parser(
            &ctx->parser,
            sizeof(MockParser),
            sizeof(MockStackEntry),
            offsetof(MockParser, yystk0),
            MOCK_STACK_DEPTH,
            offsetof(MockParser, yystack),
            offsetof(MockParser, yytos),
            offsetof(MockParser, yystackEnd),
            ctx->snap,
            i);
        if (fork) {
            parse_fork_set_add(set, fork);
            if (i == 0) parse_fork_complete(fork, NULL, NULL);
            else parse_fork_fail(fork);
        }
    }

    parse_fork_set_prune(set);
    ParseFork *best = parse_fork_set_best(set);
    (void)best;

    parse_fork_set_destroy(set);
}

static void run_fork_benchmarks(uint64_t warmup, uint64_t iters) {
    ForkCtx ctx;
    mock_parser_init(&ctx.parser);
    ctx.snap = create_test_snapshot(64, 32);
    if (!ctx.snap) return;

    BenchResult r;
    r = run_bench("parser_fork", "clone_parser_state",
                  bench_clone_parser, &ctx, warmup, iters);
    bench_print(&r);

    r = run_bench("parser_fork", "fork_parser_full",
                  bench_fork_parser, &ctx, warmup, iters);
    bench_print(&r);

    r = run_bench("parser_fork", "fork_set_2_forks",
                  bench_fork_set_lifecycle, &ctx, warmup, iters);
    bench_print(&r);

    snapshot_release(ctx.snap);
}

/* ================================================================== */
/*  7. GRAMMAR CONTEXT: mode switching                                 */
/* ================================================================== */

typedef struct {
    GrammarContextStack *stack;
} GramCtx;

static void bench_grammar_context_root_check(void *arg) {
    GramCtx *ctx = (GramCtx *)arg;
    bool root = grammar_context_is_root_only(ctx->stack);
    (void)root;
}

static void bench_grammar_context_push_pop(void *arg) {
    GramCtx *ctx = (GramCtx *)arg;
    grammar_context_push(ctx->stack, MODE_XQUERY, 100);
    grammar_context_pop(ctx->stack);
}

static void bench_grammar_context_detect_switch(void *arg) {
    GramCtx *ctx = (GramCtx *)arg;
    /* Simulate checking a token that does not trigger a switch */
    grammar_context_detect_switch(ctx->stack, 99, "SELECT", 50);
}

static void run_grammar_context_benchmarks(uint64_t warmup, uint64_t iters) {
    ParserSnapshot *root_snap = create_test_snapshot(64, 32);
    if (!root_snap) return;

    GrammarContextStack *stack = grammar_context_create(root_snap);
    if (!stack) {
        snapshot_release(root_snap);
        return;
    }

    /* Register a mode */
    ParserSnapshot *xq_snap = create_test_snapshot(32, 16);
    if (xq_snap) {
        GrammarModeInfo xq_info = {
            .mode = MODE_XQUERY,
            .name = "xquery",
            .snapshot = xq_snap,
            .trigger_token = 200,
            .trigger_lexeme = NULL,
            .exit_token = -1,
        };
        grammar_context_register_mode(stack, &xq_info);
        snapshot_release(xq_snap);
    }

    GramCtx ctx = { .stack = stack };

    BenchResult r;
    r = run_bench("grammar_ctx", "is_root_only",
                  bench_grammar_context_root_check, &ctx, warmup, iters);
    bench_print(&r);

    r = run_bench("grammar_ctx", "push_pop_mode",
                  bench_grammar_context_push_pop, &ctx, warmup, iters);
    bench_print(&r);

    r = run_bench("grammar_ctx", "detect_switch_no_match",
                  bench_grammar_context_detect_switch, &ctx, warmup, iters);
    bench_print(&r);

    grammar_context_destroy(stack);
    snapshot_release(root_snap);
}

/* ================================================================== */
/*  8. COMBINED PIPELINE: full extension overhead                      */
/* ================================================================== */

typedef struct {
    ExtensionRegistry *reg;
    DisambiguationContext *dctx;
    ExecutionPolicyConfig config;
    LimeParserHandle *parsers[2];
    GrammarExtensionMetadata *ext_ptrs[2];
    GrammarExtensionMetadata ext_storage[2];
} PipelineCtx;

static void bench_pipeline_no_extensions(void *arg) {
    PipelineCtx *ctx = (PipelineCtx *)arg;
    /* Fast path: check extension count */
    uint32_t n = get_loaded_extension_count(ctx->reg);
    (void)n;
}

static void bench_pipeline_with_extensions(void *arg) {
    PipelineCtx *ctx = (PipelineCtx *)arg;

    /* 1. Check if extensions are loaded */
    uint32_t n = get_loaded_extension_count(ctx->reg);
    if (n == 0) return;

    /* 2. Detect conflict for this token */
    ConflictPoint cp = detect_conflict(ctx->reg, 42, 10);

    /* 3. If conflict, resolve via disambiguation */
    if (cp.ncontexts > 1 && ctx->dctx) {
        StrategyResult result = disambiguation_resolve(ctx->dctx, &cp, NULL);

        /* 4. Execute via policy */
        if (result.nwinners > 0) {
            int nresults = 0;
            ExecutionResult *results = execute_semantic_actions(
                &ctx->config, &result,
                ctx->parsers, ctx->ext_ptrs, &nresults);
            if (results) {
                for (int i = 0; i < nresults; i++) {
                    free(results[i].result);
                    free(results[i].error);
                }
                execution_results_free(results, nresults);
            }
        }

        strategy_result_cleanup(&result);
    }

    conflict_point_destroy(&cp);
}

static void run_pipeline_benchmarks(uint64_t warmup, uint64_t iters) {
    /* Empty registry (no-extension fast path) */
    ExtensionRegistry *reg_empty = create_extension_registry();
    if (!reg_empty) return;

    PipelineCtx ctx_empty;
    memset(&ctx_empty, 0, sizeof(ctx_empty));
    ctx_empty.reg = reg_empty;

    BenchResult r;
    r = run_bench("pipeline", "no_extensions_fast_path",
                  bench_pipeline_no_extensions, &ctx_empty, warmup, iters);
    bench_print(&r);

    destroy_extension_registry(reg_empty);

    /* Registry with conflicting extensions */
    ExtensionRegistry *reg = create_conflicting_registry();
    if (!reg) return;

    DisambiguationContext *dctx = disambiguation_create(STRAT_PRIORITY, reg);

    PipelineCtx ctx_full;
    memset(&ctx_full, 0, sizeof(ctx_full));
    ctx_full.reg = reg;
    ctx_full.dctx = dctx;
    execution_policy_config_init(&ctx_full.config);
    ctx_full.config.execute_fn = mock_execute;

    for (int i = 0; i < 2; i++) {
        ctx_full.parsers[i] = (LimeParserHandle *)&exec_parser_slots[i];
        ctx_full.ext_storage[i].extension_id = (uint32_t)(i + 1);
        ctx_full.ext_storage[i].name = (i == 0) ? "ext-power" : "ext-bitwise";
        ctx_full.ext_storage[i].priority = (i == 0) ? 100 : 50;
        ctx_full.ext_storage[i].user_data = NULL;
        ctx_full.ext_storage[i].should_execute = NULL;
        ctx_full.ext_ptrs[i] = &ctx_full.ext_storage[i];
    }

    r = run_bench("pipeline", "full_detect_resolve_execute",
                  bench_pipeline_with_extensions, &ctx_full, warmup, iters);
    bench_print(&r);

    if (dctx) disambiguation_destroy(dctx);
    cleanup_conflicting_registry(reg);
}

/* ================================================================== */
/*  9. MEMORY USAGE REPORT                                             */
/* ================================================================== */

static void report_memory_usage(void) {
    fprintf(stderr, "\n# Memory Usage Estimates (struct sizes):\n");
    fprintf(stderr, "#   ParserSnapshot:                %4zu bytes\n",
            sizeof(ParserSnapshot));
    fprintf(stderr, "#   Extension:                     %4zu bytes\n",
            sizeof(Extension));
    fprintf(stderr, "#   GrammarModification:            %4zu bytes\n",
            sizeof(GrammarModification));
    fprintf(stderr, "#   ConflictPoint:                  %4zu bytes\n",
            sizeof(ConflictPoint));
    fprintf(stderr, "#   LimeContext:                    %4zu bytes\n",
            sizeof(LimeContext));
    fprintf(stderr, "#   ConflictSet:                    %4zu bytes\n",
            sizeof(ConflictSet));
    fprintf(stderr, "#   MultiGrammarConflictResult:     %4zu bytes\n",
            sizeof(MultiGrammarConflictResult));
    fprintf(stderr, "#   StrategyResult:                 %4zu bytes\n",
            sizeof(StrategyResult));
    fprintf(stderr, "#   GrammarExtensionMetadata (EP):  %4zu bytes\n",
            sizeof(GrammarExtensionMetadata));
    fprintf(stderr, "#   ExecutionPolicyConfig:          %4zu bytes\n",
            sizeof(ExecutionPolicyConfig));
    fprintf(stderr, "#   ExecutionResult:                %4zu bytes\n",
            sizeof(ExecutionResult));
    fprintf(stderr, "#   ParseFork:                      %4zu bytes\n",
            sizeof(ParseFork));
    fprintf(stderr, "#   ClonedParserState:              %4zu bytes\n",
            sizeof(ClonedParserState));
    fprintf(stderr, "#   ParseForkSet:                   %4zu bytes\n",
            sizeof(ParseForkSet));
    fprintf(stderr, "#\n");

    /* Per-extension overhead estimate */
    size_t per_ext = sizeof(Extension) + 64; /* name + version strings */
    size_t per_mod = sizeof(GrammarModification) + 64; /* strings in mod */
    fprintf(stderr, "#   Per-extension overhead: ~%zu bytes + %zu per mod\n",
            per_ext, per_mod);

    /* Conflict detection overhead for 2 conflicting extensions with 2 mods each */
    size_t conflict_result = sizeof(MultiGrammarConflictResult) +
                              8 * sizeof(ConflictPoint) +  /* initial capacity */
                              8 * sizeof(LimeContext);     /* contexts */
    fprintf(stderr, "#   Conflict result (2 ext, initial): ~%zu bytes\n",
            conflict_result);
}

/* ================================================================== */
/*  Main                                                               */
/* ================================================================== */

int main(int argc, char **argv) {
    uint64_t warmup = 200;
    uint64_t iters = 10000;

    if (argc > 1) {
        iters = (uint64_t)atol(argv[1]);
        if (iters == 0) iters = 10000;
    }
    if (argc > 2) {
        warmup = (uint64_t)atol(argv[2]);
    }

    fprintf(stderr, "# Extension Framework Overhead Benchmark\n");
    fprintf(stderr, "# Iterations: %lu  Warmup: %lu\n",
            (unsigned long)iters, (unsigned long)warmup);
    fprintf(stderr, "#\n");

    bench_header();

    run_baseline_benchmarks(warmup, iters);
    run_registry_benchmarks(warmup, iters);
    run_conflict_benchmarks(warmup, iters);
    run_disambiguation_benchmarks(warmup, iters);
    run_execution_policy_benchmarks(warmup, iters);
    run_fork_benchmarks(warmup, iters);
    run_grammar_context_benchmarks(warmup, iters);
    run_pipeline_benchmarks(warmup, iters);

    report_memory_usage();

    fprintf(stderr, "\n# Benchmark complete.\n");
    return 0;
}
