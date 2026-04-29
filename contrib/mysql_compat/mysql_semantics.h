/*
** MySQL SQL Compatibility -- Semantic Types and Action Prototypes
**
** Defines the AST node types and semantic action function prototypes
** used by the MySQL grammar extension.  The grammar's reduction
** actions call these functions to build a MySQL-flavored AST.
*/
#ifndef MYSQL_SEMANTICS_H
#define MYSQL_SEMANTICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Token value                                                        */
/* ------------------------------------------------------------------ */

typedef struct MysqlToken {
    int         ival;
    double      fval;
    const char *sval;
    int         line;
    int         col;
} MysqlToken;

/* ------------------------------------------------------------------ */
/*  AST node types                                                     */
/* ------------------------------------------------------------------ */

typedef enum MysqlNodeType {
    MYSQL_NODE_STMT,
    MYSQL_NODE_SELECT,
    MYSQL_NODE_CREATE_TABLE,
    MYSQL_NODE_INSERT,
    MYSQL_NODE_SHOW,
    MYSQL_NODE_EXPR,
    MYSQL_NODE_SELECT_LIST,
    MYSQL_NODE_SELECT_ITEM,
    MYSQL_NODE_FROM_LIST,
    MYSQL_NODE_FROM_ITEM,
    MYSQL_NODE_ORDER_CLAUSE,
    MYSQL_NODE_ORDER_ITEM,
    MYSQL_NODE_LIMIT_CLAUSE,
    MYSQL_NODE_TABLE_OPTION,
    MYSQL_NODE_COLUMN_DEF,
    MYSQL_NODE_UPSERT_CLAUSE,
    MYSQL_NODE_ARG_LIST,
} MysqlNodeType;

/* ------------------------------------------------------------------ */
/*  Expression subtypes                                                */
/* ------------------------------------------------------------------ */

typedef enum MysqlExprType {
    MYSQL_EXPR_IDENT,
    MYSQL_EXPR_BACKTICK_IDENT,  /* `identifier` */
    MYSQL_EXPR_QUALIFIED_IDENT,
    MYSQL_EXPR_ICONST,
    MYSQL_EXPR_FCONST,
    MYSQL_EXPR_SCONST,
    MYSQL_EXPR_NULL,
    MYSQL_EXPR_BINOP,
    MYSQL_EXPR_UNARYOP,
    MYSQL_EXPR_IS_NULL,
    MYSQL_EXPR_IFNULL,
    MYSQL_EXPR_IF_FUNC,
    MYSQL_EXPR_CONCAT,
    MYSQL_EXPR_GROUP_CONCAT,
    MYSQL_EXPR_FUNC_CALL,
    MYSQL_EXPR_INTERVAL,
    MYSQL_EXPR_LIMIT,
} MysqlExprType;

/* ------------------------------------------------------------------ */
/*  Binary operator codes                                              */
/* ------------------------------------------------------------------ */

typedef enum MysqlBinOp {
    MYSQL_OP_ADD,
    MYSQL_OP_SUB,
    MYSQL_OP_MUL,
    MYSQL_OP_DIV,
    MYSQL_OP_INT_DIV,      /* DIV keyword */
    MYSQL_OP_MOD,          /* MOD or % */
    MYSQL_OP_EQ,
    MYSQL_OP_NE,           /* != or <> */
    MYSQL_OP_LT,
    MYSQL_OP_GT,
    MYSQL_OP_LE,
    MYSQL_OP_GE,
    MYSQL_OP_AND,
    MYSQL_OP_OR,
    MYSQL_OP_NOT,
    MYSQL_OP_XOR,          /* MySQL XOR operator */
    MYSQL_OP_REGEXP,       /* REGEXP / RLIKE */
    MYSQL_OP_LIKE,
    MYSQL_OP_NULL_SAFE_EQ, /* <=> */
} MysqlBinOp;

/* ------------------------------------------------------------------ */
/*  Interval units                                                     */
/* ------------------------------------------------------------------ */

typedef enum MysqlIntervalUnit {
    MYSQL_INTERVAL_SECOND,
    MYSQL_INTERVAL_MINUTE,
    MYSQL_INTERVAL_HOUR,
    MYSQL_INTERVAL_DAY,
    MYSQL_INTERVAL_WEEK,
    MYSQL_INTERVAL_MONTH,
    MYSQL_INTERVAL_YEAR,
} MysqlIntervalUnit;

/* ------------------------------------------------------------------ */
/*  SHOW statement types                                               */
/* ------------------------------------------------------------------ */

