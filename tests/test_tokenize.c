/*
** Unit tests for the SIMD-accelerated SQL tokenizer.
**
** Tests cover:
**   - Basic token extraction (identifiers, numbers, strings, operators)
**   - SIMD vs scalar correctness (identical results)
**   - Edge cases at SIMD chunk boundaries (32-byte alignment)
**   - SQL comments (single-line and block)
**   - Quoted identifiers (double-quote, backtick, bracket)
**   - Blob literals
**   - Line/column tracking
**   - Peek functionality
**   - Large input stress test
**   - Performance comparison (SIMD vs scalar classification)
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "tokenize.h"
#include "tokenize_simd.h"
#include "token_table.h"

/* ======================================================================
** Test infrastructure
** ====================================================================== */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-50s ", #name); \
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
    } while (0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            char _buf[256]; \
            snprintf(_buf, sizeof(_buf), "%s: expected %ld, got %ld", \
                     msg, (long)(b), (long)(a)); \
            FAIL(_buf); \
            return; \
        } \
    } while (0)

#define ASSERT_STREQN(s, expected, n, msg) \
    do { \
        if (memcmp((s), (expected), (n)) != 0) { \
            FAIL(msg); \
            return; \
        } \
    } while (0)

/*
** Create a padded input buffer. The tokenizer requires 32 bytes of
** readable memory past the input end for SIMD safety.  This allocates
** a buffer with sufficient padding (zeroed).
*/
static char *make_input(const char *sql, size_t *out_len) {
    size_t len = strlen(sql);
    size_t alloc = len + 64;  /* 64 bytes of padding */
    char *buf = calloc(1, alloc);
    if (!buf) abort();
    memcpy(buf, sql, len);
    *out_len = len;
    return buf;
}

/* ======================================================================
** Basic token extraction tests
** ====================================================================== */

