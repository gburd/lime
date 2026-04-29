/*
** Oracle SQL Compatibility -- Semantic Actions
**
** Implements the AST construction functions called by the Oracle grammar
** reduction actions.  Each function allocates an AST node and fills in
** the appropriate fields.
**
** Memory management uses a simple arena allocator attached to the parse
** state.  All allocations are freed in one shot when the parse state is
** destroyed, which avoids per-node free() calls and simplifies error
** handling in the parser.
*/

#include "oracle_semantics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Arena allocator                                                    */
/* ------------------------------------------------------------------ */

#define ARENA_INITIAL_SIZE  (64 * 1024)   /* 64 KB */
#define ARENA_ALIGN         8

static void *arena_alloc(OracleParseState *pstate, size_t size) {
    /* Align size up */
    size = (size + ARENA_ALIGN - 1) & ~(ARENA_ALIGN - 1);

    if (pstate->arena == NULL) {
        pstate->arena_size = ARENA_INITIAL_SIZE;
        pstate->arena = malloc(pstate->arena_size);
        if (pstate->arena == NULL) return NULL;
        pstate->arena_used = 0;
    }

    /* Grow if needed */
    while (pstate->arena_used + size > pstate->arena_size) {
        size_t new_size = pstate->arena_size * 2;
        void *p = realloc(pstate->arena, new_size);
        if (p == NULL) return NULL;
        pstate->arena = p;
        pstate->arena_size = new_size;
    }

    void *ptr = (char *)pstate->arena + pstate->arena_used;
    pstate->arena_used += size;
    memset(ptr, 0, size);
    return ptr;
}

