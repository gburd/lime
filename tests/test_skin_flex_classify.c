/*
** tests/test_skin_flex_classify.c -- the flex skin's yylex() honours
** %keyword_class / %operator_class (Track B3's classification hook).
**
** tests/test_lex_classify_grammar.lex (no keywords, no multi-char
** operators) is compiled by `lime -X --target=c:flex` at build time.  At
** run time this driver scans through the flex-API yylex():
**   1. with no classifier installed, yylex() returns rule_id + 1 for every
**      token, exactly as before the hook existed;
**   2. with yy_set_classifier(lime_lexicon_classify-based fn, base lexicon)
**      "select" comes back as SELECT, "<=" as LE, and "a<=-b" splits into
**      IDENT LE OP(-) IDENT with yytext/yyleng covering only the claim;
**   3. an extension composes CONNECT ("connect") and ARROW2 ("->>") into a
**      snapshot at run time (no compiler, no dlopen); the SAME generated
**      scanner, pointed at that snapshot's lexicon, returns CONNECT and
**      ARROW2 from yylex(), while the base lexicon still gives IDENT/OP;
**   4. yy_set_classifier(NULL, NULL) restores case 1.
**   5. yylineno stays right around claimed tokens (a regression check
**      only: no rule in this grammar can match a newline, so it cannot
**      tell a claim from a non-claim).
**
** Negative control (by hand, see the commit): make the emitted skin skip
** the classify call; checks 2 and 3 fail.
*/
#define _POSIX_C_SOURCE 200809L
#include "extension.h"
#include "lime_lexer.h"
#include "parser.h"
#include "snapshot.h"
#include "test_skin_flex_classify_grammar_flex.h"
#include "test_skin_flex_classify_grammar_lex.h"

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
    "cols ::= IDENT.\n"
    "cols ::= cols COMMA IDENT.\n";

typedef struct {
    int code[64];
    char text[64][16];
    int n;
} Toks;

/* Lexicon codes are returned as 1000 + code so they cannot be confused
** with the skin's rule_id + 1 returns. */
typedef struct {
    const LimeLexicon *lx;
} Cls;
static int classify(const void *cls, int kind, const char *text, size_t len, size_t *out) {
    int c = lime_lexicon_classify(((const Cls *)cls)->lx, kind, text, len, out);
    return c < 0 ? -1 : 1000 + c;
}

/* Scan `in` with yylex(), dropping whitespace (rule ws). */
static void scan(const char *in, Toks *out) {
    memset(out, 0, sizeof *out);
    YY_BUFFER_STATE b = yy_scan_string(in);
    int code;
    while ((code = yylex()) != YY_FLEX_EOF) {
        if (code == CLS_FLEX_WS) continue;
        if (out->n >= 64) break;
        out->code[out->n] = code;
        snprintf(out->text[out->n], sizeof out->text[out->n], "%.*s", yyleng, yytext);
        out->n++;
    }
    yy_delete_buffer(b);
}

static void expect(const char *what, const Toks *t, const int *codes, const char *const *texts,
                   int n) {
    int ok = t->n == n;
    for (int i = 0; ok && i < n; i++)
        ok = t->code[i] == codes[i] && strcmp(t->text[i], texts[i]) == 0;
    CHECK(ok, "%s: token stream differs", what);
    if (!ok) {
        printf("    got:");
        for (int i = 0; i < t->n; i++) printf(" %d'%s'", t->code[i], t->text[i]);
        printf("\n   want:");
        for (int i = 0; i < n; i++) printf(" %d'%s'", codes[i], texts[i]);
        printf("\n");
    }
}

static GrammarModification g_mods[2];
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
    *out = g_mods;
    *nout = 2;
    return true;
}

static ParserSnapshot *compose(ParserSnapshot *base) {
    ExtensionRegistry *reg = create_extension_registry();
    ExtensionInfo info = { 0 };
    info.name = "connect_kw";
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
    char *e = lime_lex_compose(out, base, g_mods, 2);
    if (e) {
        printf("  lime_lex_compose: %s\n", e);
        free(e);
        snapshot_release(out);
        return NULL;
    }
    return out;
}

