/*
** DuckDB Features Extension - Semantic Types and Action Prototypes
**
** Defines the AST node types and semantic action function prototypes
** used by the DuckDB grammar extension.  DuckDB is an analytics-focused
** embedded SQL database with unique extensions to standard SQL:
**
**   - LIST types and list comprehensions
**   - STRUCT types with named fields
**   - Lambda functions (used in list operations)
**   - PIVOT / UNPIVOT
**   - ASOF joins
**   - QUALIFY clause (filter on window functions)
**   - EXCLUDE / REPLACE in column selection
**   - SAMPLE clause
**   - COLUMNS expression
*/
#ifndef DUCKDB_SEMANTICS_H
#define DUCKDB_SEMANTICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Token value                                                        */
/* ------------------------------------------------------------------ */

typedef struct DuckdbToken {
    int64_t     ival;
    double      fval;
    const char *sval;
    int         line;
    int         col;
} DuckdbToken;

/* ------------------------------------------------------------------ */
/*  Expression subtypes                                                */
/* ------------------------------------------------------------------ */

typedef enum DuckdbExprType {
    /* Literals */
    DUCK_EXPR_IDENT,
    DUCK_EXPR_QUALIFIED_IDENT,
    DUCK_EXPR_ICONST,
    DUCK_EXPR_FCONST,
    DUCK_EXPR_SCONST,
    DUCK_EXPR_NULL,
    DUCK_EXPR_BOOL,

    /* DuckDB-specific types */
    DUCK_EXPR_LIST,               /* [1, 2, 3]                        */
    DUCK_EXPR_STRUCT,             /* {'a': 1, 'b': 2}                 */
    DUCK_EXPR_LAMBDA,             /* x -> x * 2                       */
    DUCK_EXPR_STRUCT_ACCESS,      /* s.field_name                     */

    /* List functions */
    DUCK_EXPR_LIST_TRANSFORM,     /* list_transform(list, lambda)     */
    DUCK_EXPR_LIST_FILTER,        /* list_filter(list, lambda)        */
    DUCK_EXPR_LIST_REDUCE,        /* list_reduce(list, lambda)        */
    DUCK_EXPR_LIST_CONTAINS,      /* list_contains(list, elem)        */
    DUCK_EXPR_LIST_EXTRACT,       /* list_extract(list, index)        */
    DUCK_EXPR_LIST_SORT,          /* list_sort(list)                  */
    DUCK_EXPR_LIST_DISTINCT,      /* list_distinct(list)              */
    DUCK_EXPR_LIST_AGG,           /* list(expr) / list_agg(expr)      */
    DUCK_EXPR_FLATTEN,            /* flatten(nested_list)             */

    /* Operators */
    DUCK_EXPR_BINOP,
    DUCK_EXPR_UNARYOP,
    DUCK_EXPR_IS_NULL,

    /* General function call */
    DUCK_EXPR_FUNC_CALL,

    /* COLUMNS expression */
    DUCK_EXPR_COLUMNS,            /* COLUMNS(pattern)                 */
    DUCK_EXPR_COLUMNS_REGEX,      /* COLUMNS(regex)                   */

    /* Subscript */
    DUCK_EXPR_SUBSCRIPT,          /* list[index] or struct['field']   */
} DuckdbExprType;

/* ------------------------------------------------------------------ */
/*  Binary operator codes                                              */
/* ------------------------------------------------------------------ */

typedef enum DuckdbBinOp {
    DUCK_OP_ADD,
    DUCK_OP_SUB,
    DUCK_OP_MUL,
    DUCK_OP_DIV,
    DUCK_OP_MOD,
    DUCK_OP_EQ,
    DUCK_OP_NE,
    DUCK_OP_LT,
    DUCK_OP_GT,
    DUCK_OP_LE,
    DUCK_OP_GE,
    DUCK_OP_AND,
    DUCK_OP_OR,
    DUCK_OP_NOT,
    DUCK_OP_CONCAT,              /* || string/list concatenation      */
} DuckdbBinOp;

