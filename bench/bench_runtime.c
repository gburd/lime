/*
 * Runtime throughput harness for Bison vs Lime.
 *
 * Feeds an identical pre-tokenized stream to each parser and measures
 * wall-clock time per parse.  Token codes are taken from the shared
 * token enum in bench_tokens.h (generated from either Bison's header
 * or Lime's header -- values differ, we remap).
 *
 * This file is compiled twice with different -D settings:
 *   -DUSE_BISON  links the Bison-generated yyparse and drives via
 *                an inlined yylex returning tokens from a stream.
 *   -DUSE_LIME   links the Lime-generated BenchParser*() and drives
 *                by calling BenchParser(parser, code, 0) per token.
 *
 * Usage:  ./bench_runtime  iterations  tokens_per_parse
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(USE_BISON) && !defined(USE_LIME)
#error "Define USE_BISON or USE_LIME"
#endif

#ifdef USE_BISON
#  include "bison_out.h"
   int  yyparse(void);
   extern int yychar;
   /* We drive bison by overriding yylex to read from our stream. */
   static const int *g_stream;
   static int        g_stream_pos;
   static int        g_stream_len;
   int yylex(void) {
       if (g_stream_pos >= g_stream_len) return 0;  /* EOF */
       return g_stream[g_stream_pos++];
   }
   void yyerror(const char *s) { (void)s; }
#endif

#ifdef USE_LIME
#  include "lime_out.h"
   void *BenchParserAlloc(void *(*f)(unsigned long));
   void  BenchParserFree(void *p, void (*f)(void *));
   void  BenchParserInit(void *rawParser);
   void  BenchParserFinalize(void *p);
   void  BenchParser(void *p, int major, int minor);
#endif

/* Token stream representative of a small SQL-ish input.  We use symbolic
 * names that exist in both grammars; the preprocessor resolves them to
 * per-tool integer codes via bison_out.h / lime_out.h. */
static int stream[] = {
    SELECT, ID, COMMA, ID, COMMA, ID,
    FROM, ID, WHERE, ID, EQ, INTEGER, AND,
    ID, GT, INTEGER,
    SEMI,
    SELECT, STAR, FROM, ID, WHERE, ID, LIKE, STRING, SEMI,
    INSERT, INTO, ID, VALUES, LPAREN, INTEGER, COMMA, STRING, RPAREN, SEMI,
    UPDATE, ID, SET, ID, EQ, INTEGER, WHERE, ID, EQ, INTEGER, SEMI,
    DELETE, FROM, ID, WHERE, ID, EQ, INTEGER, SEMI,
};
static const int stream_len = sizeof(stream) / sizeof(stream[0]);

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int cmp_ll(const void *a, const void *b) {
    long long x = *(const long long *)a, y = *(const long long *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    int iterations = argc > 1 ? atoi(argv[1]) : 100000;
    int warmup     = iterations / 10;

    long long *samples = malloc((size_t)iterations * sizeof(*samples));
    if (!samples) return 1;

    /* For Lime: allocate parser state once, reset with ParseInit
     * between parses.  This matches the zero-alloc pattern most
     * real applications should use. */
#ifdef USE_LIME
    void *parser = BenchParserAlloc((void *(*)(unsigned long))malloc);
    if (!parser) return 2;
#endif

    /* Warmup */
    for (int i = 0; i < warmup; i++) {
#ifdef USE_BISON
        g_stream = stream;
        g_stream_pos = 0;
        g_stream_len = stream_len;
        (void)yyparse();
#endif
#ifdef USE_LIME
        BenchParserInit(parser);
        for (int j = 0; j < stream_len; j++) BenchParser(parser, stream[j], 0);
        BenchParser(parser, 0, 0);
#endif
    }

    /* Measured runs -- batch BATCH parses per sample so clock_gettime
     * resolution (~1us on macOS) doesn't dominate. */
    const int BATCH = 200;
    for (int i = 0; i < iterations; i++) {
        long long t0 = now_ns();
        for (int b = 0; b < BATCH; b++) {
#ifdef USE_BISON
            g_stream = stream;
            g_stream_pos = 0;
            g_stream_len = stream_len;
            (void)yyparse();
#endif
#ifdef USE_LIME
            BenchParserInit(parser);
            for (int j = 0; j < stream_len; j++) BenchParser(parser, stream[j], 0);
            BenchParser(parser, 0, 0);
#endif
        }
        samples[i] = (now_ns() - t0) / BATCH;
    }

#ifdef USE_LIME
    BenchParserFinalize(parser);
    free(parser);
#endif

    qsort(samples, iterations, sizeof(*samples), cmp_ll);
    long long sum = 0;
    for (int i = 0; i < iterations; i++) sum += samples[i];

    long long median = samples[iterations / 2];
    long long p95    = samples[(iterations * 95) / 100];
    long long p99    = samples[(iterations * 99) / 100];
    long long mean   = sum / iterations;

    printf("%-10s  %10lld  %10lld  %10lld  %10lld  %10d\n",
#ifdef USE_BISON
           "bison",
#else
           "lime",
#endif
           median, mean, p95, p99, stream_len);

    free(samples);
    return 0;
}
