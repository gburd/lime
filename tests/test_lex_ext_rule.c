/*
** tests/test_lex_ext_rule.c -- B5: a new literal syntax added at runtime.
**
** The scanner (tests/test_lex_ext_rule_grammar.lex) is compiled by
** `lime -X` at build time.  It knows identifiers, numbers, ws, the
** keywords and operators of the B1/B2 lexicon, and '#' as a one-byte
** operator.  At run time an extension composes:
**
**   MOD_ADD_TOKEN  CONNECT "connect"          (keyword)
**   MOD_ADD_TOKEN  PRIOR   "prior", soft      (soft keyword: %fallback ID)
**   MOD_ADD_TOKEN  ARROW2  "->>"              (operator)
**   MOD_ADD_LEX_RULE tmpl  /#\{[^}]*\}/ -> TEMPLATE   (new literal syntax)
**   MOD_ADD_LEX_RULE dur   /[0-9]+(ms|s)/ -> DURATION, with a host action
**
** with no compiler and no dlopen, and the SAME compiled scanner then
** lexes `#{a b}` as one TEMPLATE token, `250ms` as DURATION (the base
** scanner's `250` + `ms` lose to the strictly longer extension match),
** while a lone `#` still lexes as the base operator and `250` alone as
** a NUM; the composed parser accepts statements using all four
** additions; the base snapshot is unaffected.
**
** Also: compose failures (bad regex, unknown token, duplicate name,
** empty-string pattern), order independence, and a negative control
** (by hand, see the commit): the gate test removed must still pass
** (it is an optimisation), while "take only strictly longer" removed
** must fail.
*/
#define _POSIX_C_SOURCE 200809L
#include "extension.h"
#include "lime_lexer.h"
#include "parser.h"
#include "snapshot.h"
#include "test_lex_ext_rule_grammar_lex.h"

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

static const char *GRAMMAR = "%name XG\n%token_type { int }\n"
                             "%token ID NUM OP SELECT FROM WHERE LE.\n"
                             "stmt ::= SELECT expr FROM ID.\n"
                             "stmt ::= SELECT expr FROM ID WHERE expr LE expr.\n"
                             "expr ::= ID.\n"
                             "expr ::= NUM.\n"
                             "expr ::= expr OP expr.\n";

/* lexer output -> (code, text) */
typedef struct {
    int code[64];
    char text[64][24];
    int n;
} Toks;
static void on_tok(void *user, int code, const char *t, size_t n) {
    Toks *k = user;
    if (k->n >= 64) return;
    k->code[k->n] = code;
    snprintf(k->text[k->n], sizeof k->text[k->n], "%.*s", (int)n, t);
    k->n++;
}

/* Rule ids (< 1000) are mapped to the snapshot's base terminals; lexicon
** and extension codes come in as 1000 + code. */
typedef struct {
    const LimeLexicon *lx;
} Cls;
static int classify(const void *c, int kind, const char *t, size_t n, size_t *o) {
    int r = lime_lexicon_classify(((const Cls *)c)->lx, kind, t, n, o);
    return r < 0 ? -1 : 1000 + r;
}
static int xrules(const void *c, int es, const char *t, size_t n, size_t base, size_t *o) {
    int r = lime_lexicon_ext_match(((const Cls *)c)->lx, es, t, n, base, o);
    return r < 0 ? r : 1000 + r;
}
static int xresolve(const void *c, const char *st, const unsigned char **lead) {
    return lime_lexicon_ext_resolve(((const Cls *)c)->lx, st, lead);
}

static int lex(const char *in, const LimeLexicon *lx, Toks *out) {
    memset(out, 0, sizeof *out);
    XrLexer *l = XrLexAlloc(malloc);
    Cls c = { lx };
    if (lx) {
        XrLexSetClassifier(l, classify, &c);
        XrLexSetRuntimeRules(l, xrules, xresolve, &c);
    }
    int rc = XrLexFeedBytes(l, in, strlen(in), on_tok, out);
    XrLexFree(l, free);
    return rc == XR_LEX_OK ? 0 : -1;
}

static int to_code(const ParserSnapshot *s, int c) {
    if (c >= 1000) return c - 1000;
    switch (c) {
    case XR_RULE_IDENT: return lime_snapshot_token_code(s, "ID");
    case XR_RULE_NUMBER: return lime_snapshot_token_code(s, "NUM");
    default: return lime_snapshot_token_code(s, "OP");
    }
}

static int parses(ParserSnapshot *s, const char *sql, const LimeLexicon *lx) {
    Toks t;
    if (lex(sql, lx, &t) != 0) return -1;
    ParseContext *ctx = parse_begin(s);
    int rc = 0;
    for (int i = 0; i < t.n && rc >= 0; i++) rc = parse_token(ctx, to_code(s, t.code[i]), NULL, -1);
    if (rc >= 0) rc = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    return rc == 1;
}

