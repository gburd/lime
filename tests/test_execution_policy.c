/*
** Unit tests for the execution policy engine.
**
** Tests cover:
**   - ExecutionPolicyConfig init and defaults
**   - execution_policy_name() for all policies
**   - EXEC_FIRST_ONLY: single winner, multiple winners, error case
**   - EXEC_ALL: multiple winners, stop_on_error, max_executions
**   - EXEC_CHAIN: output chaining, error propagation
**   - EXEC_CONDITIONAL: should_execute callback filtering
**   - execute_first_only() convenience wrapper
**   - execution_results_free() and NULL safety
**   - Edge cases: NULL args, zero winners, no callback
*/
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "execution_policy.h"
#include "disambiguation.h"
#include "conflict.h"

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
/*  Mock parser and execute callback                                   */
/* ------------------------------------------------------------------ */

/*
** We use simple integers as "parser output" and "parser handles".
** The mock execute function copies the extension_id into an int as
** the "result", or returns an error if the parser handle is tagged.
*/

/* Fake parser handles -- just non-NULL pointers */
static int parser_slots[8];

static LimeParserHandle *mock_parser(int index) {
    return (LimeParserHandle *)&parser_slots[index];
}

/* Track how many times execute was called and in what order */
static int execute_call_count;
static uint32_t execute_order[16];
static void *execute_chain_inputs[16];

/*
** Mock execute: stores call info, returns extension_id as result.
** If parser_slots[i] == -1, simulate a failure.
*/
static bool mock_execute(LimeParserHandle *parser,
                         const GrammarExtensionMetadata *ext,
                         void *input,
                         void **result,
                         char **error) {
    int idx = execute_call_count;
    if (idx < 16) {
        execute_order[idx] = ext->extension_id;
        execute_chain_inputs[idx] = input;
    }
    execute_call_count++;

    int *slot = (int *)parser;
    if (*slot == -1) {
        *error = strdup("mock error");
        *result = NULL;
        return false;
    }

    /* Return the extension_id cast to a pointer as the "result" */
    int *out = malloc(sizeof(int));
    *out = (int)ext->extension_id;
    *result = out;
    *error = NULL;
    return true;
}

static void reset_mock(void) {
    execute_call_count = 0;
    memset(execute_order, 0, sizeof(execute_order));
    memset(execute_chain_inputs, 0, sizeof(execute_chain_inputs));
    for (int i = 0; i < 8; i++) parser_slots[i] = 0;
}

/* ------------------------------------------------------------------ */
/*  Helper: build a StrategyResult with N winners                      */
/* ------------------------------------------------------------------ */

static StrategyResult make_strategy_result(int nwinners, uint32_t *ext_ids) {
    StrategyResult sr;
    strategy_result_init(&sr);
    sr.nwinners = nwinners;
    if (nwinners > 0) {
        sr.winning_contexts = calloc((size_t)nwinners, sizeof(LimeContext));
        for (int i = 0; i < nwinners; i++) {
            sr.winning_contexts[i].ext_id = ext_ids[i];
            sr.winning_contexts[i].priority = nwinners - i;  /* descending */
        }
    }
    sr.confidence = 0.95f;
    return sr;
}

/* ------------------------------------------------------------------ */
/*  Helper: build extension metadata array                             */
/* ------------------------------------------------------------------ */

static GrammarExtensionMetadata *make_ext_array(int n, uint32_t *ext_ids) {
    GrammarExtensionMetadata *arr = calloc((size_t)n,
                                            sizeof(GrammarExtensionMetadata));
    for (int i = 0; i < n; i++) {
        arr[i].extension_id = ext_ids[i];
        arr[i].name = "mock-ext";
        arr[i].priority = (uint32_t)(n - i);
        arr[i].should_execute = NULL;
    }
    return arr;
}

/* ------------------------------------------------------------------ */
/*  Test: config init and defaults                                     */
/* ------------------------------------------------------------------ */

static void test_config_init(void) {
    printf("test_config_init\n");

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);

    ASSERT(config.policy == EXEC_FIRST_ONLY, "default policy FIRST_ONLY");
    ASSERT(config.execute_fn == NULL, "default execute_fn NULL");
    ASSERT(config.stop_on_error == true, "default stop_on_error true");
    ASSERT(config.max_executions == 0, "default max_executions 0");

    /* NULL should not crash */
    execution_policy_config_init(NULL);
    ASSERT(true, "config_init(NULL) safe");
}