typedef enum MysqlShowType {
    MYSQL_SHOW_TABLES,
    MYSQL_SHOW_DATABASES,
    MYSQL_SHOW_COLUMNS,
    MYSQL_SHOW_INDEX,
    MYSQL_SHOW_STATUS,
    MYSQL_SHOW_VARIABLES,
    MYSQL_SHOW_GRANTS,
    MYSQL_SHOW_CREATE_TABLE,
    MYSQL_SHOW_PROCESSLIST,
    MYSQL_SHOW_WARNINGS,
    MYSQL_SHOW_ERRORS,
    MYSQL_SHOW_ENGINE_STATUS,
} MysqlShowType;

/* ------------------------------------------------------------------ */
/*  Table engine types                                                 */
/* ------------------------------------------------------------------ */

typedef enum MysqlEngine {
    MYSQL_ENGINE_INNODB,
    MYSQL_ENGINE_MYISAM,
    MYSQL_ENGINE_MEMORY,
    MYSQL_ENGINE_CSV,
    MYSQL_ENGINE_ARCHIVE,
    MYSQL_ENGINE_BLACKHOLE,
} MysqlEngine;

/* ------------------------------------------------------------------ */
/*  AST node structures                                                */
/* ------------------------------------------------------------------ */

typedef struct MysqlExpr {
    MysqlExprType type;
    union {
        /* MYSQL_EXPR_IDENT / MYSQL_EXPR_BACKTICK_IDENT */
        struct { char *name; } ident;

        /* MYSQL_EXPR_QUALIFIED_IDENT (db.table.column) */
        struct { char *schema; char *table; char *column; } qual_ident;

        /* MYSQL_EXPR_ICONST */
        int64_t iconst;

        /* MYSQL_EXPR_FCONST */
        double fconst;

        /* MYSQL_EXPR_SCONST */
        struct { char *value; } sconst;

        /* MYSQL_EXPR_BINOP */
        struct {
            MysqlBinOp op;
            struct MysqlExpr *left;
            struct MysqlExpr *right;
        } binop;

        /* MYSQL_EXPR_UNARYOP */
        struct {
            MysqlBinOp op;
            struct MysqlExpr *operand;
        } unaryop;

        /* MYSQL_EXPR_IS_NULL */
        struct {
            struct MysqlExpr *operand;
            bool negated;
        } is_null;

        /* MYSQL_EXPR_IFNULL */
        struct {
            struct MysqlExpr *expr1;
            struct MysqlExpr *expr2;
        } ifnull;

        /* MYSQL_EXPR_IF_FUNC: IF(cond, then, else) */
        struct {
            struct MysqlExpr *cond;
            struct MysqlExpr *then_expr;
            struct MysqlExpr *else_expr;
        } if_func;

        /* MYSQL_EXPR_CONCAT */
        struct {
            struct MysqlExpr **args;
            int nargs;
        } concat;

        /* MYSQL_EXPR_GROUP_CONCAT */
        struct {
            struct MysqlExpr **args;
            int nargs;
            const char *separator;
            bool distinct;
            struct MysqlExpr *order_by;
        } group_concat;

        /* MYSQL_EXPR_FUNC_CALL */
        struct {
            char *func_name;
            struct MysqlArgList *args;
        } func_call;

        /* MYSQL_EXPR_INTERVAL */
        struct {
            struct MysqlExpr *value;
            MysqlIntervalUnit unit;
        } interval;

        /* MYSQL_EXPR_LIMIT */
        struct {
            struct MysqlExpr *count;
            struct MysqlExpr *offset;  /* NULL if no OFFSET */
        } limit;
    } u;
} MysqlExpr;

/* ------------------------------------------------------------------ */
/*  Table options                                                      */
/* ------------------------------------------------------------------ */

typedef struct MysqlTableOption {
    enum {
        MYSQL_TOPT_ENGINE,
        MYSQL_TOPT_CHARSET,
        MYSQL_TOPT_COLLATE,
        MYSQL_TOPT_AUTO_INCREMENT,
        MYSQL_TOPT_COMMENT,
        MYSQL_TOPT_ROW_FORMAT,
    } type;
    union {
        MysqlEngine engine;
        char *charset;
        char *collate;
        int64_t auto_increment_start;
        char *comment;
        char *row_format;
    } u;
    struct MysqlTableOption *next;
} MysqlTableOption;

/* ------------------------------------------------------------------ */
/*  Column definition                                                  */
/* ------------------------------------------------------------------ */