/* ------------------------------------------------------------------ */
/*  AST node structures                                                */
/* ------------------------------------------------------------------ */

typedef struct DuckdbExpr DuckdbExpr;

/*
** A list of expressions (used for LIST literals, function args, etc.)
*/
typedef struct DuckdbExprList {
    DuckdbExpr **items;
    int count;
    int capacity;
} DuckdbExprList;

/*
** A STRUCT field: 'name': expr
*/
typedef struct DuckdbStructField {
    char *name;
    DuckdbExpr *value;
    struct DuckdbStructField *next;
} DuckdbStructField;

/*
** A STRUCT: ordered list of named fields.
*/
typedef struct DuckdbStruct {
    DuckdbStructField *head;
    DuckdbStructField *tail;
    int count;
} DuckdbStruct;

/*
** Lambda parameter list (one or two parameters for list operations).
*/
typedef struct DuckdbLambdaParams {
    char *params[2];             /* Up to 2 parameters (x, or x,y)   */
    int count;
} DuckdbLambdaParams;

/*
** Core expression node.
*/
struct DuckdbExpr {
    DuckdbExprType type;
    union {
        /* DUCK_EXPR_IDENT */
        struct { char *name; } ident;

        /* DUCK_EXPR_QUALIFIED_IDENT */
        struct { char *table; char *column; } qual_ident;

        /* DUCK_EXPR_ICONST */
        int64_t iconst;

        /* DUCK_EXPR_FCONST */
        double fconst;

        /* DUCK_EXPR_SCONST */
        struct { char *value; } sconst;

        /* DUCK_EXPR_BOOL */
        bool bval;

        /* DUCK_EXPR_LIST */
        DuckdbExprList *list;

        /* DUCK_EXPR_STRUCT */
        DuckdbStruct *strct;

        /* DUCK_EXPR_LAMBDA */
        struct {
            DuckdbLambdaParams *params;
            DuckdbExpr *body;
        } lambda;

        /* DUCK_EXPR_STRUCT_ACCESS */
        struct {
            DuckdbExpr *base;
            char *field;
        } struct_access;

        /* DUCK_EXPR_LIST_TRANSFORM, LIST_FILTER, LIST_REDUCE */
        struct {
            DuckdbExpr *list;
            DuckdbExpr *lambda;
        } list_op;

        /* DUCK_EXPR_LIST_CONTAINS, LIST_EXTRACT */
        struct {
            DuckdbExpr *list;
            DuckdbExpr *arg;
        } list_func;

        /* DUCK_EXPR_LIST_SORT, LIST_DISTINCT, FLATTEN, LIST_AGG */
        struct {
            DuckdbExpr *operand;
        } unary_list;

        /* DUCK_EXPR_BINOP */
        struct {
            DuckdbBinOp op;
            DuckdbExpr *left;
            DuckdbExpr *right;
        } binop;

        /* DUCK_EXPR_UNARYOP */
        struct {
            DuckdbBinOp op;
            DuckdbExpr *operand;
        } unaryop;

        /* DUCK_EXPR_IS_NULL */
        struct {
            DuckdbExpr *operand;
            bool negated;
        } is_null;

        /* DUCK_EXPR_FUNC_CALL */
        struct {
            char *func_name;
            DuckdbExprList *args;
        } func_call;

        /* DUCK_EXPR_COLUMNS / DUCK_EXPR_COLUMNS_REGEX */
        struct {
            char *pattern;
        } columns;

        /* DUCK_EXPR_SUBSCRIPT */
        struct {
            DuckdbExpr *base;
            DuckdbExpr *index;
        } subscript;
    } u;
};

/* ------------------------------------------------------------------ */
/*  PIVOT / UNPIVOT structures                                         */
/* ------------------------------------------------------------------ */

typedef struct DuckdbPivotColumn {
    char *column_name;
    struct DuckdbPivotColumn *next;
} DuckdbPivotColumn;