static char *arena_strdup(OracleParseState *pstate, const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = arena_alloc(pstate, len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

/* ------------------------------------------------------------------ */
/*  Parse state management                                             */
/* ------------------------------------------------------------------ */

OracleParseState *oracle_parse_state_create(void) {
    OracleParseState *ps = calloc(1, sizeof(OracleParseState));
    return ps;
}

void oracle_parse_state_destroy(OracleParseState *pstate) {
    if (pstate == NULL) return;
    free(pstate->arena);
    free(pstate->error_msg);
    free(pstate);
}

void oracle_parse_error(OracleParseState *pstate, const char *msg) {
    if (pstate->has_error) return;  /* keep first error */
    pstate->has_error = true;
    free(pstate->error_msg);
    pstate->error_msg = msg ? strdup(msg) : NULL;
}

/* ------------------------------------------------------------------ */
/*  Statement constructors                                             */
/* ------------------------------------------------------------------ */

void oracle_emit_stmt(OracleParseState *pstate, OracleStmt *stmt) {
    pstate->result = stmt;
}

OracleStmt *oracle_make_select(OracleParseState *pstate,
                                OracleSelectList *targets,
                                OracleFromList *from,
                                OracleExpr *where,
                                OracleHierClause *hier,
                                OracleOrderClause *order) {
    OracleStmt *s = arena_alloc(pstate, sizeof(OracleStmt));
    if (s == NULL) return NULL;
    s->type = ORA_STMT_SELECT;
    s->u.select.targets = targets;
    s->u.select.from = from;
    s->u.select.where = where;
    s->u.select.hier = hier;
    s->u.select.order = order;
    return s;
}

OracleStmt *oracle_make_select_dual(OracleParseState *pstate,
                                     OracleSelectList *targets) {
    OracleFromItem *dual = oracle_from_item_dual(pstate);
    OracleFromList *from = oracle_from_list_new(pstate, dual);
    return oracle_make_select(pstate, targets, from, NULL, NULL, NULL);
}

OracleStmt *oracle_make_minus(OracleParseState *pstate,
                               OracleStmt *left, OracleStmt *right) {
    OracleStmt *s = arena_alloc(pstate, sizeof(OracleStmt));
    if (s == NULL) return NULL;
    s->type = ORA_STMT_MINUS;
    s->u.minus.left = left;
    s->u.minus.right = right;
    return s;
}

/* ------------------------------------------------------------------ */
/*  Select list constructors                                           */
/* ------------------------------------------------------------------ */

OracleSelectList *oracle_select_list_new(OracleParseState *pstate,
                                          OracleSelectItem *item) {
    OracleSelectList *list = arena_alloc(pstate, sizeof(OracleSelectList));
    if (list == NULL) return NULL;
    list->head = list->tail = item;
    list->count = 1;
    return list;
}

OracleSelectList *oracle_select_list_append(OracleParseState *pstate,
                                             OracleSelectList *list,
                                             OracleSelectItem *item) {
    (void)pstate;
    if (list == NULL || item == NULL) return list;
    list->tail->next = item;
    list->tail = item;
    list->count++;
    return list;
}

OracleSelectItem *oracle_select_item_new(OracleParseState *pstate,
                                          OracleExpr *expr,
                                          const char *alias) {
    OracleSelectItem *item = arena_alloc(pstate, sizeof(OracleSelectItem));
    if (item == NULL) return NULL;
    item->expr = expr;
    item->alias = arena_strdup(pstate, alias);
    item->is_star = false;
    item->next = NULL;
    return item;
}

OracleSelectItem *oracle_select_item_star(OracleParseState *pstate) {
    OracleSelectItem *item = arena_alloc(pstate, sizeof(OracleSelectItem));
    if (item == NULL) return NULL;
    item->expr = NULL;
    item->alias = NULL;
    item->is_star = true;
    item->next = NULL;
    return item;
}

/* ------------------------------------------------------------------ */
/*  From clause constructors                                           */
/* ------------------------------------------------------------------ */

OracleFromList *oracle_from_list_new(OracleParseState *pstate,
                                      OracleFromItem *item) {
    OracleFromList *list = arena_alloc(pstate, sizeof(OracleFromList));
    if (list == NULL) return NULL;
    list->head = list->tail = item;
    list->count = 1;
    return list;
}

OracleFromList *oracle_from_list_append(OracleParseState *pstate,
                                         OracleFromList *list,
                                         OracleFromItem *item) {
    (void)pstate;
    if (list == NULL || item == NULL) return list;
    list->tail->next = item;
    list->tail = item;
    list->count++;
    return list;
}

OracleFromItem *oracle_from_item_table(OracleParseState *pstate,
                                        const char *name,
                                        const char *alias) {
    OracleFromItem *item = arena_alloc(pstate, sizeof(OracleFromItem));
    if (item == NULL) return NULL;
    item->table_name = arena_strdup(pstate, name);
    item->alias = arena_strdup(pstate, alias);
    item->is_dual = false;
    item->next = NULL;
    return item;
}

OracleFromItem *oracle_from_item_dual(OracleParseState *pstate) {
    OracleFromItem *item = arena_alloc(pstate, sizeof(OracleFromItem));
    if (item == NULL) return NULL;
    item->table_name = arena_strdup(pstate, "DUAL");
    item->alias = NULL;
    item->is_dual = true;
    item->next = NULL;
    return item;
}

/* ------------------------------------------------------------------ */
/*  Hierarchical query constructors                                    */
/* ------------------------------------------------------------------ */

OracleHierClause *oracle_make_hier_clause(OracleParseState *pstate,
                                           OracleConnectBy *cb,
                                           OracleExpr *start_with) {
    OracleHierClause *hc = arena_alloc(pstate, sizeof(OracleHierClause));
    if (hc == NULL) return NULL;
    hc->connect_by = cb;
    hc->start_with = start_with;
    return hc;
}

OracleConnectBy *oracle_make_connect_by(OracleParseState *pstate,
                                         OracleExpr *condition,
                                         bool nocycle) {
    OracleConnectBy *cb = arena_alloc(pstate, sizeof(OracleConnectBy));
    if (cb == NULL) return NULL;
    cb->condition = condition;
    cb->nocycle = nocycle;
    return cb;
}

/* ------------------------------------------------------------------ */
/*  Order clause constructors                                          */
/* ------------------------------------------------------------------ */

OracleOrderClause *oracle_make_order_clause(OracleParseState *pstate,
                                             OracleOrderList *items,
                                             bool siblings) {
    OracleOrderClause *oc = arena_alloc(pstate, sizeof(OracleOrderClause));
    if (oc == NULL) return NULL;
    oc->items = items;
    oc->siblings = siblings;
    return oc;
}

OracleOrderList *oracle_order_list_new(OracleParseState *pstate,
                                        OracleOrderItem *item) {
    OracleOrderList *list = arena_alloc(pstate, sizeof(OracleOrderList));
    if (list == NULL) return NULL;
    list->head = list->tail = item;
    list->count = 1;
    return list;
}

OracleOrderList *oracle_order_list_append(OracleParseState *pstate,
                                           OracleOrderList *list,
                                           OracleOrderItem *item) {
    (void)pstate;
    if (list == NULL || item == NULL) return list;
    list->tail->next = item;
    list->tail = item;
    list->count++;
    return list;
}

OracleOrderItem *oracle_make_order_item(OracleParseState *pstate,
                                         OracleExpr *expr,
                                         bool ascending) {
    OracleOrderItem *oi = arena_alloc(pstate, sizeof(OracleOrderItem));
    if (oi == NULL) return NULL;
    oi->expr = expr;
    oi->ascending = ascending;
    oi->next = NULL;
    return oi;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- pseudo-columns                          */
/* ------------------------------------------------------------------ */

OracleExpr *oracle_make_rownum(OracleParseState *pstate) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_ROWNUM;
    return e;
}

OracleExpr *oracle_make_rowid(OracleParseState *pstate) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_ROWID;
    return e;
}

OracleExpr *oracle_make_level(OracleParseState *pstate) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_LEVEL;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- date/time                               */
/* ------------------------------------------------------------------ */

OracleExpr *oracle_make_sysdate(OracleParseState *pstate) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_SYSDATE;
    return e;
}

