/*
** test_context_switch_e2e.c -- end-to-end SQL+JSON parse via the
** examples/multi_grammar_sql_json driver.
**
** Asserts on the AST shape produced by multi_parse_sql() so that
** the trigger registry, classify_lexeme dispatch, and the embedded
** JSON parse are all exercised together.  Cases:
**   - clean trigger + clean exit
**   - multiple triggers in one input (two json '...' literals)
**   - no triggers registered (host-grammar-only fast path)
*/
#include "multi.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_total = 0;
static int g_fail = 0;

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        g_total++;                                                                                 \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);                      \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

/* ------------------------------------------------------------------ */
/*  test_clean_trigger_and_exit                                        */
/* ------------------------------------------------------------------ */

static void test_clean_trigger_and_exit(void) {
    printf("test_clean_trigger_and_exit\n");

    SqlSelect *ast = NULL;
    MultiParseStatus s =
        multi_parse_sql("SELECT id, json '{\"a\":1, \"b\":[2,3]}' FROM t WHERE id = 5;",
                        true, &ast);
    ASSERT(s == MULTI_OK, "parse status MULTI_OK");
    ASSERT(ast != NULL, "AST produced");
    if (ast == NULL) return;

    ASSERT(ast->ncolumns == 2, "two columns");
    ASSERT(ast->columns[0]->kind == SQL_COL_IDENT, "col 0 is IDENT");
    ASSERT(strcmp(ast->columns[0]->ident, "id") == 0, "col 0 ident == \"id\"");
    ASSERT(ast->columns[1]->kind == SQL_COL_JSON_LITERAL, "col 1 is JSON_LITERAL");

    JsonValue *j = ast->columns[1]->json_root;
    ASSERT(j != NULL, "JSON root is non-NULL");
    ASSERT(j->type == JSON_T_OBJECT, "JSON root is an object");
    ASSERT(j->o.count == 2, "JSON object has 2 pairs");

    /* Position tracking */
    ASSERT(ast->columns[0]->line == 1, "col 0 on line 1");
    ASSERT(ast->columns[1]->line == 1, "col 1 on line 1");
    ASSERT(ast->columns[0]->col < ast->columns[1]->col, "col 0 starts before col 1");

    /* FROM / WHERE */
    ASSERT(ast->table != NULL && strcmp(ast->table, "t") == 0, "FROM table == \"t\"");
    ASSERT(ast->where_lhs != NULL && strcmp(ast->where_lhs, "id") == 0, "WHERE lhs == \"id\"");
    ASSERT(ast->where_rhs == 5, "WHERE rhs == 5");

    sql_select_destroy(ast);
}

/* ------------------------------------------------------------------ */
/*  test_multiple_triggers_in_one_input                                */
/* ------------------------------------------------------------------ */

static void test_multiple_triggers_in_one_input(void) {
    printf("test_multiple_triggers_in_one_input\n");

    SqlSelect *ast = NULL;
    MultiParseStatus s = multi_parse_sql(
        "SELECT json '[1,2]', json '{\"k\":true}' FROM t WHERE id = 1;",
        true, &ast);
    ASSERT(s == MULTI_OK, "parse status MULTI_OK");
    if (s != MULTI_OK) return;

    ASSERT(ast->ncolumns == 2, "two json columns");
    ASSERT(ast->columns[0]->kind == SQL_COL_JSON_LITERAL, "col 0 is JSON_LITERAL");
    ASSERT(ast->columns[1]->kind == SQL_COL_JSON_LITERAL, "col 1 is JSON_LITERAL");

    JsonValue *a = ast->columns[0]->json_root;
    JsonValue *b = ast->columns[1]->json_root;
    ASSERT(a != NULL && a->type == JSON_T_ARRAY, "col 0 is JSON array");
    ASSERT(a->a.count == 2, "col 0 array has 2 items");
    ASSERT(b != NULL && b->type == JSON_T_OBJECT, "col 1 is JSON object");
    ASSERT(b->o.count == 1, "col 1 object has 1 pair");

    sql_select_destroy(ast);
}

/* ------------------------------------------------------------------ */
/*  test_no_triggers_registered                                        */
/* ------------------------------------------------------------------ */

static void test_no_triggers_registered(void) {
    printf("test_no_triggers_registered\n");

    SqlSelect *ast = NULL;
    MultiParseStatus s = multi_parse_sql(
        "SELECT id, name FROM users WHERE id = 42;", false, &ast);
    ASSERT(s == MULTI_OK, "host-only parse OK");
    if (s != MULTI_OK) return;

    ASSERT(ast->ncolumns == 2, "two ident columns");
    ASSERT(ast->columns[0]->kind == SQL_COL_IDENT, "col 0 IDENT");
    ASSERT(ast->columns[1]->kind == SQL_COL_IDENT, "col 1 IDENT");
    ASSERT(strcmp(ast->columns[0]->ident, "id") == 0, "col 0 == \"id\"");
    ASSERT(strcmp(ast->columns[1]->ident, "name") == 0, "col 1 == \"name\"");
    ASSERT(strcmp(ast->table, "users") == 0, "FROM table == users");
    ASSERT(ast->where_rhs == 42, "WHERE rhs == 42");

    sql_select_destroy(ast);
}

int main(void) {
    test_clean_trigger_and_exit();
    test_multiple_triggers_in_one_input();
    test_no_triggers_registered();

    if (g_fail == 0) {
        printf("PASS  %d/%d\n", g_total, g_total);
        return 0;
    }
    printf("FAIL  %d/%d\n", g_total - g_fail, g_total);
    return 1;
}
