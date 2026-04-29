/*
** SQLite Compatibility Extension - Semantic Types and Declarations
**
** Defines the AST node types, parse state, and function prototypes
** for the SQLite grammar extension's semantic actions.
*/
#ifndef SQLITE_SEMANTICS_H
#define SQLITE_SEMANTICS_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Table option flags                                                 */
/* ------------------------------------------------------------------ */

#define SQLITE_TBL_WITHOUT_ROWID  0x01
#define SQLITE_TBL_STRICT         0x02

/* ------------------------------------------------------------------ */
/*  Conflict action constants                                          */
/* ------------------------------------------------------------------ */

#define SQLITE_CONFLICT_ROLLBACK  1
#define SQLITE_CONFLICT_ABORT     2
#define SQLITE_CONFLICT_FAIL      3
#define SQLITE_CONFLICT_IGNORE    4
#define SQLITE_CONFLICT_REPLACE   5

/* ------------------------------------------------------------------ */
/*  Type affinity constants                                            */
/* ------------------------------------------------------------------ */

#define SQLITE_AFF_INTEGER  1
#define SQLITE_AFF_TEXT     2
#define SQLITE_AFF_REAL     3
#define SQLITE_AFF_BLOB     4
#define SQLITE_AFF_NUMERIC  5

/* ------------------------------------------------------------------ */
/*  Parse state flags                                                  */
/* ------------------------------------------------------------------ */

#define SQLITE_FLAG_AUTOINCREMENT  0x01
#define SQLITE_FLAG_PRIMARY_KEY    0x02

/* ------------------------------------------------------------------ */
/*  Token type                                                         */
/* ------------------------------------------------------------------ */

typedef struct SqliteToken {
    char *text;         /* Token text (may be dynamically allocated) */
    int   line;         /* Source line number */
    int   col;          /* Source column number */
} SqliteToken;

/* ------------------------------------------------------------------ */
/*  AST node types                                                     */
/* ------------------------------------------------------------------ */

typedef enum SqliteNodeType {
    SQLITE_NODE_CREATE_TABLE,
    SQLITE_NODE_INSERT,
    SQLITE_NODE_UPSERT,
    SQLITE_NODE_PRAGMA,
    SQLITE_NODE_JSON_FUNC,
    SQLITE_NODE_JSON_TABLE_FUNC,
    SQLITE_NODE_ATTACH,
    SQLITE_NODE_DETACH,
    SQLITE_NODE_VACUUM,
    SQLITE_NODE_EXPLAIN,
    SQLITE_NODE_GLOB,
    SQLITE_NODE_INDEXED_BY,
    SQLITE_NODE_REINDEX,
    SQLITE_NODE_ISNULL,
    SQLITE_NODE_NOTNULL,
    SQLITE_NODE_CAST,
    SQLITE_NODE_CURRENT_TIMESTAMP,
    SQLITE_NODE_CURRENT_DATE,
    SQLITE_NODE_CURRENT_TIME,
} SqliteNodeType;

/* ------------------------------------------------------------------ */
/*  AST node                                                           */
/* ------------------------------------------------------------------ */

#define SQLITE_JSON_MAX_ARGS 8

typedef struct SqliteAstNode SqliteAstNode;

struct SqliteAstNode {
    SqliteNodeType type;

    union {
        /* CREATE TABLE */
        struct {
            char *name;
            int   if_not_exists;
            int   without_rowid;
            int   strict;
        } create_table;

        /* INSERT */
        struct {
            char *table;
            int   conflict_action;
            SqliteAstNode *upsert;
        } insert;

        /* UPSERT (ON CONFLICT clause) */
        struct {
            int do_nothing;
        } upsert;

        /* PRAGMA */
        struct {
            char *name;
            char *value;
            int   has_value;
            int   is_call;
        } pragma;

        /* JSON functions */
        struct {
            char *func_name;
            SqliteAstNode *args[SQLITE_JSON_MAX_ARGS];
            int   nargs;
            int   is_aggregate;
        } json_func;

        /* ATTACH */
        struct {
            char *schema_name;
        } attach;

        /* DETACH */
        struct {
            char *schema_name;
        } detach;

        /* VACUUM */
        struct {
            char *schema_name;
            char *into_file;
        } vacuum;

        /* EXPLAIN */
        struct {
            SqliteAstNode *inner_stmt;
            int query_plan;
        } explain;

        /* GLOB */
        struct {
            SqliteAstNode *expr;
            SqliteAstNode *pattern;
            int negated;
        } glob;

        /* INDEXED BY */
        struct {
            char *table;
            char *index_name;  /* NULL for NOT INDEXED */
        } indexed_by;

        /* REINDEX */
        struct {
            char *name;
        } reindex;

        /* Unary postfix (ISNULL, NOTNULL) */
        struct {
            SqliteAstNode *operand;
        } unary;

        /* CAST */
        struct {
            SqliteAstNode *expr;
            int affinity;
        } cast;
    } u;
};

