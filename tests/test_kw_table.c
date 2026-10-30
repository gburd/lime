/*
** tests/test_kw_table.c -- B1: the snapshot keyword table.
**
** The keyword table lives in the ParserSnapshot (include/lime_lexer.h),
** is built at compose time and read lock-free.  This test proves:
**
**   1. 1000 keywords: every one is found, case-folded; every
**      non-keyword misses (near misses: one byte changed, one byte
**      added or removed, a prefix, an empty string).
**   2. Case sensitivity is a host choice (LIME_LEX_FOLD_CASE).
**   3. Composition: an extension's MOD_ADD_TOKEN.lexeme becomes a
**      keyword of the COMPOSED snapshot (soft and reserved alike), the
**      base snapshot is untouched, and every base keyword keeps its
**      token code (DESIGN.md section 2, invariant 4).
**   4. Compose failures: a spelling claimed by two tokens; a base
**      terminal whose code moved.
**   5. The composed table does not depend on the order of the mods.
**   6. Concurrent readers across a snapshot swap: readers acquire the
**      current snapshot, look keywords up and release, while a writer
**      publishes new snapshots and releases the old ones.  Every lookup
**      must agree with the snapshot it came from.  Run under TSan
**      (-Db_sanitize=thread) for the race check.
**
** Negative controls (run by hand, recorded in the commit message): make
** lime_kw_lookup stop folding case, or stop probing after the first
** slot, or make lime_lex_compose skip the base-code check; each must
** make this test fail.
*/
#define _POSIX_C_SOURCE 200809L
#include "extension.h"
#include "lime_lexer.h"
#include "parser.h"
#include "snapshot.h"
#include "tokenize.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int lime_compile_grammar_in_process(const char *, size_t, ParserSnapshot **, char **);
extern int lime_snapshot_token_code(const ParserSnapshot *snap, const char *name);

static int fails = 0;
#define CHECK(cond, ...)                                                                     \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                                    \
            printf(__VA_ARGS__);                                                             \
            printf("\n");                                                                    \
            fails++;                                                                         \
        }                                                                                    \
    } while (0)

#define NKW 1000

/* keyword i: a pseudo-random lower-case word, 2..14 bytes, unique by
** construction (the index is encoded in it). */
static void kw_text(int i, char *out) {
    unsigned x = (unsigned)i * 2654435761u + 12345u;
    int len = 2 + (int)(x % 9);
    int k = 0;
    for (int j = 0; j < len; j++) {
        x = x * 1103515245u + 12345u;
        out[k++] = (char)('a' + (x >> 16) % 26);
    }
    /* unique suffix: base-26 index, lower case */
    int v = i;
    do {
        out[k++] = (char)('a' + v % 26);
        v /= 26;
    } while (v);
    out[k] = '\0';
}

static char g_kw[NKW][32];
static char g_tok[NKW][16];
static LimeLexItem g_items[NKW];

/* "%token ID K0 ... K999." + a trivial start rule. */
static char *base_grammar(void) {
    size_t cap = 64 + NKW * 8;
    char *g = malloc(cap);
    size_t n = (size_t)snprintf(g, cap, "%%name KW\n%%token_type { int }\n%%token ID");
    for (int i = 0; i < NKW; i++) n += (size_t)snprintf(g + n, cap - n, " %s", g_tok[i]);
    n += (size_t)snprintf(g + n, cap - n, ".\nstmt ::= ID.\nstmt ::= K0 ID.\n");
    return g;
}

static void upper(const char *s, char *out) {
    for (; *s; s++) *out++ = (char)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s);
    *out = '\0';
}

