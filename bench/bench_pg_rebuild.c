/*
** bench_pg_rebuild.c -- what does an in-process LALR(1) rebuild of the
** full PostgreSQL grammar cost?
**
** This is the number the runtime-composition design rests on.  Goal B
** rebuilds the parser tables every time the extension set changes
** (publish_modified_snapshot -> lime_compile_grammar_text ->
** lime_compile_grammar_in_process).  A PEG-based design (DuckDB's
** runtime-extensible parser) avoids that cost entirely: adding a rule
** is a data-structure insert.  So the question "is LALR rebuild cheap
** enough for PG-style backend-startup extension loading?" needs a
** measurement, not an estimate.
**
** Measures, per compile: CPU time (CLOCK_PROCESS_CPUTIME_ID, immune to
** being descheduled), wall time, and peak-RSS growth.  Reports min /
** median / max over N compiles after one warm-up.
**
** usage: bench_pg_rebuild <grammar.lime> [iterations]
**
** Use examples/pg/pg_grammar.lime (tokens.lime + gram.lime, 0 conflicts).
** gram.lime alone has no precedence declarations and 1665 conflicts, so
** it measures a grammar nobody would ship.
*/
#include "lime_compiler.h"
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

static double clk(clockid_t id) {
    struct timespec ts;
    clock_gettime(id, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static long peak_rss_kb(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss; /* KB on Linux */
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    *len = (size_t)n;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <grammar.lime> [iterations]\n", argv[0]);
        return 2;
    }
    int iters = argc > 2 ? atoi(argv[2]) : 5;
    if (iters < 1) iters = 1;

    size_t len = 0;
    char *text = slurp(argv[1], &len);
    if (text == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }

    long rss0 = peak_rss_kb();

    /* Warm-up compile; also reports grammar size. */
    ParserSnapshot *snap = NULL;
    char *err = NULL;
    double w0 = clk(CLOCK_MONOTONIC), c0 = clk(CLOCK_PROCESS_CPUTIME_ID);
    int nconflict = -1;
    int rc = lime_compile_grammar_in_process_ex(text, len, &snap, &err, &nconflict);
    double c1 = clk(CLOCK_PROCESS_CPUTIME_ID), w1 = clk(CLOCK_MONOTONIC);
    if (rc != 0 || snap == NULL) {
        fprintf(stderr, "compile failed (rc=%d): %s\n", rc, err ? err : "(no message)");
        free(err);
        free(text);
        return 1;
    }
    printf("grammar: %s (%zu bytes)\n", argv[1], len);
    printf("tables : nstate=%u nterminal=%u nrule=%u action_count=%u nconflict=%d\n",
           snap->nstate, snap->nterminal, snap->nrule, snap->action_count, nconflict);
    printf("warm-up: cpu=%.1f ms wall=%.1f ms peak_rss_growth=%ld MB\n",
           (c1 - c0) * 1e3, (w1 - w0) * 1e3, (peak_rss_kb() - rss0) / 1024);
    snapshot_release(snap);
    free(err);

    double *cpu = calloc((size_t)iters, sizeof(double));
    double *wall = calloc((size_t)iters, sizeof(double));
    for (int i = 0; i < iters; i++) {
        snap = NULL;
        err = NULL;
        w0 = clk(CLOCK_MONOTONIC);
        c0 = clk(CLOCK_PROCESS_CPUTIME_ID);
        rc = lime_compile_grammar_in_process(text, len, &snap, &err);
        c1 = clk(CLOCK_PROCESS_CPUTIME_ID);
        w1 = clk(CLOCK_MONOTONIC);
        if (rc != 0 || snap == NULL) {
            fprintf(stderr, "iteration %d failed: %s\n", i, err ? err : "?");
            return 1;
        }
        cpu[i] = (c1 - c0) * 1e3;
        wall[i] = (w1 - w0) * 1e3;
        snapshot_release(snap);
        free(err);
    }
    qsort(cpu, (size_t)iters, sizeof(double), cmp_d);
    qsort(wall, (size_t)iters, sizeof(double), cmp_d);
    printf("rebuild (%d iters): cpu ms min=%.1f median=%.1f max=%.1f | "
           "wall ms min=%.1f median=%.1f max=%.1f\n",
           iters, cpu[0], cpu[iters / 2], cpu[iters - 1],
           wall[0], wall[iters / 2], wall[iters - 1]);
    printf("peak_rss_total: %ld MB\n", peak_rss_kb() / 1024);

    free(cpu);
    free(wall);
    free(text);
    return 0;
}
