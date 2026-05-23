/*
** bench_simdjson_compare.cpp -- Lime+JIT vs simdjson on a JSON
** workload, measured at steady state after warmup.
**
** This is a deliberately uneven comparison: simdjson is a
** purpose-built SIMD JSON parser that operates on the full input
** in vectorised passes; Lime is a generic LALR(1) parser generator
** that happens to be hosting a JSON grammar.  simdjson is going to
** win on raw bytes/sec.  The point of the benchmark is to put
** *some* number on how close Lime gets and to make the trade-off
** explicit.
**
** Methodology:
**   1. Warmup: 100,000 parses of the same document on each side.
**   2. Steady state: 5 trials of 100,000 parses; report median.
**   3. Both sides build the document tree on the heap and free it.
**      For Lime, that means producing JsonValue nodes (full AST).
**      For simdjson, we use ondemand to walk the document but do
**      not materialise an external tree -- simdjson's "lazy"
**      parsing means a faithful AST build would distort the
**      comparison.  We measure simdjson in its native
**      no-AST-allocation mode and explicitly call it out.
**
** Compiled as C++ to consume simdjson's API directly (it is a
** C++ library; the JSON example is C).  The Lime side calls into
** the C parser via extern "C".
*/

#include <simdjson.h>

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
/*  Lime+JIT driver: build full JsonValue AST                          */
/* ------------------------------------------------------------------ */

static double bench_lime(ParserSnapshot *snap, int iters) {
    double t0 = now_ms();
    int total = 0;
    for (int i = 0; i < iters; i++) {
        JsonScanner sc;
        json_scanner_init(&sc, kJsonInput, std::strlen(kJsonInput));
        void *parser = JsonAlloc(std::malloc);
        ParseContext *ctx = parse_begin(snap);
        (void)ctx;
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

/* ------------------------------------------------------------------ */
/*  simdjson driver: validate + walk on-demand                         */
/* ------------------------------------------------------------------ */
/*
** simdjson's ondemand API parses lazily as the user walks the
** document.  We force a full traversal so the comparison is
** apples-to-apples on "all of the input was processed".  We do
** not materialise the document into an external tree; that would
** be unfair to simdjson (its design is to skip that step).
*/
static double bench_simdjson(int iters) {
    /* Allocate the parser and padded string once; reuse across
    ** iterations.  Steady-state is what we want to measure. */
    simdjson::ondemand::parser parser;
    simdjson::padded_string input(kJsonInput, std::strlen(kJsonInput));

    double t0 = now_ms();
    int total = 0;
    for (int i = 0; i < iters; i++) {
        simdjson::ondemand::document doc = parser.iterate(input);

        /* Walk the document fully.  Drains the lazy iterator so
        ** we are not just measuring time-to-first-token. */
        for (auto field : doc.get_object()) {
            std::string_view key = field.unescaped_key();
            (void)key;
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
/*  Main: warmup + 5-trial median                                      */
/* ------------------------------------------------------------------ */

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

    /* Build snapshot, warm up, then JIT-compile and warm again. */
    ParserSnapshot *snap = JsonBuildSnapshot();
    if (!snap) { std::fprintf(stderr, "JsonBuildSnapshot returned NULL\n"); return 1; }

    /* Warmup #1: cold caches, no JIT yet. */
    bench_lime(snap, warmup);

    int rc = lime_jit_compile(snap);
    std::printf("lime_jit_compile rc=%d (0 = ok)\n", rc);

    /* Warmup #2: warm caches with JIT armed. */
    bench_lime(snap, warmup);

    /* simdjson warmup */
    bench_simdjson(warmup);

    std::vector<double> lime_times, simd_times;
    for (int t = 0; t < trials; t++) {
        double ms = bench_lime(snap, iters);
        lime_times.push_back(ms);
        std::printf("  trial %d  lime+jit     %8.2f ms  (%.0f ops/s)\n",
                    t + 1, ms, iters / (ms / 1000.0));
    }
    for (int t = 0; t < trials; t++) {
        double ms = bench_simdjson(iters);
        simd_times.push_back(ms);
        std::printf("  trial %d  simdjson     %8.2f ms  (%.0f ops/s)\n",
                    t + 1, ms, iters / (ms / 1000.0));
    }

    auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    double lm = median(lime_times);
    double sm = median(simd_times);

    auto throughput_mbs = [&](double ms_per_iters) {
        double bytes_per_sec = (double)doc_bytes * iters / (ms_per_iters / 1000.0);
        return bytes_per_sec / (1024.0 * 1024.0);
    };

    std::printf("\n=== Median across %d trials ===\n", trials);
    std::printf("  lime+jit (full AST):  %8.2f ms  %7.1f MB/s  %.0f docs/s\n",
                lm, throughput_mbs(lm), iters / (lm / 1000.0));
    std::printf("  simdjson (ondemand):  %8.2f ms  %7.1f MB/s  %.0f docs/s\n",
                sm, throughput_mbs(sm), iters / (sm / 1000.0));
    std::printf("\n  simdjson speedup over lime+jit: %.2fx\n", lm / sm);
    std::printf("\n  Note: simdjson does not materialise an external AST.\n"
                "  Lime builds a full JsonValue tree per parse, then frees it.\n"
                "  The comparison is a useful order-of-magnitude check, not a\n"
                "  like-for-like benchmark.\n");

    snapshot_release(snap);
    return 0;
}
