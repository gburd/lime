/*
** Unit tests for parser state cloning (parser_fork.c).
**
** Since the real yyParser struct is generated per-grammar and not
** available to link against in isolation, these tests use a mock
** parser struct with the same field layout that clone_parser_state()
** expects.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_fork.h"
#include "snapshot.h"

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                                */
/* ------------------------------------------------------------------ */

static int test_count = 0;
static int fail_count = 0;

#define ASSERT(cond, msg) do { \
    test_count++; \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL [%d]: %s (%s:%d)\n", \
                test_count, msg, __FILE__, __LINE__); \
        fail_count++; \
    } \
} while(0)

/* ------------------------------------------------------------------ */
/*  Mock parser structures                                             */
/*                                                                     */
/*  These mirror the layout of yyParser and yyStackEntry as defined    */
/*  in limpar.c, with concrete types instead of generated typedefs.    */
/* ------------------------------------------------------------------ */

#define MOCK_STACK_DEPTH 8

typedef struct MockStackEntry {
    uint16_t stateno;
    uint16_t major;
    uint64_t minor;  /* Stand-in for YYMINORTYPE union */
} MockStackEntry;

typedef struct MockParser {
    MockStackEntry *yytos;
    int yyerrcnt;
    MockStackEntry *yystackEnd;
    MockStackEntry *yystack;
    MockStackEntry yystk0[MOCK_STACK_DEPTH];
} MockParser;

/*
** Layout constants for MockParser.
*/
#define MOCK_PARSER_SIZE       sizeof(MockParser)
#define MOCK_ENTRY_SIZE        sizeof(MockStackEntry)
#define MOCK_INLINE_OFFSET     offsetof(MockParser, yystk0)
#define MOCK_INLINE_COUNT      MOCK_STACK_DEPTH
#define MOCK_STACK_OFFSET      offsetof(MockParser, yystack)
#define MOCK_TOS_OFFSET        offsetof(MockParser, yytos)
#define MOCK_STACKEND_OFFSET   offsetof(MockParser, yystackEnd)

/*
** Initialize a MockParser with its inline stack.
*/
static void mock_parser_init(MockParser *p) {
    memset(p, 0, sizeof(*p));
    p->yystack = p->yystk0;
    p->yytos = &p->yystk0[0];
    p->yystackEnd = &p->yystk0[MOCK_STACK_DEPTH - 1];
    p->yyerrcnt = -1;

    /* Bottom-of-stack sentinel */
    p->yystk0[0].stateno = 0;
    p->yystk0[0].major = 0;
    p->yystk0[0].minor = 0;
}

/*
** Push a mock entry onto the parser stack.
*/
static void mock_parser_push(MockParser *p, uint16_t stateno,
                              uint16_t major, uint64_t minor) {
    p->yytos++;
    p->yytos->stateno = stateno;
    p->yytos->major = major;
    p->yytos->minor = minor;
}

/*
** Create a minimal mock snapshot for testing (refcount-based lifecycle).
*/
static ParserSnapshot *create_mock_snapshot(void) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (snap == NULL) return NULL;
    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    return snap;
}

/* ------------------------------------------------------------------ */
/*  Test: clone_parser_state with inline stack                         */
/* ------------------------------------------------------------------ */