static void test_simple_select(void) {
    TEST(simple_select);
    size_t len;
    char *input = make_input("SELECT * FROM foo;", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    int expected_types[] = {
        TK_IDENTIFIER, TK_STAR, TK_IDENTIFIER, TK_IDENTIFIER, TK_SEMICOLON
    };
    size_t expected_lens[] = { 6, 1, 4, 3, 1 };
    int n = 5;

    for (int i = 0; i < n; i++) {
        assert(tokenizer_next(tok, &t));
        if (t.type != expected_types[i] || t.length != expected_lens[i]) {
            char msg[128];
            snprintf(msg, sizeof(msg), "token %d: type=%d len=%zu", i, t.type, t.length);
            FAIL(msg);
            tokenizer_destroy(tok);
            free(input);
            return;
        }
    }
    assert(!tokenizer_next(tok, &t));
    assert(t.type == TK_EOF);

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_integer_literals(void) {
    TEST(integer_literals);
    size_t len;
    char *input = make_input("0 1 42 9999 0xFF 0X1A", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    int expected_lens[] = { 1, 1, 2, 4, 4, 4 };
    for (int i = 0; i < 6; i++) {
        assert(tokenizer_next(tok, &t));
        ASSERT_EQ(t.type, TK_INTEGER, "type should be TK_INTEGER");
        ASSERT_EQ((int)t.length, expected_lens[i], "integer length");
    }

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_float_literals(void) {
    TEST(float_literals);
    size_t len;
    char *input = make_input("3.14 .5 1e10 2.5e-3 0.0E+0", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    int count = 0;
    while (tokenizer_next(tok, &t)) {
        ASSERT_EQ(t.type, TK_FLOAT, "type should be TK_FLOAT");
        count++;
    }
    ASSERT_EQ(count, 5, "should have 5 float tokens");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_string_literals(void) {
    TEST(string_literals);
    size_t len;
    char *input = make_input("'hello' 'it''s' ''", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_STRING, "first string type");
    ASSERT_EQ((int)t.length, 7, "first string length");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_STRING, "escaped string type");
    ASSERT_EQ((int)t.length, 7, "escaped string length");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_STRING, "empty string type");
    ASSERT_EQ((int)t.length, 2, "empty string length");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_blob_literals(void) {
    TEST(blob_literals);
    size_t len;
    char *input = make_input("X'48656C6C6F' x'AB'", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_BLOB, "X blob type");
    ASSERT_EQ((int)t.length, 13, "X blob length");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_BLOB, "x blob type");
    ASSERT_EQ((int)t.length, 5, "x blob length");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_all_operators(void) {
    TEST(all_operators);
    size_t len;
    char *input = make_input("( ) ; , . * + - / % = == != <> < > <= >= << >> & | || ~", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    int expected[] = {
        TK_LPAREN, TK_RPAREN, TK_SEMICOLON, TK_COMMA, TK_DOT,
        TK_STAR, TK_PLUS, TK_MINUS, TK_SLASH, TK_PERCENT,
        TK_EQ, TK_EQ, TK_NE, TK_NE, TK_LT, TK_GT,
        TK_LE, TK_GE, TK_LSHIFT, TK_RSHIFT,
        TK_BITAND, TK_BITOR, TK_CONCAT, TK_BITNOT
    };
    int n = (int)(sizeof(expected) / sizeof(expected[0]));

    Token t;
    for (int i = 0; i < n; i++) {
        assert(tokenizer_next(tok, &t));
        if (t.type != expected[i]) {
            char msg[128];
            snprintf(msg, sizeof(msg), "operator %d: expected %d, got %d", i, expected[i], t.type);
            FAIL(msg);
            tokenizer_destroy(tok);
            free(input);
            return;
        }
    }

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_quoted_identifiers(void) {
    TEST(quoted_identifiers);
    size_t len;
    char *input = make_input("\"col name\" `tbl` [idx]", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_DQUOTE_ID, "double-quote id type");
    ASSERT_EQ((int)t.length, 10, "double-quote id length");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_BACKTICK_ID, "backtick id type");
    ASSERT_EQ((int)t.length, 5, "backtick id length");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_BRACKET_ID, "bracket id type");
    ASSERT_EQ((int)t.length, 5, "bracket id length");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

/* ======================================================================
** Comment handling tests
** ====================================================================== */

static void test_line_comment(void) {
    TEST(line_comment);
    size_t len;
    char *input = make_input("a -- comment\nb", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "before comment");
    assert(*t.start == 'a');

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "after comment");
    assert(*t.start == 'b');
    ASSERT_EQ(t.line, 2, "line after comment");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_block_comment(void) {
    TEST(block_comment);
    size_t len;
    char *input = make_input("a /* multi\nline\ncomment */ b", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    assert(*t.start == 'a');

    assert(tokenizer_next(tok, &t));
    assert(*t.start == 'b');
    ASSERT_EQ(t.line, 3, "line after block comment");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

/* ======================================================================
** Line/column tracking
** ====================================================================== */

static void test_line_column_tracking(void) {
    TEST(line_column_tracking);
    size_t len;
    char *input = make_input("a b\n  c\n\nd", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t)); /* a */
    ASSERT_EQ(t.line, 1, "a line");
    ASSERT_EQ(t.column, 1, "a column");

    assert(tokenizer_next(tok, &t)); /* b */
    ASSERT_EQ(t.line, 1, "b line");
    ASSERT_EQ(t.column, 3, "b column");

    assert(tokenizer_next(tok, &t)); /* c */
    ASSERT_EQ(t.line, 2, "c line");
    ASSERT_EQ(t.column, 3, "c column");

    assert(tokenizer_next(tok, &t)); /* d */
    ASSERT_EQ(t.line, 4, "d line");
    ASSERT_EQ(t.column, 1, "d column");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

/* ======================================================================
** Peek functionality
** ====================================================================== */

static void test_peek(void) {
    TEST(peek);
    size_t len;
    char *input = make_input("a b c", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t1, t2;
    /* Peek returns 'a' */
    assert(tokenizer_peek(tok, &t1));
    assert(*t1.start == 'a');

    /* Peek again returns same token */
    assert(tokenizer_peek(tok, &t2));
    assert(t2.start == t1.start);

    /* Next consumes 'a' */
    assert(tokenizer_next(tok, &t1));
    assert(*t1.start == 'a');

    /* Next gives 'b' */
    assert(tokenizer_next(tok, &t1));
    assert(*t1.start == 'b');

    /* Peek gives 'c' */
    assert(tokenizer_peek(tok, &t1));
    assert(*t1.start == 'c');

    /* Next consumes 'c' */
    assert(tokenizer_next(tok, &t1));
    assert(*t1.start == 'c');

    /* EOF */
    assert(!tokenizer_next(tok, &t1));

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

/* ======================================================================
** SIMD chunk boundary edge cases
** ====================================================================== */

static void test_identifier_at_chunk_boundary(void) {
    TEST(identifier_at_chunk_boundary);
    /*
    ** Create an identifier that spans a 32-byte SIMD chunk boundary.
    ** Place 30 spaces followed by an identifier that starts at byte 30
    ** and extends past byte 32.
    */
    char sql[128];
    memset(sql, ' ', sizeof(sql));
    /* Identifier at position 30, length 5 = "hello" */
    memcpy(sql + 30, "hello", 5);
    sql[35] = '\0';

    size_t len;
    char *input = make_input(sql, &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "type at boundary");
    ASSERT_EQ((int)t.length, 5, "length at boundary");
    ASSERT_STREQN(t.start, "hello", 5, "text at boundary");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_long_identifier(void) {
    TEST(long_identifier);
    /* Identifier longer than 32 bytes -- tests SIMD continuation loop */
    char sql[128];
    memset(sql, 0, sizeof(sql));
    for (int i = 0; i < 70; i++) {
        sql[i] = 'a' + (i % 26);
    }
    sql[70] = '\0';

    size_t len;
    char *input = make_input(sql, &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "long id type");
    ASSERT_EQ((int)t.length, 70, "long id length");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_whitespace_exactly_32(void) {
    TEST(whitespace_exactly_32);
    /* Exactly 32 spaces followed by a token */
    char sql[128];
    memset(sql, ' ', 32);
    sql[32] = 'x';
    sql[33] = '\0';

    size_t len;
    char *input = make_input(sql, &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "token after 32 spaces");
    assert(*t.start == 'x');

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_whitespace_exactly_64(void) {
    TEST(whitespace_exactly_64);
    /* 64 spaces (two full SIMD chunks) followed by a token */
    char sql[128];
    memset(sql, ' ', 64);
    sql[64] = 'y';
    sql[65] = '\0';

    size_t len;
    char *input = make_input(sql, &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "token after 64 spaces");
    assert(*t.start == 'y');

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

/* ======================================================================
** SIMD vs Scalar correctness comparison
** ====================================================================== */

static void test_simd_scalar_classify_agreement(void) {
    TEST(simd_scalar_classify_agreement);
    /*
    ** Verify that SIMD classification agrees with scalar for a variety
    ** of character patterns.
    */
    const char *patterns[] = {
        "abcdefghijklmnopqrstuvwxyz012345",  /* 32 mixed */
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ      ",  /* upper + spaces */
        "0123456789 \t\n\rabcABC!@#$%^&*()",  /* everything */
        "________________________________",   /* all underscore */
        "                                ",   /* all space */
        "00000000000000000000000000000000",   /* all digit */
        "!@#$%^&*(){}[]<>?/|\\~`-=+;:'\",.",  /* all punctuation */
    };
    int n = (int)(sizeof(patterns) / sizeof(patterns[0]));

    ClassifyFunc best = get_classify_func();

    for (int i = 0; i < n; i++) {
        /* Ensure 32 bytes are readable */
        char buf[64];
        memset(buf, 0, sizeof(buf));
        size_t plen = strlen(patterns[i]);
        if (plen > 32) plen = 32;
        memcpy(buf, patterns[i], plen);

        CharClassVector scalar = classify_scalar(buf, 0);
        CharClassVector simd = best(buf, 0);

        if (scalar.is_alpha_mask != simd.is_alpha_mask ||
            scalar.is_digit_mask != simd.is_digit_mask ||
            scalar.is_space_mask != simd.is_space_mask) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "pattern %d mismatch: alpha(s=%08X,d=%08X) "
                     "digit(s=%08X,d=%08X) space(s=%08X,d=%08X)",
                     i,
                     scalar.is_alpha_mask, simd.is_alpha_mask,
                     scalar.is_digit_mask, simd.is_digit_mask,
                     scalar.is_space_mask, simd.is_space_mask);
            FAIL(msg);
            return;
        }
    }

    PASS();
}

/* ======================================================================
** Keyword lookup integration
** ====================================================================== */

static void test_keyword_lookup(void) {
    TEST(keyword_lookup);

    TokenTable *table = create_token_table(64);
    if (!table) {
        FAIL("failed to create token table");
        return;
    }

    /* Add some keywords */
    add_token(table, "SELECT", 100, 0);
    add_token(table, "FROM", 101, 0);
    add_token(table, "WHERE", 102, 0);

    size_t len;
    char *input = make_input("SELECT col FROM tbl WHERE x = 1", &len);
    Tokenizer *tok = tokenizer_create(table, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, 100, "SELECT keyword code");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "col is identifier");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, 101, "FROM keyword code");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "tbl is identifier");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, 102, "WHERE keyword code");

    tokenizer_destroy(tok);
    destroy_token_table(table);
    free(input);
    PASS();
}

/* ======================================================================
** Large input stress test
** ====================================================================== */

static void test_large_input(void) {
    TEST(large_input_1000_statements);
    /*
    ** Generate ~1000 simple SELECT statements and tokenize them all.
    ** This exercises SIMD paths on longer input.
    */
    size_t buf_size = 1024 * 64;
    char *sql = calloc(1, buf_size + 64);
    if (!sql) { FAIL("alloc"); return; }

    size_t pos = 0;
    int stmt_count = 0;
    while (pos + 64 < buf_size && stmt_count < 1000) {
        int n = snprintf(sql + pos, buf_size - pos,
                         "SELECT col%d FROM tbl%d WHERE id = %d;\n",
                         stmt_count, stmt_count, stmt_count * 7);
        if (n <= 0) break;
        pos += (size_t)n;
        stmt_count++;
    }

    Tokenizer *tok = tokenizer_create(NULL, sql, pos);
    if (!tok) { FAIL("create"); free(sql); return; }

    Token t;
    int token_count = 0;
    while (tokenizer_next(tok, &t)) {
        token_count++;
    }

    /* Each statement has: SELECT col FROM tbl WHERE id = num ; = 9 tokens */
    int expected_tokens = stmt_count * 9;
    if (token_count != expected_tokens) {
        char msg[128];
        snprintf(msg, sizeof(msg), "expected %d tokens, got %d", expected_tokens, token_count);
        FAIL(msg);
    } else {
        PASS();
    }

    tokenizer_destroy(tok);
    free(sql);
}

/* ======================================================================
** Performance measurement (informational, not pass/fail)
** ====================================================================== */

static void test_classify_performance(void) {
    TEST(classify_performance);
    /*
    ** Measure classification throughput for scalar vs best-available.
    ** This is informational -- it always passes.
    */
    char buf[4096];
    memset(buf, 0, sizeof(buf));
    /* Fill with mixed content */
    const char *pattern = "SELECT col_name FROM table1 WHERE id = 42 AND val > 3.14;\n";
    size_t plen = strlen(pattern);
    for (size_t i = 0; i < sizeof(buf) - 64; i += plen) {
        memcpy(buf + i, pattern, plen);
    }

    ClassifyFunc best = get_classify_func();
    int iterations = 100000;

    /* Scalar benchmark */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        for (size_t off = 0; off + 32 <= 4000; off += 32) {
            volatile CharClassVector r = classify_scalar(buf, off);
            (void)r;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double scalar_ns = (double)(end.tv_sec - start.tv_sec) * 1e9 +
                       (double)(end.tv_nsec - start.tv_nsec);

    /* Best-available benchmark */
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        for (size_t off = 0; off + 32 <= 4000; off += 32) {
            volatile CharClassVector r = best(buf, off);
            (void)r;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double best_ns = (double)(end.tv_sec - start.tv_sec) * 1e9 +
                     (double)(end.tv_nsec - start.tv_nsec);

    double speedup = scalar_ns / best_ns;
    printf("PASS (scalar=%.1fms best=%.1fms speedup=%.2fx)\n",
           scalar_ns / 1e6, best_ns / 1e6, speedup);
    tests_passed++;
}

static void test_tokenizer_performance(void) {
    TEST(tokenizer_throughput);
    /*
    ** Measure tokenizer throughput on a realistic SQL workload.
    */
    size_t buf_size = 256 * 1024;
    char *sql = calloc(1, buf_size + 64);
    if (!sql) { FAIL("alloc"); return; }

    size_t pos = 0;
    int stmt = 0;
    const char *template_sql =
        "SELECT a.id, b.name, c.value "
        "FROM alpha a "
        "JOIN beta b ON a.id = b.alpha_id "
        "JOIN gamma c ON b.id = c.beta_id "
        "WHERE a.status = 'active' AND b.count > 100;\n";
    size_t tlen = strlen(template_sql);

    while (pos + tlen + 64 < buf_size) {
        memcpy(sql + pos, template_sql, tlen);
        pos += tlen;
        stmt++;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int total_tokens = 0;
    for (int rep = 0; rep < 100; rep++) {
        Tokenizer *tok = tokenizer_create(NULL, sql, pos);
        Token t;
        while (tokenizer_next(tok, &t)) {
            total_tokens++;
        }
        tokenizer_destroy(tok);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed_ms = (double)(end.tv_sec - start.tv_sec) * 1e3 +
                        (double)(end.tv_nsec - start.tv_nsec) / 1e6;
    double mb_per_sec = (double)(pos * 100) / (1024.0 * 1024.0) / (elapsed_ms / 1e3);

    printf("PASS (%d tokens, %.1f MB/s, %.1fms)\n",
           total_tokens, mb_per_sec, elapsed_ms);
    tests_passed++;

    free(sql);
}

/* ======================================================================
** Empty and edge-case inputs
** ====================================================================== */

static void test_empty_input(void) {
    TEST(empty_input);
    size_t len;
    char *input = make_input("", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(!tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_EOF, "empty input is EOF");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_whitespace_only(void) {
    TEST(whitespace_only);
    size_t len;
    char *input = make_input("   \t\n\n  \t  ", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(!tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_EOF, "whitespace-only is EOF");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_single_char_tokens(void) {
    TEST(single_char_tokens);
    size_t len;
    char *input = make_input("a 1 +", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "single char id");
    ASSERT_EQ((int)t.length, 1, "single char id len");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_INTEGER, "single digit");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_PLUS, "single op");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_illegal_character(void) {
    TEST(illegal_character);
    size_t len;
    char *input = make_input("a # b", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "before illegal");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_ILLEGAL, "illegal char");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "after illegal");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

static void test_position_query(void) {
    TEST(position_query);
    size_t len;
    char *input = make_input("abc def", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    ASSERT_EQ(tokenizer_position(tok), 0, "initial pos");
    ASSERT_EQ(tokenizer_line(tok), 1, "initial line");
    ASSERT_EQ(tokenizer_column(tok), 1, "initial column");

    Token t;
    tokenizer_next(tok, &t);  /* abc */
    ASSERT_EQ(tokenizer_position(tok), 3, "pos after abc");

    tokenizer_next(tok, &t);  /* def */
    ASSERT_EQ(tokenizer_position(tok), 7, "pos after def");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

/* ======================================================================
** X as identifier (not blob if not followed by quote)
** ====================================================================== */

static void test_x_as_identifier(void) {
    TEST(x_as_identifier);
    size_t len;
    /* "x" alone and "X123" should be identifiers, not blobs */
    char *input = make_input("x X123", &len);
    Tokenizer *tok = tokenizer_create(NULL, input, len);

    Token t;
    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "x is identifier");
    ASSERT_EQ((int)t.length, 1, "x length");

    assert(tokenizer_next(tok, &t));
    ASSERT_EQ(t.type, TK_IDENTIFIER, "X123 is identifier");
    ASSERT_EQ((int)t.length, 4, "X123 length");

    tokenizer_destroy(tok);
    free(input);
    PASS();
}

/* ======================================================================
** Main
** ====================================================================== */

int main(void) {
    printf("SIMD Tokenizer Tests\n");
    printf("====================\n\n");

    printf("Basic token extraction:\n");
    test_simple_select();
    test_integer_literals();
    test_float_literals();
    test_string_literals();
    test_blob_literals();
    test_all_operators();
    test_quoted_identifiers();

    printf("\nComment handling:\n");
    test_line_comment();
    test_block_comment();

    printf("\nPosition tracking:\n");
    test_line_column_tracking();
    test_position_query();

    printf("\nPeek:\n");
    test_peek();

    printf("\nSIMD chunk boundary edge cases:\n");
    test_identifier_at_chunk_boundary();
    test_long_identifier();
    test_whitespace_exactly_32();
    test_whitespace_exactly_64();

    printf("\nSIMD vs scalar correctness:\n");
    test_simd_scalar_classify_agreement();

    printf("\nKeyword lookup integration:\n");
    test_keyword_lookup();

    printf("\nEdge cases:\n");
    test_empty_input();
    test_whitespace_only();
    test_single_char_tokens();
    test_illegal_character();
    test_x_as_identifier();

    printf("\nStress tests:\n");
    test_large_input();

    printf("\nPerformance (informational):\n");
    test_classify_performance();
    test_tokenizer_performance();

    printf("\n====================\n");
    printf("Results: %d/%d passed, %d failed\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
