/*
** Parse context comprehensive tests - targeting 95% coverage
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include "../src/snapshot.h"
#include "../include/parse_context.h"

#define TEST(name) printf("  %-60s", name); fflush(stdout)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static int tests_passed = 0;
static int tests_failed = 0;

/* Helper to create a minimal snapshot for testing */
static ParserSnapshot *make_test_snapshot(void) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (!snap) return NULL;

    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    snap->nstate = 10;
    snap->nterminal = 5;

    return snap;
}

/* Test create and destroy */
static void test_create_destroy(void) {
    TEST("create and destroy");

    ParserSnapshot *snap = make_test_snapshot();
    if (!snap) { FAIL("make_test_snapshot failed"); return; }

    ParseContext *ctx = parse_context_create(snap);
    if (!ctx) {
        snapshot_release(snap);
        FAIL("parse_context_create failed");
        return;
    }

    parse_context_destroy(ctx);
    snapshot_release(snap);
    PASS();
}

/* Test NULL snapshot */
static void test_null_snapshot(void) {
    TEST("NULL snapshot");

    ParseContext *ctx = parse_context_create(NULL);
    if (ctx != NULL) {
        parse_context_destroy(ctx);
        FAIL("should return NULL for NULL snapshot");
        return;
    }

    PASS();
}

/* Test NULL context destroy */
static void test_null_destroy(void) {
    TEST("NULL context destroy");
    parse_context_destroy(NULL);
    PASS();
}

/* Test refcount management */
static void test_refcount(void) {
    TEST("refcount management");

    ParserSnapshot *snap = make_test_snapshot();
    if (!snap) { FAIL("make_test_snapshot failed"); return; }

    uint32_t initial = atomic_load(&snap->refcount);

    ParseContext *ctx = parse_context_create(snap);
    if (!ctx) {
        snapshot_release(snap);
        FAIL("parse_context_create failed");
        return;
    }

    uint32_t after_create = atomic_load(&snap->refcount);
    if (after_create != initial + 1) {
        parse_context_destroy(ctx);
        snapshot_release(snap);
        FAIL("refcount not incremented");
        return;
    }

    parse_context_destroy(ctx);

    uint32_t after_destroy = atomic_load(&snap->refcount);
    if (after_destroy != initial) {
        snapshot_release(snap);
        FAIL("refcount not decremented");
        return;
    }

    snapshot_release(snap);
    PASS();
}

/* Test multiple contexts */
static void test_multiple_contexts(void) {
    TEST("multiple contexts");

    ParserSnapshot *snap = make_test_snapshot();
    if (!snap) { FAIL("make_test_snapshot failed"); return; }

    uint32_t initial = atomic_load(&snap->refcount);

    ParseContext *ctx1 = parse_context_create(snap);
    ParseContext *ctx2 = parse_context_create(snap);
    ParseContext *ctx3 = parse_context_create(snap);

    if (!ctx1 || !ctx2 || !ctx3) {
        if (ctx1) parse_context_destroy(ctx1);
        if (ctx2) parse_context_destroy(ctx2);
        if (ctx3) parse_context_destroy(ctx3);
        snapshot_release(snap);
        FAIL("failed to create contexts");
        return;
    }

    uint32_t with_contexts = atomic_load(&snap->refcount);
    if (with_contexts != initial + 3) {
        parse_context_destroy(ctx1);
        parse_context_destroy(ctx2);
        parse_context_destroy(ctx3);
        snapshot_release(snap);
        FAIL("refcount incorrect");
        return;
    }

    parse_context_destroy(ctx1);
    parse_context_destroy(ctx2);
    parse_context_destroy(ctx3);

    uint32_t after = atomic_load(&snap->refcount);
    if (after != initial) {
        snapshot_release(snap);
        FAIL("refcount not restored");
        return;
    }

    snapshot_release(snap);
    PASS();
}

/* Test repeated cycles */
static void test_repeated_cycles(void) {
    TEST("repeated cycles");

    ParserSnapshot *snap = make_test_snapshot();
    if (!snap) { FAIL("make_test_snapshot failed"); return; }

    for (int i = 0; i < 100; i++) {
        ParseContext *ctx = parse_context_create(snap);
        if (!ctx) {
            snapshot_release(snap);
            FAIL("failed on cycle");
            return;
        }
        parse_context_destroy(ctx);
    }

    uint32_t final = atomic_load(&snap->refcount);
    if (final != 1) {
        snapshot_release(snap);
        FAIL("refcount leaked");
        return;
    }

    snapshot_release(snap);
    PASS();
}

int main(void) {
    printf("\nParse Context Coverage Tests\n");
    printf("=============================\n\n");

    test_create_destroy();
    test_null_snapshot();
    test_null_destroy();
    test_refcount();
    test_multiple_contexts();
    test_repeated_cycles();

    printf("\n=============================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_passed + tests_failed);
    printf("=============================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