/* ------------------------------------------------------------------ */
/*  Test: execution_policy_name                                        */
/* ------------------------------------------------------------------ */

static void test_policy_name(void) {
    printf("test_policy_name\n");

    const char *n1 = execution_policy_name(EXEC_FIRST_ONLY);
    ASSERT(n1 != NULL && strcmp(n1, "first_only") == 0, "FIRST_ONLY name");

    const char *n2 = execution_policy_name(EXEC_ALL);
    ASSERT(n2 != NULL && strcmp(n2, "all") == 0, "ALL name");

    const char *n3 = execution_policy_name(EXEC_CHAIN);
    ASSERT(n3 != NULL && strcmp(n3, "chain") == 0, "CHAIN name");

    const char *n4 = execution_policy_name(EXEC_CONDITIONAL);
    ASSERT(n4 != NULL && strcmp(n4, "conditional") == 0, "CONDITIONAL name");

    const char *n5 = execution_policy_name((LimeExecMode)99);
    ASSERT(n5 != NULL && strcmp(n5, "unknown") == 0, "unknown policy name");
}

/* ------------------------------------------------------------------ */
/*  Test: NULL args return NULL / 0                                    */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    printf("test_null_safety\n");

    int n = -1;

    /* NULL config */
    ExecutionResult *r = execute_semantic_actions(NULL, NULL, NULL, NULL, &n);
    ASSERT(r == NULL, "NULL config returns NULL");
    ASSERT(n == 0, "nresults 0 on NULL config");

    /* NULL nresults_out */
    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    r = execute_semantic_actions(&config, NULL, NULL, NULL, NULL);
    ASSERT(r == NULL, "NULL nresults_out returns NULL");

    /* NULL disambiguation */
    r = execute_semantic_actions(&config, NULL, NULL, NULL, &n);
    ASSERT(r == NULL && n == 0, "NULL disambiguation");

    /* execution_results_free(NULL) should not crash */
    execution_results_free(NULL, 0);
    execution_results_free(NULL, 5);
    ASSERT(true, "results_free(NULL) safe");
}

/* ------------------------------------------------------------------ */
/*  Test: zero winners returns NULL                                    */
/* ------------------------------------------------------------------ */

static void test_zero_winners(void) {
    printf("test_zero_winners\n");

    reset_mock();

    StrategyResult sr;
    strategy_result_init(&sr);
    /* nwinners == 0 */

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.execute_fn = mock_execute;

    int n = -1;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, NULL, NULL, &n);
    ASSERT(r == NULL, "zero winners returns NULL");
    ASSERT(n == 0, "nresults 0 for zero winners");
    ASSERT(execute_call_count == 0, "no executions for zero winners");

    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: no execute callback returns error result                     */
/* ------------------------------------------------------------------ */

