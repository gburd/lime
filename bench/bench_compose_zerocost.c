/*
** bench_compose_zerocost.c -- does composing WITH a policy, and binding
** extension reduce callbacks, cost anything at parse time?
**
** Builds, from examples/pg/pg_grammar.lime + tests/compose_fixtures/
** dialect_ext.lime (30 rules, 9 tokens):
**   B   base snapshot (lime_compile_grammar_in_process)
**   P   base+dialect composed under a policy (lime_compose_text,
**       `conflict sr "upsert_cols ::=" on LPAREN prefer shift.`)
**   X0  the same extension as GrammarModifications, composed by
**       publish_modified_snapshot under the same policy, no callbacks
**   X   the same with a LimeReduceFn on every rule: ext_reduce bound
**       (tables identical to X0, checked; P differs from X0 only because
**       mod_serialize writes each precedence change as its own level)
** and parses every statement of a SQL corpus (crude tokenizer: keywords by
** terminal name, else IDENT) through the public push parser:
**   B, P, X0, X          recognition only (no host_reduce)
**   Bh, Xh               with a trivial pass-through host_reduce (what a
**                        host with real actions pays; Xh is X with it)
**
** Correctness first (exit 1 on any difference): every base terminal has
** the same external code in B, P and X; per-statement accept/reject is
** identical across all five; the sequence of reduced rule IDENTITIES is
** identical for B and P and X (hashed); no extension callback fires on
** the base corpus.
**
** Timing: CPU time (CLOCK_THREAD_CPUTIME_ID), R rounds, configs
** interleaved and rotated per round, median + min/max per config, and the
** median of per-round paired ratios.
**
** usage: bench_compose_zerocost pg_grammar.lime dialect_ext.lime SQLDIR [rounds=21] [passes=3]
*/
#include "extension.h"
#include "lime_compiler.h"
#include "lime_policy.h"
#include "parse_context.h"
#include "parser.h"
#include "snapshot.h"

#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END);
    long m = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)m + 1);
    if (!b || fread(b, 1, (size_t)m, f) != (size_t)m) exit(2);
    b[m] = 0;
    fclose(f);
    if (n) *n = (size_t)m;
    return b;
}