static void test_clone_inline_stack(void) {
    printf("test_clone_inline_stack\n");

    MockParser parser;
    mock_parser_init(&parser);
    mock_parser_push(&parser, 5, 10, 0xDEADBEEF);
    mock_parser_push(&parser, 12, 20, 0xCAFEBABE);

    ClonedParserState cloned;
    bool ok = clone_parser_state(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        &cloned);

    ASSERT(ok, "clone_parser_state should succeed");
    ASSERT(cloned.state_data != NULL, "cloned state_data should be non-NULL");
    ASSERT(cloned.stack_data != NULL, "cloned stack_data should be non-NULL");
    ASSERT(cloned.state_size == MOCK_PARSER_SIZE, "state_size should match");
    ASSERT(cloned.stack_depth == 3, "stack_depth should be 3 (entries 0,1,2)");
    ASSERT(cloned.stack_capacity == MOCK_STACK_DEPTH,
           "stack_capacity should match inline count");
    ASSERT(!cloned.stack_is_inline,
           "cloned stack should always be heap-allocated");

    /* Verify stack contents were copied correctly */
    MockStackEntry *stack = (MockStackEntry *)cloned.stack_data;
    ASSERT(stack[0].stateno == 0, "stack[0].stateno should be 0");
    ASSERT(stack[1].stateno == 5, "stack[1].stateno should be 5");
    ASSERT(stack[1].major == 10, "stack[1].major should be 10");
    ASSERT(stack[1].minor == 0xDEADBEEF, "stack[1].minor should match");
    ASSERT(stack[2].stateno == 12, "stack[2].stateno should be 12");
    ASSERT(stack[2].major == 20, "stack[2].major should be 20");
    ASSERT(stack[2].minor == 0xCAFEBABE, "stack[2].minor should match");

    /* Verify pointer fixup in the cloned MockParser */
    MockParser *cp = (MockParser *)cloned.state_data;
    ASSERT(cp->yystack == stack, "cloned yystack should point to stack_data");
    ASSERT(cp->yytos == &stack[2], "cloned yytos should point to stack[2]");
    ASSERT(cp->yystackEnd == &stack[MOCK_STACK_DEPTH - 1],
           "cloned yystackEnd should be correct");

    /* Verify the clone is independent from the original */
    mock_parser_push(&parser, 99, 99, 99);
    ASSERT(stack[3].stateno != 99,
           "modifying original should not affect clone");

    cloned_parser_state_destroy(&cloned);
    ASSERT(cloned.state_data == NULL, "state_data should be NULL after destroy");
    ASSERT(cloned.stack_data == NULL, "stack_data should be NULL after destroy");
}

/* ------------------------------------------------------------------ */
/*  Test: clone_parser_state with heap-allocated stack                  */
/* ------------------------------------------------------------------ */

static void test_clone_heap_stack(void) {
    printf("test_clone_heap_stack\n");

    /*
    ** Simulate a parser whose stack has been grown to the heap.
    ** yystack points to a malloc'd buffer instead of yystk0.
    */
    MockParser parser;
    mock_parser_init(&parser);

    uint32_t heap_capacity = 32;
    MockStackEntry *heap_stack = calloc(heap_capacity, MOCK_ENTRY_SIZE);
    ASSERT(heap_stack != NULL, "heap_stack allocation should succeed");

    /* Copy initial stack content */
    memcpy(heap_stack, parser.yystk0, MOCK_ENTRY_SIZE);

    /* Point parser to heap stack */
    parser.yystack = heap_stack;
    parser.yytos = &heap_stack[0];
    parser.yystackEnd = &heap_stack[heap_capacity - 1];

    /* Push some entries */
    parser.yytos++;
    parser.yytos->stateno = 42;
    parser.yytos->major = 7;
    parser.yytos->minor = 0x12345678;

    ClonedParserState cloned;
    bool ok = clone_parser_state(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        &cloned);

    ASSERT(ok, "clone heap stack should succeed");
    ASSERT(cloned.stack_capacity == heap_capacity,
           "cloned capacity should match heap capacity");
    ASSERT(cloned.stack_depth == 2, "should have 2 entries");

    MockStackEntry *cstack = (MockStackEntry *)cloned.stack_data;
    ASSERT(cstack[1].stateno == 42, "heap clone stack[1].stateno should be 42");
    ASSERT(cstack[1].minor == 0x12345678, "heap clone stack[1].minor should match");

    /* The cloned stack should be a different allocation */
    ASSERT(cloned.stack_data != heap_stack,
           "cloned stack should be a different pointer");

    cloned_parser_state_destroy(&cloned);
    free(heap_stack);
}

/* ------------------------------------------------------------------ */
/*  Test: clone_parser_state NULL and edge cases                       */
/* ------------------------------------------------------------------ */

static void test_clone_null_cases(void) {
    printf("test_clone_null_cases\n");

    ClonedParserState cloned;
    memset(&cloned, 0, sizeof(cloned));

    /* NULL parser */
    bool ok = clone_parser_state(
        NULL, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        &cloned);
    ASSERT(!ok, "clone with NULL parser should fail");

    /* NULL output */
    MockParser parser;
    mock_parser_init(&parser);
    ok = clone_parser_state(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        NULL);
    ASSERT(!ok, "clone with NULL output should fail");

    /* Zero parser_size */
    ok = clone_parser_state(
        &parser, 0, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        &cloned);
    ASSERT(!ok, "clone with zero parser_size should fail");

    /* Zero stack_entry_size */
    ok = clone_parser_state(
        &parser, MOCK_PARSER_SIZE, 0,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        &cloned);
    ASSERT(!ok, "clone with zero stack_entry_size should fail");

    /* cloned_parser_state_destroy with NULL is safe */
    cloned_parser_state_destroy(NULL);
    /* Should not crash */
}

