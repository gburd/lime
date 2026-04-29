/*
** SQLite Compatibility Extension - Semantic Actions
**
** Implements the semantic action functions referenced by the grammar
** rules in sqlite_grammar.lime.  These build an AST representation
** of SQLite-specific SQL constructs.
**
** In a full integration these functions would construct AST nodes
** compatible with the host application's tree structure.  This
** implementation provides a demonstrative skeleton using a simple
** tagged-union AST.
*/

#include "sqlite_semantics.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/*  AST node allocation helpers                                        */
/* ------------------------------------------------------------------ */

static SqliteAstNode *ast_alloc(SqliteNodeType type) {
    SqliteAstNode *n = calloc(1, sizeof(SqliteAstNode));
    if (n) n->type = type;
    return n;
}

static char *dup_string(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

/* ------------------------------------------------------------------ */
/*  Type affinity determination                                        */
/* ------------------------------------------------------------------ */

/*
** Determine the SQLite type affinity for a given type name string.
** Follows the rules from https://www.sqlite.org/datatype3.html:
**
**   1. If the type name contains "INT"  -> INTEGER affinity
**   2. If contains "CHAR", "CLOB", or "TEXT" -> TEXT affinity
**   3. If contains "BLOB" or is empty -> BLOB (NONE) affinity
**   4. If contains "REAL", "FLOA", or "DOUB" -> REAL affinity
**   5. Otherwise -> NUMERIC affinity
*/
int sqlite_determine_affinity(SqliteParseState *pstate, const SqliteToken *tok) {
    (void)pstate;

    if (!tok || !tok->text) return SQLITE_AFF_BLOB;

    /* Convert to uppercase for matching */
    size_t len = strlen(tok->text);
    char *upper = malloc(len + 1);
    if (!upper) return SQLITE_AFF_NUMERIC;

    for (size_t i = 0; i < len; i++) {
        upper[i] = (char)toupper((unsigned char)tok->text[i]);
    }
    upper[len] = '\0';

    int affinity;

    if (strstr(upper, "INT")) {
        affinity = SQLITE_AFF_INTEGER;
    } else if (strstr(upper, "CHAR") || strstr(upper, "CLOB") ||
               strstr(upper, "TEXT")) {
        affinity = SQLITE_AFF_TEXT;
    } else if (strstr(upper, "BLOB") || len == 0) {
        affinity = SQLITE_AFF_BLOB;
    } else if (strstr(upper, "REAL") || strstr(upper, "FLOA") ||
               strstr(upper, "DOUB")) {
        affinity = SQLITE_AFF_REAL;
    } else {
        affinity = SQLITE_AFF_NUMERIC;
    }

    free(upper);
    return affinity;
}

/* ------------------------------------------------------------------ */
/*  CREATE TABLE                                                       */
/* ------------------------------------------------------------------ */

void sqlite_create_table(SqliteParseState *pstate, int if_not_exists,
                         SqliteToken name, void *columns, int options) {
    (void)columns;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_CREATE_TABLE);
    if (!node) return;

    node->u.create_table.name = dup_string(name.text);
    node->u.create_table.if_not_exists = if_not_exists;
    node->u.create_table.without_rowid =
        (options & SQLITE_TBL_WITHOUT_ROWID) != 0;
    node->u.create_table.strict =
        (options & SQLITE_TBL_STRICT) != 0;

    pstate->result = node;
}

void sqlite_set_autoincrement(SqliteParseState *pstate) {
    pstate->flags |= SQLITE_FLAG_AUTOINCREMENT;
}

void sqlite_set_primary_key(SqliteParseState *pstate) {
    pstate->flags |= SQLITE_FLAG_PRIMARY_KEY;
}

/* ------------------------------------------------------------------ */
/*  INSERT / UPSERT                                                    */
/* ------------------------------------------------------------------ */

void sqlite_insert(SqliteParseState *pstate, int conflict_action,
                   SqliteToken table, void *columns, void *values,
                   void *upsert) {
    (void)columns;
    (void)values;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_INSERT);
    if (!node) return;

    node->u.insert.table = dup_string(table.text);
    node->u.insert.conflict_action = conflict_action;
    node->u.insert.upsert = (SqliteAstNode *)upsert;

    pstate->result = node;
}

