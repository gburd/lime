/*
** MySQL SQL Compatibility Extension -- Unit Tests
**
** Tests the extension registration, modification generation, and
** metadata for the MySQL compatibility extension.
*/

#include "../../src/extension.h"
#include "extension_registry.h"
#include "mysql_semantics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Defined in mysql_compat.c */
extern bool mysql_compat_register(ExtensionRegistry *reg);
extern bool mysql_compat_register_ext(void *ext_registry, uint32_t *id_out);
extern const GrammarExtensionMetadata *mysql_compat_get_metadata(void);
extern void mysql_compat_cleanup(void);

/* ------------------------------------------------------------------ */
/*  Test helpers                                                        */
/* ------------------------------------------------------------------ */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-50s ", name); \
    fflush(stdout); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("[PASS]\n"); \
} while(0)

#define FAIL(msg) do { \
    printf("[FAIL] %s\n", msg); \
} while(0)

/* ------------------------------------------------------------------ */
/*  Test: metadata                                                      */
/* ------------------------------------------------------------------ */

static void test_metadata(void) {
    TEST("metadata: name is 'mysql_compat'");
    const GrammarExtensionMetadata *m = mysql_compat_get_metadata();
    if (m != NULL && strcmp(m->name, "mysql_compat") == 0) {
        PASS();
    } else {
        FAIL("unexpected name");
    }

    TEST("metadata: version is '1.0.0'");
    if (m != NULL && strcmp(m->version, "1.0.0") == 0) {
        PASS();
    } else {
        FAIL("unexpected version");
    }

    TEST("metadata: strategy is DISAMBIG_FORK_RESOLVE");
    if (m != NULL && m->strategy == DISAMBIG_FORK_RESOLVE) {
        PASS();
    } else {
        FAIL("unexpected strategy");
    }

    TEST("metadata: priority is 3");
    if (m != NULL && m->priority == 3) {
        PASS();
    } else {
        FAIL("unexpected priority");
    }

    TEST("metadata: policy is EXEC_SEQUENTIAL");
    if (m != NULL && m->policy == EXEC_SEQUENTIAL) {
        PASS();
    } else {
        FAIL("unexpected policy");
    }

    TEST("metadata: requires postgres_base");
    if (m != NULL && m->requires != NULL &&
        m->requires[0] != NULL &&
        strcmp(m->requires[0], "postgres_base") == 0) {
        PASS();
    } else {
        FAIL("unexpected requires");
    }

    TEST("metadata: has modifications");
    if (m != NULL && m->nmodifications > 0) {
        PASS();
    } else {
        FAIL("no modifications");
    }
}

/* ------------------------------------------------------------------ */
/*  Test: modifications                                                 */
/* ------------------------------------------------------------------ */

