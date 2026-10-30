/*
** tests/bench_lexicon.c -- Track B measurements: keyword lookup,
** operator split, DFA interpreter and leading-byte gate.  Each mode
** checks that the two sides agree on every input BEFORE timing them.
** Timing is per-thread CPU time; each rep alternates the variants, and
** the report gives the median and min..max of N reps.
**
**   bench_lexicon kw <kwlist.txt> <sql files...>
**       PostgreSQL's 494 keywords (lines "text TOKEN") in a snapshot
**       lexicon vs the same set in today's TokenTable (rwlock per
**       lookup).  The input is every identifier-shaped word in the
**       given files, a realistic keyword/non-keyword mix.  Reports
**       ns/lookup with 1 thread and with 4 threads.
**
** Environment: BENCH_REPS (default 21), BENCH_CHECK_ONLY=1 (check
** agreement and exit; that is what the meson test runs).
*/
#define _POSIX_C_SOURCE 200809L
#include "lime_lexer.h"
#include "parser.h"
#include "snapshot.h"
#include "token_table.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int lime_compile_grammar_in_process(const char *, size_t, ParserSnapshot **, char **);
extern int lime_snapshot_token_code(const ParserSnapshot *snap, const char *name);

static double thread_cpu_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static int dcmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void report(const char *what, double *v, int n) {
    qsort(v, (size_t)n, sizeof *v, dcmp);
    printf("  %-34s median %8.3f  min %8.3f  max %8.3f  (n=%d)\n", what, v[n / 2], v[0],
           v[n - 1], n);
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    if (b) b[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return b;
}

static int env_int(const char *k, int d) {
    const char *v = getenv(k);
    return v && *v ? atoi(v) : d;
}

/* ------------------------------------------------------------------ */
/*  kw: snapshot keyword table vs TokenTable                           */
/* ------------------------------------------------------------------ */

typedef struct {
    const char **s;
    size_t *n;
    size_t count;
} Words;

static int is_id0(unsigned char c) {
    return c == '_' || c >= 0x80 || (unsigned)(c | 0x20) - 'a' < 26u;
}
static int is_id(unsigned char c) {
    return is_id0(c) || (c >= '0' && c <= '9') || c == '$';
}

static void words_from(Words *w, size_t *cap, const char *b, size_t len) {
    for (size_t i = 0; i < len;) {
        if (!is_id0((unsigned char)b[i])) {
            i++;
            continue;
        }
        size_t j = i;
        while (j < len && is_id((unsigned char)b[j])) j++;
        if (w->count == *cap) {
            *cap = *cap ? *cap * 2 : 4096;
            w->s = realloc(w->s, *cap * sizeof *w->s);
            w->n = realloc(w->n, *cap * sizeof *w->n);
        }
        w->s[w->count] = b + i;
        w->n[w->count++] = j - i;
        i = j;
    }
}

typedef struct {
    const LimeLexicon *lx;
    TokenTable *tt;
    const Words *w;
    int use_tt;
    int iters;
    double ns;
    long sink;
} KwJob;

static void *kw_job(void *arg) {
    KwJob *j = arg;
    long sink = 0;
    double t0 = thread_cpu_ns();
    for (int it = 0; it < j->iters; it++) {
        if (j->use_tt)
            for (size_t i = 0; i < j->w->count; i++)
                sink += lookup_token(j->tt, j->w->s[i], j->w->n[i]);
        else
            for (size_t i = 0; i < j->w->count; i++)
                sink += lime_kw_lookup(j->lx, j->w->s[i], j->w->n[i]);
    }
    j->ns = (thread_cpu_ns() - t0) / ((double)j->iters * (double)j->w->count);
    j->sink = sink;
    return NULL;
}

/* One rep with `nthreads` threads; returns mean per-thread ns/lookup. */
static double kw_rep(const LimeLexicon *lx, TokenTable *tt, const Words *w, int use_tt,
                     int nthreads, int iters) {
    pthread_t th[8];
    KwJob jobs[8];
    for (int t = 0; t < nthreads; t++) {
        jobs[t] = (KwJob){ lx, tt, w, use_tt, iters, 0, 0 };
        pthread_create(&th[t], NULL, kw_job, &jobs[t]);
    }
    double sum = 0;
    for (int t = 0; t < nthreads; t++) {
        pthread_join(th[t], NULL);
        sum += jobs[t].ns;
    }
    return sum / nthreads;
}

static int mode_kw(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: kw <kwlist> <files...>\n");
        return 2;
    }
    /* keyword list */
    size_t kl;
    char *kb = slurp(argv[2], &kl);
    if (!kb) {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 2;
    }
    /* Accepts either "text TOKEN" lines or examples/pg/pg_kwlist.h
    ** entries ('    {"text", TOKEN , ...},'). */
    LimeLexItem items[1024];
    size_t nk = 0;
    for (char *line = strtok(kb, "\n"); line && nk < 1024; line = strtok(NULL, "\n")) {
        char *t, *k;
        if (strncmp(line, "    {\"", 6) == 0) {
            t = line + 6;
            char *q = strchr(t, '"');
            if (!q) continue;
            *q = 0;
            k = q + 1;
            while (*k == ',' || *k == ' ') k++;
            char *end = k;
            while ((*end >= 'A' && *end <= 'Z') || (*end >= '0' && *end <= '9') || *end == '_')
                end++;
            *end = 0;
        } else {
            char *sp = strchr(line, ' ');
            if (!sp || line[0] == '/' || line[0] == '#' || line[0] == ' ') continue;
            *sp = 0;
            t = line;
            k = sp + 1;
        }
        if (!*t || !*k || k[0] < 'A' || k[0] > 'Z' || strspn(k, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != strlen(k))
            continue; /* not a "text TOKEN" pair */
        items[nk].text = t;
        items[nk].token = k;
        nk++;
    }
    /* grammar declaring every keyword token */
    size_t gcap = 128 + kl * 2;
    char *g = malloc(gcap);
    size_t gn = (size_t)snprintf(g, gcap, "%%name KWB\n%%token_type { int }\n%%token IDENT");
    for (size_t i = 0; i < nk; i++) gn += (size_t)snprintf(g + gn, gcap - gn, " %s", items[i].token);
    snprintf(g + gn, gcap - gn, ".\nstmt ::= IDENT.\n");
    ParserSnapshot *snap = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(g, strlen(g), &snap, &err) != 0 || !snap) {
        fprintf(stderr, "compile failed: %s\n", err ? err : "?");
        return 1;
    }
    free(err);
    char *e = lime_snapshot_set_lexicon(snap, items, nk, NULL, 0, LIME_LEX_FOLD_CASE);
    if (e) {
        fprintf(stderr, "set_lexicon: %s\n", e);
        return 1;
    }
    const LimeLexicon *lx = lime_snapshot_lexicon(snap);
    TokenTable *tt = create_token_table(1024);
    for (size_t i = 0; i < nk; i++)
        add_token(tt, items[i].text, lime_snapshot_token_code(snap, items[i].token), 0);

    free(g);
    Words w = { 0 };
    size_t cap = 0, bytes = 0;
    char **bufs = calloc((size_t)argc, sizeof *bufs);
    for (int a = 3; a < argc; a++) {
        size_t n;
        char *b = slurp(argv[a], &n);
        if (!b) continue;
        bufs[a] = b; /* kept alive: words point into it */
        words_from(&w, &cap, b, n);
        bytes += n;
    }
    size_t hits = 0, bad = 0;
    for (size_t i = 0; i < w.count; i++) {
        int x = lime_kw_lookup(lx, w.s[i], w.n[i]);
        int y = lookup_token(tt, w.s[i], w.n[i]);
        hits += x >= 0;
        if (x != y) {
            if (bad++ < 5) fprintf(stderr, "  MISMATCH '%.*s': %d vs %d\n", (int)w.n[i], w.s[i], x, y);
        }
    }
    printf("kw: %zu keywords, %zu identifiers (%zu keyword hits, %.1f%%) from %d files, %zu bytes; "
           "%zu mismatches\n",
           nk, w.count, hits, 100.0 * (double)hits / (double)w.count, argc - 3, bytes, bad);
    int rc = bad ? 1 : 0;
    if (bad || env_int("BENCH_CHECK_ONLY", 0)) goto done;

    int reps = env_int("BENCH_REPS", 21);
    int iters = env_int("BENCH_ITERS", 20);
    double *a1 = malloc(sizeof(double) * (size_t)reps), *b1 = malloc(sizeof(double) * (size_t)reps);
    double *a4 = malloc(sizeof(double) * (size_t)reps), *b4 = malloc(sizeof(double) * (size_t)reps);
    kw_rep(lx, tt, &w, 0, 1, 2); /* warm-up */
    kw_rep(lx, tt, &w, 1, 1, 2);
    for (int r = 0; r < reps; r++) {
        /* alternate the order each rep so drift hits both equally */
        if (r & 1) {
            b1[r] = kw_rep(lx, tt, &w, 1, 1, iters);
            a1[r] = kw_rep(lx, tt, &w, 0, 1, iters);
        } else {
            a1[r] = kw_rep(lx, tt, &w, 0, 1, iters);
            b1[r] = kw_rep(lx, tt, &w, 1, 1, iters);
        }
    }
    for (int r = 0; r < reps; r++) {
        if (r & 1) {
            b4[r] = kw_rep(lx, tt, &w, 1, 4, iters);
            a4[r] = kw_rep(lx, tt, &w, 0, 4, iters);
        } else {
            a4[r] = kw_rep(lx, tt, &w, 0, 4, iters);
            b4[r] = kw_rep(lx, tt, &w, 1, 4, iters);
        }
    }
    double ratio1[64], ratio4[64];
    for (int r = 0; r < reps && r < 64; r++) {
        ratio1[r] = a1[r] / b1[r];
        ratio4[r] = a4[r] / b4[r];
    }
    printf("ns per identifier (CPU time):\n");
    report("snapshot kw table, 1 thread", a1, reps);
    report("TokenTable lookup_token, 1 thread", b1, reps);
    report("ratio kw/TokenTable, 1 thread", ratio1, reps < 64 ? reps : 64);
    report("snapshot kw table, 4 threads", a4, reps);
    report("TokenTable lookup_token, 4 threads", b4, reps);
    report("ratio kw/TokenTable, 4 threads", ratio4, reps < 64 ? reps : 64);
    free(a1);
    free(b1);
    free(a4);
    free(b4);
done:
    destroy_token_table(tt);
    snapshot_release(snap);
    for (int a = 0; a < argc; a++) free(bufs[a]);
    free(bufs);
    free(w.s);
    free(w.n);
    free(kb);
    return rc;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "kw") == 0) return mode_kw(argc, argv);
    fprintf(stderr, "usage: bench_lexicon kw <kwlist> <files...>\n");
    return 2;
}
