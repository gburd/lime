/*
** tests/test_op_trie.c -- B2: the snapshot operator trie.
**
** 1. PostgreSQL: the 9 multi-char operator tokens scan.l returns as
**    their own terminals (::, .., :=, =>, <=, >=, <>, !=, and the
**    single-char "self" tokens) plus `->`/`->>` added by an extension
**    at compose time.  A scanner whose DFA matches the scan.l
**    {operator} run ({op_chars}+, after scan.l's comment/trailing-+-
**    trimming) splits it with lime_op_match; every operator run in
**    every REL_18 regression .sql file must split into the same token
**    sequence as a reference implementation of scan.l's rule, which
**    knows the extension operators too.
** 2. Property (hegel): for random operator sets and random inputs,
**    lime_op_match == brute-force longest prefix over the set.
** 3. Compose: an operator lexeme claimed by two tokens fails.
**
** Negative controls (by hand, see the commit): make lime_op_match
** return the FIRST match instead of the longest; make it stop the walk
** at a node with no code.  Both must fail this test.
*/
#define _POSIX_C_SOURCE 200809L
#include "extension.h"
#include "hegel_pbt.h"
#include "lime_lexer.h"
#include "parser.h"
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ------------------------------------------------------------------ */
/*  PostgreSQL operator lexing                                         */
/* ------------------------------------------------------------------ */

/* The grammar: PG's operator terminals, named as in gram.y. */
static const char *PG_GRAMMAR =
    "%name PGOP\n%token_type { int }\n"
    "%token Op TYPECAST DOT_DOT COLON_EQUALS EQUALS_GREATER LESS_EQUALS\n"
    "       GREATER_EQUALS NOT_EQUALS COMMA LPAREN RPAREN LBRACKET RBRACKET DOT\n"
    "       SEMI COLON PLUS MINUS STAR SLASH PERCENT CARET LT GT EQ.\n"
    "stmt ::= Op.\n";

/* Base operators: what scan.l returns as its own token (not Op). */
static const LimeLexItem PG_OPS[] = {
    { "::", "TYPECAST" },       { "..", "DOT_DOT" },     { ":=", "COLON_EQUALS" },
    { "=>", "EQUALS_GREATER" }, { "<=", "LESS_EQUALS" }, { ">=", "GREATER_EQUALS" },
    { "<>", "NOT_EQUALS" },     { "!=", "NOT_EQUALS" },  { ",", "COMMA" },
    { "(", "LPAREN" },          { ")", "RPAREN" },       { "[", "LBRACKET" },
    { "]", "RBRACKET" },        { ".", "DOT" },          { ";", "SEMI" },
    { ":", "COLON" },           { "+", "PLUS" },         { "-", "MINUS" },
    { "*", "STAR" },            { "/", "SLASH" },        { "%", "PERCENT" },
    { "^", "CARET" },           { "<", "LT" },           { ">", "GT" },
    { "=", "EQ" },
};
#define N_PG_OPS (sizeof PG_OPS / sizeof PG_OPS[0])

/* Extension: JSON arrows (MOD_ADD_TOKEN.lexeme, composed at runtime). */
static GrammarModification g_mods[4];
static const char *g_rhs1[] = { "ARROW", NULL };
static const char *g_rhs2[] = { "ARROW2", NULL };

static bool get_mods(void *ud, const struct ParserSnapshot *b, GrammarModification **out,
                     uint32_t *nout) {
    (void)ud;
    (void)b;
    memset(g_mods, 0, sizeof g_mods);
    g_mods[0].type = MOD_ADD_TOKEN;
    g_mods[0].u.add_token.name = "ARROW";
    g_mods[0].u.add_token.lexeme = "->";
    g_mods[0].u.add_token.token_code = -1;
    g_mods[1].type = MOD_ADD_TOKEN;
    g_mods[1].u.add_token.name = "ARROW2";
    g_mods[1].u.add_token.lexeme = "->>";
    g_mods[1].u.add_token.token_code = -1;
    g_mods[2].type = MOD_ADD_RULE;
    g_mods[2].u.add_rule.lhs = "stmt";
    g_mods[2].u.add_rule.rhs = g_rhs1;
    g_mods[2].u.add_rule.nrhs = 1;
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
    info.name = "json_arrows";
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
    return out; /* publish_modified_snapshot composed the lexicon too */
}

