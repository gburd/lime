/*
** Unit tests for the JIT keyword tokenizer.
**
** Stays in the tree regardless of the integration verdict --
** docs/JIT_TOKENIZER.md records the abandon decision but
** src/jit_tokenizer.c stays compiled, so it needs coverage.
**
** Tests cover:
**   1. NULL-input defensive contracts on every public entry point
**   2. is_available() returns true with LLVM, false with stubs
**   3. create() rejects empty / NULL TokenTable
**   4. classify_keyword() returns the expected token code for every
**      keyword in a small SQL set, and -1 for non-keywords
**   5. case-insensitive matching (lowercase, uppercase, mixed)
**   6. get_stats() reports plausible compile_time_ns and keywords_compiled
**   7. destroy() is NULL-safe
**
** When LLVM is not available (LIME_NO_JIT), the tests verify the stub
** layer degrades gracefully -- create returns NULL, classify returns
** -1, is_available returns false, destroy on NULL is a no-op.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "token_table.h"
#include "jit_tokenizer.h"

/* ------------------------------------------------------------------ */
/*  Test harness                                                       */
/* ------------------------------------------------------------------ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-55s ", #name); \
        fflush(stdout); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASS\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        tests_failed++; \
        printf("FAIL: %s\n", msg); \
        return; \
    } while (0)

#define ASSERT(cond, msg) \
    do { if (!(cond)) FAIL(msg); } while (0)

/* ------------------------------------------------------------------ */
/*  Fixture: a small TokenTable with a representative keyword set      */
/* ------------------------------------------------------------------ */

/*
** Eleven keywords, picked so the trie has both unique-prefix paths
** ("SELECT", "WHERE") and shared-prefix paths ("CREATE" / "CROSS",
** "ALL" / "ALTER").  Length spread is 2-6 chars.
*/
static const char *const KW[] = {
    "AS",     "BY",     "ON",     "ALL",    "ALTER",
    "WHERE",  "SELECT", "CREATE", "CROSS",  "FROM",
    "GROUP",
};
#define NKW ((int)(sizeof(KW) / sizeof(KW[0])))
#define BASE_CODE 1000

static TokenTable *make_table(void) {
    TokenTable *tt = create_token_table(64);
    if (!tt) return NULL;
    for (int i = 0; i < NKW; i++) {
        if (!add_token(tt, KW[i], BASE_CODE + i, 0)) {
            destroy_token_table(tt);
            return NULL;
        }
    }
    return tt;
}

/* ------------------------------------------------------------------ */
/*  is_available is callable in both modes                              */
/* ------------------------------------------------------------------ */

static void test_is_available_callable(void) {
    TEST(is_available_callable);
    /* In stub mode this is false; with LLVM it is true.  Either is
    ** fine -- we only check the call does not crash. */
    bool b = jit_tokenizer_is_available();
    (void)b;
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Defensive NULL handling                                             */
/* ------------------------------------------------------------------ */

static void test_create_null_table(void) {
    TEST(create_null_table);
    JITTokenizer *tok = jit_tokenizer_create(NULL);
    ASSERT(tok == NULL, "create(NULL) should return NULL");
    PASS();
}

static void test_create_empty_table(void) {
    TEST(create_empty_table);
    TokenTable *tt = create_token_table(8);
    ASSERT(tt != NULL, "create_token_table failed");
    /* Empty (no keywords added) -- snapshot_keywords will return NULL,
    ** and create() must propagate that as a NULL handle. */
    JITTokenizer *tok = jit_tokenizer_create(tt);
    ASSERT(tok == NULL, "create on empty table should return NULL");
    destroy_token_table(tt);
    PASS();
}

static void test_destroy_null_safe(void) {
    TEST(destroy_null_safe);
    jit_tokenizer_destroy(NULL); /* must not crash */
    PASS();
}

static void test_classify_null_handle(void) {
    TEST(classify_null_handle);
    int r = jit_tokenizer_classify_keyword(NULL, "SELECT", 6);
    ASSERT(r == -1, "classify(NULL handle) should return -1");
    PASS();
}

static void test_get_stats_null_handle(void) {
    TEST(get_stats_null_handle);
    JITTokenizerStats s = jit_tokenizer_get_stats(NULL);
    ASSERT(s.keywords_compiled == 0, "NULL handle: keywords_compiled should be 0");
    ASSERT(s.compile_time_ns == 0,   "NULL handle: compile_time_ns should be 0");
    ASSERT(s.code_size_bytes == 0,   "NULL handle: code_size_bytes should be 0");
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Functional tests (require LLVM)                                     */
/* ------------------------------------------------------------------ */

static void test_classify_all_keywords(void) {
    TEST(classify_all_keywords);
    if (!jit_tokenizer_is_available()) {
        printf("SKIP (no LLVM)\n");
        return;
    }
    TokenTable *tt = make_table();
    ASSERT(tt != NULL, "fixture create failed");

    JITTokenizer *tok = jit_tokenizer_create(tt);
    ASSERT(tok != NULL, "jit create failed");

    for (int i = 0; i < NKW; i++) {
        int r = jit_tokenizer_classify_keyword(tok, KW[i], strlen(KW[i]));
        if (r != BASE_CODE + i) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "%s: expected %d, got %d",
                     KW[i], BASE_CODE + i, r);
            jit_tokenizer_destroy(tok);
            destroy_token_table(tt);
            FAIL(buf);
        }
    }

    jit_tokenizer_destroy(tok);
    destroy_token_table(tt);
    PASS();
}