static void test_thousand(ParserSnapshot *base) {
    const LimeLexicon *lx = lime_snapshot_lexicon(base);
    CHECK(lx != NULL, "no lexicon on base");
    CHECK(lime_lexicon_keyword_count(lx) == NKW, "count %u", lime_lexicon_keyword_count(lx));
    int found = 0, folded = 0, missed = 0, probes = 0;
    char buf[64];
    for (int i = 0; i < NKW; i++) {
        int want = lime_snapshot_token_code(base, g_tok[i]);
        int got = lime_kw_lookup(lx, g_kw[i], strlen(g_kw[i]));
        found += got == want;
        CHECK(got == want, "'%s' -> %d, want %d", g_kw[i], got, want);
        upper(g_kw[i], buf);
        got = lime_kw_lookup(lx, buf, strlen(buf));
        folded += got == want;
        CHECK(got == want, "'%s' (upper) -> %d, want %d", buf, got, want);
        /* mixed case */
        strcpy(buf, g_kw[i]);
        buf[0] = (char)(buf[0] - 32);
        CHECK(lime_kw_lookup(lx, buf, strlen(buf)) == want, "'%s' (mixed)", buf);

        /* near misses: none of these is a keyword (verified against
        ** the brute-force list so a collision in the generator cannot
        ** make the test lie). */
        size_t L = strlen(g_kw[i]);
        const char *cands[4];
        char c0[72], c1[72], c2[72], c3[72];
        strcpy(c0, g_kw[i]);
        c0[L / 2] = c0[L / 2] == 'z' ? 'y' : (char)(c0[L / 2] + 1); /* changed byte */
        memcpy(c1, g_kw[i], L); /* extra byte */
        memcpy(c1 + L, "_", 2);
        memcpy(c2, g_kw[i], L - 1); /* truncated */
        c2[L - 1] = '\0';
        memcpy(c3, g_kw[i], L); /* longer than any keyword */
        c3[L] = 'q';
        memcpy(c3 + L + 1, g_kw[i], L + 1);
        cands[0] = c0; cands[1] = c1; cands[2] = c2; cands[3] = c3;
        for (int c = 0; c < 4; c++) {
            int is_kw = 0;
            for (int j = 0; j < NKW && !is_kw; j++) is_kw = strcmp(cands[c], g_kw[j]) == 0;
            if (is_kw) continue;
            probes++;
            int r = lime_kw_lookup(lx, cands[c], strlen(cands[c]));
            missed += r == -1;
            CHECK(r == -1, "non-keyword '%s' -> %d", cands[c], r);
        }
    }
    CHECK(lime_kw_lookup(lx, "", 0) == -1, "empty string matched");
    CHECK(lime_kw_lookup(NULL, "a", 1) == -1, "NULL lexicon matched");
    printf("  1000 keywords: %d/%d found, %d/%d case-folded, %d/%d non-keywords missed\n", found,
           NKW, folded, NKW, missed, probes);
}

static void test_case_sensitive(ParserSnapshot *base) {
    /* A second snapshot of the same grammar, case-sensitive table. */
    LimeLexItem it[2] = { { "Select", "K1" }, { "select", "K2" } };
    char *e = lime_snapshot_set_lexicon(base, it, 2, NULL, 0, 0);
    CHECK(e == NULL, "set_lexicon: %s", e ? e : "");
    free(e);
    const LimeLexicon *lx = lime_snapshot_lexicon(base);
    CHECK(lime_kw_lookup(lx, "Select", 6) == lime_snapshot_token_code(base, "K1"), "Select");
    CHECK(lime_kw_lookup(lx, "select", 6) == lime_snapshot_token_code(base, "K2"), "select");
    CHECK(lime_kw_lookup(lx, "SELECT", 6) == -1, "SELECT matched case-sensitively");
    /* Folded, the same two spellings collide: must be an error. */
    e = lime_snapshot_set_lexicon(base, it, 2, NULL, 0, LIME_LEX_FOLD_CASE);
    CHECK(e != NULL && strstr(e, "claimed by two tokens"), "fold collision not reported: %s",
          e ? e : "(none)");
    free(e);
    /* Unknown / nonterminal / end-of-input token names are rejected. */
    LimeLexItem bad[3] = { { "x", "NOPE" }, { "y", "stmt" }, { "z", "$" } };
    for (int i = 0; i < 3; i++) {
        e = lime_snapshot_set_lexicon(base, &bad[i], 1, NULL, 0, 0);
        CHECK(e != NULL, "bad token %s accepted", bad[i].token);
        free(e);
    }
    CHECK(lime_kw_lookup(lime_snapshot_lexicon(base), "Select", 6) >= 0,
          "a failed set_lexicon replaced the table");
}

