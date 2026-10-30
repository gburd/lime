/*
** tests/test_lex_classify.c -- B3: %keyword_class / %operator_class.
**
** tests/test_lex_classify_grammar.lex is compiled by `lime -X` at BUILD
** time.  It contains no keywords and no multi-character operators.  At
** run time:
**   1. with no classifier installed it lexes IDENT / OP as written
**      (the hook is inert);
**   2. with lime_lexicon_classify over the BASE snapshot's lexicon,
**      "select" is SELECT, "<=" is LE, "a<=-b" splits as IDENT LE OP(-)
**      IDENT (longest-prefix split, rest pushed back);
**   3. an extension then composes CONNECT ("connect") and ARROW2
**      ("->>") at runtime (publish_modified_snapshot + lime_lex_compose,
**      no compiler, no dlopen); the SAME compiled scanner, pointed at
**      the composed snapshot, returns CONNECT and ARROW2, while the
**      base snapshot still lexes them as IDENT and OP pieces;
**   4. the token stream feeds the composed parser and is accepted.
**   5. LIME_LEX_OP_WHOLE keeps "<=>" one OP; a rule outside the
**      classes (number) is never offered to the classifier.
**
** Negative control (by hand, see the commit): make the emitted hook
** ignore the classifier's claim; this test must fail.
*/
#define _POSIX_C_SOURCE 200809L
#include "extension.h"
#include "lime_lexer.h"
#include "parser.h"
#include "snapshot.h"
#include "test_lex_classify_grammar_lex.h"

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

static const char *GRAMMAR =
    "%name CG\n%token_type { int }\n"
    "%token SELECT FROM WHERE IDENT NUM OP LE COMMA.\n"
    "stmt ::= SELECT cols FROM IDENT.\n"
    "stmt ::= SELECT cols FROM IDENT WHERE IDENT LE NUM.\n"
    "cols ::= IDENT.\n"
    "cols ::= cols COMMA IDENT.\n";

/* Collected tokens: (code, text). */
typedef struct {
    int code[64];
    char text[64][16];
    int n;
} Toks;

static void on_tok(void *user, int code, const char *t, size_t n) {
    Toks *k = user;
    if (k->n >= 64) return;
    k->code[k->n] = code;
    snprintf(k->text[k->n], sizeof k->text[k->n], "%.*s", (int)n, t);
    k->n++;
}

/* Rule ids are < 1000; lexicon codes are mapped to 1000 + code so the
** two cannot be confused in the expectations. */
typedef struct {
    const LimeLexicon *lx;
} Cls;
static int classify(const void *cls, int kind, const char *text, size_t len, size_t *out) {
    int c = lime_lexicon_classify(((const Cls *)cls)->lx, kind, text, len, out);
    return c < 0 ? -1 : 1000 + c;
}

static int lex(const char *in, const LimeLexicon *lx, Toks *out) {
    memset(out, 0, sizeof *out);
    ClsLexer *l = ClsLexAlloc(malloc);
    Cls c = { lx };
    if (lx) ClsLexSetClassifier(l, classify, &c);
    int rc = ClsLexFeedBytes(l, in, strlen(in), on_tok, out);
    ClsLexFree(l, free);
    return rc == CLS_LEX_OK ? 0 : -1;
}

static void expect(const char *what, const Toks *t, const int *codes, const char *const *texts,
                   int n) {
    int ok = t->n == n;
    for (int i = 0; ok && i < n; i++) ok = t->code[i] == codes[i] && strcmp(t->text[i], texts[i]) == 0;
    CHECK(ok, "%s: token stream differs", what);
    if (!ok) {
        printf("    got:");
        for (int i = 0; i < t->n; i++) printf(" %d'%s'", t->code[i], t->text[i]);
        printf("\n   want:");
        for (int i = 0; i < n; i++) printf(" %d'%s'", codes[i], texts[i]);
        printf("\n");
    }
}

