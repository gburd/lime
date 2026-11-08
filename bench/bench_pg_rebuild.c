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
** Per compile it measures CPU time (CLOCK_PROCESS_CPUTIME_ID, immune to
** being descheduled), wall time, peak RSS and, where perf counters are
** allowed, user-space instructions.  It reports min / median / max over
** N compiles after one warm-up, then a per-phase CPU breakdown of the
** same pipeline (ParseText, FindStates, FindFollowSets, the action-table
** pack, ...).  Before any timing it checks that the timed pipeline and
** the per-phase pipeline build the same tables (by hash).
**
** usage: bench_pg_rebuild <grammar.lime> [iterations] [--compose ext.lime]
**
** --compose appends ext.lime to the grammar text the way
** publish_modified_snapshot does (base, '\n', fragment) and reports the
** base and the composed grammar side by side.
**
** Use examples/pg/pg_grammar.lime (tokens.lime + gram.lime, 0 conflicts).
** gram.lime alone has no precedence declarations and 1665 conflicts, so
** it measures a grammar nobody would ship.
**
** Built like tests/test_lime_compile_in_process.c: it #includes lime.c so
** the per-phase timer can call the pipeline's phases directly.
*/
#include "../lime.c"

/* lime.c's active-context macros rewire these bare identifiers. */
#undef memChunkList
#undef x1a
#undef x2a
#undef x3a
#undef x4a
#undef plink_freelist
#undef actionfreelist
#undef current
#undef currentend
#undef basis
#undef basisend
#undef nDefine
#undef nDefineUsed
#undef azDefine
#undef bDefineUsed

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
/* Instruction counts need the kernel headers (Alpine: linux-headers, not in build-base). */
#if defined(__linux__) && defined(__has_include)
#  if __has_include(<linux/perf_event.h>)
#    define LIME_BENCH_PERF 1
#  endif
#endif
#ifdef LIME_BENCH_PERF
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

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

/* User-space instructions retired, or -1 where perf counters are not
** available (non-Linux, no kernel headers, or kernel.perf_event_paranoid too high). */
static int perf_fd = -1;
static void instr_open(void) {
#ifdef LIME_BENCH_PERF
    struct perf_event_attr a;
    memset(&a, 0, sizeof a);
    a.type = PERF_TYPE_HARDWARE;
    a.size = sizeof a;
    a.config = PERF_COUNT_HW_INSTRUCTIONS;
    a.exclude_kernel = 1;
    a.exclude_hv = 1;
    perf_fd = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
#endif
}
static long long instr_now(void) {
#ifdef LIME_BENCH_PERF
    long long v;
    if (perf_fd >= 0 && read(perf_fd, &v, sizeof v) == (ssize_t)sizeof v) return v;
#endif
    return -1;
}

static uint64_t table_hash(const ParserSnapshot *s) {
    uint64_t h = 1469598103934665603ULL;
#define HASH(p, nb)                                                                                \
    do {                                                                                           \
        const unsigned char *q = (const unsigned char *)(p);                                       \
        for (size_t z = 0; z < (size_t)(nb); z++) h = (h ^ q[z]) * 1099511628211ULL;               \
    } while (0)
    HASH(s->yy_action, s->action_count * sizeof s->yy_action[0]);
    HASH(s->yy_lookahead, s->lookahead_count * sizeof s->yy_lookahead[0]);
    HASH(s->yy_shift_ofst, s->nstate * sizeof s->yy_shift_ofst[0]);
    HASH(s->yy_reduce_ofst, s->nstate * sizeof s->yy_reduce_ofst[0]);
    HASH(s->yy_default, s->nstate * sizeof s->yy_default[0]);
#undef HASH
    return h;
}

/* ---- per-phase timing: lime_compile_grammar_in_process_ex's pipeline,
** phase by phase.  Keep in step with that function. */
enum {
    P_PARSE, P_SETUP, P_FIRST, P_STATES, P_LINKS, P_FOLLOW, P_ACTIONS,
    P_COMPRESS, P_PACK, P_DESTROY, NPHASE
};
static const char *const phase_name[NPHASE] = {
    "ParseText", "post-parse setup", "FindRulePrecedences+FirstSets", "FindStates",
    "FindLinks", "FindFollowSets", "FindActions", "CompressTables+ResortStates",
    "build_snapshot (action pack)", "context teardown",
};