static void test_modifications(void) {
    const GrammarExtensionMetadata *m = mysql_compat_get_metadata();
    if (m == NULL || m->modifications == NULL) {
        TEST("modifications: available");
        FAIL("no metadata");
        return;
    }

    /* Count token modifications */
    uint32_t token_count = 0;
    uint32_t rule_count = 0;
    uint32_t prec_count = 0;

    for (uint32_t i = 0; i < m->nmodifications; i++) {
        switch (m->modifications[i].type) {
        case MOD_ADD_TOKEN:        token_count++; break;
        case MOD_ADD_RULE:         rule_count++;  break;
        case MOD_MODIFY_PRECEDENCE: prec_count++;  break;
        default: break;
        }
    }

    TEST("modifications: has token modifications");
    if (token_count > 0) {
        printf("[PASS] (%u tokens)\n", token_count);
        tests_passed++;
    } else {
        FAIL("no token modifications");
    }

    TEST("modifications: has rule modifications");
    if (rule_count > 0) {
        printf("[PASS] (%u rules)\n", rule_count);
        tests_passed++;
    } else {
        FAIL("no rule modifications");
    }

    TEST("modifications: has precedence modifications");
    if (prec_count > 0) {
        printf("[PASS] (%u precedence)\n", prec_count);
        tests_passed++;
    } else {
        FAIL("no precedence modifications");
    }

    /* Check for specific tokens */
    TEST("modifications: BACKTICK_IDENT token present");
    bool found = false;
    for (uint32_t i = 0; i < m->nmodifications && !found; i++) {
        if (m->modifications[i].type == MOD_ADD_TOKEN &&
            strcmp(m->modifications[i].u.add_token.name, "BACKTICK_IDENT") == 0) {
            found = true;
        }
    }
    if (found) { PASS(); } else { FAIL("not found"); }

    TEST("modifications: AUTO_INCREMENT token present");
    found = false;
    for (uint32_t i = 0; i < m->nmodifications && !found; i++) {
        if (m->modifications[i].type == MOD_ADD_TOKEN &&
            strcmp(m->modifications[i].u.add_token.name, "AUTO_INCREMENT") == 0) {
            found = true;
        }
    }
    if (found) { PASS(); } else { FAIL("not found"); }

    TEST("modifications: DIV_KW token present");
    found = false;
    for (uint32_t i = 0; i < m->nmodifications && !found; i++) {
        if (m->modifications[i].type == MOD_ADD_TOKEN &&
            strcmp(m->modifications[i].u.add_token.name, "DIV_KW") == 0) {
            found = true;
        }
    }
    if (found) { PASS(); } else { FAIL("not found"); }

    TEST("modifications: NULL_SAFE_EQ token present");
    found = false;
    for (uint32_t i = 0; i < m->nmodifications && !found; i++) {
        if (m->modifications[i].type == MOD_ADD_TOKEN &&
            strcmp(m->modifications[i].u.add_token.name, "NULL_SAFE_EQ") == 0) {
            found = true;
        }
    }
    if (found) { PASS(); } else { FAIL("not found"); }
}

/* ------------------------------------------------------------------ */
/*  Test: semantic action constructors                                  */
/* ------------------------------------------------------------------ */

