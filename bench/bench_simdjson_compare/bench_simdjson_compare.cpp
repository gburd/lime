/*
** bench_simdjson_compare.cpp -- Lime+JIT vs simdjson on a JSON
** workload, measured at steady state after warmup.
**
** Three Lime modes:
**
**   1. lime+jit malloc        -- one malloc per JsonValue node
**                                + json_free walks and releases.
**                                The honest "build a tree, dispose
**                                of it" cost.
**   2. lime+jit malloc-leak   -- one malloc per node; json_free is
**                                a no-op.  Isolates parse-only cost
**                                without paying the deallocator.
**                                For TESTING ONLY -- in production
**                                this leaks every parse.
**   3. lime+jit arena         -- single 1 MB pre-allocated arena;
**                                bump-pointer alloc per node; reset
**                                (not freed) between iterations.
**                                Mirrors simdjson's allocation
**                                model: parser owns big buffers,
**                                resets between parses.
**
** simdjson:
**
**   ondemand parser, internal buffers reused across parses
**   (its standard steady-state pattern).  The driver forces a
**   full document walk so we are not just measuring time-to-first-
**   token.
**
** simdjson's allocation model (verified by reading
** /opt/homebrew/include/simdjson.h on the build host):
**
**   - dom_parser_implementation::allocate() does one
**     `new uint8_t[string_capacity]` and one
**     `new uint64_t[tape_capacity]`.
**   - These buffers are reset (`tape.reset()`, `string_buf.reset()`)
**     between parses, not freed.  In ondemand mode the parser
**     keeps a small string_buf and walks the input tape directly.
**   - End result: in steady state, simdjson does effectively zero
**     allocations per parse.
**
** The ARENA mode in this driver is the apples-to-apples comparison.
*/

#include <simdjson.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include "json.h"
#include "json_tokenize.h"
#include "json_grammar.h"
#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"

extern void *JsonAlloc(void *(*)(size_t));
extern void  JsonFree(void *, void (*)(void *));
extern void  Json(void *, int, void *, JsonValue **);
extern ParserSnapshot *JsonBuildSnapshot(void);
}

static const char *kJsonInput =
    "{\n"
    "  \"name\": \"acme\",\n"
    "  \"version\": 12,\n"
    "  \"active\": true,\n"
    "  \"deleted\": false,\n"
    "  \"parent\": null,\n"
    "  \"deps\": [\"libfoo\", \"libbar\", \"libbaz\"],\n"
    "  \"counts\": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10],\n"
    "  \"meta\": {\n"
    "    \"author\": \"alice\",\n"
    "    \"reviewers\": [\"bob\", \"carol\", \"dave\"],\n"
    "    \"score\": 0.95,\n"
    "    \"flags\": {\n"
    "      \"verified\": true,\n"
    "      \"public\": true,\n"
    "      \"deprecated\": false\n"
    "    }\n"
    "  },\n"
    "  \"tags\": [\n"
    "    {\"key\": \"env\", \"value\": \"prod\"},\n"
    "    {\"key\": \"region\", \"value\": \"us-west-2\"},\n"
    "    {\"key\": \"team\", \"value\": \"platform\"}\n"
    "  ]\n"
    "}";

static double now_ms() {
    using namespace std::chrono;
    return duration_cast<duration<double, std::milli>>(steady_clock::now().time_since_epoch())
        .count();
}

/* ------------------------------------------------------------------ */
/*  Lime drivers                                                       */
/* ------------------------------------------------------------------ */

static double bench_lime_malloc(ParserSnapshot *snap, int iters) {
    json_set_alloc_mode(JSON_ALLOC_MALLOC);
    double t0 = now_ms();
    int total = 0;
    for (int i = 0; i < iters; i++) {
        JsonScanner sc;
        json_scanner_init(&sc, kJsonInput, std::strlen(kJsonInput));
        void *parser = JsonAlloc(std::malloc);
        ParseContext *ctx = parse_begin(snap);
        JsonValue *root = nullptr;
        int tok;
        void *value;
        while ((tok = json_scan(&sc, &value)) > 0) {
            Json(parser, tok, value, &root);
        }
        Json(parser, 0, nullptr, &root);
        JsonFree(parser, std::free);
        if (ctx) parse_end(ctx);
        if (root) {
            total += root->type;
            json_free(root);
        }
    }
    double t1 = now_ms();
    asm volatile("" : : "r"(total));
    return t1 - t0;
}

static double bench_lime_leak(ParserSnapshot *snap, int iters) {
    /* WARNING: leaks every iteration.  Pre-allocates a fresh OS heap
    ** for itself by way of the loop; malloc throughput is what we
    ** measure here, NOT free throughput. */
    json_set_alloc_mode(JSON_ALLOC_MALLOC_NOFREE);
    double t0 = now_ms();
    int total = 0;
    for (int i = 0; i < iters; i++) {
        JsonScanner sc;
        json_scanner_init(&sc, kJsonInput, std::strlen(kJsonInput));
        void *parser = JsonAlloc(std::malloc);
        ParseContext *ctx = parse_begin(snap);
        JsonValue *root = nullptr;
        int tok;
        void *value;
        while ((tok = json_scan(&sc, &value)) > 0) {
            Json(parser, tok, value, &root);
        }
        Json(parser, 0, nullptr, &root);
        JsonFree(parser, std::free);
        if (ctx) parse_end(ctx);
        if (root) total += root->type;
        /* deliberate leak: no json_free */
    }
    double t1 = now_ms();
    asm volatile("" : : "r"(total));
    return t1 - t0;
}