typedef struct DuckdbPivotValue {
    DuckdbExpr *value;
    char *alias;                 /* Optional alias for pivot column   */
    struct DuckdbPivotValue *next;
} DuckdbPivotValue;

typedef struct DuckdbPivotClause {
    bool is_unpivot;
    char *agg_func;              /* Aggregate function for PIVOT      */
    DuckdbExpr *agg_expr;        /* Expression being aggregated       */
    DuckdbPivotColumn *on_cols;  /* FOR column(s)                     */
    DuckdbPivotValue *values;    /* IN (value list)                   */
    DuckdbPivotColumn *group_by; /* Optional GROUP BY columns         */
} DuckdbPivotClause;

/* ------------------------------------------------------------------ */
/*  ASOF join structure                                                */
/* ------------------------------------------------------------------ */

typedef struct DuckdbAsofJoin {
    char *left_table;
    char *right_table;
    char *left_alias;
    char *right_alias;
    DuckdbExpr *on_condition;    /* >= condition for ASOF             */
    DuckdbExpr *by_condition;    /* Equality condition                */
} DuckdbAsofJoin;

/* ------------------------------------------------------------------ */
/*  SELECT extensions                                                  */
/* ------------------------------------------------------------------ */

/*
** EXCLUDE clause: SELECT * EXCLUDE (col1, col2)
*/
typedef struct DuckdbExcludeList {
    char **columns;
    int count;
    int capacity;
} DuckdbExcludeList;

/*
** REPLACE clause: SELECT * REPLACE (expr AS col)
*/
typedef struct DuckdbReplaceItem {
    DuckdbExpr *expr;
    char *column;
    struct DuckdbReplaceItem *next;
} DuckdbReplaceItem;

typedef struct DuckdbReplaceList {
    DuckdbReplaceItem *head;
    DuckdbReplaceItem *tail;
    int count;
} DuckdbReplaceList;

/*
** QUALIFY clause: QUALIFY row_number() OVER (...) = 1
*/
typedef struct DuckdbQualifyClause {
    DuckdbExpr *condition;
} DuckdbQualifyClause;

/*
** SAMPLE clause: USING SAMPLE 10%  or  USING SAMPLE 1000 ROWS
*/
typedef enum DuckdbSampleMethod {
    DUCK_SAMPLE_PERCENT,
    DUCK_SAMPLE_ROWS,
    DUCK_SAMPLE_RESERVOIR,
    DUCK_SAMPLE_BERNOULLI,
    DUCK_SAMPLE_SYSTEM,
} DuckdbSampleMethod;

typedef struct DuckdbSampleClause {
    double amount;               /* Percentage or row count           */
    DuckdbSampleMethod method;
    int seed;                    /* Repeatable seed (-1 if unset)     */
} DuckdbSampleClause;

/* ------------------------------------------------------------------ */
/*  Statement types                                                    */
/* ------------------------------------------------------------------ */

typedef enum DuckdbStmtType {
    DUCK_STMT_SELECT,
    DUCK_STMT_PIVOT,
    DUCK_STMT_UNPIVOT,
} DuckdbStmtType;

typedef struct DuckdbStmt {
    DuckdbStmtType type;
    union {
        struct {
            DuckdbExprList *targets;
            DuckdbExcludeList *exclude;
            DuckdbReplaceList *replace;
            DuckdbQualifyClause *qualify;
            DuckdbSampleClause *sample;
        } select;

        DuckdbPivotClause *pivot;
    } u;
} DuckdbStmt;

/* ------------------------------------------------------------------ */
/*  Parse state                                                        */
/* ------------------------------------------------------------------ */

typedef struct DuckdbParseState {
    DuckdbStmt  *result;         /* Final parse result                */
    char        *error_msg;      /* Non-NULL on error                 */
    bool         has_error;

    /* Memory arena */
    void   *arena;
    size_t  arena_used;
    size_t  arena_size;
} DuckdbParseState;