/* ---- composition ---- */

static GrammarModification g_mods[4];
static const char *g_rhs[] = { "CONNECT", "ID", NULL };
static const char *g_rhs2[] = { "PRIOR", "ID", NULL };

static bool get_mods(void *ud, const struct ParserSnapshot *b, GrammarModification **out,
                     uint32_t *nout) {
    (void)ud;
    (void)b;
    memset(g_mods, 0, sizeof g_mods);
    g_mods[0].type = MOD_ADD_TOKEN; /* reserved keyword */
    g_mods[0].u.add_token.name = "CONNECT";
    g_mods[0].u.add_token.lexeme = "connect";
    g_mods[0].u.add_token.token_code = -1;
    g_mods[1].type = MOD_ADD_TOKEN; /* soft keyword */
    g_mods[1].u.add_token.name = "PRIOR";
    g_mods[1].u.add_token.lexeme = "prior";
    g_mods[1].u.add_token.token_code = -1;
    g_mods[1].u.add_token.fallback_to = "ID";
    g_mods[2].type = MOD_ADD_RULE;
    g_mods[2].u.add_rule.lhs = "stmt";
    g_mods[2].u.add_rule.rhs = g_rhs;
    g_mods[2].u.add_rule.nrhs = 2;
    g_mods[2].u.add_rule.code = "{ }";
    g_mods[2].u.add_rule.precedence = -1;
    g_mods[3] = g_mods[2];
    g_mods[3].u.add_rule.rhs = g_rhs2;
    *out = g_mods;
    *nout = 4;
    return true;
}

static ParserSnapshot *compose(ParserSnapshot *base) {
    ExtensionRegistry *reg = create_extension_registry();
    ExtensionInfo info = { 0 };
    info.name = "oracle_connect";
    info.version = "1.0.0";
    info.get_modifications = get_mods;
    ExtensionID id;
    char *err = NULL;
    ParserSnapshot *out = NULL;
    if (register_extension(reg, &info, &id) && load_extension(reg, id, NULL, &err)) {
        free(err);
        err = NULL;
        if (!publish_modified_snapshot(reg, base, &out, NULL, &err)) out = NULL;
    }
    if (out == NULL) printf("  publish failed: %s\n", err ? err : "?");
    free(err);
    destroy_extension_registry(reg);
    if (out == NULL) return NULL;
    /* The compose step (at merge: one call at the SEAM in compose.c). */
    char *le = lime_lex_compose(out, base, g_mods, 4);
    if (le != NULL) {
        printf("  lime_lex_compose failed: %s\n", le);
        free(le);
        snapshot_release(out);
        return NULL;
    }
    return out;
}