static int is_op_char(unsigned char c) {
    return c && strchr("~!@#^&|`?+-*/%<>=", c) != NULL;
}
static int is_self(unsigned char c) {
    return c && strchr(",()[].;:+-*/%^<>=", c) != NULL;
}

/* scan.l's {operator} action: the length it keeps of an op_chars run
** (stop at an embedded comment start; drop trailing +/- unless the
** operator has a non-SQL char).  Shared by the reference and the
** scanner under test: it is the regex+action part, not the split. */
static size_t pg_op_keep(const char *t, size_t len) {
    size_t n = len;
    for (size_t i = 0; i + 1 < len; i++)
        if ((t[i] == '/' && t[i + 1] == '*') || (t[i] == '-' && t[i + 1] == '-')) {
            n = i;
            break;
        }
    if (n > 1 && (t[n - 1] == '+' || t[n - 1] == '-')) {
        int ic;
        for (ic = (int)n - 2; ic >= 0; ic--)
            if (strchr("~!@#^&|`?%", t[ic])) break;
        if (ic < 0)
            do n--;
            while (n > 1 && (t[n - 1] == '+' || t[n - 1] == '-'));
    }
    return n;
}

typedef struct {
    int code;
    size_t len;
} Tok;

/* Reference: scan.l's operator lexing, as written in scan.l (rule
** order + longest match over its fixed patterns, then the {operator}
** trimming and the 1-char/2-char re-classification), extended with
** the extension's arrows the way a hand-edited scan.l would add them.
** Emits tokens for one op-ish span starting at s; returns consumed. */
typedef struct {
    int Op, TYPECAST, DOT_DOT, COLON_EQUALS, EQUALS_GREATER, LESS_EQUALS, GREATER_EQUALS,
        NOT_EQUALS, ARROW, ARROW2;
    int self[256];
} Codes;

static size_t ref_one(const Codes *c, const char *s, size_t n, Tok *out) {
    /* flex longest match among: typecast/dot_dot/colon_equals (2 chars),
    ** equals_greater etc. (2 chars, all op_chars), self (1 char),
    ** operator ({op_chars}+).  The longest wins; on a tie the earlier
    ** rule (the fixed 2-char ones, then self) wins. */
    size_t run = 0;
    while (run < n && is_op_char((unsigned char)s[run])) run++;
    if (n >= 2 && s[0] == ':' && s[1] == ':') return *out = (Tok){ c->TYPECAST, 2 }, 2;
    if (n >= 2 && s[0] == '.' && s[1] == '.') return *out = (Tok){ c->DOT_DOT, 2 }, 2;
    if (n >= 2 && s[0] == ':' && s[1] == '=') return *out = (Tok){ c->COLON_EQUALS, 2 }, 2;
    if (run <= 2) {
        if (run == 2) {
            /* fixed two-char tokens tie with {operator}; they come first */
            if (s[0] == '=' && s[1] == '>') return *out = (Tok){ c->EQUALS_GREATER, 2 }, 2;
            if (s[0] == '<' && s[1] == '=') return *out = (Tok){ c->LESS_EQUALS, 2 }, 2;
            if (s[0] == '>' && s[1] == '=') return *out = (Tok){ c->GREATER_EQUALS, 2 }, 2;
            if (s[0] == '<' && s[1] == '>') return *out = (Tok){ c->NOT_EQUALS, 2 }, 2;
            if (s[0] == '!' && s[1] == '=') return *out = (Tok){ c->NOT_EQUALS, 2 }, 2;
        }
        if (run == 0 || (run == 1 && is_self((unsigned char)s[0])))
            return *out = (Tok){ c->self[(unsigned char)s[0]], 1 }, 1;
    }
    size_t k = pg_op_keep(s, run);
    if (k == 1 && is_self((unsigned char)s[0])) return *out = (Tok){ c->self[(unsigned char)s[0]], 1 }, 1;
    if (k == 2) {
        if (s[0] == '=' && s[1] == '>') return *out = (Tok){ c->EQUALS_GREATER, 2 }, 2;
        if (s[0] == '>' && s[1] == '=') return *out = (Tok){ c->GREATER_EQUALS, 2 }, 2;
        if (s[0] == '<' && s[1] == '=') return *out = (Tok){ c->LESS_EQUALS, 2 }, 2;
        if (s[0] == '<' && s[1] == '>') return *out = (Tok){ c->NOT_EQUALS, 2 }, 2;
        if (s[0] == '!' && s[1] == '=') return *out = (Tok){ c->NOT_EQUALS, 2 }, 2;
    }
    /* extension arrows, hand-added the way PG's own scan.l would need */
    if (k == 3 && memcmp(s, "->>", 3) == 0) return *out = (Tok){ c->ARROW2, 3 }, 3;
    if (k == 2 && memcmp(s, "->", 2) == 0) return *out = (Tok){ c->ARROW, 2 }, 2;
    return *out = (Tok){ c->Op, k }, k;
}