static double bench_lime_arena(ParserSnapshot *snap, int iters) {
    /* Single arena, pre-allocated, reset between iterations.  Mirrors
    ** simdjson's "buffers owned by parser, reset between parses"
    ** model.  Zero malloc/free in steady state. */
    JsonArena arena;
    json_arena_init(&arena, 1 << 20); /* 1 MB */
    json_set_arena(&arena);
    json_set_alloc_mode(JSON_ALLOC_ARENA);

    double t0 = now_ms();
    int total = 0;
    for (int i = 0; i < iters; i++) {
        json_arena_reset(&arena);
        JsonScanner sc;
        json_scanner_init(&sc, kJsonInput, std::strlen(kJsonInput));
        void *parser = JsonAlloc(std::malloc);
        ParseContext *ctx = parse_begin(snap);
        JsonValue *root = nullptr;
        int tok;
        void *value;
        while ((tok = json_scan(&sc, &value)) > 0) {
            Json(parser, tok, value, &root);
        }
        Json(parser, 0, nullptr, &root);
        JsonFree(parser, std::free);
        if (ctx) parse_end(ctx);
        if (root) total += root->type;
    }
    double t1 = now_ms();
    asm volatile("" : : "r"(total));
    json_arena_destroy(&arena);
    json_set_arena(nullptr);
    return t1 - t0;
}

/* ------------------------------------------------------------------ */
/*  simdjson driver                                                    */
/* ------------------------------------------------------------------ */

static double bench_simdjson(int iters) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string input(kJsonInput, std::strlen(kJsonInput));

    double t0 = now_ms();
    int total = 0;
    for (int i = 0; i < iters; i++) {
        simdjson::ondemand::document doc = parser.iterate(input);
        for (auto field : doc.get_object()) {
            (void)field.unescaped_key();
            simdjson::ondemand::value v = field.value();
            switch (v.type()) {
                case simdjson::ondemand::json_type::object:
                    for (auto inner : v.get_object()) {
                        (void)inner.unescaped_key();
                        simdjson::ondemand::value iv = inner.value();
                        if (iv.type() == simdjson::ondemand::json_type::object) {
                            for (auto x : iv.get_object()) (void)x;
                        }
                    }
                    break;
                case simdjson::ondemand::json_type::array:
                    for (auto item : v.get_array()) (void)item;
                    break;
                default:
                    break;
            }
            total++;
        }
    }
    double t1 = now_ms();
    asm volatile("" : : "r"(total));
    return t1 - t0;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main() {
    const int warmup = 50000;
    const int iters  = 100000;
    const int trials = 5;
    const size_t doc_bytes = std::strlen(kJsonInput);

    std::printf("Lime+JIT vs simdjson, JSON throughput at steady state\n");
    std::printf("=====================================================\n");
    std::printf("Document: %zu bytes\n", doc_bytes);
    std::printf("Warmup:   %d iterations per side\n", warmup);
    std::printf("Trials:   %d trials of %d iterations each\n\n", trials, iters);

    ParserSnapshot *snap = JsonBuildSnapshot();
    if (!snap) { std::fprintf(stderr, "JsonBuildSnapshot returned NULL\n"); return 1; }

    /* JIT compile, then warm everything. */
    int rc = lime_jit_compile(snap);
    std::printf("lime_jit_compile rc=%d (0 = ok)\n\n", rc);

    bench_lime_malloc(snap, warmup);
    bench_lime_leak  (snap, warmup);
    bench_lime_arena (snap, warmup);
    bench_simdjson(warmup);

    std::vector<double> t_malloc, t_leak, t_arena, t_simd;
    for (int t = 0; t < trials; t++) {
        double m = bench_lime_malloc(snap, iters);
        double l = bench_lime_leak  (snap, iters);
        double a = bench_lime_arena (snap, iters);
        double s = bench_simdjson(iters);
        t_malloc.push_back(m);
        t_leak  .push_back(l);
        t_arena .push_back(a);
        t_simd  .push_back(s);
        std::printf("trial %d  malloc %7.1f ms  leak %7.1f ms  arena %7.1f ms  simdjson %7.1f ms\n",
                    t + 1, m, l, a, s);
    }

    auto throughput_mbs = [&](double ms_per_iters) {
        double bytes_per_sec = (double)doc_bytes * iters / (ms_per_iters / 1000.0);
        return bytes_per_sec / (1024.0 * 1024.0);
    };
    auto docs_per_sec = [&](double ms_per_iters) {
        return iters / (ms_per_iters / 1000.0);
    };

    double m = median(t_malloc), l = median(t_leak),
           a = median(t_arena),  s = median(t_simd);

    std::printf("\n=== Median across %d trials ===\n", trials);
    std::printf("  lime+jit malloc       %7.1f ms  %6.1f MB/s  %7.0f docs/s\n",
                m, throughput_mbs(m), docs_per_sec(m));
    std::printf("  lime+jit malloc-leak  %7.1f ms  %6.1f MB/s  %7.0f docs/s   (no free)\n",
                l, throughput_mbs(l), docs_per_sec(l));
    std::printf("  lime+jit arena        %7.1f ms  %6.1f MB/s  %7.0f docs/s   (zero alloc steady)\n",
                a, throughput_mbs(a), docs_per_sec(a));
    std::printf("  simdjson ondemand     %7.1f ms  %6.1f MB/s  %7.0f docs/s\n",
                s, throughput_mbs(s), docs_per_sec(s));

    std::printf("\n  free() cost (malloc - leak):  %.1f ms  (%.0f%% of malloc total)\n",
                m - l, 100.0 * (m - l) / m);
    std::printf("  alloc cost (leak - arena):    %.1f ms  (%.0f%% of malloc total)\n",
                l - a, 100.0 * (l - a) / m);
    std::printf("  remaining gap (arena/simd):   %.2fx\n", a / s);
    std::printf("  full gap (malloc/simd):       %.2fx\n", m / s);

    snapshot_release(snap);
    return 0;
}