static void test_compose(ParserSnapshot *base, ParserSnapshot *comp) {
    const LimeLexicon *bl = lime_snapshot_lexicon(base), *cl = lime_snapshot_lexicon(comp);
    CHECK(cl != NULL && cl != bl, "composed snapshot has no lexicon of its own");
    CHECK(lime_lexicon_keyword_count(cl) == NKW + 2, "composed count %u",
          lime_lexicon_keyword_count(cl));
    int conn = lime_snapshot_token_code(comp, "CONNECT");
    int prior = lime_snapshot_token_code(comp, "PRIOR");
    CHECK(conn > 0 && lime_kw_lookup(cl, "CONNECT", 7) == conn, "CONNECT not a keyword");
    CHECK(prior > 0 && lime_kw_lookup(cl, "Prior", 5) == prior, "soft PRIOR not a keyword");
    CHECK(lime_kw_lookup(bl, "connect", 7) == -1, "base snapshot sees the extension keyword");
    /* invariant 4: every base keyword keeps its code */
    int stable = 0;
    for (int i = 0; i < NKW; i++) {
        int b = lime_kw_lookup(bl, g_kw[i], strlen(g_kw[i]));
        int c = lime_kw_lookup(cl, g_kw[i], strlen(g_kw[i]));
        stable += b == c && b == lime_snapshot_token_code(comp, g_tok[i]);
    }
    CHECK(stable == NKW, "only %d/%d base keyword codes stable", stable, NKW);
    printf("  compose: CONNECT=%d PRIOR(soft)=%d; %d/%d base codes stable\n", conn, prior,
           stable, NKW);

    /* src/tokenize.c's SQL tokenizer resolves keywords through the
    ** composed lexicon, extension keywords included. */
    {
        static const char sql[] = "CONNECT foo prior\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
                                  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
        Tokenizer *tk = tokenizer_create_lexicon(cl, sql, 17);
        Token t;
        int ty[3] = { 0, 0, 0 }, k = 0;
        while (k < 3 && tokenizer_next(tk, &t)) ty[k++] = t.type;
        tokenizer_destroy(tk);
        CHECK(ty[0] == conn && ty[1] == TK_IDENTIFIER && ty[2] == prior,
              "tokenizer: %d %d %d, want %d %d %d", ty[0], ty[1], ty[2], conn, TK_IDENTIFIER,
              prior);
    }

    /* The soft keyword is still a keyword to the lexer; the parser's
    ** %fallback lets it act as ID where CONNECT-style syntax can't. */
    ParseContext *ctx = parse_begin(comp);
    int idc = lime_snapshot_token_code(comp, "ID");
    int r1 = parse_token(ctx, prior, NULL, -1); /* PRIOR ID */
    int r2 = parse_token(ctx, idc, NULL, -1);
    int r3 = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    CHECK(r1 >= 0 && r2 >= 0 && r3 == 1, "PRIOR ID rejected");
    /* CONNECT PRIOR: after CONNECT only ID can shift, so the soft
    ** PRIOR falls back to ID (stmt ::= CONNECT ID). */
    ctx = parse_begin(comp);
    r1 = parse_token(ctx, conn, NULL, -1);
    r2 = parse_token(ctx, prior, NULL, -1);
    r3 = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    CHECK(r1 >= 0 && r2 >= 0 && r3 == 1, "soft PRIOR did not fall back to ID");
    /* PRIOR CONNECT: CONNECT is reserved, so it does NOT fall back. */
    ctx = parse_begin(comp);
    r1 = parse_token(ctx, prior, NULL, -1);
    r2 = parse_token(ctx, conn, NULL, -1);
    r3 = r2 < 0 ? -1 : parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    CHECK(r3 != 1, "reserved CONNECT was accepted as an identifier");
}

static void test_compose_errors(ParserSnapshot *base, ParserSnapshot *comp) {
    /* (a) the extension spells a BASE keyword with its own token */
    GrammarModification m[2];
    memset(m, 0, sizeof m);
    m[0].type = MOD_ADD_TOKEN;
    m[0].u.add_token.name = "CONNECT";
    m[0].u.add_token.lexeme = g_kw[7];
    char *e = lime_lex_compose(comp, base, m, 1);
    CHECK(e != NULL && strstr(e, "claimed by two tokens"), "base/ext clash: %s", e ? e : "(none)");
    free(e);
    /* (b) two extensions, one spelling, two tokens */
    m[0].u.add_token.lexeme = "connect";
    m[1] = m[0];
    m[1].u.add_token.name = "PRIOR";
    e = lime_lex_compose(comp, base, m, 2);
    CHECK(e != NULL && strstr(e, "CONNECT and PRIOR"), "ext/ext clash: %s", e ? e : "(none)");
    free(e);
    /* (c) same spelling, same token twice: fine */
    m[1].u.add_token.name = "CONNECT";
    e = lime_lex_compose(comp, base, m, 2);
    CHECK(e == NULL, "duplicate identical claim rejected: %s", e ? e : "");
    free(e);
    /* (d) an extension lexeme for a token the composed grammar lacks */
    m[0].u.add_token.name = "NOT_IN_GRAMMAR";
    e = lime_lex_compose(comp, base, m, 1);
    CHECK(e != NULL, "unknown token accepted");
    free(e);
    /* a failed compose must leave the composed lexicon in place */
    CHECK(lime_kw_lookup(lime_snapshot_lexicon(comp), "connect", 7) > 0,
          "failed compose dropped the lexicon");

    /* (e) invariant 4 enforced: compose against a grammar that
    ** renumbers base terminals (K0 declared later) must fail. */
    size_t cap = 64 + NKW * 8;
    char *g = malloc(cap);
    size_t n = (size_t)snprintf(g, cap, "%%name KW\n%%token_type { int }\n%%token ID NEWTOK");
    for (int i = 0; i < NKW; i++) n += (size_t)snprintf(g + n, cap - n, " %s", g_tok[i]);
    snprintf(g + n, cap - n, ".\nstmt ::= ID.\nstmt ::= K0 ID.\n");
    ParserSnapshot *moved = NULL;
    char *ce = NULL;
    int rc = lime_compile_grammar_in_process(g, strlen(g), &moved, &ce);
    free(g);
    free(ce);
    CHECK(rc == 0 && moved, "renumbered grammar did not compile");
    if (moved) {
        e = lime_lex_compose(moved, base, NULL, 0);
        CHECK(e != NULL && strstr(e, "changed code"), "moved base code not caught: %s",
              e ? e : "(none)");
        free(e);
        snapshot_release(moved);
    }
}