static double cpu(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* ---- extension as GrammarModifications, parsed from the .lime text ---- */
static GrammarModification g_mods[256];
static uint32_t g_nmods;
static long g_ext_calls;
static void ext_cb(void *u, void *x, int n, const void *v, const int *l, void *out) {
    (void)x; (void)l;
    ++*(long *)u;
    *(void **)out = n > 0 ? ((void *const *)v)[0] : NULL;
}
static char *dupn(const char *s, size_t n) {
    char *r = malloc(n + 1);
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}
/* statements: "%token A B.", "%fallback T A B.", "%left A B.", "lhs ::= r1 r2 ." */
static void load_ext_mods(const char *text) {
    const char *p = text;
    char *words[64];
    int nw = 0;
    while (*p) {
        if (p[0] == '/' && p[1] == '*') {
            const char *e = strstr(p + 2, "*/");
            p = e ? e + 2 : p + strlen(p);
            continue;
        }
        if (isspace((unsigned char)*p)) { p++; continue; }
        if (*p == '.') {
            if (nw == 0) { p++; continue; }
            if (strcmp(words[0], "%token") == 0) {
                for (int i = 1; i < nw; i++) {
                    GrammarModification *m = &g_mods[g_nmods++];
                    memset(m, 0, sizeof *m);
                    m->type = MOD_ADD_TOKEN;
                    m->u.add_token.name = words[i];
                    m->u.add_token.token_code = -1;
                }
            } else if (strcmp(words[0], "%fallback") == 0) {
                for (int i = 2; i < nw; i++)
                    for (uint32_t k = 0; k < g_nmods; k++)
                        if (g_mods[k].type == MOD_ADD_TOKEN && !strcmp(g_mods[k].u.add_token.name, words[i]))
                            g_mods[k].u.add_token.fallback_to = words[1];
            } else if (strcmp(words[0], "%left") == 0) {
                for (int i = 1; i < nw; i++) {
                    GrammarModification *m = &g_mods[g_nmods++];
                    memset(m, 0, sizeof *m);
                    m->type = MOD_MODIFY_PRECEDENCE;
                    m->u.modify_prec.symbol = words[i];
                    m->u.modify_prec.new_precedence = 1;
                    m->u.modify_prec.new_assoc = 1;
                }
            } else if (nw >= 2 && strcmp(words[1], "::=") == 0) {
                GrammarModification *m = &g_mods[g_nmods++];
                memset(m, 0, sizeof *m);
                m->type = MOD_ADD_RULE;
                m->u.add_rule.lhs = words[0];
                const char **rhs = calloc((size_t)nw, sizeof(char *));
                for (int i = 2; i < nw; i++) rhs[i - 2] = words[i];
                m->u.add_rule.rhs = rhs;
                m->u.add_rule.nrhs = nw - 2;
                m->u.add_rule.code = "{ }";
                m->u.add_rule.reduce = ext_cb;
                m->u.add_rule.reduce_user = &g_ext_calls;
                m->u.add_rule.precedence = -1;
            }
            nw = 0;
            p++;
            continue;
        }
        const char *q = p;
        if (p[0] == ':' && p[1] == ':' && p[2] == '=') q = p + 3;
        else while (*q && !isspace((unsigned char)*q) && *q != '.') q++;
        words[nw++] = dupn(p, (size_t)(q - p));
        p = q;
    }
}
static bool get_mods(void *u, const struct ParserSnapshot *b, GrammarModification **o, uint32_t *n) {
    (void)u; (void)b;
    *o = g_mods;
    *n = g_nmods;
    return true;
}

/* ---- corpus -> token stream (external codes of B) ---- */
static int *TT;
static size_t NT, CT;
static size_t *ST; /* statement starts */
static size_t NS, CS;
static const ParserSnapshot *TS;
static int sym(const char *n) { return lime_snapshot_token_code(TS, n); }
static int kw(const char *w, int len) {
    char b[80];
    if (len > 70) return -1;
    for (int i = 0; i < len; i++) b[i] = (char)toupper((unsigned char)w[i]);
    b[len] = 0;
    int c = sym(b);
    if (c > 0 && c < (int)TS->nterminal + (int)TS->yy_first_token) return c;
    strcat(b, "_P");
    c = sym(b);
    return (c > 0 && c < (int)TS->nterminal + (int)TS->yy_first_token) ? c : -1;
}
static void emit(int c) {
    if (NT + 2 >= CT) { CT = CT ? CT * 2 : 1 << 16; TT = realloc(TT, CT * sizeof *TT); }
    TT[NT++] = c;
}
static void tokenize_file(const char *s) {
    int IDENT = sym("IDENT"), SCONST = sym("SCONST"), ICONST = sym("ICONST"), FCONST = sym("FCONST"),
        PARAM = sym("PARAM"), OP = sym("Op") > 0 ? sym("Op") : sym("OP");
    static const struct { const char *t, *n; } ops[] = {
        {"::", "TYPECAST"}, {":=", "COLON_EQUALS"}, {"=>", "EQUALS_GREATER"}, {"<=", "LESS_EQUALS"},
        {">=", "GREATER_EQUALS"}, {"<>", "NOT_EQUALS"}, {"!=", "NOT_EQUALS"}, {"..", "DOT_DOT"},
        {";", "SEMICOLON"}, {"(", "LPAREN"}, {")", "RPAREN"}, {",", "COMMA"}, {".", "DOT"},
        {"+", "PLUS"}, {"-", "MINUS"}, {"*", "STAR"}, {"/", "SLASH"}, {"%", "PERCENT"},
        {"^", "CARET"}, {"<", "LT"}, {">", "GT"}, {"=", "EQ"}, {"[", "LBRACKET"},
        {"]", "RBRACKET"}, {":", "COLON"}, {0, 0}};
    int start = 1;
    const char *p = s;
    while (*p) {
        if (start) {
            if (NS >= CS) { CS = CS ? CS * 2 : 4096; ST = realloc(ST, CS * sizeof *ST); }
            ST[NS++] = NT;
            start = 0;
        }
        if (isspace((unsigned char)*p)) { p++; continue; }
        if (p[0] == '-' && p[1] == '-') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') { p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++; if (*p) p += 2; continue; }
        if (*p == '\\') { while (*p && *p != '\n') p++; continue; }
        if (*p == '\'') {
            p++;
            while (*p) { if (*p == '\'' && p[1] == '\'') { p += 2; continue; } if (*p == '\'') break; p++; }
            if (*p) p++;
            emit(SCONST);
            continue;
        }
        if (*p == '"') { p++; while (*p && *p != '"') p++; if (*p) p++; emit(IDENT); continue; }
        if (*p == '$' && isdigit((unsigned char)p[1])) { p++; while (isdigit((unsigned char)*p)) p++; emit(PARAM); continue; }
        if (*p == '$') {
            const char *q = p + 1;
            while (isalnum((unsigned char)*q) || *q == '_') q++;
            if (*q == '$' && (size_t)(q - p + 1) < 63) {
                char tag[64];
                size_t tl = (size_t)(q - p + 1);
                memcpy(tag, p, tl);
                tag[tl] = 0;
                const char *f = strstr(q + 1, tag);
                p = f ? f + tl : q + 1 + strlen(q + 1);
                emit(SCONST);
                continue;
            }
        }
        if (isdigit((unsigned char)*p)) {
            int fl = 0;
            while (isdigit((unsigned char)*p) || (*p == '.' && p[1] != '.') || *p == 'e' || *p == 'E') {
                if (*p == '.' || *p == 'e' || *p == 'E') fl = 1;
                p++;
            }
            emit(fl ? FCONST : ICONST);
            continue;
        }
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *q = p;
            while (isalnum((unsigned char)*p) || *p == '_' || *p == '$') p++;
            int c = kw(q, (int)(p - q));
            emit(c > 0 ? c : IDENT);
            continue;
        }
        int m = 0;
        for (int k = 0; ops[k].t; k++) {
            size_t l = strlen(ops[k].t);
            if (!strncmp(p, ops[k].t, l)) {
                if (!strcmp(ops[k].n, "SEMICOLON")) { emit(0); start = 1; }
                else { int c = sym(ops[k].n); emit(c < 0 ? OP : c); }
                p += l;
                m = 1;
                break;
            }
        }
        if (m) continue;
        if (strchr("~!@#&|`?", *p)) { while (*p && strchr("~!@#&|`?+-*/<>=%^", *p)) p++; emit(OP); continue; }
        p++;
    }
    if (!start) emit(0);
}

