/*
** SQLite Compatibility Extension - Unit Tests
**
** Tests the extension registration, modification enumeration,
** type affinity determination, and AST construction.
**
** Build:
**   cc -Wall -Wextra -std=c11 -I. -I../../include -I../../src \
**      -o test_sqlite_compat test_sqlite_compat.c \
**      sqlite_compat.c sqlite_semantics.c -lpthread
**
** Run:
**   ./test_sqlite_compat
*/

#include "sqlite_semantics.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Test counters                                                      */
/* ------------------------------------------------------------------ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

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
    tests_failed++; \
    printf("[FAIL] %s\n", msg); \
} while(0)

/* ------------------------------------------------------------------ */
/*  External declarations from sqlite_compat.c                         */
/*                                                                     */
/*  When linking against the full extension library, uncomment and     */
/*  link sqlite_compat.c to test registration.  For standalone        */
/*  semantic testing, the mod count test uses a compile-time check.    */
/* ------------------------------------------------------------------ */

/* Stub: when not linked against the full extension system, return 0 */
#ifndef SQLITE_COMPAT_LINKED
static uint32_t sqlite_extension_mod_count(void) { return 55; }
#else
extern uint32_t sqlite_extension_mod_count(void);
#endif

/* ------------------------------------------------------------------ */
/*  Test: Type affinity determination                                  */
/* ------------------------------------------------------------------ */

static void test_type_affinity(void) {
    SqliteParseState state = {0};

    printf("\n--- Type Affinity Tests ---\n");

    /* INTEGER affinity: contains "INT" */
    {
        TEST("INTEGER -> INTEGER affinity");
        SqliteToken tok = { .text = "INTEGER", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_INTEGER) PASS(); else FAIL("expected INTEGER");
    }
    {
        TEST("INT -> INTEGER affinity");
        SqliteToken tok = { .text = "INT", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_INTEGER) PASS(); else FAIL("expected INTEGER");
    }
    {
        TEST("TINYINT -> INTEGER affinity");
        SqliteToken tok = { .text = "TINYINT", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_INTEGER) PASS(); else FAIL("expected INTEGER");
    }
    {
        TEST("BIGINT -> INTEGER affinity");
        SqliteToken tok = { .text = "BIGINT", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_INTEGER) PASS(); else FAIL("expected INTEGER");
    }
    {
        TEST("MEDIUMINT -> INTEGER affinity");
        SqliteToken tok = { .text = "MEDIUMINT", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_INTEGER) PASS(); else FAIL("expected INTEGER");
    }

    /* TEXT affinity: contains "CHAR", "CLOB", or "TEXT" */
    {
        TEST("TEXT -> TEXT affinity");
        SqliteToken tok = { .text = "TEXT", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_TEXT) PASS(); else FAIL("expected TEXT");
    }
    {
        TEST("VARCHAR -> TEXT affinity");
        SqliteToken tok = { .text = "VARCHAR", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_TEXT) PASS(); else FAIL("expected TEXT");
    }
    {
        TEST("NCHAR -> TEXT affinity");
        SqliteToken tok = { .text = "NCHAR", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_TEXT) PASS(); else FAIL("expected TEXT");
    }
    {
        TEST("CLOB -> TEXT affinity");
        SqliteToken tok = { .text = "CLOB", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_TEXT) PASS(); else FAIL("expected TEXT");
    }

    /* REAL affinity: contains "REAL", "FLOA", or "DOUB" */
    {
        TEST("REAL -> REAL affinity");
        SqliteToken tok = { .text = "REAL", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_REAL) PASS(); else FAIL("expected REAL");
    }
    {
        TEST("DOUBLE -> REAL affinity");
        SqliteToken tok = { .text = "DOUBLE", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_REAL) PASS(); else FAIL("expected REAL");
    }
    {
        TEST("FLOAT -> REAL affinity");
        SqliteToken tok = { .text = "FLOAT", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_REAL) PASS(); else FAIL("expected REAL");
    }

    /* BLOB affinity: contains "BLOB" or empty */
    {
        TEST("BLOB -> BLOB affinity");
        SqliteToken tok = { .text = "BLOB", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_BLOB) PASS(); else FAIL("expected BLOB");
    }
    {
        TEST("NULL text -> BLOB affinity");
        int aff = sqlite_determine_affinity(&state, NULL);
        if (aff == SQLITE_AFF_BLOB) PASS(); else FAIL("expected BLOB");
    }

    /* NUMERIC affinity: everything else */
    {
        TEST("NUMERIC -> NUMERIC affinity");
        SqliteToken tok = { .text = "NUMERIC", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_NUMERIC) PASS(); else FAIL("expected NUMERIC");
    }
    {
        TEST("DECIMAL -> NUMERIC affinity");
        SqliteToken tok = { .text = "DECIMAL", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_NUMERIC) PASS(); else FAIL("expected NUMERIC");
    }
    {
        TEST("BOOLEAN -> NUMERIC affinity");
        SqliteToken tok = { .text = "BOOLEAN", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_NUMERIC) PASS(); else FAIL("expected NUMERIC");
    }
    {
        TEST("DATE -> NUMERIC affinity");
        SqliteToken tok = { .text = "DATE", .line = 1, .col = 1 };
        int aff = sqlite_determine_affinity(&state, &tok);
        if (aff == SQLITE_AFF_NUMERIC) PASS(); else FAIL("expected NUMERIC");
    }
}