/* ------------------------------------------------------------------ */
/*  Semantic action prototypes                                         */
/* ------------------------------------------------------------------ */

/* Parse state management */
DuckdbParseState *duckdb_parse_state_create(void);
void duckdb_parse_state_destroy(DuckdbParseState *pstate);
void duckdb_parse_error(DuckdbParseState *pstate, const char *msg);

/* Expression constructors -- primitives */
DuckdbExpr *duckdb_make_ident(DuckdbParseState *pstate, const char *name);
DuckdbExpr *duckdb_make_qualified_ident(DuckdbParseState *pstate,
                                         const char *table,
                                         const char *column);
DuckdbExpr *duckdb_make_iconst(DuckdbParseState *pstate, int64_t val);
DuckdbExpr *duckdb_make_fconst(DuckdbParseState *pstate, double val);
DuckdbExpr *duckdb_make_sconst(DuckdbParseState *pstate, const char *val);
DuckdbExpr *duckdb_make_null(DuckdbParseState *pstate);
DuckdbExpr *duckdb_make_bool(DuckdbParseState *pstate, bool val);

/* Expression constructors -- DuckDB types */
DuckdbExpr *duckdb_make_list(DuckdbParseState *pstate, DuckdbExprList *elems);
DuckdbExpr *duckdb_make_struct(DuckdbParseState *pstate, DuckdbStruct *fields);
DuckdbExpr *duckdb_make_lambda(DuckdbParseState *pstate,
                                DuckdbLambdaParams *params,
                                DuckdbExpr *body);
DuckdbExpr *duckdb_make_struct_access(DuckdbParseState *pstate,
                                       DuckdbExpr *base,
                                       const char *field);
DuckdbExpr *duckdb_make_subscript(DuckdbParseState *pstate,
                                   DuckdbExpr *base,
                                   DuckdbExpr *index);

/* Expression constructors -- list functions */
DuckdbExpr *duckdb_make_list_transform(DuckdbParseState *pstate,
                                        DuckdbExpr *list,
                                        DuckdbExpr *lambda);
DuckdbExpr *duckdb_make_list_filter(DuckdbParseState *pstate,
                                     DuckdbExpr *list,
                                     DuckdbExpr *lambda);
DuckdbExpr *duckdb_make_list_reduce(DuckdbParseState *pstate,
                                     DuckdbExpr *list,
                                     DuckdbExpr *lambda);
DuckdbExpr *duckdb_make_list_contains(DuckdbParseState *pstate,
                                       DuckdbExpr *list,
                                       DuckdbExpr *elem);
DuckdbExpr *duckdb_make_list_extract(DuckdbParseState *pstate,
                                      DuckdbExpr *list,
                                      DuckdbExpr *index);
DuckdbExpr *duckdb_make_list_sort(DuckdbParseState *pstate,
                                   DuckdbExpr *list);
DuckdbExpr *duckdb_make_list_distinct(DuckdbParseState *pstate,
                                       DuckdbExpr *list);
DuckdbExpr *duckdb_make_list_agg(DuckdbParseState *pstate,
                                  DuckdbExpr *expr);
DuckdbExpr *duckdb_make_flatten(DuckdbParseState *pstate,
                                 DuckdbExpr *list);

/* Expression constructors -- operators */
DuckdbExpr *duckdb_make_binop(DuckdbParseState *pstate, DuckdbBinOp op,
                               DuckdbExpr *left, DuckdbExpr *right);
DuckdbExpr *duckdb_make_unaryop(DuckdbParseState *pstate, DuckdbBinOp op,
                                 DuckdbExpr *operand);
DuckdbExpr *duckdb_make_is_null(DuckdbParseState *pstate,
                                 DuckdbExpr *operand, bool negated);

/* Expression constructors -- function calls */
DuckdbExpr *duckdb_make_func_call(DuckdbParseState *pstate,
                                   const char *func_name,
                                   DuckdbExprList *args);