OracleExpr *oracle_make_systimestamp(OracleParseState *pstate) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_SYSTIMESTAMP;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- PRIOR                                   */
/* ------------------------------------------------------------------ */

OracleExpr *oracle_make_prior(OracleParseState *pstate, OracleExpr *operand) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_PRIOR;
    e->u.prior.operand = operand;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- literals and identifiers                */
/* ------------------------------------------------------------------ */

OracleExpr *oracle_make_ident(OracleParseState *pstate, const char *name) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_IDENT;
    e->u.ident.name = arena_strdup(pstate, name);
    return e;
}

OracleExpr *oracle_make_qualified_ident(OracleParseState *pstate,
                                         const char *table,
                                         const char *column) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_QUALIFIED_IDENT;
    e->u.qual_ident.table = arena_strdup(pstate, table);
    e->u.qual_ident.column = arena_strdup(pstate, column);
    return e;
}

OracleExpr *oracle_make_iconst(OracleParseState *pstate, int64_t val) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_ICONST;
    e->u.iconst = val;
    return e;
}

OracleExpr *oracle_make_fconst(OracleParseState *pstate, double val) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_FCONST;
    e->u.fconst = val;
    return e;
}

OracleExpr *oracle_make_sconst(OracleParseState *pstate, const char *val) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_SCONST;
    e->u.sconst.value = arena_strdup(pstate, val);
    return e;
}

OracleExpr *oracle_make_null(OracleParseState *pstate) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_NULL;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- operators                               */
/* ------------------------------------------------------------------ */

OracleExpr *oracle_make_binop(OracleParseState *pstate, OracleBinOp op,
                               OracleExpr *left, OracleExpr *right) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_BINOP;
    e->u.binop.op = op;
    e->u.binop.left = left;
    e->u.binop.right = right;
    return e;
}

OracleExpr *oracle_make_unaryop(OracleParseState *pstate, OracleBinOp op,
                                 OracleExpr *operand) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_UNARYOP;
    e->u.unaryop.op = op;
    e->u.unaryop.operand = operand;
    return e;
}

OracleExpr *oracle_make_is_null(OracleParseState *pstate, OracleExpr *operand,
                                 bool negated) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_IS_NULL;
    e->u.is_null.operand = operand;
    e->u.is_null.negated = negated;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- DECODE                                  */
/* ------------------------------------------------------------------ */

OracleExpr *oracle_make_decode(OracleParseState *pstate,
                                OracleExpr *test_expr,
                                OracleDecodeArgs *args) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_DECODE;
    e->u.decode.test_expr = test_expr;
    e->u.decode.args = args;
    return e;
}

OracleDecodeArgs *oracle_decode_args_new(OracleParseState *pstate,
                                          OracleDecodePairList *pairs,
                                          OracleExpr *default_result) {
    OracleDecodeArgs *a = arena_alloc(pstate, sizeof(OracleDecodeArgs));
    if (a == NULL) return NULL;
    a->pairs = pairs;
    a->default_result = default_result;
    return a;
}

OracleDecodePairList *oracle_decode_pair_list_new(OracleParseState *pstate,
                                                   OracleDecodePair *pair) {
    OracleDecodePairList *list = arena_alloc(pstate, sizeof(OracleDecodePairList));
    if (list == NULL) return NULL;
    list->head = list->tail = pair;
    list->count = 1;
    return list;
}