static GrammarModification g_mods[4];
static const char *g_rhs1[] = { "CONNECT", "IDENT", NULL };
static const char *g_rhs2[] = { "IDENT", "ARROW2", "IDENT", NULL };
static bool get_mods(void *ud, const struct ParserSnapshot *b, GrammarModification **out,
                     uint32_t *nout) {
    (void)ud;
    (void)b;
    memset(g_mods, 0, sizeof g_mods);
    g_mods[0].type = MOD_ADD_TOKEN;
    g_mods[0].u.add_token.name = "CONNECT";
    g_mods[0].u.add_token.lexeme = "connect";
    g_mods[0].u.add_token.token_code = -1;
    g_mods[1].type = MOD_ADD_TOKEN;
    g_mods[1].u.add_token.name = "ARROW2";
    g_mods[1].u.add_token.lexeme = "->>";
    g_mods[1].u.add_token.token_code = -1;
    g_mods[2].type = MOD_ADD_RULE;
    g_mods[2].u.add_rule.lhs = "stmt";
    g_mods[2].u.add_rule.rhs = g_rhs1;
    g_mods[2].u.add_rule.nrhs = 2;
    g_mods[2].u.add_rule.code = "{ }";
    g_mods[2].u.add_rule.precedence = -1;
    g_mods[3] = g_mods[2];
    g_mods[3].u.add_rule.rhs = g_rhs2;
    g_mods[3].u.add_rule.nrhs = 3;
    *out = g_mods;
    *nout = 4;
    return true;
}

static ParserSnapshot *compose(ParserSnapshot *base) {
    ExtensionRegistry *reg = create_extension_registry();
    ExtensionInfo info = { 0 };
    info.name = "connect_by";
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
    free(err);
    destroy_extension_registry(reg);
    if (!out) return NULL;
    char *e = lime_lex_compose(out, base, g_mods, 4);
    if (e) {
        printf("  lime_lex_compose: %s\n", e);
        free(e);
        snapshot_release(out);
        return NULL;
    }
    return out;
}

/* Map lexer output to parser codes: lexicon claims are 1000+code;
** unclaimed IDENT/NUM/OP/punct rules map to the snapshot's terminal. */
static int parses(ParserSnapshot *s, const Toks *t) {
    ParseContext *ctx = parse_begin(s);
    int rc = 0;
    for (int i = 0; i < t->n; i++) {
        int code = t->code[i];
        if (code >= 1000) code -= 1000;
        else if (code == CLS_RULE_IDENT) code = lime_snapshot_token_code(s, "IDENT");
        else if (code == CLS_RULE_NUMBER) code = lime_snapshot_token_code(s, "NUM");
        else if (code == CLS_RULE_PUNCT) code = lime_snapshot_token_code(s, "COMMA");
        else code = lime_snapshot_token_code(s, "OP");
        rc = parse_token(ctx, code, NULL, -1);
        if (rc < 0) break;
    }
    if (rc >= 0) rc = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    return rc == 1;
}