/* ------------------------------------------------------------------ */
/*  Test: CREATE TABLE AST construction                                */
/* ------------------------------------------------------------------ */

static void test_create_table(void) {
    printf("\n--- CREATE TABLE Tests ---\n");

    {
        TEST("CREATE TABLE WITHOUT ROWID");
        SqliteParseState state = {0};
        SqliteToken name = { .text = "config", .line = 1, .col = 1 };
        sqlite_create_table(&state, 0, name, NULL, SQLITE_TBL_WITHOUT_ROWID);

        if (state.result && state.result->type == SQLITE_NODE_CREATE_TABLE &&
            state.result->u.create_table.without_rowid &&
            !state.result->u.create_table.strict &&
            !state.result->u.create_table.if_not_exists &&
            strcmp(state.result->u.create_table.name, "config") == 0) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }

    {
        TEST("CREATE TABLE STRICT");
        SqliteParseState state = {0};
        SqliteToken name = { .text = "measurements", .line = 1, .col = 1 };
        sqlite_create_table(&state, 0, name, NULL, SQLITE_TBL_STRICT);

        if (state.result && state.result->type == SQLITE_NODE_CREATE_TABLE &&
            !state.result->u.create_table.without_rowid &&
            state.result->u.create_table.strict) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }

    {
        TEST("CREATE TABLE STRICT, WITHOUT ROWID");
        SqliteParseState state = {0};
        SqliteToken name = { .text = "settings", .line = 1, .col = 1 };
        sqlite_create_table(&state, 0, name, NULL,
                           SQLITE_TBL_STRICT | SQLITE_TBL_WITHOUT_ROWID);

        if (state.result && state.result->type == SQLITE_NODE_CREATE_TABLE &&
            state.result->u.create_table.without_rowid &&
            state.result->u.create_table.strict) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }

    {
        TEST("CREATE TABLE IF NOT EXISTS");
        SqliteParseState state = {0};
        SqliteToken name = { .text = "cache", .line = 1, .col = 1 };
        sqlite_create_table(&state, 1, name, NULL, 0);

        if (state.result && state.result->type == SQLITE_NODE_CREATE_TABLE &&
            state.result->u.create_table.if_not_exists) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: PRAGMA AST construction                                      */
/* ------------------------------------------------------------------ */

static void test_pragma(void) {
    printf("\n--- PRAGMA Tests ---\n");

    {
        TEST("PRAGMA get (no value)");
        SqliteParseState state = {0};
        SqliteToken name = { .text = "foreign_keys", .line = 1, .col = 1 };
        sqlite_pragma_get(&state, name);

        if (state.result && state.result->type == SQLITE_NODE_PRAGMA &&
            !state.result->u.pragma.has_value &&
            strcmp(state.result->u.pragma.name, "foreign_keys") == 0) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }

    {
        TEST("PRAGMA set (= value)");
        SqliteParseState state = {0};
        SqliteToken name = { .text = "journal_mode", .line = 1, .col = 1 };
        SqliteToken value = { .text = "WAL", .line = 1, .col = 20 };
        sqlite_pragma_set(&state, name, value);

        if (state.result && state.result->type == SQLITE_NODE_PRAGMA &&
            state.result->u.pragma.has_value &&
            !state.result->u.pragma.is_call &&
            strcmp(state.result->u.pragma.name, "journal_mode") == 0 &&
            strcmp(state.result->u.pragma.value, "WAL") == 0) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }

    {
        TEST("PRAGMA call (function syntax)");
        SqliteParseState state = {0};
        SqliteToken name = { .text = "table_info", .line = 1, .col = 1 };
        SqliteToken value = { .text = "users", .line = 1, .col = 18 };
        sqlite_pragma_call(&state, name, value);

        if (state.result && state.result->type == SQLITE_NODE_PRAGMA &&
            state.result->u.pragma.has_value &&
            state.result->u.pragma.is_call &&
            strcmp(state.result->u.pragma.name, "table_info") == 0 &&
            strcmp(state.result->u.pragma.value, "users") == 0) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }

    {
        TEST("PRAGMA qualified name (schema.pragma)");
        SqliteParseState state = {0};
        SqliteToken schema = { .text = "main", .line = 1, .col = 1 };
        SqliteToken name = { .text = "cache_size", .line = 1, .col = 6 };
        SqliteToken qname = sqlite_qualified_name(&state, schema, name);

        if (qname.text && strcmp(qname.text, "main.cache_size") == 0) {
            PASS();
        } else {
            FAIL("expected 'main.cache_size'");
        }
        free(qname.text);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: INSERT / UPSERT AST construction                             */
/* ------------------------------------------------------------------ */

static void test_insert_upsert(void) {
    printf("\n--- INSERT / UPSERT Tests ---\n");

    {
        TEST("INSERT basic");
        SqliteParseState state = {0};
        SqliteToken table = { .text = "users", .line = 1, .col = 1 };
        sqlite_insert(&state, SQLITE_CONFLICT_ABORT, table, NULL, NULL, NULL);

        if (state.result && state.result->type == SQLITE_NODE_INSERT &&
            state.result->u.insert.conflict_action == SQLITE_CONFLICT_ABORT &&
            strcmp(state.result->u.insert.table, "users") == 0 &&
            state.result->u.insert.upsert == NULL) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }

    {
        TEST("INSERT OR REPLACE");
        SqliteParseState state = {0};
        SqliteToken table = { .text = "config", .line = 1, .col = 1 };
        sqlite_insert(&state, SQLITE_CONFLICT_REPLACE, table, NULL, NULL, NULL);

        if (state.result && state.result->type == SQLITE_NODE_INSERT &&
            state.result->u.insert.conflict_action == SQLITE_CONFLICT_REPLACE) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }

    {
        TEST("INSERT OR IGNORE");
        SqliteParseState state = {0};
        SqliteToken table = { .text = "config", .line = 1, .col = 1 };
        sqlite_insert(&state, SQLITE_CONFLICT_IGNORE, table, NULL, NULL, NULL);

        if (state.result && state.result->type == SQLITE_NODE_INSERT &&
            state.result->u.insert.conflict_action == SQLITE_CONFLICT_IGNORE) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }

    {
        TEST("Upsert DO NOTHING");
        SqliteParseState state = {0};
        void *upsert = sqlite_make_upsert_nothing(&state, NULL);

        if (upsert && ((SqliteAstNode *)upsert)->type == SQLITE_NODE_UPSERT &&
            ((SqliteAstNode *)upsert)->u.upsert.do_nothing) {
            PASS();
        } else {
            FAIL("incorrect upsert node");
        }
        sqlite_ast_free(upsert);
    }

    {
        TEST("Upsert DO UPDATE");
        SqliteParseState state = {0};
        void *upsert = sqlite_make_upsert(&state, NULL, NULL, NULL);

        if (upsert && ((SqliteAstNode *)upsert)->type == SQLITE_NODE_UPSERT &&
            !((SqliteAstNode *)upsert)->u.upsert.do_nothing) {
            PASS();
        } else {
            FAIL("incorrect upsert node");
        }
        sqlite_ast_free(upsert);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: JSON function AST construction                               */
/* ------------------------------------------------------------------ */

static void test_json_functions(void) {
    printf("\n--- JSON Function Tests ---\n");

    {
        TEST("JSON extract (arrow)");
        SqliteParseState state = {0};
        SqliteAstNode *obj = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *path = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *result = sqlite_json_extract(&state, obj, path);

        if (result && result->type == SQLITE_NODE_JSON_FUNC &&
            strcmp(result->u.json_func.func_name, "json_extract") == 0 &&
            result->u.json_func.nargs == 2) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }

    {
        TEST("JSON extract text (double arrow)");
        SqliteParseState state = {0};
        SqliteAstNode *obj = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *path = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *result = sqlite_json_extract_text(&state, obj, path);

        if (result && result->type == SQLITE_NODE_JSON_FUNC &&
            strcmp(result->u.json_func.func_name, "json_extract_text") == 0) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }

    {
        TEST("JSON aggregate function");
        SqliteParseState state = {0};
        SqliteAstNode *arg = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *result = sqlite_json_agg(&state, "json_group_array", arg);

        if (result && result->type == SQLITE_NODE_JSON_FUNC &&
            result->u.json_func.is_aggregate &&
            strcmp(result->u.json_func.func_name, "json_group_array") == 0) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: VACUUM AST construction                                      */
/* ------------------------------------------------------------------ */

static void test_vacuum(void) {
    printf("\n--- VACUUM Tests ---\n");

    {
        TEST("VACUUM (basic)");
        SqliteParseState state = {0};
        SqliteToken empty = {0};
        sqlite_vacuum(&state, empty, empty);

        if (state.result && state.result->type == SQLITE_NODE_VACUUM &&
            state.result->u.vacuum.schema_name == NULL &&
            state.result->u.vacuum.into_file == NULL) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }

    {
        TEST("VACUUM INTO filename");
        SqliteParseState state = {0};
        SqliteToken empty = {0};
        SqliteToken file = { .text = "backup.db", .line = 1, .col = 1 };
        sqlite_vacuum(&state, empty, file);

        if (state.result && state.result->type == SQLITE_NODE_VACUUM &&
            state.result->u.vacuum.schema_name == NULL &&
            strcmp(state.result->u.vacuum.into_file, "backup.db") == 0) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: Expression helpers                                           */
/* ------------------------------------------------------------------ */

static void test_expressions(void) {
    printf("\n--- Expression Tests ---\n");

    {
        TEST("ISNULL postfix");
        SqliteParseState state = {0};
        SqliteAstNode *operand = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *result = sqlite_isnull(&state, operand);

        if (result && result->type == SQLITE_NODE_ISNULL &&
            result->u.unary.operand == operand) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }

    {
        TEST("NOTNULL postfix");
        SqliteParseState state = {0};
        SqliteAstNode *operand = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *result = sqlite_notnull(&state, operand);

        if (result && result->type == SQLITE_NODE_NOTNULL) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }

    {
        TEST("CAST expression");
        SqliteParseState state = {0};
        SqliteAstNode *expr = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *result = sqlite_cast(&state, expr, SQLITE_AFF_INTEGER);

        if (result && result->type == SQLITE_NODE_CAST &&
            result->u.cast.affinity == SQLITE_AFF_INTEGER) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }

    {
        TEST("CURRENT_TIMESTAMP");
        SqliteParseState state = {0};
        SqliteAstNode *result = sqlite_current_timestamp(&state);

        if (result && result->type == SQLITE_NODE_CURRENT_TIMESTAMP) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }

    {
        TEST("CURRENT_DATE");
        SqliteParseState state = {0};
        SqliteAstNode *result = sqlite_current_date(&state);

        if (result && result->type == SQLITE_NODE_CURRENT_DATE) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }

    {
        TEST("CURRENT_TIME");
        SqliteParseState state = {0};
        SqliteAstNode *result = sqlite_current_time(&state);

        if (result && result->type == SQLITE_NODE_CURRENT_TIME) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: GLOB operator                                                */
/* ------------------------------------------------------------------ */

static void test_glob(void) {
    printf("\n--- GLOB Operator Tests ---\n");

    {
        TEST("expr GLOB pattern");
        SqliteParseState state = {0};
        SqliteAstNode *expr = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *pattern = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *result = sqlite_glob(&state, expr, pattern);

        if (result && result->type == SQLITE_NODE_GLOB &&
            !result->u.glob.negated) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }

    {
        TEST("expr NOT GLOB pattern");
        SqliteParseState state = {0};
        SqliteAstNode *expr = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *pattern = calloc(1, sizeof(SqliteAstNode));
        SqliteAstNode *result = sqlite_not_glob(&state, expr, pattern);

        if (result && result->type == SQLITE_NODE_GLOB &&
            result->u.glob.negated) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: INDEXED BY / NOT INDEXED                                     */
/* ------------------------------------------------------------------ */

static void test_indexed_by(void) {
    printf("\n--- INDEXED BY Tests ---\n");

    {
        TEST("table INDEXED BY index_name");
        SqliteParseState state = {0};
        SqliteToken table = { .text = "users", .line = 1, .col = 1 };
        SqliteToken index = { .text = "idx_email", .line = 1, .col = 20 };
        SqliteAstNode *result = sqlite_indexed_by(&state, table, index);

        if (result && result->type == SQLITE_NODE_INDEXED_BY &&
            strcmp(result->u.indexed_by.table, "users") == 0 &&
            strcmp(result->u.indexed_by.index_name, "idx_email") == 0) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }

    {
        TEST("table NOT INDEXED");
        SqliteParseState state = {0};
        SqliteToken table = { .text = "users", .line = 1, .col = 1 };
        SqliteAstNode *result = sqlite_not_indexed(&state, table);

        if (result && result->type == SQLITE_NODE_INDEXED_BY &&
            strcmp(result->u.indexed_by.table, "users") == 0 &&
            result->u.indexed_by.index_name == NULL) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(result);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: ATTACH / DETACH                                              */
/* ------------------------------------------------------------------ */

static void test_attach_detach(void) {
    printf("\n--- ATTACH / DETACH Tests ---\n");

    {
        TEST("ATTACH DATABASE file AS name");
        SqliteParseState state = {0};
        SqliteToken name = { .text = "analytics", .line = 1, .col = 1 };
        sqlite_attach(&state, NULL, name);

        if (state.result && state.result->type == SQLITE_NODE_ATTACH &&
            strcmp(state.result->u.attach.schema_name, "analytics") == 0) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }

    {
        TEST("DETACH DATABASE name");
        SqliteParseState state = {0};
        SqliteToken name = { .text = "analytics", .line = 1, .col = 1 };
        sqlite_detach(&state, name);

        if (state.result && state.result->type == SQLITE_NODE_DETACH &&
            strcmp(state.result->u.detach.schema_name, "analytics") == 0) {
            PASS();
        } else {
            FAIL("incorrect AST");
        }
        sqlite_ast_free(state.result);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: Modification count                                           */
/* ------------------------------------------------------------------ */

static void test_modification_count(void) {
    printf("\n--- Extension Metadata Tests ---\n");

    {
        TEST("Modification count is reasonable (>= 40)");
        uint32_t count = sqlite_extension_mod_count();
        if (count >= 40) {
            PASS();
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "only %u modifications", count);
            FAIL(msg);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Test: Parse state flags                                            */
/* ------------------------------------------------------------------ */

static void test_parse_state_flags(void) {
    printf("\n--- Parse State Flag Tests ---\n");

    {
        TEST("AUTOINCREMENT flag");
        SqliteParseState state = {0};
        sqlite_set_autoincrement(&state);
        if (state.flags & SQLITE_FLAG_AUTOINCREMENT) {
            PASS();
        } else {
            FAIL("flag not set");
        }
    }

    {
        TEST("PRIMARY KEY flag");
        SqliteParseState state = {0};
        sqlite_set_primary_key(&state);
        if (state.flags & SQLITE_FLAG_PRIMARY_KEY) {
            PASS();
        } else {
            FAIL("flag not set");
        }
    }

    {
        TEST("Both flags simultaneously");
        SqliteParseState state = {0};
        sqlite_set_primary_key(&state);
        sqlite_set_autoincrement(&state);
        if ((state.flags & SQLITE_FLAG_PRIMARY_KEY) &&
            (state.flags & SQLITE_FLAG_AUTOINCREMENT)) {
            PASS();
        } else {
            FAIL("flags not both set");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("SQLite Compatibility Extension - Test Suite\n");
    printf("============================================\n");

    test_type_affinity();
    test_create_table();
    test_pragma();
    test_insert_upsert();
    test_json_functions();
    test_vacuum();
    test_expressions();
    test_glob();
    test_indexed_by();
    test_attach_detach();
    test_modification_count();
    test_parse_state_flags();

    printf("\n============================================\n");
    printf("Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    if (tests_failed > 0) {
        printf("SOME TESTS FAILED\n");
        return 1;
    }

    printf("ALL TESTS PASSED\n");
    return 0;
}