void *sqlite_make_upsert(SqliteParseState *pstate, void *target,
                         void *set_list, void *where_clause) {
    (void)pstate;
    (void)target;
    (void)set_list;
    (void)where_clause;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_UPSERT);
    if (!node) return NULL;

    node->u.upsert.do_nothing = 0;
    return node;
}

void *sqlite_make_upsert_nothing(SqliteParseState *pstate, void *target) {
    (void)pstate;
    (void)target;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_UPSERT);
    if (!node) return NULL;

    node->u.upsert.do_nothing = 1;
    return node;
}

/* ------------------------------------------------------------------ */
/*  PRAGMA                                                             */
/* ------------------------------------------------------------------ */

void sqlite_pragma_get(SqliteParseState *pstate, SqliteToken name) {
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_PRAGMA);
    if (!node) return;

    node->u.pragma.name = dup_string(name.text);
    node->u.pragma.has_value = 0;

    pstate->result = node;
}

void sqlite_pragma_set(SqliteParseState *pstate, SqliteToken name,
                       SqliteToken value) {
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_PRAGMA);
    if (!node) return;

    node->u.pragma.name = dup_string(name.text);
    node->u.pragma.value = dup_string(value.text);
    node->u.pragma.has_value = 1;
    node->u.pragma.is_call = 0;

    pstate->result = node;
}

void sqlite_pragma_call(SqliteParseState *pstate, SqliteToken name,
                        SqliteToken value) {
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_PRAGMA);
    if (!node) return;

    node->u.pragma.name = dup_string(name.text);
    node->u.pragma.value = dup_string(value.text);
    node->u.pragma.has_value = 1;
    node->u.pragma.is_call = 1;

    pstate->result = node;
}

SqliteToken sqlite_qualified_name(SqliteParseState *pstate,
                                  SqliteToken schema, SqliteToken name) {
    (void)pstate;
    SqliteToken result;
    size_t slen = schema.text ? strlen(schema.text) : 0;
    size_t nlen = name.text ? strlen(name.text) : 0;

    result.text = malloc(slen + 1 + nlen + 1);
    if (result.text) {
        snprintf(result.text, slen + 1 + nlen + 1, "%s.%s",
                 schema.text ? schema.text : "",
                 name.text ? name.text : "");
    }
    result.line = schema.line;
    result.col = schema.col;
    return result;
}

SqliteToken sqlite_make_ident(SqliteParseState *pstate, const char *text) {
    (void)pstate;
    SqliteToken tok;
    tok.text = dup_string(text);
    tok.line = 0;
    tok.col = 0;
    return tok;
}

/* ------------------------------------------------------------------ */
/*  JSON functions                                                     */
/* ------------------------------------------------------------------ */

SqliteAstNode *sqlite_json_extract(SqliteParseState *pstate,
                                   SqliteAstNode *obj,
                                   SqliteAstNode *path) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_JSON_FUNC);
    if (!node) return NULL;

    node->u.json_func.func_name = dup_string("json_extract");
    node->u.json_func.args[0] = obj;
    node->u.json_func.args[1] = path;
    node->u.json_func.nargs = 2;
    return node;
}

SqliteAstNode *sqlite_json_extract_text(SqliteParseState *pstate,
                                        SqliteAstNode *obj,
                                        SqliteAstNode *path) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_JSON_FUNC);
    if (!node) return NULL;

    node->u.json_func.func_name = dup_string("json_extract_text");
    node->u.json_func.args[0] = obj;
    node->u.json_func.args[1] = path;
    node->u.json_func.nargs = 2;
    return node;
}

SqliteAstNode *sqlite_json_func(SqliteParseState *pstate,
                                const char *name, void *args) {
    (void)pstate;
    (void)args;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_JSON_FUNC);
    if (!node) return NULL;

    node->u.json_func.func_name = dup_string(name);
    return node;
}

SqliteAstNode *sqlite_json_func2(SqliteParseState *pstate,
                                 const char *name,
                                 SqliteAstNode *arg1,
                                 SqliteAstNode *arg2) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_JSON_FUNC);
    if (!node) return NULL;

    node->u.json_func.func_name = dup_string(name);
    node->u.json_func.args[0] = arg1;
    node->u.json_func.args[1] = arg2;
    node->u.json_func.nargs = 2;
    return node;
}

