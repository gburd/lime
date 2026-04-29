/*
** Oracle SQL Compatibility -- Semantic Types and Action Prototypes
**
** Defines the AST node types and semantic action function prototypes
** used by the Oracle grammar extension.  The grammar's reduction
** actions call these functions to build an Oracle-flavored AST.
*/
#ifndef ORACLE_SEMANTICS_H
#define ORACLE_SEMANTICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Token value                                                        */
/* ------------------------------------------------------------------ */

typedef struct OracleToken {
    int         ival;
    double      fval;
    const char *sval;
    int         line;
    int         col;
} OracleToken;

/* ------------------------------------------------------------------ */
/*  AST node types                                                     */
/* ------------------------------------------------------------------ */

typedef enum OracleNodeType {
    ORA_NODE_STMT,
    ORA_NODE_SELECT,
    ORA_NODE_EXPR,
    ORA_NODE_SELECT_LIST,
    ORA_NODE_SELECT_ITEM,
    ORA_NODE_FROM_LIST,
    ORA_NODE_FROM_ITEM,
    ORA_NODE_HIER_CLAUSE,
    ORA_NODE_CONNECT_BY,
    ORA_NODE_ORDER_CLAUSE,
    ORA_NODE_ORDER_ITEM,
    ORA_NODE_DECODE_ARGS,
    ORA_NODE_DECODE_PAIR,
    ORA_NODE_ARG_LIST,
} OracleNodeType;

/* ------------------------------------------------------------------ */
/*  Expression subtypes                                                */
/* ------------------------------------------------------------------ */

typedef enum OracleExprType {
    ORA_EXPR_IDENT,
    ORA_EXPR_QUALIFIED_IDENT,
    ORA_EXPR_ICONST,
    ORA_EXPR_FCONST,
    ORA_EXPR_SCONST,
    ORA_EXPR_NULL,
    ORA_EXPR_ROWNUM,
    ORA_EXPR_ROWID,
    ORA_EXPR_LEVEL,
    ORA_EXPR_SYSDATE,
    ORA_EXPR_SYSTIMESTAMP,
    ORA_EXPR_PRIOR,
    ORA_EXPR_BINOP,
    ORA_EXPR_UNARYOP,
    ORA_EXPR_IS_NULL,
    ORA_EXPR_DECODE,
    ORA_EXPR_NVL,
    ORA_EXPR_NVL2,
    ORA_EXPR_OUTER_JOIN,
    ORA_EXPR_OUTER_JOIN_COND,
    ORA_EXPR_SEQ_CURRVAL,
    ORA_EXPR_SEQ_NEXTVAL,
    ORA_EXPR_FUNC_CALL,
} OracleExprType;

/* ------------------------------------------------------------------ */
/*  Binary operator codes                                              */
/* ------------------------------------------------------------------ */

typedef enum OracleBinOp {
    ORACLE_OP_ADD,
    ORACLE_OP_SUB,
    ORACLE_OP_MUL,
    ORACLE_OP_DIV,
    ORACLE_OP_EQ,
    ORACLE_OP_NE,
    ORACLE_OP_LT,
    ORACLE_OP_GT,
    ORACLE_OP_LE,
    ORACLE_OP_GE,
    ORACLE_OP_AND,
    ORACLE_OP_OR,
    ORACLE_OP_NOT,
} OracleBinOp;

/* ------------------------------------------------------------------ */
/*  AST node structures                                                */
/* ------------------------------------------------------------------ */