static ParserSnapshot *phased_build(const char *text, size_t len, double *t, long long *ins) {
    LimeCompilerContext cc;
    LimeCompilerContext *prev = lime_active_ctx;
    struct lime lem;
    ParserSnapshot *s = NULL;
    double c[NPHASE + 1];
    long long n[NPHASE + 1];
    int p = 0;
#define MARK() (c[p] = clk(CLOCK_PROCESS_CPUTIME_ID), n[p] = instr_now(), p++)
    MARK();
    lime_compiler_context_init(&cc);
    memset(&lem, 0, sizeof lem);
    lem.nexpect = -1;
    lem.filename = (char *)"<grammar text>";
    Strsafe_init();
    Symbol_init();
    State_init();
    Symbol_new("$");
    ParseText(&lem, text, len);
    MARK();
    const char *why = NULL;
    lem.errsym = Symbol_find("error");
    if (lem.errorcnt == 0 && lime_post_parse_setup(&lem, &why) == 0) {
        MARK();
        SetSize(lem.nterminal + 1);
        FindRulePrecedences(&lem);
        FindFirstSets(&lem);
        MARK();
        lem.nstate = 0;
        FindStates(&lem);
        lem.sorted = State_arrayof();
        MARK();
        FindLinks(&lem);
        MARK();
        FindFollowSets(&lem);
        MARK();
        FindActions(&lem);
        MARK();
        int saved = g_keep_unit_reductions;
        g_keep_unit_reductions = 1;
        CompressTables(&lem);
        g_keep_unit_reductions = saved;
        ResortStates(&lem);
        MARK();
        s = build_snapshot_from_lime(&lem, text, len);
        MARK();
    }
    lime_compiler_context_destroy(&cc);
    lime_active_ctx = prev;
    if (p == NPHASE) MARK();
#undef MARK
    if (p != NPHASE + 1) {
        if (s) snapshot_release(s);
        return NULL;
    }
    for (int i = 0; i < NPHASE; i++) {
        t[i] = (c[i + 1] - c[i]) * 1e3;
        ins[i] = (n[0] >= 0) ? n[i + 1] - n[i] : -1;
    }
    return s;
}

typedef struct {
    const char *label;
    double cpu_med, cpu_min, cpu_max, wall_med;
    long long ins_med;
    long rss_mb;
    double ph_med[NPHASE];
    long long ph_ins[NPHASE];
    uint32_t nstate, nterminal, nrule, action_count;
    int nconflict;
} Result;

static int measure(const char *label, const char *text, size_t len, int iters, Result *r) {
    memset(r, 0, sizeof *r);
    r->label = label;
    ParserSnapshot *snap = NULL;
    char *err = NULL;
    int nconflict = -1;
    /* Warm-up, and the reference table hash. */
    if (lime_compile_grammar_in_process_ex(text, len, &snap, &err, &nconflict) != 0 || snap == NULL) {
        fprintf(stderr, "%s: compile failed: %s\n", label, err ? err : "(no message)");
        free(err);
        return -1;
    }
    free(err);
    uint64_t want = table_hash(snap);
    r->nstate = snap->nstate;
    r->nterminal = snap->nterminal;
    r->nrule = snap->nrule;
    r->action_count = snap->action_count;
    r->nconflict = nconflict;
    snapshot_release(snap);

    double *cpu = calloc((size_t)iters, sizeof(double));
    double *wall = calloc((size_t)iters, sizeof(double));
    double *ins = calloc((size_t)iters, sizeof(double));
    double (*ph)[NPHASE] = calloc((size_t)iters, sizeof *ph);
    double (*phi)[NPHASE] = calloc((size_t)iters, sizeof *phi);
    if (!cpu || !wall || !ins || !ph || !phi) return -1;
    for (int i = 0; i < iters; i++) {
        snap = NULL;
        err = NULL;
        long long i0 = instr_now();
        double w0 = clk(CLOCK_MONOTONIC), c0 = clk(CLOCK_PROCESS_CPUTIME_ID);
        int rc = lime_compile_grammar_in_process(text, len, &snap, &err);
        double c1 = clk(CLOCK_PROCESS_CPUTIME_ID), w1 = clk(CLOCK_MONOTONIC);
        long long i1 = instr_now();
        if (rc != 0 || snap == NULL || table_hash(snap) != want) {
            fprintf(stderr, "%s: iteration %d failed or built different tables\n", label, i);
            return -1;
        }
        snapshot_release(snap);
        free(err);
        cpu[i] = (c1 - c0) * 1e3;
        wall[i] = (w1 - w0) * 1e3;
        ins[i] = (i0 >= 0) ? (double)(i1 - i0) : -1;
    }
    for (int i = 0; i < iters; i++) {
        long long pi[NPHASE];
        ParserSnapshot *s = phased_build(text, len, ph[i], pi);
        if (s == NULL || table_hash(s) != want) {
            fprintf(stderr, "%s: per-phase pipeline built different tables\n", label);
            return -1;
        }
        snapshot_release(s);
        for (int k = 0; k < NPHASE; k++) phi[i][k] = (double)pi[k];
    }
    r->rss_mb = peak_rss_kb() / 1024;
    qsort(cpu, (size_t)iters, sizeof(double), cmp_d);
    qsort(wall, (size_t)iters, sizeof(double), cmp_d);
    qsort(ins, (size_t)iters, sizeof(double), cmp_d);
    r->cpu_min = cpu[0];
    r->cpu_med = cpu[iters / 2];
    r->cpu_max = cpu[iters - 1];
    r->wall_med = wall[iters / 2];
    r->ins_med = (long long)ins[iters / 2];
    double *col = calloc((size_t)iters, sizeof(double));
    for (int k = 0; k < NPHASE && col; k++) {
        for (int i = 0; i < iters; i++) col[i] = ph[i][k];
        qsort(col, (size_t)iters, sizeof(double), cmp_d);
        r->ph_med[k] = col[iters / 2];
        for (int i = 0; i < iters; i++) col[i] = phi[i][k];
        qsort(col, (size_t)iters, sizeof(double), cmp_d);
        r->ph_ins[k] = (long long)col[iters / 2];
    }
    free(col);
    free(cpu);
    free(wall);
    free(ins);
    free(ph);
    free(phi);
    return 0;
}