static void test_no_execute_callback(void) {
    printf("test_no_execute_callback\n");

    reset_mock();

    uint32_t ids[] = {10};
    StrategyResult sr = make_strategy_result(1, ids);

    LimeParserHandle *parsers[] = { mock_parser(0) };
    GrammarExtensionMetadata *exts = make_ext_array(1, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    /* config.execute_fn remains NULL */

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(r != NULL, "should return error result");
    ASSERT(n == 1, "1 result");
    ASSERT(r[0].error != NULL, "error should be set");
    ASSERT(r[0].result == NULL, "result should be NULL on error");

    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_FIRST_ONLY with single winner                           */
/* ------------------------------------------------------------------ */

static void test_first_only_single(void) {
    printf("test_first_only_single\n");

    reset_mock();

    uint32_t ids[] = {42};
    StrategyResult sr = make_strategy_result(1, ids);

    LimeParserHandle *parsers[] = { mock_parser(0) };
    GrammarExtensionMetadata *exts = make_ext_array(1, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.execute_fn = mock_execute;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(r != NULL, "result not NULL");
    ASSERT(n == 1, "exactly 1 result");
    ASSERT(r[0].extension_id == 42, "correct extension_id");
    ASSERT(r[0].error == NULL, "no error");
    ASSERT(r[0].result != NULL, "result present");
    ASSERT(*(int *)r[0].result == 42, "result value matches ext_id");
    ASSERT(execute_call_count == 1, "executed exactly once");

    free(r[0].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_FIRST_ONLY with multiple winners (only first runs)      */
/* ------------------------------------------------------------------ */

static void test_first_only_multiple(void) {
    printf("test_first_only_multiple\n");

    reset_mock();

    uint32_t ids[] = {10, 20, 30};
    StrategyResult sr = make_strategy_result(3, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1), mock_parser(2) };
    GrammarExtensionMetadata *exts = make_ext_array(3, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1], &exts[2] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.execute_fn = mock_execute;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 1, "FIRST_ONLY: exactly 1 result");
    ASSERT(r[0].extension_id == 10, "first winner executed");
    ASSERT(execute_call_count == 1, "only 1 execution");

    free(r[0].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_FIRST_ONLY with error                                   */
/* ------------------------------------------------------------------ */

static void test_first_only_error(void) {
    printf("test_first_only_error\n");

    reset_mock();
    parser_slots[0] = -1;  /* Trigger mock error */

    uint32_t ids[] = {5};
    StrategyResult sr = make_strategy_result(1, ids);

    LimeParserHandle *parsers[] = { mock_parser(0) };
    GrammarExtensionMetadata *exts = make_ext_array(1, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.execute_fn = mock_execute;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 1, "1 result even on error");
    ASSERT(r[0].error != NULL, "error string set");
    ASSERT(r[0].result == NULL, "no result on error");

    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_ALL runs all winners                                    */
/* ------------------------------------------------------------------ */

static void test_all_basic(void) {
    printf("test_all_basic\n");

    reset_mock();

    uint32_t ids[] = {1, 2, 3};
    StrategyResult sr = make_strategy_result(3, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1), mock_parser(2) };
    GrammarExtensionMetadata *exts = make_ext_array(3, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1], &exts[2] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_ALL;
    config.execute_fn = mock_execute;
    config.stop_on_error = false;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 3, "ALL: 3 results");
    ASSERT(execute_call_count == 3, "3 executions");

    for (int i = 0; i < n; i++) {
        ASSERT(r[i].error == NULL, "no errors");
        ASSERT(r[i].extension_id == ids[i], "correct ext_id");
        free(r[i].result);
    }

    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_ALL with stop_on_error                                  */
/* ------------------------------------------------------------------ */

static void test_all_stop_on_error(void) {
    printf("test_all_stop_on_error\n");

    reset_mock();
    parser_slots[1] = -1;  /* Second parser fails */

    uint32_t ids[] = {1, 2, 3};
    StrategyResult sr = make_strategy_result(3, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1), mock_parser(2) };
    GrammarExtensionMetadata *exts = make_ext_array(3, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1], &exts[2] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_ALL;
    config.execute_fn = mock_execute;
    config.stop_on_error = true;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 2, "stopped after error: 2 results");
    ASSERT(execute_call_count == 2, "2 executions before stop");
    ASSERT(r[0].error == NULL, "first succeeded");
    ASSERT(r[1].error != NULL, "second failed");

    free(r[0].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_ALL without stop_on_error continues past errors         */
/* ------------------------------------------------------------------ */

static void test_all_continue_on_error(void) {
    printf("test_all_continue_on_error\n");

    reset_mock();
    parser_slots[1] = -1;  /* Second parser fails */

    uint32_t ids[] = {1, 2, 3};
    StrategyResult sr = make_strategy_result(3, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1), mock_parser(2) };
    GrammarExtensionMetadata *exts = make_ext_array(3, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1], &exts[2] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_ALL;
    config.execute_fn = mock_execute;
    config.stop_on_error = false;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 3, "all 3 ran despite error");
    ASSERT(execute_call_count == 3, "3 executions");
    ASSERT(r[0].error == NULL, "first ok");
    ASSERT(r[1].error != NULL, "second failed");
    ASSERT(r[2].error == NULL, "third ok");

    free(r[0].result);
    free(r[2].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_ALL with max_executions limit                           */
/* ------------------------------------------------------------------ */

static void test_all_max_executions(void) {
    printf("test_all_max_executions\n");

    reset_mock();

    uint32_t ids[] = {1, 2, 3, 4};
    StrategyResult sr = make_strategy_result(4, ids);

    LimeParserHandle *parsers[] = {
        mock_parser(0), mock_parser(1), mock_parser(2), mock_parser(3)
    };
    GrammarExtensionMetadata *exts = make_ext_array(4, ids);
    GrammarExtensionMetadata *ext_ptrs[] = {
        &exts[0], &exts[1], &exts[2], &exts[3]
    };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_ALL;
    config.execute_fn = mock_execute;
    config.max_executions = 2;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 2, "limited to 2 results");
    ASSERT(execute_call_count == 2, "only 2 executions");
    ASSERT(r[0].extension_id == 1, "first winner");
    ASSERT(r[1].extension_id == 2, "second winner");

    free(r[0].result);
    free(r[1].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_CHAIN basic output chaining                             */
/* ------------------------------------------------------------------ */

static void test_chain_basic(void) {
    printf("test_chain_basic\n");

    reset_mock();

    uint32_t ids[] = {10, 20, 30};
    StrategyResult sr = make_strategy_result(3, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1), mock_parser(2) };
    GrammarExtensionMetadata *exts = make_ext_array(3, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1], &exts[2] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_CHAIN;
    config.execute_fn = mock_execute;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 3, "chain: 3 results");
    ASSERT(execute_call_count == 3, "3 executions");

    /* First call should receive NULL input (start of chain) */
    ASSERT(execute_chain_inputs[0] == NULL, "first input is NULL");

    /* Second call should receive first result as input */
    ASSERT(execute_chain_inputs[1] == r[0].result, "second receives first output");

    /* Third call should receive second result as input */
    ASSERT(execute_chain_inputs[2] == r[1].result, "third receives second output");

    for (int i = 0; i < n; i++) {
        ASSERT(r[i].error == NULL, "no errors in chain");
        free(r[i].result);
    }

    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_CHAIN error breaks chain (stop_on_error)                */
/* ------------------------------------------------------------------ */

static void test_chain_error_stops(void) {
    printf("test_chain_error_stops\n");

    reset_mock();
    parser_slots[1] = -1;  /* Second fails */

    uint32_t ids[] = {10, 20, 30};
    StrategyResult sr = make_strategy_result(3, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1), mock_parser(2) };
    GrammarExtensionMetadata *exts = make_ext_array(3, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1], &exts[2] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_CHAIN;
    config.execute_fn = mock_execute;
    config.stop_on_error = true;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 2, "chain stopped at error: 2 results");
    ASSERT(r[0].error == NULL, "first ok");
    ASSERT(r[1].error != NULL, "second error");

    free(r[0].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_CHAIN error continues with NULL (stop_on_error=false)   */
/* ------------------------------------------------------------------ */

static void test_chain_error_continues(void) {
    printf("test_chain_error_continues\n");

    reset_mock();
    parser_slots[1] = -1;  /* Second fails */

    uint32_t ids[] = {10, 20, 30};
    StrategyResult sr = make_strategy_result(3, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1), mock_parser(2) };
    GrammarExtensionMetadata *exts = make_ext_array(3, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1], &exts[2] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_CHAIN;
    config.execute_fn = mock_execute;
    config.stop_on_error = false;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 3, "chain continued: 3 results");
    ASSERT(r[1].error != NULL, "second error");
    /* After error, third receives NULL input */
    ASSERT(execute_chain_inputs[2] == NULL, "input after error is NULL");
    ASSERT(r[2].error == NULL, "third ok");

    free(r[0].result);
    free(r[2].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_CHAIN with max_executions                               */
/* ------------------------------------------------------------------ */

static void test_chain_max_executions(void) {
    printf("test_chain_max_executions\n");

    reset_mock();

    uint32_t ids[] = {1, 2, 3};
    StrategyResult sr = make_strategy_result(3, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1), mock_parser(2) };
    GrammarExtensionMetadata *exts = make_ext_array(3, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1], &exts[2] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_CHAIN;
    config.execute_fn = mock_execute;
    config.max_executions = 1;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 1, "chain limited to 1");
    ASSERT(execute_call_count == 1, "only 1 execution");

    free(r[0].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Conditional callback helpers                                       */
/* ------------------------------------------------------------------ */

static bool should_execute_always(const GrammarExtensionMetadata *self,
                                  const StrategyResult *sr) {
    (void)self; (void)sr;
    return true;
}

static bool should_execute_never(const GrammarExtensionMetadata *self,
                                 const StrategyResult *sr) {
    (void)self; (void)sr;
    return false;
}

/* Only execute if extension_id is odd */
static bool should_execute_odd(const GrammarExtensionMetadata *self,
                               const StrategyResult *sr) {
    (void)sr;
    return (self->extension_id % 2) == 1;
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_CONDITIONAL basic filtering                             */
/* ------------------------------------------------------------------ */

static void test_conditional_basic(void) {
    printf("test_conditional_basic\n");

    reset_mock();

    uint32_t ids[] = {1, 2, 3, 4};
    StrategyResult sr = make_strategy_result(4, ids);

    LimeParserHandle *parsers[] = {
        mock_parser(0), mock_parser(1), mock_parser(2), mock_parser(3)
    };
    GrammarExtensionMetadata *exts = make_ext_array(4, ids);
    /* Set odd-only filter on all */
    for (int i = 0; i < 4; i++) {
        exts[i].should_execute = should_execute_odd;
    }
    GrammarExtensionMetadata *ext_ptrs[] = {
        &exts[0], &exts[1], &exts[2], &exts[3]
    };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_CONDITIONAL;
    config.execute_fn = mock_execute;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    /* Only ext_ids 1 and 3 are odd */
    ASSERT(n == 2, "conditional: 2 odd results");
    ASSERT(execute_call_count == 2, "2 executions");
    ASSERT(r[0].extension_id == 1, "first odd");
    ASSERT(r[1].extension_id == 3, "second odd");

    free(r[0].result);
    free(r[1].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_CONDITIONAL with NULL callback (always executes)        */
/* ------------------------------------------------------------------ */

static void test_conditional_null_callback(void) {
    printf("test_conditional_null_callback\n");

    reset_mock();

    uint32_t ids[] = {5, 6};
    StrategyResult sr = make_strategy_result(2, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1) };
    GrammarExtensionMetadata *exts = make_ext_array(2, ids);
    /* should_execute is NULL by default from make_ext_array */
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_CONDITIONAL;
    config.execute_fn = mock_execute;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 2, "NULL callback = always execute");

    free(r[0].result);
    free(r[1].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_CONDITIONAL all filtered out                            */
/* ------------------------------------------------------------------ */

static void test_conditional_all_filtered(void) {
    printf("test_conditional_all_filtered\n");

    reset_mock();

    uint32_t ids[] = {10, 20};
    StrategyResult sr = make_strategy_result(2, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1) };
    GrammarExtensionMetadata *exts = make_ext_array(2, ids);
    exts[0].should_execute = should_execute_never;
    exts[1].should_execute = should_execute_never;
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_CONDITIONAL;
    config.execute_fn = mock_execute;

    int n = -1;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(r == NULL, "all filtered returns NULL");
    ASSERT(n == 0, "0 results when all filtered");
    ASSERT(execute_call_count == 0, "no executions");

    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_CONDITIONAL with stop_on_error                          */
/* ------------------------------------------------------------------ */

static void test_conditional_stop_on_error(void) {
    printf("test_conditional_stop_on_error\n");

    reset_mock();
    parser_slots[0] = -1;  /* First passing parser fails */

    uint32_t ids[] = {1, 2, 3};
    StrategyResult sr = make_strategy_result(3, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1), mock_parser(2) };
    GrammarExtensionMetadata *exts = make_ext_array(3, ids);
    exts[0].should_execute = should_execute_always;
    exts[1].should_execute = should_execute_always;
    exts[2].should_execute = should_execute_always;
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1], &exts[2] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_CONDITIONAL;
    config.execute_fn = mock_execute;
    config.stop_on_error = true;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 1, "stopped after first error");
    ASSERT(r[0].error != NULL, "first result is error");

    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: EXEC_CONDITIONAL with max_executions                         */
/* ------------------------------------------------------------------ */

static void test_conditional_max_executions(void) {
    printf("test_conditional_max_executions\n");

    reset_mock();

    uint32_t ids[] = {1, 3, 5, 7};
    StrategyResult sr = make_strategy_result(4, ids);

    LimeParserHandle *parsers[] = {
        mock_parser(0), mock_parser(1), mock_parser(2), mock_parser(3)
    };
    GrammarExtensionMetadata *exts = make_ext_array(4, ids);
    /* All pass (NULL callback = always execute) */
    GrammarExtensionMetadata *ext_ptrs[] = {
        &exts[0], &exts[1], &exts[2], &exts[3]
    };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_CONDITIONAL;
    config.execute_fn = mock_execute;
    config.max_executions = 2;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 2, "limited to 2");

    free(r[0].result);
    free(r[1].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: execute_first_only convenience wrapper                       */
/* ------------------------------------------------------------------ */

static void test_convenience_first_only(void) {
    printf("test_convenience_first_only\n");

    reset_mock();

    uint32_t ids[] = {99, 100};
    StrategyResult sr = make_strategy_result(2, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1) };
    GrammarExtensionMetadata *exts = make_ext_array(2, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1] };

    int n = 0;
    ExecutionResult *r = execute_first_only(mock_execute, &sr, parsers,
                                             ext_ptrs, &n);
    ASSERT(n == 1, "convenience: 1 result");
    ASSERT(r[0].extension_id == 99, "first winner");
    ASSERT(execute_call_count == 1, "1 execution");

    free(r[0].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: unknown policy falls back to FIRST_ONLY                      */
/* ------------------------------------------------------------------ */

static void test_unknown_policy_fallback(void) {
    printf("test_unknown_policy_fallback\n");

    reset_mock();

    uint32_t ids[] = {50, 60};
    StrategyResult sr = make_strategy_result(2, ids);

    LimeParserHandle *parsers[] = { mock_parser(0), mock_parser(1) };
    GrammarExtensionMetadata *exts = make_ext_array(2, ids);
    GrammarExtensionMetadata *ext_ptrs[] = { &exts[0], &exts[1] };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = (LimeExecMode)999;  /* Invalid policy */
    config.execute_fn = mock_execute;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 1, "fallback: 1 result");
    ASSERT(r[0].extension_id == 50, "fallback executes first");

    free(r[0].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Test: execution order matches winner order                         */
/* ------------------------------------------------------------------ */

static void test_execution_order(void) {
    printf("test_execution_order\n");

    reset_mock();

    uint32_t ids[] = {100, 200, 300, 400};
    StrategyResult sr = make_strategy_result(4, ids);

    LimeParserHandle *parsers[] = {
        mock_parser(0), mock_parser(1), mock_parser(2), mock_parser(3)
    };
    GrammarExtensionMetadata *exts = make_ext_array(4, ids);
    GrammarExtensionMetadata *ext_ptrs[] = {
        &exts[0], &exts[1], &exts[2], &exts[3]
    };

    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.policy = EXEC_ALL;
    config.execute_fn = mock_execute;

    int n = 0;
    ExecutionResult *r = execute_semantic_actions(&config, &sr, parsers,
                                                   ext_ptrs, &n);
    ASSERT(n == 4, "4 results");
    ASSERT(execute_order[0] == 100, "order[0] = 100");
    ASSERT(execute_order[1] == 200, "order[1] = 200");
    ASSERT(execute_order[2] == 300, "order[2] = 300");
    ASSERT(execute_order[3] == 400, "order[3] = 400");

    for (int i = 0; i < n; i++) free(r[i].result);
    execution_results_free(r, n);
    free(exts);
    strategy_result_cleanup(&sr);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Execution Policy Engine Tests ===\n");

    test_config_init();
    test_policy_name();
    test_null_safety();
    test_zero_winners();
    test_no_execute_callback();

    /* EXEC_FIRST_ONLY */
    test_first_only_single();
    test_first_only_multiple();
    test_first_only_error();

    /* EXEC_ALL */
    test_all_basic();
    test_all_stop_on_error();
    test_all_continue_on_error();
    test_all_max_executions();

    /* EXEC_CHAIN */
    test_chain_basic();
    test_chain_error_stops();
    test_chain_error_continues();
    test_chain_max_executions();

    /* EXEC_CONDITIONAL */
    test_conditional_basic();
    test_conditional_null_callback();
    test_conditional_all_filtered();
    test_conditional_stop_on_error();
    test_conditional_max_executions();

    /* Convenience and edge cases */
    test_convenience_first_only();
    test_unknown_policy_fallback();
    test_execution_order();

    printf("\n%d tests, %d failures\n", test_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
