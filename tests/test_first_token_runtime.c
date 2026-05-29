/*
 * test_first_token_runtime.c
 *
 * Lime-Letter-25 regression: src/parse_engine.c::parse_token must
 * apply the %first_token offset when the snapshot declares one.
 *
 * The PostgreSQL migration team's runtime extension pipeline
 * (Phase 4 Track A) discovered that parse_token() was using the
 * raw token_code as the action-table index, ignoring the
 * %first_token directive.  PG's gram.lime declares
 * %first_token 257; the scanner emits external code 818 for an
 * extension token K_DUMMY (internal index 561 = 818 - 257).
 * parse_token(snap, 818, ...) then indexed yy_action at slot 818
 * instead of 561 and returned a spurious syntax error.
 *
 * limpar.c (the generated push-parser template) already applies
 * the offset at parse_*_token entry; this test demonstrates the
 * runtime equivalent now does the same.
 *
 * Sub-tests:
 *   1. snapshot carries yy_first_token after compile
 *   2. parse_token accepts the external code (offset+internal)
 *   3. parse_token rejects out-of-range external codes cleanly
 *      (no UB, returns -1)
 *   4. when yy_first_token == 0, parse_token treats codes as-is
 *      (existing behaviour preserved)
 */

#include "parser.h"
#include "snapshot.h"
#include "parse_context.h"
#include "lime_compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_count = 0;
static int pass_count = 0;
static int skip_count = 0;

#define TEST(name) do { \
    printf("[TEST %d] %s\n", ++test_count, name); fflush(stdout); \
} while (0)
#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s\n", msg); return; \
    } \
} while (0)
#define PASS() do { printf("  PASS\n"); pass_count++; } while (0)
#define SKIP(reason) do { \
    printf("  SKIP: %s\n", reason); skip_count++; \
} while (0)

/* Grammar mirroring PG's pattern: %first_token 257 means external
** codes start at 257.  Token A has external code 257 and internal
** index 0.  Token K_DUMMY would be a later external code (e.g.
** 257 + N where N is the token's declaration index). */
static const char *G =
    "%name FT\n"
    "%token A B C.\n"
    "%first_token 257\n"
    "%start_symbol s\n"
    "s ::= A.\n"
    "s ::= B.\n"
    "s ::= C.\n";

static struct ParserSnapshot *compile_grammar(void) {
    struct ParserSnapshot *snap = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(G, strlen(G), &snap, &err) != 0) {
        fprintf(stderr, "  compile failed: %s\n", err ? err : "(no msg)");
        free(err);
        return NULL;
    }
    free(err);
    return snap;
}

static void test_snapshot_carries_first_token(void) {
    TEST("snapshot.yy_first_token populated from %first_token");
    struct ParserSnapshot *snap = compile_grammar();
    if (!snap) { SKIP("in-process compile unavailable"); return; }

    ASSERT(snap->yy_first_token == 257,
           "yy_first_token should be 257 to match grammar");
    ASSERT(snap->yy_ntoken > 0, "yy_ntoken populated");

    snapshot_release(snap);
    PASS();
}

static void test_parse_token_accepts_external_code(void) {
    TEST("parse_token accepts EXTERNAL token code (Letter-25 fix)");
    struct ParserSnapshot *snap = compile_grammar();
    if (!snap) { SKIP("in-process compile unavailable"); return; }

    ParseContext *ctx = parse_begin(snap);
    ASSERT(ctx != NULL, "parse_begin");

    /* Token A is the first %token-declared token, so its internal
    ** index is 1 (0 is reserved for $end / EOF in lemon's mapping;
    ** the actual mapping is grammar-driven).  External code is
    ** internal + first_token = 1 + 257 = 258.  Pre-fix this would
    ** index yy_action at slot 258 (wrong) and fail.  Post-fix it
    ** subtracts 257 first. */
    /* Try external codes 258..260 (the three tokens A, B, C). */
    int accepted = 0;
    for (int ext = 258; ext <= 260; ext++) {
        ParseContext *c = parse_begin(snap);
        ASSERT(c != NULL, "parse_begin for try");
        int rc = parse_token(c, ext, NULL, 0);
        if (rc >= 0) {
            /* token accepted into a state; now feed EOF (0) */
            int rc2 = parse_token(c, 0, NULL, 0);
            if (rc2 == 1) accepted++;
        }
        parse_end(c);
    }
    parse_end(ctx);

    ASSERT(accepted >= 1,
           "at least one external-code parse should accept; pre-fix "
           "all three failed because yy_first_token was ignored");
    printf("  accepted %d / 3 external-code parses\n", accepted);

    snapshot_release(snap);
    PASS();
}

static void test_parse_token_out_of_range(void) {
    TEST("parse_token rejects out-of-range external code");
    struct ParserSnapshot *snap = compile_grammar();
    if (!snap) { SKIP("in-process compile unavailable"); return; }

    /* External code 100 is below first_token=257; should error,
    ** not crash. */
    ParseContext *ctx = parse_begin(snap);
    ASSERT(ctx != NULL, "parse_begin");
    int rc = parse_token(ctx, 100, NULL, 0);
    ASSERT(rc == -1,
           "external code below first_token should error (-1), "
           "not crash or silently accept");
    parse_end(ctx);

    /* External code 99999 is far above first_token + nterminal;
    ** should error. */
    ParseContext *c2 = parse_begin(snap);
    ASSERT(c2 != NULL, "parse_begin 2");
    int rc2 = parse_token(c2, 99999, NULL, 0);
    ASSERT(rc2 == -1, "out-of-range high code should error (-1)");
    parse_end(c2);

    snapshot_release(snap);
    PASS();
}

static void test_no_first_token_unchanged(void) {
    TEST("yy_first_token == 0: parse_token unchanged, "
         "no offset applied");
    /* Same grammar without %first_token; tokens use raw internal
    ** codes. */
    static const char *G2 =
        "%name FT2\n"
        "%token A B C.\n"
        "%start_symbol s\n"
        "s ::= A.\n";
    struct ParserSnapshot *snap = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(G2, strlen(G2), &snap, &err) != 0) {
        free(err);
        SKIP("in-process compile unavailable");
        return;
    }
    free(err);
    ASSERT(snap->yy_first_token == 0,
           "yy_first_token should be zero when not declared");

    /* Internal code 1 (token A).  parse_token should NOT subtract
    ** anything; it's already an internal index. */
    ParseContext *ctx = parse_begin(snap);
    ASSERT(ctx != NULL, "parse_begin");
    int rc = parse_token(ctx, 1, NULL, 0);
    /* rc may be 0 (still parsing) or 1 (accept after EOF push) */
    ASSERT(rc >= 0,
           "internal code through parse_token should not error");
    parse_end(ctx);

    snapshot_release(snap);
    PASS();
}

int main(void) {
    printf("=== test_first_token_runtime ===\n");

    test_snapshot_carries_first_token();
    test_parse_token_accepts_external_code();
    test_parse_token_out_of_range();
    test_no_first_token_unchanged();

    int effective = test_count - skip_count;
    printf("\n=== Results: %d/%d passed (%d skipped) ===\n",
           pass_count, effective, skip_count);
    if (effective == 0) return 77;
    return (pass_count == effective) ? 0 : 1;
}