int main(int argc, char **argv) {
    const char *gpath = NULL, *epath = NULL;
    int iters = 7;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--compose") == 0 && i + 1 < argc)
            epath = argv[++i];
        else if (gpath == NULL)
            gpath = argv[i];
        else
            iters = atoi(argv[i]);
    }
    if (gpath == NULL) {
        fprintf(stderr, "usage: %s <grammar.lime> [iterations] [--compose ext.lime]\n", argv[0]);
        return 2;
    }
    if (iters < 1) iters = 1;

    size_t len = 0, elen = 0;
    char *text = slurp(gpath, &len);
    char *ext = epath ? slurp(epath, &elen) : NULL;
    if (text == NULL || (epath && ext == NULL)) {
        fprintf(stderr, "cannot read %s\n", text == NULL ? gpath : epath);
        return 2;
    }
    instr_open();

    Result r[2];
    int nres = 0;
    if (measure("base", text, len, iters, &r[nres]) != 0) return 1;
    nres++;
    if (ext) {
        /* publish_modified_snapshot's merge: base, '\n', fragment. */
        size_t clen = len + 1 + elen;
        char *composed = malloc(clen + 1);
        if (composed == NULL) return 1;
        memcpy(composed, text, len);
        composed[len] = '\n';
        memcpy(composed + len + 1, ext, elen + 1);
        if (measure("composed", composed, clen, iters, &r[nres]) != 0) return 1;
        nres++;
        free(composed);
    }

    printf("grammar : %s (%zu bytes)%s%s\n", gpath, len, ext ? "  + " : "", ext ? epath : "");
    printf("measure : %d iterations after one warm-up; CPU ms; median [min, max]%s\n", iters,
           perf_fd >= 0 ? "; instructions = user-space, median" : "; no perf counters");
    for (int k = 0; k < nres; k++)
        printf("%-9s: nstate=%u nterminal=%u nrule=%u action_count=%u nconflict=%d\n", r[k].label,
               r[k].nstate, r[k].nterminal, r[k].nrule, r[k].action_count, r[k].nconflict);
    printf("\n%-30s", "phase (CPU ms, median)");
    for (int k = 0; k < nres; k++) printf(" %12s", r[k].label);
    if (perf_fd >= 0)
        for (int k = 0; k < nres; k++) printf(" %10s G-ins", r[k].label);
    printf("\n");
    for (int p = 0; p < NPHASE; p++) {
        printf("%-30s", phase_name[p]);
        for (int k = 0; k < nres; k++) printf(" %12.1f", r[k].ph_med[p]);
        if (perf_fd >= 0)
            for (int k = 0; k < nres; k++) printf(" %16.2f", r[k].ph_ins[p] / 1e9);
        printf("\n");
    }
    printf("\n");
    for (int k = 0; k < nres; k++) {
        printf("rebuild %-8s: cpu ms median=%.1f min=%.1f max=%.1f | wall median=%.1f", r[k].label,
               r[k].cpu_med, r[k].cpu_min, r[k].cpu_max, r[k].wall_med);
        if (r[k].ins_med >= 0) printf(" | %.2f G instructions", r[k].ins_med / 1e9);
        printf(" | peak RSS so far %ld MB\n", r[k].rss_mb);
    }
    if (nres == 2)
        printf("composed / base: %.3f (CPU median)\n", r[1].cpu_med / r[0].cpu_med);

    free(text);
    free(ext);
    return 0;
}