/* ---- parse the corpus ---- */
static uint64_t g_hash;
static long g_reduces;
static int trace_red(void *user, int ruleno, const void *rhs, const int *locs, int nrhs, void *lhs,
                     int *lhs_loc) {
    (void)rhs; (void)locs; (void)lhs_loc;
    const char *id = lime_snapshot_rule_id((const ParserSnapshot *)user, ruleno);
    for (const char *c = id ? id : "?"; *c; c++) { g_hash ^= (unsigned char)*c; g_hash *= 1099511628211ULL; }
    g_hash ^= 0xff; g_hash *= 1099511628211ULL;
    g_reduces++;
    *(void **)lhs = nrhs > 0 ? ((void *const *)rhs)[0] : NULL;
    return 0;
}
static int pass_red(void *user, int ruleno, const void *rhs, const int *locs, int nrhs, void *lhs,
                    int *lhs_loc) {
    (void)user; (void)ruleno; (void)locs; (void)lhs_loc;
    *(void **)lhs = nrhs > 0 ? ((void *const *)rhs)[0] : NULL;
    return 0;
}
/* returns #accepted; acc[] per statement if non-NULL */
static long run(ParserSnapshot *s, LimeHostReduceFn hr, void *hu, unsigned char *acc) {
    long ok = 0;
    for (size_t i = 0; i < NS; i++) {
        size_t a = ST[i], b = i + 1 < NS ? ST[i + 1] : NT;
        ParseContext *ctx = parse_begin(s);
        if (hr) parse_set_host_reduce(ctx, hr, hu);
        int rc = 0;
        for (size_t k = a; k < b && rc == 0; k++) rc = parse_token(ctx, TT[k], NULL, -1);
        parse_end(ctx);
        if (acc) acc[i] = rc == 1;
        ok += rc == 1;
    }
    return ok;
}