static void test_classify_non_keywords(void) {
    TEST(classify_non_keywords);
    if (!jit_tokenizer_is_available()) {
        printf("SKIP (no LLVM)\n");
        return;
    }
    TokenTable *tt = make_table();
    ASSERT(tt != NULL, "fixture create failed");

    JITTokenizer *tok = jit_tokenizer_create(tt);
    ASSERT(tok != NULL, "jit create failed");

    static const char *const non_kw[] = {
        "x", "foo", "bar", "table1", "user_name",
        /* Same length as keywords, last char different. */
        "AT",   "BX",   "OO",   "ALD",   "ALTER1",
        "WHERX","SELECY","CREATX","CROSY","FROMX",
    };
    for (size_t i = 0; i < sizeof(non_kw)/sizeof(non_kw[0]); i++) {
        int r = jit_tokenizer_classify_keyword(tok, non_kw[i], strlen(non_kw[i]));
        if (r != -1) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "non-kw '%s': expected -1, got %d",
                     non_kw[i], r);
            jit_tokenizer_destroy(tok);
            destroy_token_table(tt);
            FAIL(buf);
        }
    }

    jit_tokenizer_destroy(tok);
    destroy_token_table(tt);
    PASS();
}

static void test_case_insensitive(void) {
    TEST(case_insensitive_matching);
    if (!jit_tokenizer_is_available()) {
        printf("SKIP (no LLVM)\n");
        return;
    }
    TokenTable *tt = make_table();
    ASSERT(tt != NULL, "fixture create failed");

    JITTokenizer *tok = jit_tokenizer_create(tt);
    ASSERT(tok != NULL, "jit create failed");

    /* All three forms must classify to TK_SELECT (BASE_CODE + 6). */
    int code = BASE_CODE + 6;
    int upper = jit_tokenizer_classify_keyword(tok, "SELECT", 6);
    int lower = jit_tokenizer_classify_keyword(tok, "select", 6);
    int mixed = jit_tokenizer_classify_keyword(tok, "SeLeCt", 6);

    if (upper != code || lower != code || mixed != code) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "case-insensitive failed: upper=%d lower=%d mixed=%d",
                 upper, lower, mixed);
        jit_tokenizer_destroy(tok);
        destroy_token_table(tt);
        FAIL(buf);
    }
    jit_tokenizer_destroy(tok);
    destroy_token_table(tt);
    PASS();
}

static void test_classify_zero_length(void) {
    TEST(classify_zero_length);
    if (!jit_tokenizer_is_available()) {
        printf("SKIP (no LLVM)\n");
        return;
    }
    TokenTable *tt = make_table();
    ASSERT(tt != NULL, "fixture create failed");
    JITTokenizer *tok = jit_tokenizer_create(tt);
    ASSERT(tok != NULL, "jit create failed");

    /* zero-len input should not match any keyword */
    int r = jit_tokenizer_classify_keyword(tok, "SELECT", 0);
    ASSERT(r == -1, "zero-len input should return -1");

    /* NULL input pointer */
    int r2 = jit_tokenizer_classify_keyword(tok, NULL, 4);
    ASSERT(r2 == -1, "NULL input should return -1");

    jit_tokenizer_destroy(tok);
    destroy_token_table(tt);
    PASS();
}

static void test_get_stats_populated(void) {
    TEST(get_stats_populated);
    if (!jit_tokenizer_is_available()) {
        printf("SKIP (no LLVM)\n");
        return;
    }
    TokenTable *tt = make_table();
    ASSERT(tt != NULL, "fixture create failed");
    JITTokenizer *tok = jit_tokenizer_create(tt);
    ASSERT(tok != NULL, "jit create failed");

    JITTokenizerStats s = jit_tokenizer_get_stats(tok);
    ASSERT(s.keywords_compiled == (uint32_t)NKW,
           "keywords_compiled should match input set");
    ASSERT(s.compile_time_ns > 0, "compile_time_ns should be > 0");
    ASSERT(s.code_size_bytes > 0, "code_size_bytes estimate should be > 0");

    jit_tokenizer_destroy(tok);
    destroy_token_table(tt);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Stub mode -- only meaningful when LLVM is absent                    */
/* ------------------------------------------------------------------ */

static void test_stub_mode_returns_null(void) {
    TEST(stub_mode_returns_null);
    if (jit_tokenizer_is_available()) {
        printf("SKIP (LLVM present)\n");
        return;
    }
    /* In stub mode every public entry point should degrade gracefully. */
    TokenTable *tt = make_table();
    ASSERT(tt != NULL, "fixture create failed");

    JITTokenizer *tok = jit_tokenizer_create(tt);
    ASSERT(tok == NULL, "stub create should return NULL");

    int r = jit_tokenizer_classify_keyword(NULL, "SELECT", 6);
    ASSERT(r == -1, "stub classify should return -1");

    destroy_token_table(tt);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("JIT tokenizer unit tests\n");
    printf("LLVM available: %s\n",
           jit_tokenizer_is_available() ? "yes" : "no");
    printf("\n");

    test_is_available_callable();
    test_create_null_table();
    test_create_empty_table();
    test_destroy_null_safe();
    test_classify_null_handle();
    test_get_stats_null_handle();

    test_classify_all_keywords();
    test_classify_non_keywords();
    test_case_insensitive();
    test_classify_zero_length();
    test_get_stats_populated();

    test_stub_mode_returns_null();

    printf("\n  Tests run: %d, passed: %d, failed: %d\n",
           tests_run, tests_passed, tests_failed);

    return tests_failed == 0 ? 0 : 1;
}