int main(void) {
    ParserSnapshot *base = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(GRAMMAR, strlen(GRAMMAR), &base, &err) != 0 || !base) {
        printf("  [BROKEN] compile: %s\n", err ? err : "?");
        return 1;
    }
    free(err);
    static const LimeLexItem kws[] = { { "select", "SELECT" }, { "from", "FROM" } };
    static const LimeLexItem ops[] = { { "<=", "LE" }, { ",", "COMMA" } };
    char *e = lime_snapshot_set_lexicon(base, kws, 2, ops, 2, LIME_LEX_FOLD_CASE);
    if (e) {
        printf("  [BROKEN] %s\n", e);
        return 1;
    }
    const LimeLexicon *bl = lime_snapshot_lexicon(base);
    const int SELECT = 1000 + lime_snapshot_token_code(base, "SELECT");
    const int LE = 1000 + lime_snapshot_token_code(base, "LE");
    Toks t;

    /* 1. inert without a classifier */
    scan("SELECT a<=-b", &t);
    {
        int c[] = { CLS_FLEX_IDENT, CLS_FLEX_IDENT, CLS_FLEX_OP, CLS_FLEX_IDENT };
        const char *x[] = { "SELECT", "a", "<=-", "b" };
        expect("no classifier", &t, c, x, 4);
    }
    /* 2. base lexicon through yylex() */
    Cls cb = { bl };
    yy_set_classifier(classify, &cb);
    scan("Select a<=-b", &t);
    {
        int c[] = { SELECT, CLS_FLEX_IDENT, LE, CLS_FLEX_OP, CLS_FLEX_IDENT };
        const char *x[] = { "Select", "a", "<=", "-", "b" };
        expect("base lexicon", &t, c, x, 5);
    }
    /* 3. runtime extension keyword */
    ParserSnapshot *comp = compose(base);
    CHECK(comp != NULL, "compose failed");
    if (comp) {
        Cls cc = { lime_snapshot_lexicon(comp) };
        const int CONNECT = 1000 + lime_snapshot_token_code(comp, "CONNECT");
        const int ARROW2 = 1000 + lime_snapshot_token_code(comp, "ARROW2");
        CHECK(CONNECT >= 1000 && ARROW2 >= 1000, "extension tokens missing from the snapshot");
        yy_set_classifier(classify, &cc);
        scan("connect a; j->>k", &t);
        {
            int c[] = { CONNECT, CLS_FLEX_IDENT, CLS_FLEX_PUNCT, CLS_FLEX_IDENT, ARROW2,
                        CLS_FLEX_IDENT };
            const char *x[] = { "connect", "a", ";", "j", "->>", "k" };
            expect("composed lexicon", &t, c, x, 6);
        }
        yy_set_classifier(classify, &cb);
        scan("connect a; j->>k", &t);
        {
            int c[] = { CLS_FLEX_IDENT, CLS_FLEX_IDENT, CLS_FLEX_PUNCT, CLS_FLEX_IDENT, CLS_FLEX_OP,
                        CLS_FLEX_IDENT };
            const char *x[] = { "connect", "a", ";", "j", "->>", "k" };
            expect("base lexicon, extension text", &t, c, x, 6);
        }
        snapshot_release(comp);
    }
    /* 5. yylineno: the claimed prefix "<=" holds no newline; the rest of
    ** the op match is rescanned, so each newline is counted exactly once. */
    yylineno = 1;
    scan("a\n<=\n-b", &t);
    CHECK(yylineno == 3, "yylineno after claimed tokens is %d, want 3", yylineno);
    /* 4. classifier removed */
    yy_set_classifier(NULL, NULL);
    scan("select", &t);
    {
        int c[] = { CLS_FLEX_IDENT };
        const char *x[] = { "select" };
        expect("classifier removed", &t, c, x, 1);
    }
    snapshot_release(base);
    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all flex-skin classify checks passed", fails,
           fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
