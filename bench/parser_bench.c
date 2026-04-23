/*
** Performance benchmark suite for the extensible SQL parser.
**
** Benchmarks cover:
**   1. Tokenizer throughput (SIMD vs scalar classification)
**   2. Action table lookup (JIT vs interpreted)
**   3. Snapshot operations (acquire/release, creation)
**   4. Token table lookup performance
**   5. JIT policy overhead
**
** Output is CSV-formatted for easy consumption by analysis tools.
** Each benchmark is run for multiple iterations with warmup.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#include "parser.h"
#include "jit_context.h"
#include "jit_policy.h"
#include "snapshot.h"
#include "token_table.h"
#include "tokenize.h"
#include "tokenize_simd.h"

/* ------------------------------------------------------------------ */
/*  Timing infrastructure                                              */
/* ------------------------------------------------------------------ */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

typedef struct BenchResult {
    const char *name;
    const char *category;
    uint64_t iterations;
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    double   ops_per_sec;
    double   ns_per_op;
} BenchResult;

static void bench_result_print_header(void) {
    printf("category,benchmark,iterations,total_ns,min_ns,max_ns,"
           "ns_per_op,ops_per_sec\n");
}

static void bench_result_print(const BenchResult *r) {
    printf("%s,%s,%lu,%lu,%lu,%lu,%.2f,%.0f\n",
           r->category, r->name,
           (unsigned long)r->iterations,
           (unsigned long)r->total_ns,
           (unsigned long)r->min_ns,
           (unsigned long)r->max_ns,
           r->ns_per_op,
           r->ops_per_sec);
}

/*
** Run a benchmark function for the specified number of iterations
** with a warmup phase.
*/
typedef void (*BenchFn)(void *ctx);

static BenchResult run_bench(const char *category, const char *name,
                             BenchFn fn, void *ctx,
                             uint64_t warmup_iters, uint64_t bench_iters) {
    BenchResult result;
    result.name = name;
    result.category = category;
    result.iterations = bench_iters;
    result.min_ns = UINT64_MAX;
    result.max_ns = 0;

    /* Warmup */
    for (uint64_t i = 0; i < warmup_iters; i++) {
        fn(ctx);
    }

    /* Benchmark */
    uint64_t total = 0;
    for (uint64_t i = 0; i < bench_iters; i++) {
        uint64_t start = now_ns();
        fn(ctx);
        uint64_t elapsed = now_ns() - start;

        total += elapsed;
        if (elapsed < result.min_ns) result.min_ns = elapsed;
        if (elapsed > result.max_ns) result.max_ns = elapsed;
    }

    result.total_ns = total;
    result.ns_per_op = (double)total / (double)bench_iters;
    result.ops_per_sec = bench_iters > 0
        ? (double)bench_iters / ((double)total / 1e9)
        : 0.0;

    return result;
}

/* ------------------------------------------------------------------ */
/*  SQL test strings                                                   */
/* ------------------------------------------------------------------ */

/* Padded to 32 bytes past the end for SIMD safety */
#define SQL_PAD "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"

static const char sql_simple_select[] =
    "SELECT id, name FROM users WHERE active = 1;" SQL_PAD;

static const char sql_complex_join[] =
    "SELECT u.id, u.name, o.total, p.name AS product "
    "FROM users u "
    "INNER JOIN orders o ON u.id = o.user_id "
    "LEFT JOIN order_items oi ON o.id = oi.order_id "
    "LEFT JOIN products p ON oi.product_id = p.id "
    "WHERE u.active = 1 AND o.total > 100 "
    "ORDER BY o.total DESC "
    "LIMIT 50;" SQL_PAD;

static const char sql_cte[] =
    "WITH RECURSIVE category_tree AS ("
    "  SELECT id, name, parent_id, 0 AS depth "
    "  FROM categories WHERE parent_id IS NULL "
    "  UNION ALL "
    "  SELECT c.id, c.name, c.parent_id, ct.depth + 1 "
    "  FROM categories c "
    "  INNER JOIN category_tree ct ON c.parent_id = ct.id "
    ") "
    "SELECT * FROM category_tree ORDER BY depth, name;" SQL_PAD;

