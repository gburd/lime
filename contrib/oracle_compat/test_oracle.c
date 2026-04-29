/*
** Oracle SQL Compatibility Extension -- Test Program
**
** Tests the Oracle compatibility extension by:
**   1. Creating an extension registry
**   2. Registering a mock postgres_base extension (dependency)
**   3. Registering the Oracle compatibility extension
**   4. Validating dependencies and topological order
**   5. Exercising the semantic action constructors
**   6. Verifying AST construction for Oracle-specific features
*/

#include "oracle_compat.h"
#include "oracle_semantics.h"
#include "extension.h"           /* GrammarModification full definition */
#include "extension_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ------------------------------------------------------------------ */
/*  Test counters                                                      */
/* ------------------------------------------------------------------ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_BEGIN(name) do { \
    tests_run++; \
    printf("  %-50s ", name); \
} while(0)

#define TEST_PASS() do { \
    tests_passed++; \
    printf("[PASS]\n"); \
} while(0)

#define TEST_FAIL(msg) do { \
    tests_failed++; \
    printf("[FAIL] %s\n", msg); \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { TEST_FAIL(msg); return; } \
} while(0)

#define ASSERT_EQ_INT(a, b, msg) do { \
    if ((a) != (b)) { TEST_FAIL(msg); return; } \
} while(0)

#define ASSERT_EQ_STR(a, b, msg) do { \
    if (strcmp((a), (b)) != 0) { TEST_FAIL(msg); return; } \
} while(0)

#define ASSERT_NOT_NULL(p, msg) do { \
    if ((p) == NULL) { TEST_FAIL(msg); return; } \
} while(0)

/* ------------------------------------------------------------------ */
/*  Test: Extension registration                                       */
/* ------------------------------------------------------------------ */