/* ------------------------------------------------------------------ */
/*  Test: fork_parser lifecycle                                        */
/* ------------------------------------------------------------------ */

static void test_fork_lifecycle(void) {
    printf("test_fork_lifecycle\n");

    MockParser parser;
    mock_parser_init(&parser);
    mock_parser_push(&parser, 3, 7, 100);

    ParserSnapshot *snap = create_mock_snapshot();
    ASSERT(snap != NULL, "mock snapshot should be created");

    ParseFork *fork = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 5);

    ASSERT(fork != NULL, "fork_parser should succeed");
    ASSERT(fork->priority == 5, "priority should be 5");
    ASSERT(fork->status == FORK_PENDING, "initial status should be PENDING");
    ASSERT(fork->tokens_consumed == 0, "initial tokens_consumed should be 0");
    ASSERT(fork->error_count == 0, "initial error_count should be 0");
    ASSERT(fork->semantic_result == NULL, "initial result should be NULL");
    ASSERT(fork->fork_id > 0, "fork_id should be positive");

    /* Snapshot should have an extra reference */
    ASSERT(fork->snapshot == snap, "snapshot should be stored");

    /* Get parser pointer */
    void *p = parse_fork_get_parser(fork);
    ASSERT(p != NULL, "get_parser should return non-NULL");
    ASSERT(p == fork->cloned_state.state_data, "should return state_data");

    /* Get snapshot */
    ParserSnapshot *s = parse_fork_get_snapshot(fork);
    ASSERT(s == snap, "get_snapshot should return the snapshot");

    free_parse_fork(fork);
    /* snap was acquired by fork, and released by free_parse_fork.
    ** Original reference still valid since we created it with refcount=1
    ** and fork acquired a second reference.  But free_parse_fork released
    ** one, so refcount should be back to 1. */
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Test: fork_parser with NULL parser                                 */
/* ------------------------------------------------------------------ */

static void test_fork_null_parser(void) {
    printf("test_fork_null_parser\n");

    ParseFork *fork = fork_parser(
        NULL, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        NULL, 0);

    ASSERT(fork == NULL, "fork_parser with NULL parser should return NULL");

    /* free_parse_fork(NULL) should be safe */
    free_parse_fork(NULL);
}

/* ------------------------------------------------------------------ */
/*  Test: fork status transitions                                      */
/* ------------------------------------------------------------------ */

static void test_fork_status_transitions(void) {
    printf("test_fork_status_transitions\n");

    MockParser parser;
    mock_parser_init(&parser);

    ParserSnapshot *snap = create_mock_snapshot();
    ParseFork *fork = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 1);
    ASSERT(fork != NULL, "fork should be created");

    /* PENDING -> RUNNING (set manually) */
    fork->status = FORK_RUNNING;
    ASSERT(fork->status == FORK_RUNNING, "status should be RUNNING");

    /* Complete with a result */
    int *result = malloc(sizeof(int));
    *result = 42;
    parse_fork_complete(fork, result, free);
    ASSERT(fork->status == FORK_COMPLETED, "status should be COMPLETED");
    ASSERT(fork->semantic_result == result, "result should be stored");

    /* free_parse_fork should call free() on the result */
    free_parse_fork(fork);
    snapshot_release(snap);

    /* Test fail transition */
    snap = create_mock_snapshot();
    fork = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 2);
    parse_fork_fail(fork);
    ASSERT(fork->status == FORK_FAILED, "status should be FAILED");
    free_parse_fork(fork);
    snapshot_release(snap);

    /* Test abandon transition */
    snap = create_mock_snapshot();
    fork = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 3);
    parse_fork_abandon(fork);
    ASSERT(fork->status == FORK_ABANDONED, "status should be ABANDONED");
    free_parse_fork(fork);
    snapshot_release(snap);

    /* NULL safety for status functions */
    parse_fork_complete(NULL, NULL, NULL);
    parse_fork_fail(NULL);
    parse_fork_abandon(NULL);
}