SqliteAstNode *sqlite_json_agg(SqliteParseState *pstate,
                               const char *name, SqliteAstNode *arg) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_JSON_FUNC);
    if (!node) return NULL;

    node->u.json_func.func_name = dup_string(name);
    node->u.json_func.args[0] = arg;
    node->u.json_func.nargs = 1;
    node->u.json_func.is_aggregate = 1;
    return node;
}

SqliteAstNode *sqlite_json_agg2(SqliteParseState *pstate,
                                const char *name,
                                SqliteAstNode *arg1,
                                SqliteAstNode *arg2) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_JSON_FUNC);
    if (!node) return NULL;

    node->u.json_func.func_name = dup_string(name);
    node->u.json_func.args[0] = arg1;
    node->u.json_func.args[1] = arg2;
    node->u.json_func.nargs = 2;
    node->u.json_func.is_aggregate = 1;
    return node;
}

SqliteAstNode *sqlite_json_table_func(SqliteParseState *pstate,
                                      const char *name, void *args) {
    (void)pstate;
    (void)args;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_JSON_TABLE_FUNC);
    if (!node) return NULL;

    node->u.json_func.func_name = dup_string(name);
    return node;
}

/* ------------------------------------------------------------------ */
/*  ATTACH / DETACH                                                    */
/* ------------------------------------------------------------------ */

void sqlite_attach(SqliteParseState *pstate, SqliteAstNode *file,
                   SqliteToken name) {
    (void)file;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_ATTACH);
    if (!node) return;

    node->u.attach.schema_name = dup_string(name.text);
    pstate->result = node;
}

void sqlite_detach(SqliteParseState *pstate, SqliteToken name) {
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_DETACH);
    if (!node) return;

    node->u.detach.schema_name = dup_string(name.text);
    pstate->result = node;
}

/* ------------------------------------------------------------------ */
/*  VACUUM                                                             */
/* ------------------------------------------------------------------ */

void sqlite_vacuum(SqliteParseState *pstate, SqliteToken schema,
                   SqliteToken into_file) {
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_VACUUM);
    if (!node) return;

    node->u.vacuum.schema_name = schema.text ? dup_string(schema.text) : NULL;
    node->u.vacuum.into_file = into_file.text ? dup_string(into_file.text) : NULL;

    pstate->result = node;
}

/* ------------------------------------------------------------------ */
/*  EXPLAIN                                                            */
/* ------------------------------------------------------------------ */

void sqlite_explain(SqliteParseState *pstate, SqliteAstNode *stmt,
                    int query_plan) {
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_EXPLAIN);
    if (!node) return;

    node->u.explain.inner_stmt = stmt;
    node->u.explain.query_plan = query_plan;

    pstate->result = node;
}

/* ------------------------------------------------------------------ */
/*  GLOB operator                                                      */
/* ------------------------------------------------------------------ */

SqliteAstNode *sqlite_glob(SqliteParseState *pstate,
                           SqliteAstNode *expr, SqliteAstNode *pattern) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_GLOB);
    if (!node) return NULL;

    node->u.glob.expr = expr;
    node->u.glob.pattern = pattern;
    node->u.glob.negated = 0;
    return node;
}

SqliteAstNode *sqlite_not_glob(SqliteParseState *pstate,
                               SqliteAstNode *expr,
                               SqliteAstNode *pattern) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_GLOB);
    if (!node) return NULL;

    node->u.glob.expr = expr;
    node->u.glob.pattern = pattern;
    node->u.glob.negated = 1;
    return node;
}

/* ------------------------------------------------------------------ */
/*  INDEXED BY / NOT INDEXED                                           */
/* ------------------------------------------------------------------ */

SqliteAstNode *sqlite_indexed_by(SqliteParseState *pstate,
                                 SqliteToken table, SqliteToken index) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_INDEXED_BY);
    if (!node) return NULL;

    node->u.indexed_by.table = dup_string(table.text);
    node->u.indexed_by.index_name = dup_string(index.text);
    return node;
}

SqliteAstNode *sqlite_not_indexed(SqliteParseState *pstate,
                                  SqliteToken table) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_INDEXED_BY);
    if (!node) return NULL;

    node->u.indexed_by.table = dup_string(table.text);
    node->u.indexed_by.index_name = NULL;  /* NOT INDEXED */
    return node;
}

/* ------------------------------------------------------------------ */
/*  REINDEX                                                            */
/* ------------------------------------------------------------------ */