typedef struct MysqlColumnDef {
    char *name;
    char *type_name;
    int type_size;             /* e.g., VARCHAR(255) -> 255 */
    bool is_not_null;
    bool is_auto_increment;
    bool is_unsigned;
    bool is_primary_key;
    bool is_unique;
    MysqlExpr *default_val;    /* NULL if no default */
    char *charset;             /* Per-column charset (may be NULL) */
    char *collate;             /* Per-column collation (may be NULL) */
    char *comment;             /* Column comment (may be NULL) */
    struct MysqlColumnDef *next;
} MysqlColumnDef;

/* ------------------------------------------------------------------ */
/*  List and container types                                           */
/* ------------------------------------------------------------------ */

typedef struct MysqlSelectItem {
    MysqlExpr *expr;
    char *alias;
    bool is_star;
    struct MysqlSelectItem *next;
} MysqlSelectItem;

typedef struct MysqlSelectList {
    MysqlSelectItem *head;
    MysqlSelectItem *tail;
    int count;
} MysqlSelectList;

typedef struct MysqlFromItem {
    char *table_name;
    char *alias;
    char *index_hint;          /* USE INDEX / FORCE INDEX */
    struct MysqlFromItem *next;
} MysqlFromItem;

typedef struct MysqlFromList {
    MysqlFromItem *head;
    MysqlFromItem *tail;
    int count;
} MysqlFromList;

typedef struct MysqlOrderItem {
    MysqlExpr *expr;
    bool ascending;
    struct MysqlOrderItem *next;
} MysqlOrderItem;

typedef struct MysqlOrderList {
    MysqlOrderItem *head;
    MysqlOrderItem *tail;
    int count;
} MysqlOrderList;

typedef struct MysqlArgList {
    MysqlExpr **args;
    int count;
    int capacity;
} MysqlArgList;

/* ------------------------------------------------------------------ */
/*  UPSERT clause (ON DUPLICATE KEY UPDATE)                            */
/* ------------------------------------------------------------------ */

typedef struct MysqlUpsertAssign {
    char *column;
    MysqlExpr *value;
    struct MysqlUpsertAssign *next;
} MysqlUpsertAssign;

typedef struct MysqlUpsertClause {
    MysqlUpsertAssign *assignments;
    int nassignments;
} MysqlUpsertClause;

/* ------------------------------------------------------------------ */
/*  Statement types                                                    */
/* ------------------------------------------------------------------ */

typedef enum MysqlStmtType {
    MYSQL_STMT_SELECT,
    MYSQL_STMT_CREATE_TABLE,
    MYSQL_STMT_INSERT,
    MYSQL_STMT_SHOW,
} MysqlStmtType;

typedef struct MysqlStmt {
    MysqlStmtType type;
    union {
        struct {
            MysqlSelectList *targets;
            MysqlFromList *from;
            MysqlExpr *where;
            MysqlExpr *group_by;
            MysqlExpr *having;
            MysqlOrderList *order;
            MysqlExpr *limit;         /* LIMIT clause */
            bool high_priority;
            bool sql_calc_found_rows;
        } select;

        struct {
            char *table_name;
            MysqlColumnDef *columns;
            MysqlTableOption *options;
            bool if_not_exists;
            bool temporary;
        } create_table;

        struct {
            char *table_name;
            char **columns;
            int ncolumns;
            MysqlExpr **values;
            int nvalues;
            MysqlUpsertClause *on_dup;  /* ON DUPLICATE KEY UPDATE */
            bool ignore;
        } insert;

        struct {
            MysqlShowType show_type;
            char *filter;              /* LIKE or WHERE filter text */
            char *from_db;             /* FROM database_name */
        } show;
    } u;
} MysqlStmt;

/* ------------------------------------------------------------------ */
/*  Parse state                                                        */
/* ------------------------------------------------------------------ */

typedef struct MysqlParseState {
    MysqlStmt *result;
    char *error_msg;
    bool has_error;
    int error_line;
    int error_col;

    /* Memory arena for AST allocations */
    void *arena;
    size_t arena_used;
    size_t arena_size;
} MysqlParseState;

/* ------------------------------------------------------------------ */
/*  Semantic action prototypes                                         */
/* ------------------------------------------------------------------ */

/* Parse state management */
MysqlParseState *mysql_parse_state_create(void);
void mysql_parse_state_destroy(MysqlParseState *pstate);
void mysql_parse_error(MysqlParseState *pstate, const char *msg);