static void test_order_independent(ParserSnapshot *base, ParserSnapshot *comp) {
    /* Reverse the mod list and recompose onto the same snapshot: every
    ** lookup must agree.  (Build is a function of the item set.) */
    GrammarModification rev[4];
    for (int i = 0; i < 4; i++) rev[i] = g_mods[3 - i];
    int before[NKW + 2];
    char *e = lime_lex_compose(comp, base, g_mods, 4); /* forward order first */
    CHECK(e == NULL, "forward compose: %s", e ? e : "");
    free(e);
    const LimeLexicon *lx = lime_snapshot_lexicon(comp);
    for (int i = 0; i < NKW; i++) before[i] = lime_kw_lookup(lx, g_kw[i], strlen(g_kw[i]));
    before[NKW] = lime_kw_lookup(lx, "connect", 7);
    before[NKW + 1] = lime_kw_lookup(lx, "prior", 5);
    CHECK(before[NKW] > 0 && before[NKW + 1] > 0, "forward compose lost a keyword");
    e = lime_lex_compose(comp, base, rev, 4);
    CHECK(e == NULL, "reverse compose: %s", e ? e : "");
    free(e);
    lx = lime_snapshot_lexicon(comp);
    int same = 0;
    for (int i = 0; i < NKW; i++) same += before[i] == lime_kw_lookup(lx, g_kw[i], strlen(g_kw[i]));
    same += before[NKW] == lime_kw_lookup(lx, "connect", 7);
    same += before[NKW + 1] == lime_kw_lookup(lx, "prior", 5);
    CHECK(same == NKW + 2, "order changed %d lookups", NKW + 2 - same);
}

/* ---- concurrent readers across snapshot swaps ---- */

static _Atomic(ParserSnapshot *) g_current;
static pthread_mutex_t g_swap_mu = PTHREAD_MUTEX_INITIALIZER;
static atomic_int g_stop;

/* The current-snapshot slot.  Acquire under a short mutex (the same
** discipline as parser_manager_get_snapshot's read lock: it closes the
** load/acquire window against a concurrent release); the LOOKUPS run
** with no lock held, on the reader's own reference. */
static ParserSnapshot *current_acquire(void) {
    pthread_mutex_lock(&g_swap_mu);
    ParserSnapshot *s = snapshot_acquire(atomic_load(&g_current));
    pthread_mutex_unlock(&g_swap_mu);
    return s;
}

typedef struct {
    long lookups, wrong, saw_ext, saw_base;
} ReaderStats;

static void *reader(void *arg) {
    ReaderStats *st = arg;
    unsigned x = (unsigned)(size_t)arg;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        ParserSnapshot *s = current_acquire();
        const LimeLexicon *lx = lime_snapshot_lexicon(s);
        int has_ext = lime_kw_lookup(lx, "connect", 7) >= 0;
        if (has_ext) st->saw_ext++;
        else st->saw_base++;
        for (int k = 0; k < 64; k++) {
            x = x * 1103515245u + 12345u;
            int i = (int)((x >> 8) % NKW);
            int got = lime_kw_lookup(lx, g_kw[i], strlen(g_kw[i]));
            /* the code must be the one THIS snapshot assigns */
            if (got != lime_snapshot_token_code(s, g_tok[i])) st->wrong++;
            st->lookups++;
        }
        if ((lime_kw_lookup(lx, "connect", 7) >= 0) != has_ext) st->wrong++; /* torn view */
        snapshot_release(s);
    }
    return NULL;
}

