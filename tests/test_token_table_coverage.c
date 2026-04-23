/*
** Token table comprehensive tests - targeting 95% coverage
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "token_table.h"

#define TEST(name) printf("  %-60s", name); fflush(stdout)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static int tests_passed = 0;
static int tests_failed = 0;

/* Test basic lifecycle */
static void test_create_destroy(void) {
    TEST("create and destroy");
    TokenTable *tt = create_token_table(64);
    if (!tt) { FAIL("create failed"); return; }
    destroy_token_table(tt);
    PASS();
}

/* Test NULL safety */
static void test_null_safety(void) {
    TEST("NULL safety");
    destroy_token_table(NULL);
    int code = lookup_token(NULL, "test", 4);
    if (code != -1) { FAIL("NULL lookup should return -1"); return; }
    PASS();
}

/* Test add and lookup */
static void test_add_lookup(void) {
    TEST("add and lookup");
    TokenTable *tt = create_token_table(64);
    if (!tt) { FAIL("create failed"); return; }

    if (!add_token(tt, "SELECT", 100, 0)) {
        destroy_token_table(tt);
        FAIL("add failed");
        return;
    }

    int code = lookup_token(tt, "SELECT", 6);
    if (code != 100) {
        destroy_token_table(tt);
        FAIL("lookup failed");
        return;
    }

    destroy_token_table(tt);
    PASS();
}

/* Test case insensitivity */
static void test_case_insensitive(void) {
    TEST("case insensitive lookup");
    TokenTable *tt = create_token_table(64);
    if (!tt) { FAIL("create failed"); return; }

    add_token(tt, "SELECT", 100, 0);

    if (lookup_token(tt, "SELECT", 6) != 100 ||
        lookup_token(tt, "select", 6) != 100 ||
        lookup_token(tt, "Select", 6) != 100) {
        destroy_token_table(tt);
        FAIL("case insensitive lookup failed");
        return;
    }

    destroy_token_table(tt);
    PASS();
}

/* Test many tokens for hash table growth */
static void test_many_tokens(void) {
    TEST("hash table growth");
    TokenTable *tt = create_token_table(16);
    if (!tt) { FAIL("create failed"); return; }

    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "TOK%d", i);
        if (!add_token(tt, name, i + 1000, 0)) {
            destroy_token_table(tt);
            FAIL("add failed");
            return;
        }
    }

    /* Verify lookup after growth */
    if (lookup_token(tt, "TOK50", 5) != 1050) {
        destroy_token_table(tt);
        FAIL("lookup after growth failed");
        return;
    }

    destroy_token_table(tt);
    PASS();
}

/* Test empty string */
static void test_empty_string(void) {
    TEST("empty string token");
    TokenTable *tt = create_token_table(64);
    if (!tt) { FAIL("create failed"); return; }

    if (!add_token(tt, "", 999, 0)) {
        destroy_token_table(tt);
        FAIL("add empty string failed");
        return;
    }

    if (lookup_token(tt, "", 0) != 999) {
        destroy_token_table(tt);
        FAIL("lookup empty string failed");
        return;
    }

    destroy_token_table(tt);
    PASS();
}

/* Test removal by extension */
static void test_remove_by_extension(void) {
    TEST("remove tokens by extension");
    TokenTable *tt = create_token_table(64);
    if (!tt) { FAIL("create failed"); return; }

    add_token(tt, "TOK1", 1, 0);
    add_token(tt, "TOK2", 2, 1);  /* extension 1 */
    add_token(tt, "TOK3", 3, 1);  /* extension 1 */
    add_token(tt, "TOK4", 4, 0);

    remove_tokens_by_extension(tt, 1);

    /* TOK1 and TOK4 should still be there, TOK2 and TOK3 gone */
    if (lookup_token(tt, "TOK1", 4) != 1 ||
        lookup_token(tt, "TOK4", 4) != 4 ||
        lookup_token(tt, "TOK2", 4) != -1 ||
        lookup_token(tt, "TOK3", 4) != -1) {
        destroy_token_table(tt);
        FAIL("remove by extension failed");
        return;
    }

    destroy_token_table(tt);
    PASS();
}

/* Test duplicate token */
static void test_duplicate_token(void) {
    TEST("duplicate token detection");
    TokenTable *tt = create_token_table(64);
    if (!tt) { FAIL("create failed"); return; }

    /* Add a token */
    if (!add_token(tt, "SELECT", 100, 0)) {
        destroy_token_table(tt);
        FAIL("first add failed");
        return;
    }

    /* Try to add the same token again - should fail */
    if (add_token(tt, "SELECT", 101, 0)) {
        destroy_token_table(tt);
        FAIL("duplicate should be rejected");
        return;
    }

    destroy_token_table(tt);
    PASS();
}

int main(void) {
    printf("\nToken Table Coverage Tests\n");
    printf("===========================\n\n");

    test_create_destroy();
    test_null_safety();
    test_add_lookup();
    test_case_insensitive();
    test_many_tokens();
    test_empty_string();
    test_remove_by_extension();
    test_duplicate_token();

    printf("\n===========================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_passed + tests_failed);
    printf("===========================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