/* ------------------------------------------------------------------ */
/*  Test: fork with custom result destructor                           */
/* ------------------------------------------------------------------ */

static int custom_free_called = 0;
static void custom_free(void *p) {
    custom_free_called++;
    free(p);
}

static void test_fork_custom_destructor(void) {
    printf("test_fork_custom_destructor\n");

    MockParser parser;
    mock_parser_init(&parser);

    ParserSnapshot *snap = create_mock_snapshot();
    ParseFork *fork = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 1);

    char *result = malloc(64);
    strcpy(result, "test result");
    custom_free_called = 0;
    parse_fork_complete(fork, result, custom_free);

    free_parse_fork(fork);
    ASSERT(custom_free_called == 1, "custom destructor should be called once");
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Test: fork ID uniqueness and monotonicity                          */
/* ------------------------------------------------------------------ */

static void test_fork_id_generation(void) {
    printf("test_fork_id_generation\n");

    uint64_t id1 = parser_fork_next_id();
    uint64_t id2 = parser_fork_next_id();
    uint64_t id3 = parser_fork_next_id();

    ASSERT(id1 > 0, "first ID should be positive");
    ASSERT(id2 > id1, "IDs should be monotonically increasing");
    ASSERT(id3 > id2, "IDs should be monotonically increasing");

    /* Fork IDs assigned to actual forks */
    MockParser parser;
    mock_parser_init(&parser);

    ParserSnapshot *snap = create_mock_snapshot();
    ParseFork *f1 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 1);
    ParseFork *f2 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 2);

    ASSERT(f1->fork_id != f2->fork_id, "fork IDs should be unique");
    ASSERT(f2->fork_id > f1->fork_id, "later fork should have higher ID");

    free_parse_fork(f1);
    free_parse_fork(f2);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Test: parse_fork_get_parser / get_snapshot NULL safety              */
/* ------------------------------------------------------------------ */

static void test_fork_accessors_null(void) {
    printf("test_fork_accessors_null\n");

    ASSERT(parse_fork_get_parser(NULL) == NULL,
           "get_parser(NULL) should return NULL");
    ASSERT(parse_fork_get_snapshot(NULL) == NULL,
           "get_snapshot(NULL) should return NULL");
}

/* ------------------------------------------------------------------ */
/*  Test: ParseForkSet create and destroy                              */
/* ------------------------------------------------------------------ */

static void test_fork_set_lifecycle(void) {
    printf("test_fork_set_lifecycle\n");

    ParseForkSet *set = parse_fork_set_create(0); /* unlimited */
    ASSERT(set != NULL, "set should be created");
    ASSERT(set->count == 0, "new set should be empty");
    ASSERT(set->max_forks == 0, "max_forks should be 0 (unlimited)");

    parse_fork_set_destroy(set);

    /* With a limit */
    set = parse_fork_set_create(3);
    ASSERT(set != NULL, "limited set should be created");
    ASSERT(set->max_forks == 3, "max_forks should be 3");
    parse_fork_set_destroy(set);

    /* Destroy NULL is safe */
    parse_fork_set_destroy(NULL);
}

/* ------------------------------------------------------------------ */
/*  Test: ParseForkSet add and capacity enforcement                    */
/* ------------------------------------------------------------------ */

static void test_fork_set_add(void) {
    printf("test_fork_set_add\n");

    MockParser parser;
    mock_parser_init(&parser);
    ParserSnapshot *snap = create_mock_snapshot();

    ParseForkSet *set = parse_fork_set_create(2); /* max 2 forks */

    ParseFork *f1 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 1);
    ParseFork *f2 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 2);
    ParseFork *f3 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 3);

    ASSERT(parse_fork_set_add(set, f1), "add f1 should succeed");
    ASSERT(set->count == 1, "count should be 1");

    ASSERT(parse_fork_set_add(set, f2), "add f2 should succeed");
    ASSERT(set->count == 2, "count should be 2");

    ASSERT(!parse_fork_set_add(set, f3), "add f3 should fail (at capacity)");

    /* f3 was not added to the set, we must free it ourselves */
    free_parse_fork(f3);

    /* NULL args */
    ASSERT(!parse_fork_set_add(NULL, f1), "add to NULL set should fail");
    ASSERT(!parse_fork_set_add(set, NULL), "add NULL fork should fail");

    parse_fork_set_destroy(set); /* Frees f1 and f2 */
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Test: ParseForkSet prune                                           */
/* ------------------------------------------------------------------ */