static const char sql_insert[] =
    "INSERT INTO events (user_id, event_type, payload, created_at) "
    "VALUES (42, 'click', '{\"page\": \"/home\"}', '2025-01-15 10:30:00');" SQL_PAD;

static const char sql_subquery[] =
    "SELECT * FROM orders "
    "WHERE user_id IN ("
    "  SELECT id FROM users "
    "  WHERE created_at > '2024-01-01' "
    "  AND status = 'active'"
    ") "
    "AND total > ("
    "  SELECT AVG(total) FROM orders"
    ");" SQL_PAD;

/* ------------------------------------------------------------------ */
/*  Tokenizer benchmarks                                               */
/* ------------------------------------------------------------------ */

typedef struct TokenizerBenchCtx {
    const char *sql;
    size_t len;
    TokenTable *table;
} TokenizerBenchCtx;

static void bench_tokenize(void *arg) {
    TokenizerBenchCtx *ctx = (TokenizerBenchCtx *)arg;
    Tokenizer *tok = tokenizer_create(ctx->table, ctx->sql, ctx->len);
    if (tok == NULL) return;

    Token t;
    while (tokenizer_next(tok, &t)) {
        /* consume all tokens */
    }

    tokenizer_destroy(tok);
}

static void run_tokenizer_benchmarks(uint64_t warmup, uint64_t iters) {
    /* Create a token table with common SQL keywords */
    TokenTable *table = create_token_table(64);

    if (table) {
        add_token(table, "SELECT", 1, 0);
        add_token(table, "FROM", 2, 0);
        add_token(table, "WHERE", 3, 0);
        add_token(table, "INSERT", 4, 0);
        add_token(table, "INTO", 5, 0);
        add_token(table, "VALUES", 6, 0);
        add_token(table, "AND", 7, 0);
        add_token(table, "OR", 8, 0);
        add_token(table, "ORDER", 9, 0);
        add_token(table, "BY", 10, 0);
        add_token(table, "LIMIT", 11, 0);
        add_token(table, "JOIN", 12, 0);
        add_token(table, "INNER", 13, 0);
        add_token(table, "LEFT", 14, 0);
        add_token(table, "ON", 15, 0);
        add_token(table, "AS", 16, 0);
        add_token(table, "WITH", 17, 0);
        add_token(table, "RECURSIVE", 18, 0);
        add_token(table, "UNION", 19, 0);
        add_token(table, "ALL", 20, 0);
        add_token(table, "NULL", 21, 0);
        add_token(table, "IS", 22, 0);
        add_token(table, "NOT", 23, 0);
        add_token(table, "IN", 24, 0);
        add_token(table, "DESC", 25, 0);
        add_token(table, "ASC", 26, 0);
        add_token(table, "AVG", 27, 0);
    }

    struct { const char *name; const char *sql; } queries[] = {
        { "simple_select",  sql_simple_select },
        { "complex_join",   sql_complex_join },
        { "cte_recursive",  sql_cte },
        { "insert",         sql_insert },
        { "subquery",       sql_subquery },
    };

    for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
        TokenizerBenchCtx ctx;
        ctx.sql = queries[i].sql;
        ctx.len = strlen(queries[i].sql);
        ctx.table = table;

        BenchResult r = run_bench("tokenizer", queries[i].name,
                                  bench_tokenize, &ctx, warmup, iters);
        bench_result_print(&r);
    }

    /* Benchmark without keyword table (identifier-only mode) */
    TokenizerBenchCtx ctx_nokw;
    ctx_nokw.sql = sql_complex_join;
    ctx_nokw.len = strlen(sql_complex_join);
    ctx_nokw.table = NULL;

    BenchResult r = run_bench("tokenizer", "complex_join_no_keywords",
                              bench_tokenize, &ctx_nokw, warmup, iters);
    bench_result_print(&r);

    if (table) destroy_token_table(table);
}

/* ------------------------------------------------------------------ */
/*  SIMD classification benchmarks                                     */
/* ------------------------------------------------------------------ */

typedef struct SimdBenchCtx {
    ClassifyFunc func;
    const char *input;
    size_t len;
} SimdBenchCtx;

