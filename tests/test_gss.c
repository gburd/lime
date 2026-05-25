/*
** test_gss.c -- direct unit tests for the Graph-Structured Stack
** primitives that back the GLR engine.
**
** Coverage targets:
**   - gss_node_create                (allocation, default refcount)
**   - gss_node_acquire               (refcount increment)
**   - gss_node_release               (refcount decrement; predecessors
**                                     are freed when refcount hits 0)
**   - gss_node_add_predecessor       (predecessor array growth across
**                                     the GLR_INITIAL_PREDS=4 boundary)
**
** The arena type used here is the LimeArena from src/lime_ast.c, which
** the GLR module already depends on for GSSNode storage.
*/
#include "glr.h"
#include "lime_ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(name) \
    do { printf("  %-60s", name); fflush(stdout); } while (0)
#define PASS() \
    do { printf("PASS\n"); tests_passed++; } while (0)
#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); tests_failed++; return; } while (0)

static int tests_passed = 0;
static int tests_failed = 0;

static void test_create_basic(void) {
    TEST("gss_node_create returns node with refcount 1");
    LimeArena *arena = lime_arena_create(0);
    if (!arena) FAIL("arena create");

    GSSNode *n = gss_node_create(arena, 42);
    if (!n) { lime_arena_destroy(arena); FAIL("gss_node_create returned NULL"); }
    if (n->state != 42) {
        lime_arena_destroy(arena); FAIL("state mismatch");
    }
    if (n->refcount != 1) {
        lime_arena_destroy(arena); FAIL("refcount != 1 on creation");
    }
    if (n->npred != 0 || n->pred_capacity != 0 || n->predecessors != NULL) {
        lime_arena_destroy(arena); FAIL("predecessor state not zeroed");
    }
    gss_node_release(n);
    lime_arena_destroy(arena);
    PASS();
}

static void test_acquire_release_refcount(void) {
    TEST("acquire/release increments and decrements refcount");
    LimeArena *arena = lime_arena_create(0);
    if (!arena) FAIL("arena");

    GSSNode *n = gss_node_create(arena, 7);
    if (!n) { lime_arena_destroy(arena); FAIL("create"); }

    GSSNode *r = gss_node_acquire(n);
    if (r != n) { lime_arena_destroy(arena); FAIL("acquire return value"); }
    if (n->refcount != 2) { lime_arena_destroy(arena); FAIL("refcount after acquire"); }

    gss_node_acquire(n);
    if (n->refcount != 3) { lime_arena_destroy(arena); FAIL("acquire #3"); }

    gss_node_release(n);
    if (n->refcount != 2) { lime_arena_destroy(arena); FAIL("release back to 2"); }
    gss_node_release(n);
    if (n->refcount != 1) { lime_arena_destroy(arena); FAIL("release back to 1"); }

    /* Final release brings refcount to 0; predecessor cleanup runs. */
    gss_node_release(n);
    if (n->refcount != 0) { lime_arena_destroy(arena); FAIL("final release"); }

    lime_arena_destroy(arena);
    PASS();
}

static void test_add_predecessor_growth(void) {
    TEST("add_predecessor grows past GLR_INITIAL_PREDS=4");
    LimeArena *arena = lime_arena_create(0);
    if (!arena) FAIL("arena");

    GSSNode *child = gss_node_create(arena, 0);
    if (!child) { lime_arena_destroy(arena); FAIL("child"); }

    /* Push 16 distinct predecessor nodes through the growth boundary. */
    GSSNode *preds[16];
    for (int i = 0; i < 16; i++) {
        preds[i] = gss_node_create(arena, (uint32_t)(100 + i));
        if (!preds[i]) {
            lime_arena_destroy(arena); FAIL("predecessor create");
        }
        gss_node_add_predecessor(child, preds[i]);
    }

    if (child->npred != 16) {
        lime_arena_destroy(arena); FAIL("npred not 16");
    }
    if (child->pred_capacity < 16) {
        lime_arena_destroy(arena); FAIL("capacity not grown");
    }
    for (int i = 0; i < 16; i++) {
        if (child->predecessors[i] != preds[i]) {
            lime_arena_destroy(arena); FAIL("predecessor identity");
        }
        /* add_predecessor should have acquired each, so they're at refcount 2. */
        if (preds[i]->refcount != 2) {
            lime_arena_destroy(arena); FAIL("predecessor refcount not 2");
        }
    }

    /* Releasing the child to refcount 0 must release predecessors. */
    gss_node_release(child);
    for (int i = 0; i < 16; i++) {
        if (preds[i]->refcount != 1) {
            lime_arena_destroy(arena); FAIL("predecessor not released");
        }
        gss_node_release(preds[i]);
    }

    lime_arena_destroy(arena);
    PASS();
}

static void test_add_predecessor_null_safety(void) {
    TEST("add_predecessor null-safe");
    /* These calls should not crash. */
    gss_node_add_predecessor(NULL, NULL);
    LimeArena *a = lime_arena_create(0);
    if (!a) FAIL("arena");
    GSSNode *n = gss_node_create(a, 0);
    gss_node_add_predecessor(n, NULL);
    gss_node_add_predecessor(NULL, n);
    if (n->npred != 0) { lime_arena_destroy(a); FAIL("null calls modified state"); }
    gss_node_release(n);
    lime_arena_destroy(a);
    PASS();
}

static void test_release_null_safety(void) {
    TEST("release null-safe");
    gss_node_release(NULL);
    GSSNode *r = gss_node_acquire(NULL);
    if (r != NULL) FAIL("acquire(NULL) should return NULL");
    PASS();
}

static void test_release_chains_predecessors(void) {
    TEST("release recursively releases predecessor chain");
    LimeArena *arena = lime_arena_create(0);
    if (!arena) FAIL("arena");

    /* Build a -> b -> c chain via predecessor links. */
    GSSNode *c = gss_node_create(arena, 1);
    GSSNode *b = gss_node_create(arena, 2);
    GSSNode *a = gss_node_create(arena, 3);
    if (!a || !b || !c) {
        lime_arena_destroy(arena); FAIL("create");
    }
    gss_node_add_predecessor(b, c);
    gss_node_add_predecessor(a, b);

    /* a:1, b:2 (1+1 from a's predecessor link), c:2 (1+1 from b's link) */
    if (a->refcount != 1) { lime_arena_destroy(arena); FAIL("a rc"); }
    if (b->refcount != 2) { lime_arena_destroy(arena); FAIL("b rc"); }
    if (c->refcount != 2) { lime_arena_destroy(arena); FAIL("c rc"); }

    gss_node_release(a);
    /* a:0 -> drops b's reference; b:1 (still owned by the test) */
    if (b->refcount != 1) { lime_arena_destroy(arena); FAIL("b rc after a release"); }

    gss_node_release(b);
    /* b:0 -> drops c's reference; c:1 */
    if (c->refcount != 1) { lime_arena_destroy(arena); FAIL("c rc after b release"); }

    gss_node_release(c);
    lime_arena_destroy(arena);
    PASS();
}

int main(void) {
    printf("GSS unit tests\n");
    printf("===============\n");

    test_create_basic();
    test_acquire_release_refcount();
    test_add_predecessor_growth();
    test_add_predecessor_null_safety();
    test_release_null_safety();
    test_release_chains_predecessors();

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