typedef struct OracleExpr {
    OracleExprType type;
    union {
        /* ORA_EXPR_IDENT */
        struct { char *name; } ident;

        /* ORA_EXPR_QUALIFIED_IDENT (table.column) */
        struct { char *table; char *column; } qual_ident;

        /* ORA_EXPR_ICONST */
        int64_t iconst;

        /* ORA_EXPR_FCONST */
        double fconst;

        /* ORA_EXPR_SCONST */
        struct { char *value; } sconst;

        /* ORA_EXPR_BINOP */
        struct {
            OracleBinOp op;
            struct OracleExpr *left;
            struct OracleExpr *right;
        } binop;

        /* ORA_EXPR_UNARYOP */
        struct {
            OracleBinOp op;
            struct OracleExpr *operand;
        } unaryop;

        /* ORA_EXPR_IS_NULL */
        struct {
            struct OracleExpr *operand;
            bool negated;  /* IS NOT NULL */
        } is_null;

        /* ORA_EXPR_PRIOR */
        struct { struct OracleExpr *operand; } prior;

        /* ORA_EXPR_DECODE */
        struct {
            struct OracleExpr *test_expr;
            struct OracleDecodeArgs *args;
        } decode;

        /* ORA_EXPR_NVL */
        struct {
            struct OracleExpr *expr1;
            struct OracleExpr *expr2;
        } nvl;

        /* ORA_EXPR_NVL2 */
        struct {
            struct OracleExpr *expr1;
            struct OracleExpr *expr2;
            struct OracleExpr *expr3;
        } nvl2;

        /* ORA_EXPR_OUTER_JOIN */
        struct { struct OracleExpr *inner; } outer_join;

        /* ORA_EXPR_OUTER_JOIN_COND */
        struct {
            struct OracleExpr *left;
            struct OracleExpr *right;  /* the (+) side */
        } outer_join_cond;

        /* ORA_EXPR_SEQ_CURRVAL / ORA_EXPR_SEQ_NEXTVAL */
        struct { char *seq_name; } seq_ref;

        /* ORA_EXPR_FUNC_CALL */
        struct {
            char *func_name;
            struct OracleArgList *args;
        } func_call;
    } u;
} OracleExpr;

/* ------------------------------------------------------------------ */
/*  List and container types                                           */
/* ------------------------------------------------------------------ */

typedef struct OracleSelectItem {
    OracleExpr *expr;
    char *alias;               /* NULL if no alias */
    bool is_star;
    struct OracleSelectItem *next;
} OracleSelectItem;

typedef struct OracleSelectList {
    OracleSelectItem *head;
    OracleSelectItem *tail;
    int count;
} OracleSelectList;

typedef struct OracleFromItem {
    char *table_name;
    char *alias;
    bool is_dual;
    struct OracleFromItem *next;
} OracleFromItem;

typedef struct OracleFromList {
    OracleFromItem *head;
    OracleFromItem *tail;
    int count;
} OracleFromList;

typedef struct OracleConnectBy {
    OracleExpr *condition;
    bool nocycle;
} OracleConnectBy;

typedef struct OracleHierClause {
    OracleConnectBy *connect_by;
    OracleExpr *start_with;    /* NULL if no START WITH */
} OracleHierClause;

typedef struct OracleOrderItem {
    OracleExpr *expr;
    bool ascending;
    struct OracleOrderItem *next;
} OracleOrderItem;

typedef struct OracleOrderList {
    OracleOrderItem *head;
    OracleOrderItem *tail;
    int count;
} OracleOrderList;

typedef struct OracleOrderClause {
    OracleOrderList *items;
    bool siblings;             /* ORDER SIBLINGS BY */
} OracleOrderClause;

typedef struct OracleDecodePair {
    OracleExpr *search;
    OracleExpr *result;
    struct OracleDecodePair *next;
} OracleDecodePair;

typedef struct OracleDecodePairList {
    OracleDecodePair *head;
    OracleDecodePair *tail;
    int count;
} OracleDecodePairList;

typedef struct OracleDecodeArgs {
    OracleDecodePairList *pairs;
    OracleExpr *default_result; /* NULL if no default */
} OracleDecodeArgs;

typedef struct OracleArgList {
    OracleExpr **args;
    int count;
    int capacity;
} OracleArgList;