static void test_concurrent(ParserSnapshot *base) {
    enum { NREADERS = 4, NSWAPS = 200 };
    atomic_store(&g_current, snapshot_acquire(base));
    pthread_t th[NREADERS];
    ReaderStats st[NREADERS];
    memset(st, 0, sizeof st);
    for (int i = 0; i < NREADERS; i++) pthread_create(&th[i], NULL, reader, &st[i]);
    int swaps = 0;
    for (int k = 0; k < NSWAPS; k++) {
        /* alternate: freshly composed snapshot, then the base again */
        ParserSnapshot *next = (k % 2 == 0) ? compose(base) : snapshot_acquire(base);
        if (next == NULL) break;
        pthread_mutex_lock(&g_swap_mu);
        ParserSnapshot *old = atomic_exchange(&g_current, next);
        pthread_mutex_unlock(&g_swap_mu);
        snapshot_release(old); /* readers still holding it keep it alive */
        swaps++;
        /* let readers see each snapshot, not only the slow-to-build one */
        struct timespec ts = { 0, 200000 };
        nanosleep(&ts, NULL);
    }
    atomic_store(&g_stop, 1);
    long lookups = 0, wrong = 0, se = 0, sb = 0;
    for (int i = 0; i < NREADERS; i++) {
        pthread_join(th[i], NULL);
        lookups += st[i].lookups;
        wrong += st[i].wrong;
        se += st[i].saw_ext;
        sb += st[i].saw_base;
    }
    snapshot_release(atomic_exchange(&g_current, NULL));
    CHECK(swaps == NSWAPS, "only %d swaps", swaps);
    CHECK(wrong == 0, "%ld wrong lookups", wrong);
    CHECK(se > 0 && sb > 0, "readers never saw both tables (ext %ld, base %ld)", se, sb);
    printf("  concurrent: %d swaps, %d readers, %ld lookups, %ld wrong, saw ext %ld / base %ld\n",
           swaps, NREADERS, lookups, wrong, se, sb);
}

int main(void) {
    for (int i = 0; i < NKW; i++) {
        kw_text(i, g_kw[i]);
        snprintf(g_tok[i], sizeof g_tok[i], "K%d", i);
        g_items[i].text = g_kw[i];
        g_items[i].token = g_tok[i];
    }
    for (int i = 0; i < NKW; i++)
        for (int j = i + 1; j < NKW; j++)
            if (strcmp(g_kw[i], g_kw[j]) == 0) {
                printf("  [BROKEN] generator made duplicate keyword %s\n", g_kw[i]);
                return 1;
            }

    char *g = base_grammar();
    ParserSnapshot *base = NULL, *cs = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(g, strlen(g), &base, &err) != 0 || !base ||
        lime_compile_grammar_in_process(g, strlen(g), &cs, NULL) != 0 || !cs) {
        printf("  [BROKEN] base compile failed: %s\n", err ? err : "?");
        return 1;
    }
    free(g);
    free(err);
    char *e = lime_snapshot_set_lexicon(base, g_items, NKW, NULL, 0, LIME_LEX_FOLD_CASE);
    if (e) {
        printf("  [BROKEN] set_lexicon: %s\n", e);
        return 1;
    }

    test_thousand(base);
    test_case_sensitive(cs);
    ParserSnapshot *comp = compose(base);
    CHECK(comp != NULL, "compose failed");
    if (comp) {
        test_compose(base, comp);
        test_compose_errors(base, comp);
        test_order_independent(base, comp);
        snapshot_release(comp);
    }
    test_concurrent(base);

    snapshot_release(cs);
    snapshot_release(base);
    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all keyword-table checks passed", fails,
           fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