/* Statement constructors */
void mysql_emit_stmt(MysqlParseState *pstate, MysqlStmt *stmt);
MysqlStmt *mysql_make_select(MysqlParseState *pstate,
                              MysqlSelectList *targets,
                              MysqlFromList *from,
                              MysqlExpr *where,
                              MysqlOrderList *order,
                              MysqlExpr *limit);
MysqlStmt *mysql_make_create_table(MysqlParseState *pstate,
                                    const char *name,
                                    MysqlColumnDef *cols,
                                    MysqlTableOption *opts,
                                    bool if_not_exists);
MysqlStmt *mysql_make_insert(MysqlParseState *pstate,
                              const char *table,
                              char **cols, int ncols,
                              MysqlExpr **vals, int nvals,
                              MysqlUpsertClause *on_dup);
MysqlStmt *mysql_make_show(MysqlParseState *pstate,
                            MysqlShowType show_type,
                            const char *filter,
                            const char *from_db);

/* Select list constructors */
MysqlSelectList *mysql_select_list_new(MysqlParseState *pstate,
                                        MysqlSelectItem *item);
MysqlSelectList *mysql_select_list_append(MysqlParseState *pstate,
                                           MysqlSelectList *list,
                                           MysqlSelectItem *item);
MysqlSelectItem *mysql_select_item_new(MysqlParseState *pstate,
                                        MysqlExpr *expr, const char *alias);
MysqlSelectItem *mysql_select_item_star(MysqlParseState *pstate);

/* From clause constructors */
MysqlFromList *mysql_from_list_new(MysqlParseState *pstate,
                                    MysqlFromItem *item);
MysqlFromList *mysql_from_list_append(MysqlParseState *pstate,
                                       MysqlFromList *list,
                                       MysqlFromItem *item);
MysqlFromItem *mysql_from_item_table(MysqlParseState *pstate,
                                      const char *name, const char *alias);

/* Expression constructors */
MysqlExpr *mysql_make_ident(MysqlParseState *pstate, const char *name);
MysqlExpr *mysql_make_backtick_ident(MysqlParseState *pstate, const char *name);
MysqlExpr *mysql_make_iconst(MysqlParseState *pstate, int64_t val);
MysqlExpr *mysql_make_fconst(MysqlParseState *pstate, double val);
MysqlExpr *mysql_make_sconst(MysqlParseState *pstate, const char *val);
MysqlExpr *mysql_make_null(MysqlParseState *pstate);
MysqlExpr *mysql_make_binop(MysqlParseState *pstate, MysqlBinOp op,
                              MysqlExpr *left, MysqlExpr *right);
MysqlExpr *mysql_make_ifnull(MysqlParseState *pstate,
                              MysqlExpr *expr1, MysqlExpr *expr2);
MysqlExpr *mysql_make_if_func(MysqlParseState *pstate,
                               MysqlExpr *cond,
                               MysqlExpr *then_expr,
                               MysqlExpr *else_expr);
MysqlExpr *mysql_make_limit(MysqlParseState *pstate,
                             MysqlExpr *count, MysqlExpr *offset);
MysqlExpr *mysql_make_interval(MysqlParseState *pstate,
                                MysqlExpr *value, MysqlIntervalUnit unit);

/* Column definition constructors */
MysqlColumnDef *mysql_make_column_def(MysqlParseState *pstate,
                                       const char *name,
                                       const char *type_name,
                                       int type_size);

/* Table option constructors */
MysqlTableOption *mysql_make_engine_option(MysqlParseState *pstate,
                                            MysqlEngine engine);
MysqlTableOption *mysql_make_charset_option(MysqlParseState *pstate,
                                             const char *charset);
MysqlTableOption *mysql_make_collate_option(MysqlParseState *pstate,
                                             const char *collate);

/* UPSERT constructors */
MysqlUpsertClause *mysql_make_upsert(MysqlParseState *pstate,
                                      MysqlUpsertAssign *assigns);
MysqlUpsertAssign *mysql_make_upsert_assign(MysqlParseState *pstate,
                                             const char *column,
                                             MysqlExpr *value);

/* Argument list */
MysqlArgList *mysql_arg_list_new(MysqlParseState *pstate, MysqlExpr *expr);
MysqlArgList *mysql_arg_list_append(MysqlParseState *pstate,
                                     MysqlArgList *list, MysqlExpr *expr);

/* AST cleanup */
void mysql_expr_free(MysqlExpr *expr);
void mysql_stmt_free(MysqlStmt *stmt);

#ifdef __cplusplus
}
#endif

#endif /* MYSQL_SEMANTICS_H */