/* ------------------------------------------------------------------ */
/*  Parse state                                                        */
/* ------------------------------------------------------------------ */

typedef struct SqliteParseState {
    SqliteAstNode *result;   /* Root AST node after parse completes */
    uint32_t       flags;    /* Accumulated column constraint flags */
    int            nerrors;  /* Number of parse errors              */
    char          *errmsg;   /* Last error message (owned)          */
} SqliteParseState;

/* ------------------------------------------------------------------ */
/*  Semantic action prototypes                                         */
/* ------------------------------------------------------------------ */

/* Type affinity */
int sqlite_determine_affinity(SqliteParseState *pstate,
                              const SqliteToken *tok);

/* CREATE TABLE */
void sqlite_create_table(SqliteParseState *pstate, int if_not_exists,
                         SqliteToken name, void *columns, int options);
void sqlite_set_autoincrement(SqliteParseState *pstate);
void sqlite_set_primary_key(SqliteParseState *pstate);

/* INSERT / UPSERT */
void sqlite_insert(SqliteParseState *pstate, int conflict_action,
                   SqliteToken table, void *columns, void *values,
                   void *upsert);
void *sqlite_make_upsert(SqliteParseState *pstate, void *target,
                         void *set_list, void *where_clause);
void *sqlite_make_upsert_nothing(SqliteParseState *pstate, void *target);

/* PRAGMA */
void sqlite_pragma_get(SqliteParseState *pstate, SqliteToken name);
void sqlite_pragma_set(SqliteParseState *pstate, SqliteToken name,
                       SqliteToken value);
void sqlite_pragma_call(SqliteParseState *pstate, SqliteToken name,
                        SqliteToken value);
SqliteToken sqlite_qualified_name(SqliteParseState *pstate,
                                  SqliteToken schema, SqliteToken name);
SqliteToken sqlite_make_ident(SqliteParseState *pstate, const char *text);

/* JSON functions */
SqliteAstNode *sqlite_json_extract(SqliteParseState *pstate,
                                   SqliteAstNode *obj,
                                   SqliteAstNode *path);
SqliteAstNode *sqlite_json_extract_text(SqliteParseState *pstate,
                                        SqliteAstNode *obj,
                                        SqliteAstNode *path);
SqliteAstNode *sqlite_json_func(SqliteParseState *pstate,
                                const char *name, void *args);
SqliteAstNode *sqlite_json_func2(SqliteParseState *pstate,
                                 const char *name,
                                 SqliteAstNode *arg1,
                                 SqliteAstNode *arg2);
SqliteAstNode *sqlite_json_agg(SqliteParseState *pstate,
                               const char *name, SqliteAstNode *arg);
SqliteAstNode *sqlite_json_agg2(SqliteParseState *pstate,
                                const char *name,
                                SqliteAstNode *arg1,
                                SqliteAstNode *arg2);
SqliteAstNode *sqlite_json_table_func(SqliteParseState *pstate,
                                      const char *name, void *args);

/* ATTACH / DETACH */
void sqlite_attach(SqliteParseState *pstate, SqliteAstNode *file,
                   SqliteToken name);
void sqlite_detach(SqliteParseState *pstate, SqliteToken name);

/* VACUUM */
void sqlite_vacuum(SqliteParseState *pstate, SqliteToken schema,
                   SqliteToken into_file);

/* EXPLAIN */
void sqlite_explain(SqliteParseState *pstate, SqliteAstNode *stmt,
                    int query_plan);

/* GLOB */
SqliteAstNode *sqlite_glob(SqliteParseState *pstate,
                           SqliteAstNode *expr, SqliteAstNode *pattern);
SqliteAstNode *sqlite_not_glob(SqliteParseState *pstate,
                               SqliteAstNode *expr,
                               SqliteAstNode *pattern);

/* INDEXED BY */
SqliteAstNode *sqlite_indexed_by(SqliteParseState *pstate,
                                 SqliteToken table, SqliteToken index);
SqliteAstNode *sqlite_not_indexed(SqliteParseState *pstate,
                                  SqliteToken table);

/* REINDEX */
void sqlite_reindex(SqliteParseState *pstate, SqliteToken name);
void sqlite_reindex_qualified(SqliteParseState *pstate,
                              SqliteToken schema, SqliteToken name);

/* Expression helpers */
SqliteAstNode *sqlite_isnull(SqliteParseState *pstate, SqliteAstNode *expr);
SqliteAstNode *sqlite_notnull(SqliteParseState *pstate, SqliteAstNode *expr);
SqliteAstNode *sqlite_cast(SqliteParseState *pstate,
                           SqliteAstNode *expr, int affinity);
SqliteAstNode *sqlite_current_timestamp(SqliteParseState *pstate);
SqliteAstNode *sqlite_current_date(SqliteParseState *pstate);
SqliteAstNode *sqlite_current_time(SqliteParseState *pstate);

/* AST cleanup */
void sqlite_ast_free(SqliteAstNode *node);

#endif /* SQLITE_SEMANTICS_H */