static void bench_classify(void *arg) {
    SimdBenchCtx *ctx = (SimdBenchCtx *)arg;
    /* Classify in chunks of 32 bytes across the input */
    for (size_t off = 0; off + 32 <= ctx->len; off += 32) {
        CharClassVector v = ctx->func(ctx->input, off);
        (void)v;
    }
}

static void run_simd_benchmarks(uint64_t warmup, uint64_t iters) {
    /* Build a large-ish input buffer */
    size_t buf_size = 4096 + 32; /* 4KB plus SIMD padding */
    char *buf = calloc(1, buf_size);
    if (!buf) return;

    /* Fill with a mix of SQL-like characters */
    const char *pattern = "SELECT col1, col2 FROM table1 WHERE id > 42 AND name = 'test'; ";
    size_t plen = strlen(pattern);
    for (size_t i = 0; i < 4096; i++) {
        buf[i] = pattern[i % plen];
    }

    /* Scalar benchmark */
    SimdBenchCtx scalar_ctx;
    scalar_ctx.func = classify_scalar;
    scalar_ctx.input = buf;
    scalar_ctx.len = 4096;

    BenchResult r = run_bench("simd", "classify_scalar_4kb",
                              bench_classify, &scalar_ctx, warmup, iters);
    bench_result_print(&r);

    /* Best-available (may be SIMD or scalar) */
    ClassifyFunc best = get_classify_func();
    SimdBenchCtx best_ctx;
    best_ctx.func = best;
    best_ctx.input = buf;
    best_ctx.len = 4096;

    r = run_bench("simd", "classify_best_4kb",
                  bench_classify, &best_ctx, warmup, iters);
    bench_result_print(&r);

    /* Report which implementation is in use */
    if (best == classify_scalar) {
        fprintf(stderr, "# simd: best_available = scalar\n");
    } else {
        fprintf(stderr, "# simd: best_available = SIMD (AVX2 or NEON)\n");
    }

    free(buf);
}

/* ------------------------------------------------------------------ */
/*  Token table lookup benchmarks                                      */
/* ------------------------------------------------------------------ */

typedef struct TokenLookupCtx {
    TokenTable *table;
    const char **keywords;
    size_t *lengths;
    size_t nkeywords;
} TokenLookupCtx;

static void bench_token_lookup(void *arg) {
    TokenLookupCtx *ctx = (TokenLookupCtx *)arg;
    for (size_t i = 0; i < ctx->nkeywords; i++) {
        int code = lookup_token(ctx->table, ctx->keywords[i], ctx->lengths[i]);
        (void)code;
    }
}

static void run_token_table_benchmarks(uint64_t warmup, uint64_t iters) {
    TokenTable *table = create_token_table(64);
    if (!table) return;

    const char *keywords[] = {
        "SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES",
        "UPDATE", "DELETE", "CREATE", "DROP", "ALTER", "TABLE",
        "INDEX", "JOIN", "INNER", "LEFT", "RIGHT", "OUTER",
        "AND", "OR", "NOT", "NULL", "IS", "IN",
        "ORDER", "BY", "GROUP", "HAVING", "LIMIT", "OFFSET",
        "UNION", "ALL", "DISTINCT", "AS", "ON", "SET",
    };
    size_t nkeywords = sizeof(keywords) / sizeof(keywords[0]);

    size_t *lengths = malloc(nkeywords * sizeof(size_t));
    if (!lengths) { destroy_token_table(table); return; }

    for (size_t i = 0; i < nkeywords; i++) {
        lengths[i] = strlen(keywords[i]);
        add_token(table, keywords[i], (int)(i + 1), 0);
    }

    /* Lookup all keywords (hit) */
    TokenLookupCtx ctx;
    ctx.table = table;
    ctx.keywords = keywords;
    ctx.lengths = lengths;
    ctx.nkeywords = nkeywords;

    BenchResult r = run_bench("token_table", "lookup_36_keywords_hit",
                              bench_token_lookup, &ctx, warmup, iters);
    bench_result_print(&r);

    /* Lookup with misses */
    const char *miss_keywords[] = {
        "BOGUS", "FOOBAR", "QUUX", "NONEXIST", "BADTOKEN",
    };
    size_t miss_lengths[] = { 5, 6, 4, 8, 8 };

    TokenLookupCtx miss_ctx;
    miss_ctx.table = table;
    miss_ctx.keywords = miss_keywords;
    miss_ctx.lengths = miss_lengths;
    miss_ctx.nkeywords = 5;

    r = run_bench("token_table", "lookup_5_keywords_miss",
                  bench_token_lookup, &miss_ctx, warmup, iters);
    bench_result_print(&r);

    free(lengths);
    destroy_token_table(table);
}