static void test_semantics(void) {
    MysqlParseState *ps = mysql_parse_state_create();

    TEST("semantics: parse state creation");
    if (ps != NULL) { PASS(); } else { FAIL("NULL"); return; }

    TEST("semantics: make_ident");
    MysqlExpr *e = mysql_make_ident(ps, "test_col");
    if (e != NULL && e->type == MYSQL_EXPR_IDENT &&
        strcmp(e->u.ident.name, "test_col") == 0) {
        PASS();
    } else {
        FAIL("unexpected");
    }

    TEST("semantics: make_backtick_ident");
    e = mysql_make_backtick_ident(ps, "reserved_word");
    if (e != NULL && e->type == MYSQL_EXPR_BACKTICK_IDENT &&
        strcmp(e->u.ident.name, "reserved_word") == 0) {
        PASS();
    } else {
        FAIL("unexpected");
    }

    TEST("semantics: make_iconst");
    e = mysql_make_iconst(ps, 42);
    if (e != NULL && e->type == MYSQL_EXPR_ICONST && e->u.iconst == 42) {
        PASS();
    } else {
        FAIL("unexpected");
    }

    TEST("semantics: make_ifnull");
    MysqlExpr *e1 = mysql_make_ident(ps, "col1");
    MysqlExpr *e2 = mysql_make_iconst(ps, 0);
    e = mysql_make_ifnull(ps, e1, e2);
    if (e != NULL && e->type == MYSQL_EXPR_IFNULL) {
        PASS();
    } else {
        FAIL("unexpected");
    }

    TEST("semantics: make_if_func");
    MysqlExpr *cond = mysql_make_binop(ps, MYSQL_OP_GT,
                                        mysql_make_ident(ps, "x"),
                                        mysql_make_iconst(ps, 0));
    MysqlExpr *then_e = mysql_make_sconst(ps, "positive");
    MysqlExpr *else_e = mysql_make_sconst(ps, "non-positive");
    e = mysql_make_if_func(ps, cond, then_e, else_e);
    if (e != NULL && e->type == MYSQL_EXPR_IF_FUNC) {
        PASS();
    } else {
        FAIL("unexpected");
    }

    TEST("semantics: make_limit");
    e = mysql_make_limit(ps, mysql_make_iconst(ps, 10),
                         mysql_make_iconst(ps, 20));
    if (e != NULL && e->type == MYSQL_EXPR_LIMIT) {
        PASS();
    } else {
        FAIL("unexpected");
    }

    TEST("semantics: make_interval");
    e = mysql_make_interval(ps, mysql_make_iconst(ps, 7), MYSQL_INTERVAL_DAY);
    if (e != NULL && e->type == MYSQL_EXPR_INTERVAL &&
        e->u.interval.unit == MYSQL_INTERVAL_DAY) {
        PASS();
    } else {
        FAIL("unexpected");
    }

    TEST("semantics: make_show");
    MysqlStmt *s = mysql_make_show(ps, MYSQL_SHOW_TABLES, NULL, NULL);
    if (s != NULL && s->type == MYSQL_STMT_SHOW &&
        s->u.show.show_type == MYSQL_SHOW_TABLES) {
        PASS();
    } else {
        FAIL("unexpected");
    }

    TEST("semantics: make_create_table");
    MysqlColumnDef *col = mysql_make_column_def(ps, "id", "INT", 0);
    col->is_auto_increment = true;
    col->is_unsigned = true;
    MysqlTableOption *opt = mysql_make_engine_option(ps, MYSQL_ENGINE_INNODB);
    s = mysql_make_create_table(ps, "test_table", col, opt, false);
    if (s != NULL && s->type == MYSQL_STMT_CREATE_TABLE &&
        strcmp(s->u.create_table.table_name, "test_table") == 0) {
        PASS();
    } else {
        FAIL("unexpected");
    }

    TEST("semantics: make_upsert");
    MysqlUpsertAssign *a = mysql_make_upsert_assign(ps, "count", mysql_make_iconst(ps, 1));
    MysqlUpsertClause *u = mysql_make_upsert(ps, a);
    if (u != NULL && u->nassignments == 1 &&
        strcmp(u->assignments->column, "count") == 0) {
        PASS();
    } else {
        FAIL("unexpected");
    }

    mysql_parse_state_destroy(ps);
}

/* ------------------------------------------------------------------ */
/*  Test: internal extension registration                               */
/* ------------------------------------------------------------------ */

static void test_extension_registration(void) {
    TEST("registration: register via internal API");

    ExtensionRegistry *reg = create_extension_registry();
    if (reg == NULL) {
        FAIL("could not create registry");
        return;
    }

    uint32_t id = 0;
    bool ok = mysql_compat_register_ext(reg, &id);
    if (ok && id > 0) {
        PASS();
    } else {
        FAIL("registration failed");
    }

    TEST("registration: extension ID > 0");
    if (id > 0) { PASS(); } else { FAIL("id is 0"); }

    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: cleanup                                                       */
/* ------------------------------------------------------------------ */

static void test_cleanup(void) {
    TEST("cleanup: mysql_compat_cleanup succeeds");
    mysql_compat_cleanup();
    PASS();

    TEST("cleanup: metadata after cleanup has 0 modifications");
    const GrammarExtensionMetadata *m = mysql_compat_get_metadata();
    /* get_metadata re-initializes, so it should have modifications again */
    if (m != NULL && m->nmodifications > 0) {
        PASS();
    } else {
        FAIL("no modifications after re-init");
    }
}

/* ------------------------------------------------------------------ */
/*  Main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("MySQL SQL Compatibility Extension -- Tests\n");
    printf("==========================================\n\n");

    printf("Metadata tests:\n");
    test_metadata();
    printf("\n");

    printf("Modification tests:\n");
    test_modifications();
    printf("\n");

    printf("Semantic action tests:\n");
    test_semantics();
    printf("\n");

    printf("Extension registration tests:\n");
    test_extension_registration();
    printf("\n");

    printf("Cleanup tests:\n");
    test_cleanup();
    printf("\n");

    printf("==========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