static void dump(const char *what, const Toks *t) {
    printf("    %s:", what);
    for (int i = 0; i < t->n; i++) printf(" %d'%s'", t->code[i], t->text[i]);
    printf("\n");
}

/* host action for DURATION: rejects "0ms" (returns -1 = skip the text) */
static int dur_calls = 0;
static int dur_action(void *user, int code, const char *text, size_t len) {
    (void)user;
    dur_calls++;
    if (len >= 3 && text[0] == '0' && (text[1] == 'm' || text[1] == 's')) return -1;
    return code;
}

#define NMODS 11 /* 5 tokens + 2 lex rules + 4 grammar rules */
static GrammarModification g_mods[NMODS];
static int g_nmods;
static const char *rhs_connect[] = { "CONNECT", "ID", NULL };
static const char *rhs_arrow[] = { "expr", "ARROW2", "expr", NULL };
static const char *rhs_tmpl[] = { "TEMPLATE", NULL };
static const char *rhs_dur[] = { "DURATION", NULL };

static void make_mods(int reverse) {
    GrammarModification m[NMODS];
    memset(m, 0, sizeof m);
    int k = 0;
    m[k].type = MOD_ADD_TOKEN;
    m[k].u.add_token.name = "CONNECT";
    m[k].u.add_token.lexeme = "connect";
    m[k++].u.add_token.token_code = -1;
    m[k].type = MOD_ADD_TOKEN;
    m[k].u.add_token.name = "PRIOR";
    m[k].u.add_token.lexeme = "prior";
    m[k].u.add_token.fallback_to = "ID";
    m[k++].u.add_token.token_code = -1;
    m[k].type = MOD_ADD_TOKEN;
    m[k].u.add_token.name = "ARROW2";
    m[k].u.add_token.lexeme = "->>";
    m[k++].u.add_token.token_code = -1;
    m[k].type = MOD_ADD_TOKEN;
    m[k].u.add_token.name = "TEMPLATE";
    m[k++].u.add_token.token_code = -1;
    m[k].type = MOD_ADD_TOKEN;
    m[k].u.add_token.name = "DURATION";
    m[k++].u.add_token.token_code = -1;
    m[k].type = MOD_ADD_LEX_RULE;
    m[k].u.add_lex_rule.name = "tmpl";
    m[k].u.add_lex_rule.pattern = "#\\{[^}]*\\}";
    m[k++].u.add_lex_rule.token = "TEMPLATE";
    m[k].type = MOD_ADD_LEX_RULE;
    m[k].u.add_lex_rule.name = "dur";
    m[k].u.add_lex_rule.pattern = "[0-9]+(ms|s)";
    m[k].u.add_lex_rule.token = "DURATION";
    m[k++].u.add_lex_rule.action = dur_action;
    const char **rhs[] = { rhs_connect, rhs_arrow, rhs_tmpl, rhs_dur };
    const char *lhs[] = { "stmt", "expr", "expr", "expr" };
    int nrhs[] = { 2, 3, 1, 1 };
    for (int r = 0; r < 4; r++) {
        m[k].type = MOD_ADD_RULE;
        m[k].u.add_rule.lhs = lhs[r];
        m[k].u.add_rule.rhs = rhs[r];
        m[k].u.add_rule.nrhs = nrhs[r];
        m[k].u.add_rule.code = "{ }";
        m[k++].u.add_rule.precedence = -1;
    }
    g_nmods = k;
    for (int i = 0; i < k; i++) g_mods[i] = m[reverse ? k - 1 - i : i];
}

static bool get_mods(void *ud, const struct ParserSnapshot *b, GrammarModification **out,
                     uint32_t *nout) {
    (void)ud;
    (void)b;
    *out = g_mods;
    *nout = (uint32_t)g_nmods;
    return true;
}

static ParserSnapshot *compose(ParserSnapshot *base, char **why) {
    ExtensionRegistry *reg = create_extension_registry();
    ExtensionInfo info = { 0 };
    info.name = "dialect";
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
    destroy_extension_registry(reg);
    if (!out) {
        *why = err ? err : strdup("publish failed");
        return NULL;
    }
    free(err);
    char *e = lime_lex_compose(out, base, g_mods, (uint32_t)g_nmods);
    if (e) {
        *why = e;
        snapshot_release(out);
        return NULL;
    }
    *why = NULL;
    return out;
}