static void test_fork_set_prune(void) {
    printf("test_fork_set_prune\n");

    MockParser parser;
    mock_parser_init(&parser);
    ParserSnapshot *snap = create_mock_snapshot();

    ParseForkSet *set = parse_fork_set_create(0);

    ParseFork *f1 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 1);
    ParseFork *f2 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 2);
    ParseFork *f3 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 3);

    parse_fork_set_add(set, f1);
    parse_fork_set_add(set, f2);
    parse_fork_set_add(set, f3);

    ASSERT(set->count == 3, "should have 3 forks");

    /* Mark f2 as failed, f3 as abandoned */
    parse_fork_fail(f2);
    parse_fork_abandon(f3);

    uint32_t pruned = parse_fork_set_prune(set);
    ASSERT(pruned == 2, "should prune 2 forks");
    ASSERT(set->count == 1, "should have 1 fork remaining");
    ASSERT(set->forks[0] == f1, "surviving fork should be f1");

    /* Prune empty set */
    ASSERT(parse_fork_set_prune(set) == 0, "prune with no dead forks returns 0");

    /* Prune NULL */
    ASSERT(parse_fork_set_prune(NULL) == 0, "prune NULL returns 0");

    parse_fork_set_destroy(set);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Test: ParseForkSet best selection                                  */
/* ------------------------------------------------------------------ */

static void test_fork_set_best(void) {
    printf("test_fork_set_best\n");

    MockParser parser;
    mock_parser_init(&parser);
    ParserSnapshot *snap = create_mock_snapshot();

    ParseForkSet *set = parse_fork_set_create(0);

    /* Create forks with different priorities */
    ParseFork *f_high = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 10);  /* Lower priority number = higher priority */
    ParseFork *f_low = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 50);
    ParseFork *f_mid = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 25);

    parse_fork_set_add(set, f_high);
    parse_fork_set_add(set, f_low);
    parse_fork_set_add(set, f_mid);

    /* No completed forks yet */
    ASSERT(parse_fork_set_best(set) == NULL,
           "best should be NULL when no forks are completed");

    /* Complete f_low and f_high */
    parse_fork_complete(f_low, NULL, NULL);
    parse_fork_complete(f_high, NULL, NULL);

    ParseFork *best = parse_fork_set_best(set);
    ASSERT(best == f_high,
           "best should be f_high (priority 10, lower wins)");

    /* NULL set */
    ASSERT(parse_fork_set_best(NULL) == NULL, "best of NULL should be NULL");

    parse_fork_set_destroy(set);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Test: ParseForkSet best with tie-breaking                          */
/* ------------------------------------------------------------------ */

static void test_fork_set_best_tiebreak(void) {
    printf("test_fork_set_best_tiebreak\n");

    MockParser parser;
    mock_parser_init(&parser);
    ParserSnapshot *snap = create_mock_snapshot();

    ParseForkSet *set = parse_fork_set_create(0);

    /* Same priority, different error counts */
    ParseFork *f1 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 5);
    ParseFork *f2 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 5);

    f1->error_count = 3;
    f2->error_count = 1;

    parse_fork_set_add(set, f1);
    parse_fork_set_add(set, f2);

    parse_fork_complete(f1, NULL, NULL);
    parse_fork_complete(f2, NULL, NULL);

    ParseFork *best = parse_fork_set_best(set);
    ASSERT(best == f2,
           "best should be f2 (fewer errors at same priority)");

    parse_fork_set_destroy(set);

    /* Same priority, same errors, different tokens consumed */
    set = parse_fork_set_create(0);

    ParseFork *fa = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 5);
    ParseFork *fb = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 5);

    fa->error_count = 0;
    fb->error_count = 0;
    fa->tokens_consumed = 10;
    fb->tokens_consumed = 5;

    parse_fork_set_add(set, fa);
    parse_fork_set_add(set, fb);

    parse_fork_complete(fa, NULL, NULL);
    parse_fork_complete(fb, NULL, NULL);

    best = parse_fork_set_best(set);
    ASSERT(best == fb,
           "best should be fb (fewer tokens at same priority/errors)");

    parse_fork_set_destroy(set);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Test: ParseForkSet active count                                    */