/* COLUMNS expression */
DuckdbExpr *duckdb_make_columns(DuckdbParseState *pstate,
                                 const char *pattern);
DuckdbExpr *duckdb_make_columns_regex(DuckdbParseState *pstate,
                                       const char *regex);

/* Expression list builders */
DuckdbExprList *duckdb_expr_list_new(DuckdbParseState *pstate,
                                      DuckdbExpr *first);
DuckdbExprList *duckdb_expr_list_append(DuckdbParseState *pstate,
                                         DuckdbExprList *list,
                                         DuckdbExpr *item);

/* Struct builders */
DuckdbStruct *duckdb_struct_new(DuckdbParseState *pstate,
                                 const char *name, DuckdbExpr *value);
DuckdbStruct *duckdb_struct_append(DuckdbParseState *pstate,
                                    DuckdbStruct *s,
                                    const char *name, DuckdbExpr *value);

/* Lambda parameter builders */
DuckdbLambdaParams *duckdb_lambda_params_new(DuckdbParseState *pstate,
                                              const char *param1);
DuckdbLambdaParams *duckdb_lambda_params_add(DuckdbParseState *pstate,
                                              DuckdbLambdaParams *params,
                                              const char *param2);

/* PIVOT / UNPIVOT */
DuckdbPivotClause *duckdb_make_pivot(DuckdbParseState *pstate,
                                      const char *agg_func,
                                      DuckdbExpr *agg_expr,
                                      DuckdbPivotColumn *on_cols,
                                      DuckdbPivotValue *values);
DuckdbPivotClause *duckdb_make_unpivot(DuckdbParseState *pstate,
                                        DuckdbPivotColumn *on_cols,
                                        DuckdbPivotValue *values);
DuckdbPivotColumn *duckdb_pivot_col_new(DuckdbParseState *pstate,
                                         const char *name);
DuckdbPivotColumn *duckdb_pivot_col_append(DuckdbParseState *pstate,
                                            DuckdbPivotColumn *list,
                                            const char *name);
DuckdbPivotValue *duckdb_pivot_value_new(DuckdbParseState *pstate,
                                          DuckdbExpr *value,
                                          const char *alias);
DuckdbPivotValue *duckdb_pivot_value_append(DuckdbParseState *pstate,
                                             DuckdbPivotValue *list,
                                             DuckdbExpr *value,
                                             const char *alias);

/* ASOF join */
DuckdbAsofJoin *duckdb_make_asof_join(DuckdbParseState *pstate,
                                       const char *left_table,
                                       const char *right_table,
                                       DuckdbExpr *on_condition);

/* SELECT extensions */
DuckdbExcludeList *duckdb_exclude_new(DuckdbParseState *pstate,
                                       const char *col);
DuckdbExcludeList *duckdb_exclude_append(DuckdbParseState *pstate,
                                          DuckdbExcludeList *list,
                                          const char *col);
DuckdbReplaceList *duckdb_replace_new(DuckdbParseState *pstate,
                                       DuckdbExpr *expr,
                                       const char *col);
DuckdbReplaceList *duckdb_replace_append(DuckdbParseState *pstate,
                                          DuckdbReplaceList *list,
                                          DuckdbExpr *expr,
                                          const char *col);
DuckdbQualifyClause *duckdb_make_qualify(DuckdbParseState *pstate,
                                          DuckdbExpr *condition);
DuckdbSampleClause *duckdb_make_sample(DuckdbParseState *pstate,
                                        double amount,
                                        DuckdbSampleMethod method);

/* Statement constructors */
void duckdb_emit_stmt(DuckdbParseState *pstate, DuckdbStmt *stmt);

/* AST cleanup (no-ops with arena allocator) */
void duckdb_expr_free(DuckdbExpr *expr);
void duckdb_stmt_free(DuckdbStmt *stmt);

#ifdef __cplusplus
}
#endif

#endif /* DUCKDB_SEMANTICS_H */
