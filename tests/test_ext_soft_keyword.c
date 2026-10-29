/*
** test_ext_soft_keyword.c -- an extension keyword must not break an
** existing identifier of the same spelling.
**
** The scenario from the review: an Oracle-compat extension adds CONNECT
** as a keyword.  Every existing query with a column named `connect` --
** `SELECT connect FROM t`, which a keyword-aware scanner lexes as
** SELECT CONNECT FROM ID -- becomes a syntax error, at CREATE EXTENSION
** time, for everyone.  SQLite avoids this with Lemon's %fallback: a
** keyword that cannot be shifted falls back to the identifier token.
**
** MOD_ADD_TOKEN.fallback_to exposes that to extensions.  This test
** composes the same extension twice -- reserved and soft -- through
** publish_modified_snapshot() and checks, on the runtime engine:
**
**   reserved: SELECT CONNECT FROM ID  -> rejected   (the problem)
**   soft:     SELECT CONNECT FROM ID  -> accepted   (the fix)
**   soft:     CONNECT ID              -> accepted   (the new syntax works)
**   both:     SELECT ID FROM ID       -> accepted   (base unaffected)
*/
#define _POSIX_C_SOURCE 200809L
#include "extension.h"
#include "parser.h"
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern int lime_compile_grammar_in_process(const char *, size_t, ParserSnapshot **, char **)
    __attribute__((weak));
extern int lime_snapshot_token_code(const ParserSnapshot *snap, const char *name);

static const char *BASE =
    "%name SK\n"
    "%token_type { int }\n"
    "%token ID SELECT FROM COMMA.\n"
    "%start_symbol stmt\n"
    "stmt ::= SELECT cols FROM ID.\n"
    "cols ::= ID.\n"
    "cols ::= cols COMMA ID.\n";

static GrammarModification g_mods[2];
static const char *g_rhs[] = {"CONNECT", "ID", NULL};
static const char *g_fallback; /* NULL = reserved, "ID" = soft */

static bool get_mods(void *ud, const struct ParserSnapshot *base, GrammarModification **out,
                     uint32_t *nout) {
    (void)ud; (void)base;
    memset(g_mods, 0, sizeof g_mods);
    g_mods[0].type = MOD_ADD_TOKEN;
    g_mods[0].u.add_token.name = "CONNECT";
    g_mods[0].u.add_token.lexeme = "connect";
    g_mods[0].u.add_token.token_code = -1;
    g_mods[0].u.add_token.fallback_to = g_fallback;
    g_mods[1].type = MOD_ADD_RULE;
    g_mods[1].u.add_rule.lhs = "stmt";
    g_mods[1].u.add_rule.rhs = g_rhs;
    g_mods[1].u.add_rule.nrhs = 2;
    g_mods[1].u.add_rule.code = "{ }";
    g_mods[1].u.add_rule.precedence = -1;
    *out = g_mods;
    *nout = 2;
    return true;
}

static ParserSnapshot *compose(ParserSnapshot *base, const char *fallback) {
    g_fallback = fallback;
    ExtensionRegistry *reg = create_extension_registry();
    ExtensionInfo info = {0};
    info.name = "oracle_connect";
    info.version = "1.0.0";
    info.get_modifications = get_mods;
    ExtensionID id;
    char *err = NULL;
    ParserSnapshot *out = NULL;
    struct ConflictSet *cs = NULL;
    if (register_extension(reg, &info, &id) && load_extension(reg, id, NULL, &err)) {
        free(err); err = NULL;
        if (!publish_modified_snapshot(reg, base, &out, &cs, &err)) {
            fprintf(stderr, "  publish failed: %s\n", err ? err : "?");
            out = NULL;
        }
    }
    free(err);
    destroy_extension_registry(reg);
    return out;
}

/* Feed token NAMES (resolved against this snapshot); 1 = accept. */
static int parses(ParserSnapshot *s, const char *const *names) {
    ParseContext *ctx = parse_begin(s);
    int rc = 0;
    for (int i = 0; names[i]; i++) {
        int code = lime_snapshot_token_code(s, names[i]);
        if (code < 0) { parse_end(ctx); return -2; }
        rc = parse_token(ctx, code, NULL, -1);
        if (rc < 0) { parse_end(ctx); return 0; }
    }
    rc = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    return rc == 1;
}

static int fails = 0;
static void check(const char *what, int got, int want) {
    printf("  %-58s %s\n", what, got == want ? "ok" : "FAIL");
    if (got != want) {
        printf("      got %d, want %d\n", got, want);
        fails++;
    }
}

int main(void) {
    if (lime_compile_grammar_in_process == NULL) {
        printf("  [SKIP] in-process compiler not linked\n");
        return 77;
    }
    ParserSnapshot *base = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(BASE, strlen(BASE), &base, &err) != 0 || base == NULL) {
        printf("  [SKIP] base compile failed: %s\n", err ? err : "?");
        return 77;
    }
    free(err);

    static const char *const col_named_connect[] = {"SELECT", "CONNECT", "FROM", "ID", NULL};
    static const char *const connect_stmt[] = {"CONNECT", "ID", NULL};
    static const char *const plain_select[] = {"SELECT", "ID", "FROM", "ID", NULL};

    ParserSnapshot *reserved = compose(base, NULL);
    ParserSnapshot *soft = compose(base, "ID");
    if (reserved == NULL || soft == NULL) {
        printf("  [BROKEN] composition failed\n");
        return 1;
    }

    check("reserved CONNECT: SELECT connect FROM t is rejected", parses(reserved, col_named_connect), 0);
    check("reserved CONNECT: CONNECT t is accepted", parses(reserved, connect_stmt), 1);
    check("soft CONNECT:     SELECT connect FROM t is ACCEPTED", parses(soft, col_named_connect), 1);
    check("soft CONNECT:     CONNECT t is accepted", parses(soft, connect_stmt), 1);
    check("soft CONNECT:     SELECT a FROM t unaffected", parses(soft, plain_select), 1);

    snapshot_release(reserved);
    snapshot_release(soft);
    snapshot_release(base);
    printf("\n%s\n", fails ? "FAILED" : "all soft-keyword checks passed");
    return fails ? 1 : 0;
}