int main(void) {
    ParserSnapshot *base = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(GRAMMAR, strlen(GRAMMAR), &base, &err) != 0 || !base) {
        printf("  [BROKEN] compile: %s\n", err ? err : "?");
        return 1;
    }
    free(err);
    static const LimeLexItem kws[] = { { "select", "SELECT" }, { "from", "FROM" },
                                       { "where", "WHERE" } };
    static const LimeLexItem ops[] = { { "<=", "LE" }, { ",", "COMMA" } };
    char *e = lime_snapshot_set_lexicon(base, kws, 3, ops, 2, LIME_LEX_FOLD_CASE);
    if (e) {
        printf("  [BROKEN] %s\n", e);
        return 1;
    }
    const LimeLexicon *bl = lime_snapshot_lexicon(base);
    int SELECT = 1000 + lime_snapshot_token_code(base, "SELECT");
    int WHERE = 1000 + lime_snapshot_token_code(base, "WHERE");
    int LE = 1000 + lime_snapshot_token_code(base, "LE");
    Toks t;

    /* 1. inert without a classifier */
    CHECK(lex("SELECT a<=-b", NULL, &t) == 0, "lex failed");
    {
        int c[] = { CLS_RULE_IDENT, CLS_RULE_IDENT, CLS_RULE_OP, CLS_RULE_IDENT };
        const char *x[] = { "SELECT", "a", "<=-", "b" };
        expect("no classifier", &t, c, x, 4);
    }
    /* 2. base lexicon */
    CHECK(lex("Select a<=-b where x <= 7", bl, &t) == 0, "lex failed");
    {
        int c[] = { SELECT, CLS_RULE_IDENT, LE, CLS_RULE_OP, CLS_RULE_IDENT, WHERE, CLS_RULE_IDENT, LE,
                    CLS_RULE_NUMBER };
        const char *x[] = { "Select", "a", "<=", "-", "b", "where", "x", "<=", "7" };
        expect("base lexicon", &t, c, x, 9);
    }
    /* 3. runtime extension: CONNECT and ->> */
    ParserSnapshot *comp = compose(base);
    CHECK(comp != NULL, "compose failed");
    if (comp) {
        const LimeLexicon *cl = lime_snapshot_lexicon(comp);
        int CONNECT = 1000 + lime_snapshot_token_code(comp, "CONNECT");
        int ARROW2 = 1000 + lime_snapshot_token_code(comp, "ARROW2");
        CHECK(lex("connect a; j->>k", cl, &t) == 0, "lex failed");
        {
            int c[] = { CONNECT, CLS_RULE_IDENT, CLS_RULE_PUNCT, CLS_RULE_IDENT, ARROW2,
                        CLS_RULE_IDENT };
            const char *x[] = { "connect", "a", ";", "j", "->>", "k" };
            expect("composed lexicon", &t, c, x, 6);
        }
        CHECK(lex("connect a; j->>k", bl, &t) == 0, "lex failed");
        {
            int c[] = { CLS_RULE_IDENT, CLS_RULE_IDENT, CLS_RULE_PUNCT, CLS_RULE_IDENT, CLS_RULE_OP,
                        CLS_RULE_IDENT };
            const char *x[] = { "connect", "a", ";", "j", "->>", "k" };
            expect("base lexicon, extension text", &t, c, x, 6);
        }
        /* 4. into the composed parser */
        lex("CONNECT root", cl, &t);
        CHECK(parses(comp, &t), "CONNECT root rejected by the composed parser");
        lex("doc ->> key", cl, &t);
        CHECK(parses(comp, &t), "doc ->> key rejected by the composed parser");
        lex("select a, b from t where c <= 3", cl, &t);
        CHECK(parses(comp, &t), "base SELECT rejected by the composed parser");
        lex("select a from connect", cl, &t);
        CHECK(!parses(comp, &t), "reserved CONNECT accepted as a table name");
        snapshot_release(comp);
    }
    /* 5. LIME_LEX_OP_WHOLE: "<=>" is one OP, "<=" still LE */
    e = lime_snapshot_set_lexicon(base, kws, 3, ops, 2, LIME_LEX_FOLD_CASE | LIME_LEX_OP_WHOLE);
    CHECK(e == NULL, "set_lexicon: %s", e ? e : "");
    free(e);
    bl = lime_snapshot_lexicon(base);
    lex("a<=>b<=c", bl, &t);
    {
        int c[] = { CLS_RULE_IDENT, CLS_RULE_OP, CLS_RULE_IDENT, LE, CLS_RULE_IDENT };
        const char *x[] = { "a", "<=>", "b", "<=", "c" };
        expect("LIME_LEX_OP_WHOLE", &t, c, x, 5);
    }
    /* a rule outside the classes is never offered, even to a greedy
    ** classifier */
    {
        size_t n = 0;
        CHECK(Cls_classify(classify, &(Cls){ bl }, CLS_RULE_NUMBER, "7", 1, &n) == -1,
              "NUMBER was offered to the classifier");
        CHECK(Cls_classify(classify, &(Cls){ bl }, CLS_RULE_IDENT, "select", 6, &n) == SELECT &&
                  n == 6,
              "Cls_classify(IDENT, select)");
        CHECK(Cls_classify(NULL, NULL, CLS_RULE_IDENT, "select", 6, &n) == -1, "NULL fn claimed");
    }
    snapshot_release(base);
    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all classify checks passed", fails,
           fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