/* ------------------------------------------------------------------ */
/*  Statement types                                                    */
/* ------------------------------------------------------------------ */

typedef enum OracleStmtType {
    ORA_STMT_SELECT,
    ORA_STMT_MINUS,
} OracleStmtType;

typedef struct OracleStmt {
    OracleStmtType type;
    union {
        struct {
            OracleSelectList *targets;
            OracleFromList *from;
            OracleExpr *where;
            OracleHierClause *hier;
            OracleOrderClause *order;
        } select;

        struct {
            struct OracleStmt *left;
            struct OracleStmt *right;
        } minus;
    } u;
} OracleStmt;

/* ------------------------------------------------------------------ */
/*  Parse state                                                        */
/* ------------------------------------------------------------------ */

typedef struct OracleParseState {
    OracleStmt *result;        /* Final parse result */
    char *error_msg;           /* Non-NULL on error */
    bool has_error;
    int error_line;
    int error_col;

    /* Memory arena for AST allocations */
    void *arena;
    size_t arena_used;
    size_t arena_size;
} OracleParseState;

/* ------------------------------------------------------------------ */
/*  Semantic action prototypes                                         */
/* ------------------------------------------------------------------ */

/* Parse state management */
OracleParseState *oracle_parse_state_create(void);
void oracle_parse_state_destroy(OracleParseState *pstate);
void oracle_parse_error(OracleParseState *pstate, const char *msg);

/* Statement constructors */
void oracle_emit_stmt(OracleParseState *pstate, OracleStmt *stmt);
OracleStmt *oracle_make_select(OracleParseState *pstate,
                                OracleSelectList *targets,
                                OracleFromList *from,
                                OracleExpr *where,
                                OracleHierClause *hier,
                                OracleOrderClause *order);
OracleStmt *oracle_make_select_dual(OracleParseState *pstate,
                                     OracleSelectList *targets);
OracleStmt *oracle_make_minus(OracleParseState *pstate,
                               OracleStmt *left, OracleStmt *right);

/* Select list constructors */
OracleSelectList *oracle_select_list_new(OracleParseState *pstate,
                                          OracleSelectItem *item);
OracleSelectList *oracle_select_list_append(OracleParseState *pstate,
                                             OracleSelectList *list,
                                             OracleSelectItem *item);
OracleSelectItem *oracle_select_item_new(OracleParseState *pstate,
                                          OracleExpr *expr,
                                          const char *alias);
OracleSelectItem *oracle_select_item_star(OracleParseState *pstate);

/* From clause constructors */
OracleFromList *oracle_from_list_new(OracleParseState *pstate,
                                      OracleFromItem *item);
OracleFromList *oracle_from_list_append(OracleParseState *pstate,
                                         OracleFromList *list,
                                         OracleFromItem *item);
OracleFromItem *oracle_from_item_table(OracleParseState *pstate,
                                        const char *name,
                                        const char *alias);
OracleFromItem *oracle_from_item_dual(OracleParseState *pstate);

/* Hierarchical query constructors */
OracleHierClause *oracle_make_hier_clause(OracleParseState *pstate,
                                           OracleConnectBy *cb,
                                           OracleExpr *start_with);
OracleConnectBy *oracle_make_connect_by(OracleParseState *pstate,
                                         OracleExpr *condition,
                                         bool nocycle);

/* Order clause constructors */
OracleOrderClause *oracle_make_order_clause(OracleParseState *pstate,
                                             OracleOrderList *items,
                                             bool siblings);
OracleOrderList *oracle_order_list_new(OracleParseState *pstate,
                                        OracleOrderItem *item);
OracleOrderList *oracle_order_list_append(OracleParseState *pstate,
                                           OracleOrderList *list,
                                           OracleOrderItem *item);
OracleOrderItem *oracle_make_order_item(OracleParseState *pstate,
                                         OracleExpr *expr,
                                         bool ascending);