/* ------------------------------------------------------------------ */

static void test_fork_set_active_count(void) {
    printf("test_fork_set_active_count\n");

    MockParser parser;
    mock_parser_init(&parser);
    ParserSnapshot *snap = create_mock_snapshot();

    ParseForkSet *set = parse_fork_set_create(0);

    ParseFork *f1 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 1);
    ParseFork *f2 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 2);
    ParseFork *f3 = fork_parser(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        snap, 3);

    parse_fork_set_add(set, f1);
    parse_fork_set_add(set, f2);
    parse_fork_set_add(set, f3);

    /* All PENDING */
    ASSERT(parse_fork_set_active_count(set) == 3,
           "all 3 should be active (PENDING)");

    /* Mark one as RUNNING */
    f1->status = FORK_RUNNING;
    ASSERT(parse_fork_set_active_count(set) == 3,
           "RUNNING counts as active");

    /* Complete one, fail one */
    parse_fork_complete(f2, NULL, NULL);
    parse_fork_fail(f3);
    ASSERT(parse_fork_set_active_count(set) == 1,
           "only f1 (RUNNING) should be active");

    /* NULL */
    ASSERT(parse_fork_set_active_count(NULL) == 0,
           "active_count of NULL should be 0");

    parse_fork_set_destroy(set);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Test: ParseForkSet grow beyond initial capacity                    */
/* ------------------------------------------------------------------ */

static void test_fork_set_grow(void) {
    printf("test_fork_set_grow\n");

    MockParser parser;
    mock_parser_init(&parser);
    ParserSnapshot *snap = create_mock_snapshot();

    ParseForkSet *set = parse_fork_set_create(0); /* unlimited */

    /* Add more forks than the initial capacity (which is 4) */
    for (int i = 0; i < 10; i++) {
        ParseFork *f = fork_parser(
            &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
            MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
            MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
            snap, i);
        ASSERT(f != NULL, "fork should be created");
        ASSERT(parse_fork_set_add(set, f),
               "add should succeed (unlimited set)");
    }

    ASSERT(set->count == 10, "should have 10 forks");
    ASSERT(set->capacity >= 10, "capacity should have grown");

    parse_fork_set_destroy(set);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Test: cloned state independence                                    */
/* ------------------------------------------------------------------ */

static void test_clone_independence(void) {
    printf("test_clone_independence\n");

    MockParser parser;
    mock_parser_init(&parser);
    mock_parser_push(&parser, 1, 2, 3);

    ClonedParserState c1, c2;
    bool ok1 = clone_parser_state(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        &c1);
    bool ok2 = clone_parser_state(
        &parser, MOCK_PARSER_SIZE, MOCK_ENTRY_SIZE,
        MOCK_INLINE_OFFSET, MOCK_INLINE_COUNT,
        MOCK_STACK_OFFSET, MOCK_TOS_OFFSET, MOCK_STACKEND_OFFSET,
        &c2);

    ASSERT(ok1 && ok2, "both clones should succeed");
    ASSERT(c1.state_data != c2.state_data,
           "clones should have different state buffers");
    ASSERT(c1.stack_data != c2.stack_data,
           "clones should have different stack buffers");

    /* Modify one clone's stack */
    MockStackEntry *s1 = (MockStackEntry *)c1.stack_data;
    s1[1].minor = 999;

    MockStackEntry *s2 = (MockStackEntry *)c2.stack_data;
    ASSERT(s2[1].minor == 3,
           "modifying clone1 should not affect clone2");

    cloned_parser_state_destroy(&c1);
    cloned_parser_state_destroy(&c2);
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== test_parser_fork ===\n");

    test_clone_inline_stack();
    test_clone_heap_stack();
    test_clone_null_cases();
    test_fork_lifecycle();
    test_fork_null_parser();
    test_fork_status_transitions();
    test_fork_custom_destructor();
    test_fork_id_generation();
    test_fork_accessors_null();
    test_fork_set_lifecycle();
    test_fork_set_add();
    test_fork_set_prune();
    test_fork_set_best();
    test_fork_set_best_tiebreak();
    test_fork_set_active_count();
    test_fork_set_grow();
    test_clone_independence();

    printf("\n%d tests, %d failures\n", test_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