static int cmpd(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s pg_grammar.lime dialect_ext.lime SQLDIR [rounds] [passes]\n", argv[0]);
        return 2;
    }
    int R = argc > 4 ? atoi(argv[4]) : 21, K = argc > 5 ? atoi(argv[5]) : 3;
    size_t nb, ne;
    char *base = slurp(argv[1], &nb), *ext = slurp(argv[2], &ne), *err = NULL;
    static const char POL[] = "conflict sr \"upsert_cols ::=\" on LPAREN prefer shift.";
    LimeComposePolicy *pol = lime_policy_parse(POL, sizeof POL - 1, &err);

    ParserSnapshot *B = NULL, *B2 = NULL, *P = NULL, *X = NULL, *X0 = NULL;
    if (lime_compile_grammar_in_process(base, nb, &B, &err) != 0) { printf("base: %s\n", err); return 1; }
    /* A/A: the same base compiled again (same tables, different addresses) */
    if (lime_compile_grammar_in_process(base, nb, &B2, &err) != 0) { printf("base: %s\n", err); return 1; }
    LimeComposeUnit u = {"dialect_ext", "dialect_ext.lime", ext, ne};
    if (lime_compose_text(base, nb, &u, 1, pol, &P, NULL, &err) != 0) { printf("P: %s\n", err); return 1; }
    load_ext_mods(ext);
    for (int pass = 0; pass < 2; pass++) { /* pass 0: X0, no callbacks; pass 1: X, bound */
        for (uint32_t k = 0; k < g_nmods; k++)
            if (g_mods[k].type == MOD_ADD_RULE) g_mods[k].u.add_rule.reduce = pass ? ext_cb : NULL;
        ExtensionRegistry *reg = create_extension_registry();
        extension_registry_set_policy(reg, pol);
        ExtensionInfo info = {.name = "dialect_ext", .version = "1", .get_modifications = get_mods};
        ExtensionID id;
        register_extension(reg, &info, &id);
        load_extension(reg, id, NULL, NULL);
        ParserSnapshot **dst = pass ? &X : &X0;
        if (!publish_modified_snapshot(reg, B, dst, NULL, &err)) { printf("X: %s\n", err); return 1; }
        destroy_extension_registry(reg);
    }
    if (X0->ext_reduce != NULL) { printf("X0 unexpectedly has bindings\n"); return 1; }
    uint32_t nbound = 0;
    for (uint32_t i = 0; X->ext_reduce && i < X->ext_reduce_count; i++) nbound += X->ext_reduce[i].reduce != NULL;
    printf("B: nstate %u nrule %u nterminal %u | P: nstate %u nrule %u | X: nstate %u nrule %u, "
           "ext_reduce bound on %u rules\n",
           B->nstate, B->nrule, B->nterminal, P->nstate, P->nrule, X->nstate, X->nrule, nbound);

    {
        ParserSnapshot *S3[4] = {B, P, X, X0};
        uint64_t f[4];
        for (int i = 0; i < 4; i++) {
            const ParserSnapshot *q = S3[i];
            uint64_t h = 1469598103934665603ULL;
#define HB(ptr, nb) do { const unsigned char *z_ = (const void *)(ptr); \
        for (size_t i_ = 0; i_ < (size_t)(nb); i_++) { h ^= z_[i_]; h *= 1099511628211ULL; } } while (0)
            HB(q->yy_action, q->action_count * 2); HB(q->yy_lookahead, q->lookahead_count * 2);
            HB(q->yy_shift_ofst, q->nstate * 4); HB(q->yy_reduce_ofst, q->nstate * 4);
            HB(q->yy_default, q->nstate * 2);
#undef HB
            f[i] = h;
        }
        printf("table hash B %016llx P %016llx X %016llx X0 %016llx (X and X0 %s)\n",
               (unsigned long long)f[0], (unsigned long long)f[1], (unsigned long long)f[2],
               (unsigned long long)f[3], f[2] == f[3] ? "identical" : "DIFFER");
        if (f[2] != f[3]) return 1;
    }
    /* base terminal codes are stable across composition */
    int moved = 0;
    for (uint32_t i = 1; i < B->nterminal; i++) {
        const char *n = B->token_names[i];
        int c = lime_snapshot_token_code(B, n);
        if (lime_snapshot_token_code(P, n) != c || lime_snapshot_token_code(X, n) != c ||
            lime_snapshot_token_code(X0, n) != c) moved++;
    }
    printf("base terminals whose code differs in P or X: %d\n", moved);

    TS = B;
    DIR *d = opendir(argv[3]);
    struct dirent *de;
    int nf = 0;
    while (d && (de = readdir(d))) {
        size_t l = strlen(de->d_name);
        if (l < 5 || strcmp(de->d_name + l - 4, ".sql")) continue;
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", argv[3], de->d_name);
        char *t = slurp(path, NULL);
        tokenize_file(t);
        free(t);
        nf++;
    }
    if (d) closedir(d);
    printf("corpus: %d files, %zu statements, %zu tokens\n", nf, NS, NT - NS);

    /* correctness */
    unsigned char *aB = malloc(NS), *aP = malloc(NS), *aX = malloc(NS), *aBh = malloc(NS), *aXh = malloc(NS);
    g_hash = 1469598103934665603ULL; g_reduces = 0;
    long okB = run(B, trace_red, B, aB);
    uint64_t hB = g_hash; long rB = g_reduces;
    g_hash = 1469598103934665603ULL; g_reduces = 0;
    long okP = run(P, trace_red, P, aP);
    uint64_t hP = g_hash; long rP = g_reduces;
    g_hash = 1469598103934665603ULL; g_reduces = 0;
    long okXt = run(X, trace_red, X, aXh);
    uint64_t hX = g_hash; long rX = g_reduces;
    g_ext_calls = 0;
    long okX = run(X, NULL, NULL, aX);
    long ext_calls = g_ext_calls;
    long okBr = run(B, NULL, NULL, aBh);
    unsigned char *aX0 = malloc(NS);
    run(X0, NULL, NULL, aX0);
    int same = !memcmp(aB, aP, NS) && !memcmp(aB, aX, NS) && !memcmp(aB, aXh, NS) && !memcmp(aB, aBh, NS) &&
               !memcmp(aB, aX0, NS);
    printf("accepted: B %ld  P %ld  X %ld (trace %ld)  B(recog) %ld  of %zu; per-statement identical: %s\n",
           okB, okP, okX, okXt, okBr, NS, same ? "yes" : "NO");
    printf("reduces B %ld P %ld X %ld; rule-identity sequence hash B %016llx P %016llx X %016llx\n", rB, rP, rX,
           (unsigned long long)hB, (unsigned long long)hP, (unsigned long long)hX);
    printf("extension callbacks fired on the base corpus: %ld\n", ext_calls);
    if (moved || !same || hB != hP || hB != hX || rB != rP || ext_calls != 0) {
        printf("CORRECTNESS FAILED\n");
        return 1;
    }

    /* LIME_BENCH_ONLY=<cfg> R: run one configuration R times and exit
    ** (for perf stat / perf record on a single configuration, and for
    ** comparing two builds process by process). */
    const char *only = getenv("LIME_BENCH_ONLY");
    if (only) {
        int hr = only[0] && only[strlen(only) - 1] == 'h'; /* "Bh", "Xh": + host_reduce */
        ParserSnapshot *o = only[0] == 'X' ? (only[1] == '0' ? X0 : X) : B;
        double t0 = cpu();
        for (int k = 0; k < R * K; k++) run(o, hr ? pass_red : NULL, NULL, NULL);
        printf("only %s: %.2f ms\n", only, cpu() - t0);
        return 0;
    }

    /* timing */
    enum { NC = 7 };
    static const char *names[NC] = {"B  base, recognition", "B2 base again (A/A noise floor)",
                                    "P  +policy, recognition", "X0 +policy (publish), no callbacks",
                                    "X  X0 + bound ext callbacks", "Bh base + host_reduce",
                                    "Xh X + host_reduce"};
    ParserSnapshot *snaps[NC] = {B, B2, P, X0, X, B, X};
    LimeHostReduceFn hrs[NC] = {NULL, NULL, NULL, NULL, NULL, pass_red, pass_red};
    static const int vs[NC] = {0, 0, 0, 0, 3, 5, 5};  /* the comparison for each row */
    static const char *vsname[NC] = {"B ", "B ", "B ", "B ", "X0", "Bh", "Bh"};
    double *t[NC];
    for (int c = 0; c < NC; c++) t[c] = calloc((size_t)R, sizeof(double));
    for (int r = -1; r < R; r++) { /* r = -1: warm-up */
        for (int j = 0; j < NC; j++) {
            int c = (j + (r < 0 ? 0 : r)) % NC; /* rotate the order each round */
            double t0 = cpu();
            for (int k = 0; k < K; k++) run(snaps[c], hrs[c], NULL, NULL);
            double dt = cpu() - t0;
            if (r >= 0) t[c][r] = dt;
        }
    }
    double ratio[NC][64];
    for (int c = 0; c < NC; c++)
        for (int r = 0; r < R && r < 64; r++) ratio[c][r] = t[c][r] / t[vs[c]][r];
    printf("\n%d rounds x %d passes, CPU ms per round (median [min, max]); ratio = median of per-round ratios\n", R, K);
    for (int c = 0; c < NC; c++) {
        double s[64];
        memcpy(s, t[c], (size_t)R * sizeof(double));
        qsort(s, (size_t)R, sizeof(double), cmpd);
        qsort(ratio[c], (size_t)R, sizeof(double), cmpd);
        printf("  %-36s %8.2f [%8.2f, %8.2f]   vs %s: %.4f [%.4f, %.4f]\n", names[c], s[R / 2], s[0],
               s[R - 1], vsname[c], ratio[c][R / 2], ratio[c][0], ratio[c][R - 1]);
    }
    {
        double s[64];
        memcpy(s, t[0], (size_t)R * sizeof(double));
        qsort(s, (size_t)R, sizeof(double), cmpd);
        printf("  B: %.1f ns per token\n", s[R / 2] * 1e6 / ((double)(NT - NS) * K));
    }
    return 0;
}