/* Expression constructors -- pseudo-columns */
OracleExpr *oracle_make_rownum(OracleParseState *pstate);
OracleExpr *oracle_make_rowid(OracleParseState *pstate);
OracleExpr *oracle_make_level(OracleParseState *pstate);

/* Expression constructors -- date/time */
OracleExpr *oracle_make_sysdate(OracleParseState *pstate);
OracleExpr *oracle_make_systimestamp(OracleParseState *pstate);

/* Expression constructors -- PRIOR */
OracleExpr *oracle_make_prior(OracleParseState *pstate, OracleExpr *operand);

/* Expression constructors -- literals and identifiers */
OracleExpr *oracle_make_ident(OracleParseState *pstate, const char *name);
OracleExpr *oracle_make_qualified_ident(OracleParseState *pstate,
                                         const char *table,
                                         const char *column);
OracleExpr *oracle_make_iconst(OracleParseState *pstate, int64_t val);
OracleExpr *oracle_make_fconst(OracleParseState *pstate, double val);
OracleExpr *oracle_make_sconst(OracleParseState *pstate, const char *val);
OracleExpr *oracle_make_null(OracleParseState *pstate);

/* Expression constructors -- operators */
OracleExpr *oracle_make_binop(OracleParseState *pstate, OracleBinOp op,
                               OracleExpr *left, OracleExpr *right);
OracleExpr *oracle_make_unaryop(OracleParseState *pstate, OracleBinOp op,
                                 OracleExpr *operand);
OracleExpr *oracle_make_is_null(OracleParseState *pstate, OracleExpr *operand,
                                 bool negated);

/* Expression constructors -- Oracle functions */
OracleExpr *oracle_make_decode(OracleParseState *pstate,
                                OracleExpr *test_expr,
                                OracleDecodeArgs *args);
OracleDecodeArgs *oracle_decode_args_new(OracleParseState *pstate,
                                          OracleDecodePairList *pairs,
                                          OracleExpr *default_result);
OracleDecodePairList *oracle_decode_pair_list_new(OracleParseState *pstate,
                                                   OracleDecodePair *pair);
OracleDecodePairList *oracle_decode_pair_list_append(OracleParseState *pstate,
                                                      OracleDecodePairList *list,
                                                      OracleDecodePair *pair);
OracleDecodePair *oracle_make_decode_pair(OracleParseState *pstate,
                                           OracleExpr *search,
                                           OracleExpr *result);

OracleExpr *oracle_make_nvl(OracleParseState *pstate,
                             OracleExpr *expr1, OracleExpr *expr2);
OracleExpr *oracle_make_nvl2(OracleParseState *pstate,
                              OracleExpr *expr1, OracleExpr *expr2,
                              OracleExpr *expr3);

/* Expression constructors -- outer join */
OracleExpr *oracle_make_outer_join(OracleParseState *pstate, OracleExpr *expr);
OracleExpr *oracle_make_outer_join_cond(OracleParseState *pstate,
                                         OracleExpr *left,
                                         OracleExpr *right);

/* Expression constructors -- sequences */
OracleExpr *oracle_make_seq_currval(OracleParseState *pstate,
                                     const char *seq_name);
OracleExpr *oracle_make_seq_nextval(OracleParseState *pstate,
                                     const char *seq_name);

/* Expression constructors -- function calls */
OracleExpr *oracle_make_func_call(OracleParseState *pstate,
                                   const char *func_name,
                                   OracleArgList *args);
OracleArgList *oracle_arg_list_new(OracleParseState *pstate, OracleExpr *expr);
OracleArgList *oracle_arg_list_append(OracleParseState *pstate,
                                       OracleArgList *list, OracleExpr *expr);

/* AST cleanup */
void oracle_expr_free(OracleExpr *expr);
void oracle_stmt_free(OracleStmt *stmt);

#ifdef __cplusplus
}
#endif

#endif /* ORACLE_SEMANTICS_H */