static void test_registration(void) {
    TEST_BEGIN("Extension registration");

    ExtensionRegistry *reg = extension_registry_create();
    ASSERT_NOT_NULL(reg, "failed to create registry");

    /* Register a mock postgres_base dependency */
    GrammarExtensionMetadata pg_meta = {
        .name = "postgres_base",
        .version = "16.0.0",
        .strategy = DISAMBIG_PRIORITY,
        .priority = 1,
        .policy = EXEC_SEQUENTIAL,
        .oracle = NULL,
        .conflict_threshold = 0.0f,
        .requires = NULL,
        .conflicts_with = NULL,
        .modifications = NULL,
        .nmodifications = 0,
    };
    ASSERT_TRUE(extension_registry_register(reg, &pg_meta),
                "failed to register postgres_base");

    /* Register Oracle compat */
    ASSERT_TRUE(oracle_compat_register(reg),
                "failed to register oracle_compat");

    /* Verify count */
    ASSERT_EQ_INT(extension_registry_count(reg), 2,
                  "expected 2 registered extensions");

    /* Verify lookup */
    const GrammarExtensionMetadata *found =
        extension_registry_find(reg, "oracle_compat");
    ASSERT_NOT_NULL(found, "oracle_compat not found in registry");
    ASSERT_EQ_STR(found->version, "1.0.0", "wrong version");
    ASSERT_TRUE(found->nmodifications > 0, "no modifications registered");

    extension_registry_destroy(reg);
    oracle_compat_cleanup();
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Dependency validation                                        */
/* ------------------------------------------------------------------ */

static void test_dependencies(void) {
    TEST_BEGIN("Dependency validation");

    ExtensionRegistry *reg = extension_registry_create();
    ASSERT_NOT_NULL(reg, "failed to create registry");

    /* Register postgres_base first */
    GrammarExtensionMetadata pg_meta = {
        .name = "postgres_base",
        .version = "16.0.0",
        .strategy = DISAMBIG_PRIORITY,
        .priority = 1,
        .policy = EXEC_SEQUENTIAL,
        .requires = NULL,
        .conflicts_with = NULL,
        .modifications = NULL,
        .nmodifications = 0,
    };
    extension_registry_register(reg, &pg_meta);
    oracle_compat_register(reg);

    /* Validate dependencies */
    char *error = NULL;
    bool ok = extension_registry_check_dependencies(reg, &error);
    ASSERT_TRUE(ok, error ? error : "dependency check failed");

    /* Get topological order */
    ExtensionOrder order;
    ok = extension_registry_get_order(reg, &order, &error);
    ASSERT_TRUE(ok, error ? error : "get_order failed");
    ASSERT_EQ_INT(order.count, 2, "expected 2 in order");

    /* postgres_base should come before oracle_compat */
    ASSERT_EQ_STR(order.names[0], "postgres_base",
                  "postgres_base should be first");
    ASSERT_EQ_STR(order.names[1], "oracle_compat",
                  "oracle_compat should be second");

    extension_order_destroy(&order);
    extension_registry_destroy(reg);
    oracle_compat_cleanup();
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Missing dependency detection                                 */
/* ------------------------------------------------------------------ */

static void test_missing_dependency(void) {
    TEST_BEGIN("Missing dependency detection");

    ExtensionRegistry *reg = extension_registry_create();
    ASSERT_NOT_NULL(reg, "failed to create registry");

    /* Register Oracle without postgres_base */
    oracle_compat_register(reg);

    char *error = NULL;
    bool ok = extension_registry_check_dependencies(reg, &error);
    ASSERT_TRUE(!ok, "should fail without postgres_base");
    ASSERT_NOT_NULL(error, "expected error message");

    free(error);
    extension_registry_destroy(reg);
    oracle_compat_cleanup();
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Duplicate registration                                       */
/* ------------------------------------------------------------------ */

static void test_duplicate_registration(void) {
    TEST_BEGIN("Duplicate registration rejected");

    ExtensionRegistry *reg = extension_registry_create();
    ASSERT_NOT_NULL(reg, "failed to create registry");

    GrammarExtensionMetadata pg_meta = {
        .name = "postgres_base",
        .version = "16.0.0",
        .requires = NULL,
        .conflicts_with = NULL,
        .modifications = NULL,
        .nmodifications = 0,
    };
    extension_registry_register(reg, &pg_meta);
    ASSERT_TRUE(oracle_compat_register(reg), "first register should succeed");
    ASSERT_TRUE(!oracle_compat_register(reg), "second register should fail");

    extension_registry_destroy(reg);
    oracle_compat_cleanup();
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Metadata inspection                                          */
/* ------------------------------------------------------------------ */

static void test_metadata(void) {
    TEST_BEGIN("Extension metadata");

    const GrammarExtensionMetadata *meta = oracle_compat_get_metadata();
    ASSERT_NOT_NULL(meta, "metadata is NULL");
    ASSERT_EQ_STR(meta->name, "oracle_compat", "wrong name");
    ASSERT_EQ_STR(meta->version, "1.0.0", "wrong version");
    ASSERT_EQ_INT(meta->strategy, DISAMBIG_FORK_RESOLVE, "wrong strategy");
    ASSERT_EQ_INT(meta->priority, 2, "wrong priority");
    ASSERT_EQ_INT(meta->policy, EXEC_SEQUENTIAL, "wrong policy");
    ASSERT_NOT_NULL(meta->requires, "requires is NULL");
    ASSERT_EQ_STR(meta->requires[0], "postgres_base", "wrong dependency");
    ASSERT_TRUE(meta->requires[1] == NULL, "requires not NULL-terminated");
    ASSERT_TRUE(meta->nmodifications > 0, "no modifications");

    oracle_compat_cleanup();
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Semantic actions -- pseudo-columns                           */
/* ------------------------------------------------------------------ */

static void test_pseudo_columns(void) {
    TEST_BEGIN("Pseudo-column AST nodes");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    OracleExpr *rownum = oracle_make_rownum(ps);
    ASSERT_NOT_NULL(rownum, "ROWNUM is NULL");
    ASSERT_EQ_INT(rownum->type, ORA_EXPR_ROWNUM, "wrong type for ROWNUM");

    OracleExpr *rowid = oracle_make_rowid(ps);
    ASSERT_NOT_NULL(rowid, "ROWID is NULL");
    ASSERT_EQ_INT(rowid->type, ORA_EXPR_ROWID, "wrong type for ROWID");

    OracleExpr *level = oracle_make_level(ps);
    ASSERT_NOT_NULL(level, "LEVEL is NULL");
    ASSERT_EQ_INT(level->type, ORA_EXPR_LEVEL, "wrong type for LEVEL");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Semantic actions -- date/time functions                       */
/* ------------------------------------------------------------------ */

static void test_datetime_functions(void) {
    TEST_BEGIN("Date/time function AST nodes");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    OracleExpr *sysdate = oracle_make_sysdate(ps);
    ASSERT_NOT_NULL(sysdate, "SYSDATE is NULL");
    ASSERT_EQ_INT(sysdate->type, ORA_EXPR_SYSDATE, "wrong type for SYSDATE");

    OracleExpr *systimestamp = oracle_make_systimestamp(ps);
    ASSERT_NOT_NULL(systimestamp, "SYSTIMESTAMP is NULL");
    ASSERT_EQ_INT(systimestamp->type, ORA_EXPR_SYSTIMESTAMP,
                  "wrong type for SYSTIMESTAMP");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Semantic actions -- DECODE                                   */
/* ------------------------------------------------------------------ */

static void test_decode(void) {
    TEST_BEGIN("DECODE function AST");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    /* Build: DECODE(status, 1, 'Active', 2, 'Inactive', 'Unknown') */
    OracleExpr *test_expr = oracle_make_ident(ps, "status");

    OracleExpr *s1 = oracle_make_iconst(ps, 1);
    OracleExpr *r1 = oracle_make_sconst(ps, "Active");
    OracleDecodePair *p1 = oracle_make_decode_pair(ps, s1, r1);

    OracleExpr *s2 = oracle_make_iconst(ps, 2);
    OracleExpr *r2 = oracle_make_sconst(ps, "Inactive");
    OracleDecodePair *p2 = oracle_make_decode_pair(ps, s2, r2);

    OracleDecodePairList *pairs = oracle_decode_pair_list_new(ps, p1);
    pairs = oracle_decode_pair_list_append(ps, pairs, p2);
    ASSERT_EQ_INT(pairs->count, 2, "expected 2 decode pairs");

    OracleExpr *default_val = oracle_make_sconst(ps, "Unknown");
    OracleDecodeArgs *args = oracle_decode_args_new(ps, pairs, default_val);

    OracleExpr *decode = oracle_make_decode(ps, test_expr, args);
    ASSERT_NOT_NULL(decode, "DECODE is NULL");
    ASSERT_EQ_INT(decode->type, ORA_EXPR_DECODE, "wrong type for DECODE");
    ASSERT_NOT_NULL(decode->u.decode.test_expr, "test_expr is NULL");
    ASSERT_NOT_NULL(decode->u.decode.args, "args is NULL");
    ASSERT_NOT_NULL(decode->u.decode.args->default_result,
                    "default is NULL");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Semantic actions -- NVL / NVL2                               */
/* ------------------------------------------------------------------ */

static void test_nvl(void) {
    TEST_BEGIN("NVL/NVL2 function AST");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    /* NVL(commission_pct, 0) */
    OracleExpr *e1 = oracle_make_ident(ps, "commission_pct");
    OracleExpr *e2 = oracle_make_iconst(ps, 0);
    OracleExpr *nvl = oracle_make_nvl(ps, e1, e2);
    ASSERT_NOT_NULL(nvl, "NVL is NULL");
    ASSERT_EQ_INT(nvl->type, ORA_EXPR_NVL, "wrong type for NVL");

    /* NVL2(manager_id, 'Has Manager', 'Top Level') */
    OracleExpr *e3 = oracle_make_ident(ps, "manager_id");
    OracleExpr *e4 = oracle_make_sconst(ps, "Has Manager");
    OracleExpr *e5 = oracle_make_sconst(ps, "Top Level");
    OracleExpr *nvl2 = oracle_make_nvl2(ps, e3, e4, e5);
    ASSERT_NOT_NULL(nvl2, "NVL2 is NULL");
    ASSERT_EQ_INT(nvl2->type, ORA_EXPR_NVL2, "wrong type for NVL2");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Semantic actions -- hierarchical query                       */
/* ------------------------------------------------------------------ */

static void test_hierarchical_query(void) {
    TEST_BEGIN("Hierarchical query AST (CONNECT BY)");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    /* PRIOR employee_id = manager_id */
    OracleExpr *emp_id = oracle_make_ident(ps, "employee_id");
    OracleExpr *prior_emp = oracle_make_prior(ps, emp_id);
    OracleExpr *mgr_id = oracle_make_ident(ps, "manager_id");
    OracleExpr *cond = oracle_make_binop(ps, ORACLE_OP_EQ, prior_emp, mgr_id);

    /* CONNECT BY NOCYCLE ... */
    OracleConnectBy *cb = oracle_make_connect_by(ps, cond, true);
    ASSERT_NOT_NULL(cb, "CONNECT BY is NULL");
    ASSERT_TRUE(cb->nocycle, "NOCYCLE should be true");

    /* START WITH manager_id IS NULL */
    OracleExpr *mgr_id2 = oracle_make_ident(ps, "manager_id");
    OracleExpr *start_cond = oracle_make_is_null(ps, mgr_id2, false);

    /* Complete hierarchical clause */
    OracleHierClause *hier = oracle_make_hier_clause(ps, cb, start_cond);
    ASSERT_NOT_NULL(hier, "hierarchical clause is NULL");
    ASSERT_NOT_NULL(hier->connect_by, "connect_by is NULL");
    ASSERT_NOT_NULL(hier->start_with, "start_with is NULL");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Semantic actions -- outer join                                */
/* ------------------------------------------------------------------ */

static void test_outer_join(void) {
    TEST_BEGIN("Outer join (+) AST");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    /* d.department_id(+) */
    OracleExpr *qual = oracle_make_qualified_ident(ps, "d", "department_id");
    OracleExpr *oj = oracle_make_outer_join(ps, qual);
    ASSERT_NOT_NULL(oj, "outer join is NULL");
    ASSERT_EQ_INT(oj->type, ORA_EXPR_OUTER_JOIN, "wrong type for outer join");

    /* e.department_id = d.department_id(+) */
    OracleExpr *left = oracle_make_qualified_ident(ps, "e", "department_id");
    OracleExpr *oj_cond = oracle_make_outer_join_cond(ps, left, oj);
    ASSERT_NOT_NULL(oj_cond, "outer join condition is NULL");
    ASSERT_EQ_INT(oj_cond->type, ORA_EXPR_OUTER_JOIN_COND,
                  "wrong type for outer join cond");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Semantic actions -- sequences                                */
/* ------------------------------------------------------------------ */

static void test_sequences(void) {
    TEST_BEGIN("Sequence reference AST");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    OracleExpr *nextval = oracle_make_seq_nextval(ps, "emp_seq");
    ASSERT_NOT_NULL(nextval, "NEXTVAL is NULL");
    ASSERT_EQ_INT(nextval->type, ORA_EXPR_SEQ_NEXTVAL,
                  "wrong type for NEXTVAL");
    ASSERT_EQ_STR(nextval->u.seq_ref.seq_name, "emp_seq",
                  "wrong sequence name");

    OracleExpr *currval = oracle_make_seq_currval(ps, "emp_seq");
    ASSERT_NOT_NULL(currval, "CURRVAL is NULL");
    ASSERT_EQ_INT(currval->type, ORA_EXPR_SEQ_CURRVAL,
                  "wrong type for CURRVAL");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Semantic actions -- SELECT FROM DUAL                         */
/* ------------------------------------------------------------------ */

static void test_select_dual(void) {
    TEST_BEGIN("SELECT ... FROM DUAL AST");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    /* SELECT SYSDATE FROM DUAL */
    OracleExpr *sysdate = oracle_make_sysdate(ps);
    OracleSelectItem *item = oracle_select_item_new(ps, sysdate, NULL);
    OracleSelectList *list = oracle_select_list_new(ps, item);

    OracleStmt *stmt = oracle_make_select_dual(ps, list);
    ASSERT_NOT_NULL(stmt, "SELECT ... FROM DUAL is NULL");
    ASSERT_EQ_INT(stmt->type, ORA_STMT_SELECT, "wrong stmt type");
    ASSERT_NOT_NULL(stmt->u.select.from, "from is NULL");
    ASSERT_TRUE(stmt->u.select.from->head->is_dual, "should be DUAL");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Semantic actions -- MINUS set operator                       */
/* ------------------------------------------------------------------ */

static void test_minus_operator(void) {
    TEST_BEGIN("MINUS set operator AST");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    /* Build two simple SELECT stmts */
    OracleExpr *e1 = oracle_make_ident(ps, "id");
    OracleSelectItem *i1 = oracle_select_item_new(ps, e1, NULL);
    OracleSelectList *l1 = oracle_select_list_new(ps, i1);
    OracleFromItem *f1 = oracle_from_item_table(ps, "table_a", NULL);
    OracleFromList *fl1 = oracle_from_list_new(ps, f1);
    OracleStmt *s1 = oracle_make_select(ps, l1, fl1, NULL, NULL, NULL);

    OracleExpr *e2 = oracle_make_ident(ps, "id");
    OracleSelectItem *i2 = oracle_select_item_new(ps, e2, NULL);
    OracleSelectList *l2 = oracle_select_list_new(ps, i2);
    OracleFromItem *f2 = oracle_from_item_table(ps, "table_b", NULL);
    OracleFromList *fl2 = oracle_from_list_new(ps, f2);
    OracleStmt *s2 = oracle_make_select(ps, l2, fl2, NULL, NULL, NULL);

    /* MINUS */
    OracleStmt *minus = oracle_make_minus(ps, s1, s2);
    ASSERT_NOT_NULL(minus, "MINUS is NULL");
    ASSERT_EQ_INT(minus->type, ORA_STMT_MINUS, "wrong stmt type");
    ASSERT_NOT_NULL(minus->u.minus.left, "left is NULL");
    ASSERT_NOT_NULL(minus->u.minus.right, "right is NULL");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Parse error handling                                         */
/* ------------------------------------------------------------------ */

static void test_parse_error(void) {
    TEST_BEGIN("Parse error handling");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    ASSERT_TRUE(!ps->has_error, "should not have error initially");

    oracle_parse_error(ps, "test error message");
    ASSERT_TRUE(ps->has_error, "should have error");
    ASSERT_EQ_STR(ps->error_msg, "test error message", "wrong error message");

    /* Second error should be ignored (keep first) */
    oracle_parse_error(ps, "second error");
    ASSERT_EQ_STR(ps->error_msg, "test error message",
                  "should keep first error");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Grammar modification count                                   */
/* ------------------------------------------------------------------ */

static void test_modification_count(void) {
    TEST_BEGIN("Grammar modification count");

    const GrammarExtensionMetadata *meta = oracle_compat_get_metadata();
    ASSERT_NOT_NULL(meta, "metadata is NULL");

    /* We should have:
    **   17 tokens + 1 precedence + ~18 rules = ~36 modifications
    ** The exact count depends on how many we packed in.
    */
    ASSERT_TRUE(meta->nmodifications >= 20,
                "expected at least 20 modifications");
    ASSERT_TRUE(meta->nmodifications <= 64,
                "too many modifications");

    /* Verify some specific modifications exist */
    bool found_rownum_token = false;
    bool found_decode_rule = false;
    bool found_prior_prec = false;

    for (uint32_t i = 0; i < meta->nmodifications; i++) {
        const GrammarModification *m = &meta->modifications[i];
        if (m->type == MOD_ADD_TOKEN &&
            strcmp(m->u.add_token.name, "ROWNUM") == 0) {
            found_rownum_token = true;
        }
        if (m->type == MOD_ADD_RULE &&
            m->description != NULL &&
            strstr(m->description, "DECODE") != NULL) {
            found_decode_rule = true;
        }
        if (m->type == MOD_MODIFY_PRECEDENCE &&
            strcmp(m->u.modify_prec.symbol, "PRIOR") == 0) {
            found_prior_prec = true;
        }
    }

    ASSERT_TRUE(found_rownum_token, "ROWNUM token not found");
    ASSERT_TRUE(found_decode_rule, "DECODE rule not found");
    ASSERT_TRUE(found_prior_prec, "PRIOR precedence not found");

    oracle_compat_cleanup();
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: ORDER SIBLINGS BY                                            */
/* ------------------------------------------------------------------ */

static void test_order_siblings(void) {
    TEST_BEGIN("ORDER SIBLINGS BY AST");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    OracleExpr *name = oracle_make_ident(ps, "employee_name");
    OracleOrderItem *oi = oracle_make_order_item(ps, name, true);
    OracleOrderList *ol = oracle_order_list_new(ps, oi);

    /* ORDER SIBLINGS BY */
    OracleOrderClause *oc = oracle_make_order_clause(ps, ol, true);
    ASSERT_NOT_NULL(oc, "order clause is NULL");
    ASSERT_TRUE(oc->siblings, "siblings should be true");
    ASSERT_EQ_INT(oc->items->count, 1, "expected 1 order item");

    /* Regular ORDER BY */
    OracleOrderClause *oc2 = oracle_make_order_clause(ps, ol, false);
    ASSERT_TRUE(!oc2->siblings, "siblings should be false");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: Function call AST                                            */
/* ------------------------------------------------------------------ */

static void test_function_call(void) {
    TEST_BEGIN("Function call AST");

    OracleParseState *ps = oracle_parse_state_create();
    ASSERT_NOT_NULL(ps, "failed to create parse state");

    /* UPPER('hello') */
    OracleExpr *arg = oracle_make_sconst(ps, "hello");
    OracleArgList *args = oracle_arg_list_new(ps, arg);
    ASSERT_EQ_INT(args->count, 1, "expected 1 arg");

    OracleExpr *func = oracle_make_func_call(ps, "UPPER", args);
    ASSERT_NOT_NULL(func, "function call is NULL");
    ASSERT_EQ_INT(func->type, ORA_EXPR_FUNC_CALL, "wrong type");
    ASSERT_EQ_STR(func->u.func_call.func_name, "UPPER", "wrong func name");

    /* Multi-argument: SUBSTR('hello', 1, 3) */
    OracleExpr *a1 = oracle_make_sconst(ps, "hello");
    OracleExpr *a2 = oracle_make_iconst(ps, 1);
    OracleExpr *a3 = oracle_make_iconst(ps, 3);
    OracleArgList *args2 = oracle_arg_list_new(ps, a1);
    args2 = oracle_arg_list_append(ps, args2, a2);
    args2 = oracle_arg_list_append(ps, args2, a3);
    ASSERT_EQ_INT(args2->count, 3, "expected 3 args");

    oracle_parse_state_destroy(ps);
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("Oracle SQL Compatibility Extension -- Test Suite\n");
    printf("================================================\n\n");

    printf("Extension Registration Tests:\n");
    test_registration();
    test_dependencies();
    test_missing_dependency();
    test_duplicate_registration();
    test_metadata();
    test_modification_count();

    printf("\nSemantic Action Tests:\n");
    test_pseudo_columns();
    test_datetime_functions();
    test_decode();
    test_nvl();
    test_hierarchical_query();
    test_outer_join();
    test_sequences();
    test_select_dual();
    test_minus_operator();
    test_order_siblings();
    test_function_call();
    test_parse_error();

    printf("\n================================================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_run);

    return tests_failed > 0 ? 1 : 0;
}