OracleDecodePairList *oracle_decode_pair_list_append(OracleParseState *pstate,
                                                      OracleDecodePairList *list,
                                                      OracleDecodePair *pair) {
    (void)pstate;
    if (list == NULL || pair == NULL) return list;
    list->tail->next = pair;
    list->tail = pair;
    list->count++;
    return list;
}

OracleDecodePair *oracle_make_decode_pair(OracleParseState *pstate,
                                           OracleExpr *search,
                                           OracleExpr *result) {
    OracleDecodePair *p = arena_alloc(pstate, sizeof(OracleDecodePair));
    if (p == NULL) return NULL;
    p->search = search;
    p->result = result;
    p->next = NULL;
    return p;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- NVL / NVL2                              */
/* ------------------------------------------------------------------ */

OracleExpr *oracle_make_nvl(OracleParseState *pstate,
                             OracleExpr *expr1, OracleExpr *expr2) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_NVL;
    e->u.nvl.expr1 = expr1;
    e->u.nvl.expr2 = expr2;
    return e;
}

OracleExpr *oracle_make_nvl2(OracleParseState *pstate,
                              OracleExpr *expr1, OracleExpr *expr2,
                              OracleExpr *expr3) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_NVL2;
    e->u.nvl2.expr1 = expr1;
    e->u.nvl2.expr2 = expr2;
    e->u.nvl2.expr3 = expr3;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- outer join                              */
/* ------------------------------------------------------------------ */

OracleExpr *oracle_make_outer_join(OracleParseState *pstate, OracleExpr *expr) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_OUTER_JOIN;
    e->u.outer_join.inner = expr;
    return e;
}

OracleExpr *oracle_make_outer_join_cond(OracleParseState *pstate,
                                         OracleExpr *left,
                                         OracleExpr *right) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_OUTER_JOIN_COND;
    e->u.outer_join_cond.left = left;
    e->u.outer_join_cond.right = right;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- sequences                               */
/* ------------------------------------------------------------------ */

OracleExpr *oracle_make_seq_currval(OracleParseState *pstate,
                                     const char *seq_name) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_SEQ_CURRVAL;
    e->u.seq_ref.seq_name = arena_strdup(pstate, seq_name);
    return e;
}

OracleExpr *oracle_make_seq_nextval(OracleParseState *pstate,
                                     const char *seq_name) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_SEQ_NEXTVAL;
    e->u.seq_ref.seq_name = arena_strdup(pstate, seq_name);
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- function calls                          */
/* ------------------------------------------------------------------ */

OracleExpr *oracle_make_func_call(OracleParseState *pstate,
                                   const char *func_name,
                                   OracleArgList *args) {
    OracleExpr *e = arena_alloc(pstate, sizeof(OracleExpr));
    if (e == NULL) return NULL;
    e->type = ORA_EXPR_FUNC_CALL;
    e->u.func_call.func_name = arena_strdup(pstate, func_name);
    e->u.func_call.args = args;
    return e;
}

OracleArgList *oracle_arg_list_new(OracleParseState *pstate, OracleExpr *expr) {
    OracleArgList *list = arena_alloc(pstate, sizeof(OracleArgList));
    if (list == NULL) return NULL;
    list->capacity = 8;
    list->args = arena_alloc(pstate, list->capacity * sizeof(OracleExpr *));
    if (list->args == NULL) return NULL;
    list->args[0] = expr;
    list->count = 1;
    return list;
}

OracleArgList *oracle_arg_list_append(OracleParseState *pstate,
                                       OracleArgList *list, OracleExpr *expr) {
    if (list == NULL || expr == NULL) return list;
    if (list->count >= list->capacity) {
        int new_cap = list->capacity * 2;
        OracleExpr **new_args = arena_alloc(pstate, new_cap * sizeof(OracleExpr *));
        if (new_args == NULL) return list;
        memcpy(new_args, list->args, list->count * sizeof(OracleExpr *));
        list->args = new_args;
        list->capacity = new_cap;
    }
    list->args[list->count++] = expr;
    return list;
}

/* ------------------------------------------------------------------ */
/*  AST cleanup (no-ops when using arena allocator)                    */
/* ------------------------------------------------------------------ */

/*
** When the arena allocator is used, individual node frees are no-ops.
** The entire AST is freed when oracle_parse_state_destroy() releases
** the arena.  These functions exist to satisfy the interface and to
** support non-arena usage in tests.
*/

void oracle_expr_free(OracleExpr *expr) {
    (void)expr;
}

void oracle_stmt_free(OracleStmt *stmt) {
    (void)stmt;
}