/* Under test: scan.l's {operator} regex + trimming action (pg_op_keep),
** then the snapshot trie splits the kept run.  A kept run that is
** exactly one known operator is that token; otherwise it is Op (PG
** operator names are atomic: "@>" is one Op, never "@" ">"), so the
** trie is asked for a whole-run match.  Non-run bytes (':' '.' and
** the brackets) go straight to the trie, longest match. */
static size_t trie_one(const LimeLexicon *lx, const Codes *c, const char *s, size_t n, Tok *out) {
    size_t run = 0;
    while (run < n && is_op_char((unsigned char)s[run])) run++;
    int code = -1;
    size_t m = lime_op_match(lx, s, n, &code);
    if (run == 0 || m > run) return *out = (Tok){ code, m }, m; /* ::, .., :=, (, ... */
    size_t k = pg_op_keep(s, run);
    m = lime_op_match(lx, s, k, &code);
    if (m == k) return *out = (Tok){ code, k }, k;
    return *out = (Tok){ c->Op, k }, k;
}

static int is_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* Lex the operator-ish parts of a buffer (identifiers, numbers,
** strings and comments are skipped by both sides identically) and
** compare the two token streams.  Returns the number of tokens. */
static long compare_buffer(const LimeLexicon *lx, const Codes *c, const char *b, size_t len,
                           long *bad) {
    long ntok = 0;
    size_t i = 0;
    while (i < len) {
        unsigned char ch = (unsigned char)b[i];
        if (is_ws(ch) || (ch >= '0' && ch <= '9') || ch == '_' || ch >= 0x80 ||
            (unsigned)(ch | 0x20) - 'a' < 26u || ch == '$' || ch == '"' || ch == '\'' ||
            ch == '\\' || ch == '{' || ch == '}') {
            i++;
            continue;
        }
        if (ch == '-' && i + 1 < len && b[i + 1] == '-') { /* line comment */
            while (i < len && b[i] != '\n') i++;
            continue;
        }
        if (ch == '/' && i + 1 < len && b[i + 1] == '*') { /* (unnested) block comment */
            i += 2;
            while (i + 1 < len && !(b[i] == '*' && b[i + 1] == '/')) i++;
            i += 2;
            continue;
        }
        Tok r, t;
        size_t rn = ref_one(c, b + i, len - i, &r);
        size_t tn = trie_one(lx, c, b + i, len - i, &t);
        if (rn != tn || r.code != t.code) {
            if ((*bad)++ < 8)
                printf("  MISMATCH at '%.*s': scan.l %d/%zu, trie %d/%zu\n",
                       (int)(len - i < 12 ? len - i : 12), b + i, r.code, rn, t.code, tn);
        }
        ntok++;
        i += rn ? rn : 1;
    }
    return ntok;
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

static void codes_for(const ParserSnapshot *s, Codes *c) {
    memset(c, 0xff, sizeof *c);
#define TC(x) c->x = lime_snapshot_token_code(s, #x)
    TC(Op); TC(TYPECAST); TC(DOT_DOT); TC(COLON_EQUALS); TC(EQUALS_GREATER); TC(LESS_EQUALS);
    TC(GREATER_EQUALS); TC(NOT_EQUALS); TC(ARROW); TC(ARROW2);
#undef TC
    for (size_t i = 0; i < N_PG_OPS; i++)
        if (PG_OPS[i].text[1] == '\0')
            c->self[(unsigned char)PG_OPS[i].text[0]] = lime_snapshot_token_code(s, PG_OPS[i].token);
}

static void test_pg(ParserSnapshot *base, ParserSnapshot *comp, int argc, char **argv) {
    const LimeLexicon *bl = lime_snapshot_lexicon(base), *cl = lime_snapshot_lexicon(comp);
    Codes cc;
    codes_for(comp, &cc);
    CHECK(lime_lexicon_operator_count(bl) == N_PG_OPS, "base ops %u", lime_lexicon_operator_count(bl));
    CHECK(lime_lexicon_operator_count(cl) == N_PG_OPS + 2, "composed ops %u",
          lime_lexicon_operator_count(cl));

    /* hand cases, composed snapshot */
    struct {
        const char *in;
        size_t len;
        int code;
    } hc[] = {
        { "->>'a'", 3, cc.ARROW2 },  { "->'a'", 2, cc.ARROW },        { "->", 2, cc.ARROW },
        { "-", 1, cc.self['-'] },    { "::int", 2, cc.TYPECAST },     { ":=", 2, cc.COLON_EQUALS },
        { ":x", 1, cc.self[':'] },   { "<=>", 2, cc.LESS_EQUALS },    { "!=1", 2, cc.NOT_EQUALS },
        { "!", 0, -1 },              { "@>", 0, -1 },                 { "...", 2, cc.DOT_DOT },
    };
    for (size_t i = 0; i < sizeof hc / sizeof hc[0]; i++) {
        int code = -7;
        size_t m = lime_op_match(cl, hc[i].in, strlen(hc[i].in), &code);
        CHECK(m == hc[i].len && (m == 0 ? code == -7 : code == hc[i].code),
              "lime_op_match('%s') = %zu/%d, want %zu/%d", hc[i].in, m, code, hc[i].len,
              hc[i].code);
    }
    int code = -7;
    CHECK(lime_op_match(bl, "->>", 3, &code) == 1 && code == lime_snapshot_token_code(base, "MINUS"),
          "base snapshot sees the extension arrow");
    CHECK(lime_op_match(NULL, "<=", 2, &code) == 0, "NULL lexicon matched");
    /* base codes stable across the compose */
    int stable = 0;
    for (size_t i = 0; i < N_PG_OPS; i++) {
        int a = -1, b = -1;
        lime_op_match(bl, PG_OPS[i].text, strlen(PG_OPS[i].text), &a);
        lime_op_match(cl, PG_OPS[i].text, strlen(PG_OPS[i].text), &b);
        stable += a == b && a == lime_snapshot_token_code(comp, PG_OPS[i].token);
    }
    CHECK(stable == (int)N_PG_OPS, "%d/%zu base operator codes stable", stable, N_PG_OPS);

    /* scan.l equivalence over SQL files */
    long ntok = 0, bad = 0, files = 0;
    size_t bytes = 0;
    for (int a = 1; a < argc; a++) {
        size_t len;
        char *b = slurp(argv[a], &len);
        if (!b) {
            CHECK(0, "cannot read %s", argv[a]);
            continue;
        }
        ntok += compare_buffer(cl, &cc, b, len, &bad);
        bytes += len;
        files++;
        free(b);
    }
    /* synthetic: every op-char string of length 1..4 over the PG set */
    static const char alpha[] = "~!@#^&|`?+-*/%<>=:.,()[];";
    char buf[8];
    size_t na = sizeof alpha - 1;
    long synth = 0;
    for (size_t L = 1; L <= 4; L++) {
        size_t total = 1;
        for (size_t k = 0; k < L; k++) total *= na;
        for (size_t x = 0; x < total; x++) {
            size_t y = x;
            for (size_t k = 0; k < L; k++) {
                buf[k] = alpha[y % na];
                y /= na;
            }
            buf[L] = ' ';
            synth += compare_buffer(cl, &cc, buf, L + 1, &bad);
        }
    }
    CHECK(bad == 0, "%ld operator tokens split differently from scan.l", bad);
    printf("  scan.l operator rule: %ld tokens from %ld files (%zu bytes) + %ld synthetic, "
           "%ld mismatches\n",
           ntok, files, bytes, synth, bad);
}

static void test_compose_clash(ParserSnapshot *base, ParserSnapshot *comp) {
    GrammarModification m[1];
    memset(m, 0, sizeof m);
    m[0].type = MOD_ADD_TOKEN;
    m[0].u.add_token.name = "ARROW";
    m[0].u.add_token.lexeme = "<>"; /* base: NOT_EQUALS */
    char *e = lime_lex_compose(comp, base, m, 1);
    CHECK(e != NULL && strstr(e, "operator '<>'"), "operator clash not reported: %s",
          e ? e : "(none)");
    free(e);
}

/* ------------------------------------------------------------------ */
/*  Property: lime_op_match == brute-force longest prefix              */
/* ------------------------------------------------------------------ */

typedef struct {
    ParserSnapshot *snap; /* grammar with tokens T0..T63 */
} PropCtx;

static void prop_longest_prefix(hpbt_case *c, void *ud) {
    PropCtx *pc = ud;
    /* small alphabet makes shared prefixes (the interesting case) common */
    static const char alpha[] = "<>=-!|&";
    char ops[24][8];
    char names[24][8];
    LimeLexItem items[24];
    int nops = (int)hpbt_int(c, 1, 24);
    int n = 0;
    for (int i = 0; i < nops && !hpbt_done(c); i++) {
        int len = (int)hpbt_int(c, 1, 5);
        for (int k = 0; k < len; k++) ops[n][k] = alpha[hpbt_int(c, 0, 6)];
        ops[n][len] = '\0';
        int dup = 0;
        for (int j = 0; j < n; j++) dup |= strcmp(ops[j], ops[n]) == 0;
        if (dup) continue;
        snprintf(names[n], sizeof names[n], "T%d", (int)hpbt_int(c, 0, 63));
        items[n] = (LimeLexItem){ ops[n], names[n] };
        n++;
    }
    char input[16];
    int ilen = (int)hpbt_int(c, 0, 12);
    for (int k = 0; k < ilen; k++) input[k] = alpha[hpbt_int(c, 0, 6)];
    input[ilen] = '\0';
    if (hpbt_done(c)) return;

    char *e = lime_snapshot_set_lexicon(pc->snap, NULL, 0, items, (size_t)n, 0);
    if (e) {
        HPBT_FAIL(c, "set_lexicon: %s", e);
        free(e);
        return;
    }
    const LimeLexicon *lx = lime_snapshot_lexicon(pc->snap);
    /* oracle */
    size_t best = 0;
    int best_code = -1;
    for (int i = 0; i < n; i++) {
        size_t L = strlen(ops[i]);
        if (L <= (size_t)ilen && L > best && memcmp(ops[i], input, L) == 0) {
            best = L;
            best_code = lime_snapshot_token_code(pc->snap, names[i]);
        }
    }
    int code = -7;
    size_t got = lime_op_match(lx, input, (size_t)ilen, &code);
    if (got != best || (best ? code != best_code : code != -7)) {
        char ops_s[256] = "";
        for (int i = 0; i < n; i++) {
            strncat(ops_s, ops[i], sizeof ops_s - strlen(ops_s) - 2);
            strncat(ops_s, " ", sizeof ops_s - strlen(ops_s) - 1);
        }
        HPBT_FAIL(c, "ops {%s} input '%s': got %zu/%d, want %zu/%d", ops_s, input, got, code,
                  best, best_code);
    }
}

int main(int argc, char **argv) {
    ParserSnapshot *base = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(PG_GRAMMAR, strlen(PG_GRAMMAR), &base, &err) != 0 || !base) {
        printf("  [BROKEN] compile: %s\n", err ? err : "?");
        return 1;
    }
    free(err);
    char *e = lime_snapshot_set_lexicon(base, NULL, 0, PG_OPS, N_PG_OPS, 0);
    if (e) {
        printf("  [BROKEN] set_lexicon: %s\n", e);
        return 1;
    }
    ParserSnapshot *comp = compose(base);
    CHECK(comp != NULL, "compose failed");
    if (comp) {
        test_pg(base, comp, argc, argv);
        test_compose_clash(base, comp);
        snapshot_release(comp);
    }
    snapshot_release(base);

    /* property */
    char g[1024];
    size_t gn = (size_t)snprintf(g, sizeof g, "%%name PROP\n%%token_type { int }\n%%token");
    for (int i = 0; i < 64; i++) gn += (size_t)snprintf(g + gn, sizeof g - gn, " T%d", i);
    snprintf(g + gn, sizeof g - gn, ".\nstmt ::= T0.\n");
    PropCtx pc = { NULL };
    if (lime_compile_grammar_in_process(g, strlen(g), &pc.snap, NULL) != 0 || !pc.snap) {
        printf("  [BROKEN] prop grammar\n");
        return 1;
    }
    int prc = hpbt_run("prop_op_trie_longest_prefix", prop_longest_prefix, &pc, 2000);
    if (prc) fails++;
    snapshot_release(pc.snap);

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all operator-trie checks passed", fails,
           fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