int main(void) {
    char *err = NULL;
    /* Precedence keeps `expr OP expr` and the extension's
    ** `expr ARROW2 expr` conflict-free.  ARROW2 is declared by the base
    ** only to carry that precedence; the extension supplies its lexeme. */
    char g[1024];
    snprintf(g, sizeof g, "%%left OP ARROW2.\n%s%%token ARROW2.\n", GRAMMAR);
    ParserSnapshot *base = NULL;
    if (lime_compile_grammar_in_process(g, strlen(g), &base, &err) != 0 || !base) {
        printf("  [BROKEN] base compile: %s\n", err ? err : "?");
        return 1;
    }
    free(err);
    static const LimeLexItem kws[] = { { "select", "SELECT" }, { "from", "FROM" }, { "where", "WHERE" } };
    static const LimeLexItem ops[] = { { "<=", "LE" } };
    char *e = lime_snapshot_set_lexicon(base, kws, 3, ops, 1, LIME_LEX_FOLD_CASE);
    if (e) {
        printf("  [BROKEN] %s\n", e);
        return 1;
    }
    const LimeLexicon *bl = lime_snapshot_lexicon(base);
    Toks t;

    make_mods(0);
    ParserSnapshot *comp = compose(base, &err);
    CHECK(comp != NULL, "compose: %s", err ? err : "");
    free(err);
    if (!comp) return 1;
    const LimeLexicon *cl = lime_snapshot_lexicon(comp);
    int TEMPLATE = 1000 + lime_snapshot_token_code(comp, "TEMPLATE");
    int DURATION = 1000 + lime_snapshot_token_code(comp, "DURATION");
    int CONNECT = 1000 + lime_snapshot_token_code(comp, "CONNECT");
    int ARROW2 = 1000 + lime_snapshot_token_code(comp, "ARROW2");
    CHECK(lime_lexicon_ext_rule_count(cl) == 2, "ext rule count %u", lime_lexicon_ext_rule_count(cl));

    /* new literal syntax + duration + keyword + operator, one scanner */
    CHECK(lex("connect #{a b} 250ms j->>k # 250 0ms", cl, &t) == 0, "lex failed");
    {
        int want[] = { CONNECT, TEMPLATE, DURATION, XR_RULE_IDENT, ARROW2, XR_RULE_IDENT,
                       XR_RULE_OP, XR_RULE_NUMBER };
        const char *wt[] = { "connect", "#{a b}", "250ms", "j", "->>", "k", "#", "250" };
        int ok = t.n == 8;
        for (int i = 0; ok && i < 8; i++) ok = t.code[i] == want[i] && strcmp(t.text[i], wt[i]) == 0;
        CHECK(ok, "composed token stream");
        if (!ok) dump("got", &t);
        CHECK(dur_calls == 2, "DURATION action called %d times (250ms, 0ms)", dur_calls);
    }
    /* the base snapshot is untouched: '#' '{' ... and 250 'ms' */
    lex("#{a} 250ms", bl, &t);
    {
        int ok = t.n >= 4 && t.code[0] == XR_RULE_OP && strcmp(t.text[0], "#") == 0;
        for (int i = 0; i < t.n; i++) ok = ok && t.code[i] != TEMPLATE && t.code[i] != DURATION;
        CHECK(ok, "base lexicon saw the extension syntax");
        if (!ok) dump("base", &t);
    }
    /* strictly longer only: an extension rule matching the same length
    ** as the base token loses (the base rule was declared first).
    ** lime_lexicon_ext_match directly: "#{}" is 3 bytes; with base_len 3
    ** it must decline, with base_len 1 it must win. */
    {
        size_t n = 0;
        const unsigned char *lead = NULL;
        int es = lime_lexicon_ext_resolve(cl, "INITIAL", &lead);
        CHECK(es == 0 && lead && lead['#'] && lead['2'] && !lead['a'] && !lead[' '],
              "INITIAL leading bytes");
        CHECK(lime_lexicon_ext_match(cl, es, "#{}", 3, 3, &n) == -1,
              "equal-length extension match won");
        CHECK(lime_lexicon_ext_match(cl, es, "#{}", 3, 1, &n) == TEMPLATE - 1000 && n == 3,
              "longer extension match lost");
        CHECK(lime_lexicon_ext_resolve(cl, "XYZ", &lead) == -1 && lead == NULL,
              "unknown state resolved");
        CHECK(lime_lexicon_ext_match(cl, 7, "#{}", 3, 0, &n) == -1, "bad ext state matched");
        CHECK(lime_lexicon_ext_resolve(bl, "INITIAL", &lead) == -1, "base lexicon has ext rules");
        CHECK(lime_lexicon_ext_match(bl, 0, "#{}", 3, 0, &n) == -1, "base lexicon matched");
    }
    /* strictly longer, end to end: an extension rule "<=[0-9]*"
    ** (TEMPLATE) ties the base operator rule on "<=" (both 2 bytes: the
    ** base DFA's op run) and is longer on "<=5" (the op run is "<=").
    ** The generated scanner must keep LE for "<=" and take the extension
    ** for "<=5".  (The comparison is against the base DFA match, before
    ** any classifier trims it, as for a rule appended to a flex file.) */
    {
        GrammarModification m[NMODS + 1];
        memcpy(m, g_mods, NMODS * sizeof m[0]);
        memset(&m[NMODS], 0, sizeof m[NMODS]);
        m[NMODS].type = MOD_ADD_LEX_RULE;
        m[NMODS].u.add_lex_rule.name = "lexx";
        m[NMODS].u.add_lex_rule.pattern = "<=[0-9]*";
        m[NMODS].u.add_lex_rule.token = "TEMPLATE";
        char *ce = lime_lex_compose(comp, base, m, NMODS + 1);
        CHECK(ce == NULL, "compose with <=[0-9]*: %s", ce ? ce : "");
        free(ce);
        int LE = 1000 + lime_snapshot_token_code(comp, "LE");
        lex("a <= b <=5 c", lime_snapshot_lexicon(comp), &t);
        int ok = t.n == 5 && t.code[1] == LE && strcmp(t.text[1], "<=") == 0 &&
                 t.code[3] == TEMPLATE && strcmp(t.text[3], "<=5") == 0;
        CHECK(ok, "tie keeps the base token, longer takes the extension");
        if (!ok) dump("tie", &t);
        ce = lime_lex_compose(comp, base, g_mods, NMODS); /* back to the plain set */
        CHECK(ce == NULL, "recompose: %s", ce ? ce : "");
        free(ce);
        cl = lime_snapshot_lexicon(comp); /* a compose replaces the lexicon */
    }
    /* longest only: "#" then "{" without "}" is not a template */
    lex("#{open", cl, &t);
    CHECK(t.n >= 1 && t.code[0] == XR_RULE_OP, "unterminated template taken");
    /* parser */
    CHECK(parses(comp, "select #{x} from t", cl) == 1, "select #{x} from t");
    CHECK(parses(comp, "select a ->> b from t where d <= 250ms", cl) == 1, "->> and 250ms");
    CHECK(parses(comp, "connect prior", cl) == 1, "soft PRIOR as ID after CONNECT");
    CHECK(parses(comp, "select prior from t", cl) == 1, "soft PRIOR as a column");
    CHECK(parses(comp, "select a from connect", cl) == 0, "reserved CONNECT as a table");
    CHECK(parses(comp, "select a + b from t", cl) == 1, "base statement");

    /* order independence: reversed mods give identical lexing */
    {
        Toks a, b;
        lex("connect #{a b} 250ms j->>k # 250", cl, &a);
        make_mods(1);
        ParserSnapshot *c2 = compose(base, &err);
        CHECK(c2 != NULL, "reverse compose: %s", err ? err : "");
        free(err);
        if (c2) {
            lex("connect #{a b} 250ms j->>k # 250", lime_snapshot_lexicon(c2), &b);
            /* compare by token NAME: codes of extension tokens may be
            ** assigned differently by the two composes */
            int same = a.n == b.n;
            for (int i = 0; same && i < a.n; i++) {
                same = strcmp(a.text[i], b.text[i]) == 0 &&
                       (a.code[i] >= 1000) == (b.code[i] >= 1000);
                if (same && a.code[i] < 1000) same = a.code[i] == b.code[i];
            }
            CHECK(same, "reversed mod order lexes differently");
            snapshot_release(c2);
        }
        make_mods(0);
    }

    /* compose failures */
    struct {
        const char *name, *pattern, *token, *expect;
    } bad[] = {
        { "x1", "[a-", "TEMPLATE", "do not compile" },
        { "x2", "q+", "NO_SUCH", "not a terminal" },
        { "tmpl", "@+", "TEMPLATE", "declared twice" },
        { "x4", "z*", "TEMPLATE", "empty string" },
        { "x5", "a/b", "TEMPLATE", "escape '/'" },
        { "9x", "a", "TEMPLATE", "not an identifier" },
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GrammarModification m[NMODS + 1];
        memcpy(m, g_mods, NMODS * sizeof m[0]);
        memset(&m[NMODS], 0, sizeof m[NMODS]);
        m[NMODS].type = MOD_ADD_LEX_RULE;
        m[NMODS].u.add_lex_rule.name = bad[i].name;
        m[NMODS].u.add_lex_rule.pattern = bad[i].pattern;
        m[NMODS].u.add_lex_rule.token = bad[i].token;
        char *ce = lime_lex_compose(comp, base, m, NMODS + 1);
        CHECK(ce != NULL && strstr(ce, bad[i].expect), "bad rule %s: %s", bad[i].name, ce ? ce : "(accepted)");
        free(ce);
    }
    lex("#{still}", lime_snapshot_lexicon(comp), &t);
    CHECK(t.n == 1 && t.code[0] == TEMPLATE, "a failed compose damaged the lexicon");

    snapshot_release(comp);
    snapshot_release(base);
    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all extension-lex-rule checks passed", fails,
           fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