/* ------------------------------------------------------------------ */
/*  Snapshot operation benchmarks                                      */
/* ------------------------------------------------------------------ */

typedef struct SnapshotBenchCtx {
    ParserSnapshot *snap;
} SnapshotBenchCtx;

static void bench_snapshot_acquire_release(void *arg) {
    SnapshotBenchCtx *ctx = (SnapshotBenchCtx *)arg;
    ParserSnapshot *ref = snapshot_acquire(ctx->snap);
    snapshot_release(ref);
}

static void run_snapshot_benchmarks(uint64_t warmup, uint64_t iters) {
    /* Create a minimal snapshot for benchmarking acquire/release */
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (!snap) return;

    atomic_init(&snap->refcount, 1);
    snap->version = 1;

    SnapshotBenchCtx ctx;
    ctx.snap = snap;

    BenchResult r = run_bench("snapshot", "acquire_release",
                              bench_snapshot_acquire_release, &ctx,
                              warmup, iters);
    bench_result_print(&r);

    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  JIT action table lookup benchmarks                                 */
/* ------------------------------------------------------------------ */

/*
** Create a snapshot with realistic action tables for benchmarking.
** Uses a moderate number of states and terminals.
*/
static ParserSnapshot *make_bench_snapshot(uint32_t nstates,
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

    /* Fill with a pattern: each state has an offset, and valid
    ** lookahead entries map to distinct actions */
    for (uint32_t s = 0; s < nstates; s++) {
        snap->yy_shift_ofst[s] = (int16_t)(s * nterminals);
        snap->yy_default[s] = (uint16_t)(1000 + s);

        for (uint32_t t = 0; t < nterminals; t++) {
            uint32_t idx = s * nterminals + t;
            snap->yy_lookahead[idx] = (uint16_t)t;
            snap->yy_action[idx] = (uint16_t)(s * 100 + t);
        }
    }

    snap->jit_ctx = NULL;
    return snap;
}

typedef struct JitBenchCtx {
    ParserSnapshot *snap;
    uint32_t nstates;
    uint32_t nterminals;
} JitBenchCtx;

static void bench_action_lookup_table(void *arg) {
    JitBenchCtx *ctx = (JitBenchCtx *)arg;
    /* Look up action for every state+terminal combination */
    for (uint32_t s = 0; s < ctx->nstates; s++) {
        for (uint32_t t = 0; t < ctx->nterminals; t++) {
            uint16_t action = jit_find_shift_action(ctx->snap,
                                                    (uint16_t)s,
                                                    (uint16_t)t);
            (void)action;
        }
    }
}

static void run_jit_benchmarks(uint64_t warmup, uint64_t iters) {
    uint32_t nstates = 64;
    uint32_t nterminals = 32;

    ParserSnapshot *snap = make_bench_snapshot(nstates, nterminals);
    if (!snap) {
        fprintf(stderr, "# jit: failed to create benchmark snapshot\n");
        return;
    }

    JitBenchCtx ctx;
    ctx.snap = snap;
    ctx.nstates = nstates;
    ctx.nterminals = nterminals;

    /* Table-driven (interpreted) path */
    BenchResult r = run_bench("jit", "action_lookup_interpreted_64x32",
                              bench_action_lookup_table, &ctx,
                              warmup, iters);
    bench_result_print(&r);

    /* Try JIT compilation */
    JITStatus st = jit_attach_to_snapshot(snap);

    if (st == JIT_OK) {
        r = run_bench("jit", "action_lookup_jit_64x32",
                      bench_action_lookup_table, &ctx,
                      warmup, iters);
        bench_result_print(&r);

        JITStats stats = jit_get_stats((JITContext *)snap->jit_ctx);
        fprintf(stderr, "# jit: compiled %u/%u states in %lu ns\n",
                stats.states_compiled, stats.states_total,
                (unsigned long)stats.compile_time_ns);
    } else {
        fprintf(stderr, "# jit: LLVM not available, skipping JIT benchmark"
                " (status: %s)\n", jit_status_string(st));
    }

    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  JIT policy overhead benchmarks                                     */
/* ------------------------------------------------------------------ */

static void bench_metrics_record(void *arg) {
    JITMetrics *m = (JITMetrics *)arg;
    jit_metrics_record_parse(m, 1000, 200);
}

typedef struct PolicyEvalCtx {
    JITMetrics *metrics;
    JITPolicyConfig *config;
} PolicyEvalCtx;

static void bench_policy_eval(void *arg) {
    PolicyEvalCtx *ctx = (PolicyEvalCtx *)arg;
    bool result = jit_should_compile(ctx->metrics, ctx->config);
    (void)result;
}

static void run_policy_benchmarks(uint64_t warmup, uint64_t iters) {
    JITMetrics m;
    jit_metrics_init(&m);

    /* Benchmark metrics recording overhead */
    BenchResult r = run_bench("policy", "metrics_record",
                              bench_metrics_record, &m,
                              warmup, iters);
    bench_result_print(&r);

    /* Benchmark policy evaluation overhead */
    JITPolicyConfig cfg = jit_policy_default_config();
    PolicyEvalCtx eval_ctx;
    eval_ctx.metrics = &m;
    eval_ctx.config = &cfg;

    r = run_bench("policy", "should_compile_eval",
                  bench_policy_eval, &eval_ctx,
                  warmup, iters);
    bench_result_print(&r);
}

/* ------------------------------------------------------------------ */
/*  Memory usage reporting                                             */
/* ------------------------------------------------------------------ */

static void report_memory_usage(void) {
    fprintf(stderr, "\n# Memory usage estimates:\n");
    fprintf(stderr, "#   ParserSnapshot struct:  %zu bytes\n",
            sizeof(ParserSnapshot));
    fprintf(stderr, "#   JITMetrics struct:      %zu bytes\n",
            sizeof(JITMetrics));
    fprintf(stderr, "#   JITPolicyConfig struct: %zu bytes\n",
            sizeof(JITPolicyConfig));
    fprintf(stderr, "#   TokenTable struct:      %zu bytes\n",
            sizeof(TokenTable));
    fprintf(stderr, "#   TokenDefinition struct: %zu bytes\n",
            sizeof(TokenDefinition));
    fprintf(stderr, "#   Token struct:           %zu bytes\n",
            sizeof(Token));
    fprintf(stderr, "#   JITStats struct:        %zu bytes\n",
            sizeof(JITStats));

    /* Estimate action table memory for a realistic grammar */
    uint32_t nstates = 500;
    uint32_t nterminals = 150;
    uint32_t table_size = nstates * nterminals;
    size_t action_mem = table_size * sizeof(uint16_t) * 2  /* action + lookahead */
                      + nstates * sizeof(int16_t) * 2      /* shift + reduce ofst */
                      + nstates * sizeof(uint16_t);        /* defaults */
    fprintf(stderr, "#   Action tables (%u states, %u terminals): %zu bytes (%.1f KB)\n",
            nstates, nterminals, action_mem, (double)action_mem / 1024.0);
}

/* ------------------------------------------------------------------ */
/*  Throughput benchmarks (MB/s)                                       */
/* ------------------------------------------------------------------ */

typedef struct ThroughputCtx {
    char *sql;
    size_t len;
    TokenTable *table;
} ThroughputCtx;

static void bench_throughput(void *arg) {
    ThroughputCtx *ctx = (ThroughputCtx *)arg;
    Tokenizer *tok = tokenizer_create(ctx->table, ctx->sql, ctx->len);
    if (!tok) return;
    Token t;
    while (tokenizer_next(tok, &t)) { /* drain */ }
    tokenizer_destroy(tok);
}

static void run_throughput_benchmarks(uint64_t warmup, uint64_t iters) {
    /* Build keyword table */
    TokenTable *table = create_token_table(64);
    if (table) {
        const char *kws[] = {
            "SELECT","FROM","WHERE","INSERT","INTO","VALUES",
            "AND","OR","JOIN","INNER","LEFT","ON","AS","ORDER",
            "BY","LIMIT","WITH","UNION","ALL","NULL","IS","NOT","IN",
        };
        for (size_t i = 0; i < sizeof(kws)/sizeof(kws[0]); i++) {
            add_token(table, kws[i], (int)(i + 1), 0);
        }
    }

    /* Generate a realistic multi-statement SQL workload */
    size_t sizes[] = { 4096, 64 * 1024, 256 * 1024 };
    const char *labels[] = { "4kb", "64kb", "256kb" };

    const char *templates[] = {
        "SELECT a.id, b.name, c.value FROM alpha a "
        "JOIN beta b ON a.id = b.alpha_id "
        "JOIN gamma c ON b.id = c.beta_id "
        "WHERE a.status = 'active' AND b.count > 100;\n",

        "INSERT INTO events (user_id, type, payload) "
        "VALUES (42, 'click', 'data');\n",

        "SELECT * FROM orders WHERE total > ("
        "SELECT AVG(total) FROM orders WHERE status = 'done');\n",
    };
    size_t ntemplates = sizeof(templates) / sizeof(templates[0]);
    size_t tlens[3];
    for (size_t i = 0; i < ntemplates; i++) {
        tlens[i] = strlen(templates[i]);
    }

    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        size_t target = sizes[s];
        size_t alloc = target + 64; /* SIMD padding */
        char *buf = calloc(1, alloc);
        if (!buf) continue;

        /* Fill with rotating templates */
        size_t pos = 0;
        size_t ti = 0;
        while (pos + tlens[ti % ntemplates] < target) {
            size_t tl = tlens[ti % ntemplates];
            memcpy(buf + pos, templates[ti % ntemplates], tl);
            pos += tl;
            ti++;
        }

        ThroughputCtx ctx;
        ctx.sql = buf;
        ctx.len = pos;
        ctx.table = table;

        char name[64];
        snprintf(name, sizeof(name), "throughput_%s", labels[s]);

        BenchResult r = run_bench("throughput", name,
                                  bench_throughput, &ctx, warmup, iters);
        bench_result_print(&r);

        /* Report MB/s on stderr */
        double total_bytes = (double)pos * (double)iters;
        double total_sec = (double)r.total_ns / 1e9;
        double mb_per_sec = total_bytes / (1024.0 * 1024.0) / total_sec;
        fprintf(stderr, "# throughput_%s: %.1f MB/s (%.1f KB input)\n",
                labels[s], mb_per_sec, (double)pos / 1024.0);

        free(buf);
    }

    if (table) destroy_token_table(table);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    uint64_t warmup = 100;
    uint64_t iters = 10000;

    /* Allow overriding iteration count from command line */
    if (argc > 1) {
        iters = (uint64_t)atol(argv[1]);
        if (iters == 0) iters = 10000;
    }
    if (argc > 2) {
        warmup = (uint64_t)atol(argv[2]);
    }

    fprintf(stderr, "# Parser benchmark suite\n");
    fprintf(stderr, "# Iterations: %lu  Warmup: %lu\n",
            (unsigned long)iters, (unsigned long)warmup);
    fprintf(stderr, "# JIT available: %s\n",
            jit_is_available() ? "yes" : "no (stub mode)");
    fprintf(stderr, "#\n");

    bench_result_print_header();

    run_tokenizer_benchmarks(warmup, iters);
    run_simd_benchmarks(warmup, iters);
    run_token_table_benchmarks(warmup, iters);
    run_snapshot_benchmarks(warmup, iters);
    run_jit_benchmarks(warmup, iters);
    run_policy_benchmarks(warmup, iters);
    run_throughput_benchmarks(warmup, iters);

    report_memory_usage();

    fprintf(stderr, "\n# Benchmark complete.\n");
    return 0;
}