void sqlite_reindex(SqliteParseState *pstate, SqliteToken name) {
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_REINDEX);
    if (!node) return;

    node->u.reindex.name = name.text ? dup_string(name.text) : NULL;
    pstate->result = node;
}

void sqlite_reindex_qualified(SqliteParseState *pstate,
                              SqliteToken schema, SqliteToken name) {
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_REINDEX);
    if (!node) return;

    SqliteToken qname = sqlite_qualified_name(pstate, schema, name);
    node->u.reindex.name = qname.text;
    pstate->result = node;
}

/* ------------------------------------------------------------------ */
/*  Expression helpers                                                 */
/* ------------------------------------------------------------------ */

SqliteAstNode *sqlite_isnull(SqliteParseState *pstate, SqliteAstNode *expr) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_ISNULL);
    if (!node) return NULL;

    node->u.unary.operand = expr;
    return node;
}

SqliteAstNode *sqlite_notnull(SqliteParseState *pstate, SqliteAstNode *expr) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_NOTNULL);
    if (!node) return NULL;

    node->u.unary.operand = expr;
    return node;
}

SqliteAstNode *sqlite_cast(SqliteParseState *pstate,
                           SqliteAstNode *expr, int affinity) {
    (void)pstate;
    SqliteAstNode *node = ast_alloc(SQLITE_NODE_CAST);
    if (!node) return NULL;

    node->u.cast.expr = expr;
    node->u.cast.affinity = affinity;
    return node;
}

SqliteAstNode *sqlite_current_timestamp(SqliteParseState *pstate) {
    (void)pstate;
    return ast_alloc(SQLITE_NODE_CURRENT_TIMESTAMP);
}

SqliteAstNode *sqlite_current_date(SqliteParseState *pstate) {
    (void)pstate;
    return ast_alloc(SQLITE_NODE_CURRENT_DATE);
}

SqliteAstNode *sqlite_current_time(SqliteParseState *pstate) {
    (void)pstate;
    return ast_alloc(SQLITE_NODE_CURRENT_TIME);
}

/* ------------------------------------------------------------------ */
/*  AST cleanup                                                        */
/* ------------------------------------------------------------------ */

void sqlite_ast_free(SqliteAstNode *node) {
    if (!node) return;

    switch (node->type) {
    case SQLITE_NODE_CREATE_TABLE:
        free(node->u.create_table.name);
        break;

    case SQLITE_NODE_INSERT:
        free(node->u.insert.table);
        sqlite_ast_free(node->u.insert.upsert);
        break;

    case SQLITE_NODE_PRAGMA:
        free(node->u.pragma.name);
        free(node->u.pragma.value);
        break;

    case SQLITE_NODE_JSON_FUNC:
    case SQLITE_NODE_JSON_TABLE_FUNC:
        free(node->u.json_func.func_name);
        for (int i = 0; i < node->u.json_func.nargs; i++) {
            sqlite_ast_free(node->u.json_func.args[i]);
        }
        break;

    case SQLITE_NODE_ATTACH:
        free(node->u.attach.schema_name);
        break;

    case SQLITE_NODE_DETACH:
        free(node->u.detach.schema_name);
        break;

    case SQLITE_NODE_VACUUM:
        free(node->u.vacuum.schema_name);
        free(node->u.vacuum.into_file);
        break;

    case SQLITE_NODE_EXPLAIN:
        sqlite_ast_free(node->u.explain.inner_stmt);
        break;

    case SQLITE_NODE_GLOB:
        sqlite_ast_free(node->u.glob.expr);
        sqlite_ast_free(node->u.glob.pattern);
        break;

    case SQLITE_NODE_INDEXED_BY:
        free(node->u.indexed_by.table);
        free(node->u.indexed_by.index_name);
        break;

    case SQLITE_NODE_REINDEX:
        free(node->u.reindex.name);
        break;

    case SQLITE_NODE_ISNULL:
    case SQLITE_NODE_NOTNULL:
        sqlite_ast_free(node->u.unary.operand);
        break;

    case SQLITE_NODE_CAST:
        sqlite_ast_free(node->u.cast.expr);
        break;

    case SQLITE_NODE_UPSERT:
    case SQLITE_NODE_CURRENT_TIMESTAMP:
    case SQLITE_NODE_CURRENT_DATE:
    case SQLITE_NODE_CURRENT_TIME:
        /* No dynamic members */
        break;
    }

    free(node);
}
