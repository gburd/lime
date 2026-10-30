/*
** test_compose_proposals.c -- C5: an extension's on_conflict answer is a
** PROPOSAL; only the host policy resolves.
**
** Two extensions declare token POW with different lexemes ("^" vs "**"),
** a declaration conflict.  ext B's on_conflict proposes USE_NEW (keep the
** later modification = B's).
**   no host policy at all     -> legacy mode: B's proposal applies (old
**                                behaviour, kept for one release)
**   a host policy, B untrusted -> proposal ignored: FAIL, and the error
**                                names B's proposal and the trust line
**   trust-extension-proposals B. -> proposal applies
**   trust-extension-proposals A. -> B still ignored (trust is per ext)
*/
#include "extension.h"
#include "lime_compiler.h"
#include "lime_policy.h"
#include "parser.h"
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int n_pass, n_fail;
#define CHECK(c, msg)                                                                     \
    do {                                                                                  \
        if (c) { n_pass++; printf("  ok   %s\n", msg); }                                  \
        else   { n_fail++; printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); }     \
    } while (0)

static const char *BASE = "%token_type {int}\n%token NUM.\nprog ::= expr.\nexpr ::= NUM.\n";

static GrammarModification ma, mb;
static int b_called;
static bool get_a(void *u, const struct ParserSnapshot *s, GrammarModification **o, uint32_t *n) {
    (void)u; (void)s;
    memset(&ma, 0, sizeof ma);
    ma.type = MOD_ADD_TOKEN;
    ma.u.add_token.name = "POW";
    ma.u.add_token.lexeme = "^";
    ma.u.add_token.token_code = -1;
    *o = &ma; *n = 1;
    return true;
}
static bool get_b(void *u, const struct ParserSnapshot *s, GrammarModification **o, uint32_t *n) {
    (void)u; (void)s;
    memset(&mb, 0, sizeof mb);
    mb.type = MOD_ADD_TOKEN;
    mb.u.add_token.name = "POW";
    mb.u.add_token.lexeme = "**";
    mb.u.add_token.token_code = -1;
    *o = &mb; *n = 1;
    return true;
}
static ConflictResolution b_says_mine(void *u, const ConflictInfo *ci) {
    (void)u; (void)ci;
    b_called++;
    return CONFLICT_USE_NEW;
}

static ParserSnapshot *g_base;
static char *g_err;
static ParserSnapshot *compose(const char *policy) {
    ExtensionRegistry *reg = create_extension_registry();
    LimeComposePolicy *pol = policy ? lime_policy_parse(policy, strlen(policy), NULL) : NULL;
    extension_registry_set_policy(reg, pol);
    ExtensionInfo a = {.name = "A", .version = "1", .get_modifications = get_a};
    ExtensionInfo b = {.name = "B", .version = "1", .get_modifications = get_b,
                       .on_conflict = b_says_mine};
    ExtensionID ia, ib;
    register_extension(reg, &a, &ia);
    register_extension(reg, &b, &ib);
    load_extension(reg, ia, NULL, NULL);
    load_extension(reg, ib, NULL, NULL);
    free(g_err);
    g_err = NULL;
    ParserSnapshot *out = NULL;
    if (!publish_modified_snapshot(reg, g_base, &out, NULL, &g_err)) out = NULL;
    destroy_extension_registry(reg);
    lime_policy_free(pol);
    return out;
}

int main(void) {
    if (lime_compile_grammar_in_process(BASE, strlen(BASE), &g_base, NULL) != 0) return 1;
    ParserSnapshot *s;

    b_called = 0;
    s = compose(NULL);
    CHECK(s != NULL && b_called > 0, "no host policy (legacy): B's USE_NEW applies, compose succeeds");
    snapshot_release(s);

    b_called = 0;
    s = compose("rank A 1.");
    CHECK(s == NULL && b_called > 0, "host policy, B untrusted: B was asked, proposal ignored, FAIL");
    CHECK(g_err && strstr(g_err, "proposal from B") && strstr(g_err, "prefer-extension") &&
              strstr(g_err, "trust-extension-proposals B."),
          "error lists B's proposal (USE_NEW = prefer-extension) and the trust line");
    if (g_err) printf("     %s\n", strchr(g_err, '\n') ? strchr(g_err, '\n') + 1 : g_err);
    snapshot_release(s);

    s = compose("trust-extension-proposals B.");
    CHECK(s != NULL, "trust-extension-proposals B.: proposal applies");
    snapshot_release(s);

    s = compose("trust-extension-proposals A.");
    CHECK(s == NULL, "trusting A does not trust B");
    snapshot_release(s);

    free(g_err);
    snapshot_release(g_base);
    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
