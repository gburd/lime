/*
** DuckDB Features Extension - Semantic Actions
**
** Implements the AST construction functions called by the DuckDB grammar
** reduction actions.  Each function allocates an AST node and fills in
** the appropriate fields.
**
** Memory management uses a simple arena allocator attached to the parse
** state.  All allocations are freed in one shot when the parse state is
** destroyed.
*/

#include "duckdb_semantics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Arena allocator                                                    */
/* ------------------------------------------------------------------ */

#define ARENA_INITIAL_SIZE  (64 * 1024)   /* 64 KB */
#define ARENA_ALIGN         8

static void *arena_alloc(DuckdbParseState *pstate, size_t size) {
    size = (size + ARENA_ALIGN - 1) & ~(ARENA_ALIGN - 1);

    if (pstate->arena == NULL) {
        pstate->arena_size = ARENA_INITIAL_SIZE;
        pstate->arena = malloc(pstate->arena_size);
        if (pstate->arena == NULL) return NULL;
        pstate->arena_used = 0;
    }

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

static char *arena_strdup(DuckdbParseState *pstate, const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = arena_alloc(pstate, len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

/* ------------------------------------------------------------------ */
/*  Parse state management                                             */
/* ------------------------------------------------------------------ */

DuckdbParseState *duckdb_parse_state_create(void) {
    DuckdbParseState *ps = calloc(1, sizeof(DuckdbParseState));
    return ps;
}

void duckdb_parse_state_destroy(DuckdbParseState *pstate) {
    if (pstate == NULL) return;
    free(pstate->arena);
    free(pstate->error_msg);
    free(pstate);
}

void duckdb_parse_error(DuckdbParseState *pstate, const char *msg) {
    if (pstate->has_error) return;
    pstate->has_error = true;
    free(pstate->error_msg);
    pstate->error_msg = msg ? strdup(msg) : NULL;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- primitives                              */
/* ------------------------------------------------------------------ */

DuckdbExpr *duckdb_make_ident(DuckdbParseState *pstate, const char *name) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_IDENT;
    e->u.ident.name = arena_strdup(pstate, name);
    return e;
}

DuckdbExpr *duckdb_make_qualified_ident(DuckdbParseState *pstate,
                                         const char *table,
                                         const char *column) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_QUALIFIED_IDENT;
    e->u.qual_ident.table = arena_strdup(pstate, table);
    e->u.qual_ident.column = arena_strdup(pstate, column);
    return e;
}

DuckdbExpr *duckdb_make_iconst(DuckdbParseState *pstate, int64_t val) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_ICONST;
    e->u.iconst = val;
    return e;
}

DuckdbExpr *duckdb_make_fconst(DuckdbParseState *pstate, double val) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_FCONST;
    e->u.fconst = val;
    return e;
}

DuckdbExpr *duckdb_make_sconst(DuckdbParseState *pstate, const char *val) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_SCONST;
    e->u.sconst.value = arena_strdup(pstate, val);
    return e;
}

DuckdbExpr *duckdb_make_null(DuckdbParseState *pstate) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_NULL;
    return e;
}

DuckdbExpr *duckdb_make_bool(DuckdbParseState *pstate, bool val) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_BOOL;
    e->u.bval = val;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- DuckDB types                            */
/* ------------------------------------------------------------------ */

DuckdbExpr *duckdb_make_list(DuckdbParseState *pstate,
                              DuckdbExprList *elems) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_LIST;
    e->u.list = elems;
    return e;
}

DuckdbExpr *duckdb_make_struct(DuckdbParseState *pstate,
                                DuckdbStruct *fields) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_STRUCT;
    e->u.strct = fields;
    return e;
}

DuckdbExpr *duckdb_make_lambda(DuckdbParseState *pstate,
                                DuckdbLambdaParams *params,
                                DuckdbExpr *body) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_LAMBDA;
    e->u.lambda.params = params;
    e->u.lambda.body = body;
    return e;
}

DuckdbExpr *duckdb_make_struct_access(DuckdbParseState *pstate,
                                       DuckdbExpr *base,
                                       const char *field) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_STRUCT_ACCESS;
    e->u.struct_access.base = base;
    e->u.struct_access.field = arena_strdup(pstate, field);
    return e;
}

DuckdbExpr *duckdb_make_subscript(DuckdbParseState *pstate,
                                   DuckdbExpr *base,
                                   DuckdbExpr *index) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_SUBSCRIPT;
    e->u.subscript.base = base;
    e->u.subscript.index = index;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- list functions                          */
/* ------------------------------------------------------------------ */

DuckdbExpr *duckdb_make_list_transform(DuckdbParseState *pstate,
                                        DuckdbExpr *list,
                                        DuckdbExpr *lambda) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_LIST_TRANSFORM;
    e->u.list_op.list = list;
    e->u.list_op.lambda = lambda;
    return e;
}

DuckdbExpr *duckdb_make_list_filter(DuckdbParseState *pstate,
                                     DuckdbExpr *list,
                                     DuckdbExpr *lambda) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_LIST_FILTER;
    e->u.list_op.list = list;
    e->u.list_op.lambda = lambda;
    return e;
}

DuckdbExpr *duckdb_make_list_reduce(DuckdbParseState *pstate,
                                     DuckdbExpr *list,
                                     DuckdbExpr *lambda) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_LIST_REDUCE;
    e->u.list_op.list = list;
    e->u.list_op.lambda = lambda;
    return e;
}

DuckdbExpr *duckdb_make_list_contains(DuckdbParseState *pstate,
                                       DuckdbExpr *list,
                                       DuckdbExpr *elem) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_LIST_CONTAINS;
    e->u.list_func.list = list;
    e->u.list_func.arg = elem;
    return e;
}

DuckdbExpr *duckdb_make_list_extract(DuckdbParseState *pstate,
                                      DuckdbExpr *list,
                                      DuckdbExpr *index) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_LIST_EXTRACT;
    e->u.list_func.list = list;
    e->u.list_func.arg = index;
    return e;
}

DuckdbExpr *duckdb_make_list_sort(DuckdbParseState *pstate,
                                   DuckdbExpr *list) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_LIST_SORT;
    e->u.unary_list.operand = list;
    return e;
}

DuckdbExpr *duckdb_make_list_distinct(DuckdbParseState *pstate,
                                       DuckdbExpr *list) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_LIST_DISTINCT;
    e->u.unary_list.operand = list;
    return e;
}

DuckdbExpr *duckdb_make_list_agg(DuckdbParseState *pstate,
                                  DuckdbExpr *expr) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_LIST_AGG;
    e->u.unary_list.operand = expr;
    return e;
}

DuckdbExpr *duckdb_make_flatten(DuckdbParseState *pstate,
                                 DuckdbExpr *list) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_FLATTEN;
    e->u.unary_list.operand = list;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- operators                               */
/* ------------------------------------------------------------------ */

DuckdbExpr *duckdb_make_binop(DuckdbParseState *pstate, DuckdbBinOp op,
                               DuckdbExpr *left, DuckdbExpr *right) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_BINOP;
    e->u.binop.op = op;
    e->u.binop.left = left;
    e->u.binop.right = right;
    return e;
}

DuckdbExpr *duckdb_make_unaryop(DuckdbParseState *pstate, DuckdbBinOp op,
                                 DuckdbExpr *operand) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_UNARYOP;
    e->u.unaryop.op = op;
    e->u.unaryop.operand = operand;
    return e;
}

DuckdbExpr *duckdb_make_is_null(DuckdbParseState *pstate,
                                 DuckdbExpr *operand, bool negated) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_IS_NULL;
    e->u.is_null.operand = operand;
    e->u.is_null.negated = negated;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors -- function calls                          */
/* ------------------------------------------------------------------ */

DuckdbExpr *duckdb_make_func_call(DuckdbParseState *pstate,
                                   const char *func_name,
                                   DuckdbExprList *args) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_FUNC_CALL;
    e->u.func_call.func_name = arena_strdup(pstate, func_name);
    e->u.func_call.args = args;
    return e;
}

/* ------------------------------------------------------------------ */
/*  COLUMNS expression                                                 */
/* ------------------------------------------------------------------ */

DuckdbExpr *duckdb_make_columns(DuckdbParseState *pstate,
                                 const char *pattern) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_COLUMNS;
    e->u.columns.pattern = arena_strdup(pstate, pattern);
    return e;
}

DuckdbExpr *duckdb_make_columns_regex(DuckdbParseState *pstate,
                                       const char *regex) {
    DuckdbExpr *e = arena_alloc(pstate, sizeof(DuckdbExpr));
    if (e == NULL) return NULL;
    e->type = DUCK_EXPR_COLUMNS_REGEX;
    e->u.columns.pattern = arena_strdup(pstate, regex);
    return e;
}

/* ------------------------------------------------------------------ */
/*  Expression list builders                                           */
/* ------------------------------------------------------------------ */

DuckdbExprList *duckdb_expr_list_new(DuckdbParseState *pstate,
                                      DuckdbExpr *first) {
    DuckdbExprList *list = arena_alloc(pstate, sizeof(DuckdbExprList));
    if (list == NULL) return NULL;

    list->capacity = 8;
    list->items = arena_alloc(pstate, list->capacity * sizeof(DuckdbExpr *));
    if (list->items == NULL) return NULL;

    list->items[0] = first;
    list->count = 1;
    return list;
}

DuckdbExprList *duckdb_expr_list_append(DuckdbParseState *pstate,
                                         DuckdbExprList *list,
                                         DuckdbExpr *item) {
    if (list == NULL || item == NULL) return list;

    if (list->count >= list->capacity) {
        int new_cap = list->capacity * 2;
        DuckdbExpr **new_items = arena_alloc(pstate,
                                              new_cap * sizeof(DuckdbExpr *));
        if (new_items == NULL) return list;
        memcpy(new_items, list->items, list->count * sizeof(DuckdbExpr *));
        list->items = new_items;
        list->capacity = new_cap;
    }

    list->items[list->count++] = item;
    return list;
}

/* ------------------------------------------------------------------ */
/*  Struct builders                                                    */
/* ------------------------------------------------------------------ */

DuckdbStruct *duckdb_struct_new(DuckdbParseState *pstate,
                                 const char *name, DuckdbExpr *value) {
    DuckdbStruct *s = arena_alloc(pstate, sizeof(DuckdbStruct));
    if (s == NULL) return NULL;

    DuckdbStructField *f = arena_alloc(pstate, sizeof(DuckdbStructField));
    if (f == NULL) return NULL;

    f->name = arena_strdup(pstate, name);
    f->value = value;
    f->next = NULL;

    s->head = s->tail = f;
    s->count = 1;
    return s;
}

DuckdbStruct *duckdb_struct_append(DuckdbParseState *pstate,
                                    DuckdbStruct *s,
                                    const char *name, DuckdbExpr *value) {
    if (s == NULL) return duckdb_struct_new(pstate, name, value);

    DuckdbStructField *f = arena_alloc(pstate, sizeof(DuckdbStructField));
    if (f == NULL) return s;

    f->name = arena_strdup(pstate, name);
    f->value = value;
    f->next = NULL;

    s->tail->next = f;
    s->tail = f;
    s->count++;
    return s;
}

/* ------------------------------------------------------------------ */
/*  Lambda parameter builders                                          */
/* ------------------------------------------------------------------ */

DuckdbLambdaParams *duckdb_lambda_params_new(DuckdbParseState *pstate,
                                              const char *param1) {
    DuckdbLambdaParams *p = arena_alloc(pstate, sizeof(DuckdbLambdaParams));
    if (p == NULL) return NULL;
    p->params[0] = arena_strdup(pstate, param1);
    p->count = 1;
    return p;
}

DuckdbLambdaParams *duckdb_lambda_params_add(DuckdbParseState *pstate,
                                              DuckdbLambdaParams *params,
                                              const char *param2) {
    if (params == NULL || params->count >= 2) return params;
    params->params[params->count] = arena_strdup(pstate, param2);
    params->count++;
    return params;
}

/* ------------------------------------------------------------------ */
/*  PIVOT / UNPIVOT constructors                                       */
/* ------------------------------------------------------------------ */

DuckdbPivotClause *duckdb_make_pivot(DuckdbParseState *pstate,
                                      const char *agg_func,
                                      DuckdbExpr *agg_expr,
                                      DuckdbPivotColumn *on_cols,
                                      DuckdbPivotValue *values) {
    DuckdbPivotClause *p = arena_alloc(pstate, sizeof(DuckdbPivotClause));
    if (p == NULL) return NULL;
    p->is_unpivot = false;
    p->agg_func = arena_strdup(pstate, agg_func);
    p->agg_expr = agg_expr;
    p->on_cols = on_cols;
    p->values = values;
    p->group_by = NULL;
    return p;
}

DuckdbPivotClause *duckdb_make_unpivot(DuckdbParseState *pstate,
                                        DuckdbPivotColumn *on_cols,
                                        DuckdbPivotValue *values) {
    DuckdbPivotClause *p = arena_alloc(pstate, sizeof(DuckdbPivotClause));
    if (p == NULL) return NULL;
    p->is_unpivot = true;
    p->agg_func = NULL;
    p->agg_expr = NULL;
    p->on_cols = on_cols;
    p->values = values;
    p->group_by = NULL;
    return p;
}

DuckdbPivotColumn *duckdb_pivot_col_new(DuckdbParseState *pstate,
                                         const char *name) {
    DuckdbPivotColumn *c = arena_alloc(pstate, sizeof(DuckdbPivotColumn));
    if (c == NULL) return NULL;
    c->column_name = arena_strdup(pstate, name);
    c->next = NULL;
    return c;
}

DuckdbPivotColumn *duckdb_pivot_col_append(DuckdbParseState *pstate,
                                            DuckdbPivotColumn *list,
                                            const char *name) {
    DuckdbPivotColumn *c = duckdb_pivot_col_new(pstate, name);
    if (c == NULL || list == NULL) return c;

    DuckdbPivotColumn *tail = list;
    while (tail->next != NULL) tail = tail->next;
    tail->next = c;
    return list;
}

DuckdbPivotValue *duckdb_pivot_value_new(DuckdbParseState *pstate,
                                          DuckdbExpr *value,
                                          const char *alias) {
    DuckdbPivotValue *v = arena_alloc(pstate, sizeof(DuckdbPivotValue));
    if (v == NULL) return NULL;
    v->value = value;
    v->alias = arena_strdup(pstate, alias);
    v->next = NULL;
    return v;
}

DuckdbPivotValue *duckdb_pivot_value_append(DuckdbParseState *pstate,
                                             DuckdbPivotValue *list,
                                             DuckdbExpr *value,
                                             const char *alias) {
    DuckdbPivotValue *v = duckdb_pivot_value_new(pstate, value, alias);
    if (v == NULL || list == NULL) return v;

    DuckdbPivotValue *tail = list;
    while (tail->next != NULL) tail = tail->next;
    tail->next = v;
    return list;
}

/* ------------------------------------------------------------------ */
/*  ASOF join                                                          */
/* ------------------------------------------------------------------ */

DuckdbAsofJoin *duckdb_make_asof_join(DuckdbParseState *pstate,
                                       const char *left_table,
                                       const char *right_table,
                                       DuckdbExpr *on_condition) {
    DuckdbAsofJoin *j = arena_alloc(pstate, sizeof(DuckdbAsofJoin));
    if (j == NULL) return NULL;
    j->left_table = arena_strdup(pstate, left_table);
    j->right_table = arena_strdup(pstate, right_table);
    j->left_alias = NULL;
    j->right_alias = NULL;
    j->on_condition = on_condition;
    j->by_condition = NULL;
    return j;
}

/* ------------------------------------------------------------------ */
/*  SELECT extension constructors                                      */
/* ------------------------------------------------------------------ */

DuckdbExcludeList *duckdb_exclude_new(DuckdbParseState *pstate,
                                       const char *col) {
    DuckdbExcludeList *list = arena_alloc(pstate, sizeof(DuckdbExcludeList));
    if (list == NULL) return NULL;

    list->capacity = 8;
    list->columns = arena_alloc(pstate, list->capacity * sizeof(char *));
    if (list->columns == NULL) return NULL;

    list->columns[0] = arena_strdup(pstate, col);
    list->count = 1;
    return list;
}

DuckdbExcludeList *duckdb_exclude_append(DuckdbParseState *pstate,
                                          DuckdbExcludeList *list,
                                          const char *col) {
    if (list == NULL) return duckdb_exclude_new(pstate, col);

    if (list->count >= list->capacity) {
        int new_cap = list->capacity * 2;
        char **new_cols = arena_alloc(pstate, new_cap * sizeof(char *));
        if (new_cols == NULL) return list;
        memcpy(new_cols, list->columns, list->count * sizeof(char *));
        list->columns = new_cols;
        list->capacity = new_cap;
    }

    list->columns[list->count++] = arena_strdup(pstate, col);
    return list;
}

DuckdbReplaceList *duckdb_replace_new(DuckdbParseState *pstate,
                                       DuckdbExpr *expr,
                                       const char *col) {
    DuckdbReplaceList *list = arena_alloc(pstate, sizeof(DuckdbReplaceList));
    if (list == NULL) return NULL;

    DuckdbReplaceItem *item = arena_alloc(pstate, sizeof(DuckdbReplaceItem));
    if (item == NULL) return NULL;

    item->expr = expr;
    item->column = arena_strdup(pstate, col);
    item->next = NULL;

    list->head = list->tail = item;
    list->count = 1;
    return list;
}

DuckdbReplaceList *duckdb_replace_append(DuckdbParseState *pstate,
                                          DuckdbReplaceList *list,
                                          DuckdbExpr *expr,
                                          const char *col) {
    if (list == NULL) return duckdb_replace_new(pstate, expr, col);

    DuckdbReplaceItem *item = arena_alloc(pstate, sizeof(DuckdbReplaceItem));
    if (item == NULL) return list;

    item->expr = expr;
    item->column = arena_strdup(pstate, col);
    item->next = NULL;

    list->tail->next = item;
    list->tail = item;
    list->count++;
    return list;
}

DuckdbQualifyClause *duckdb_make_qualify(DuckdbParseState *pstate,
                                          DuckdbExpr *condition) {
    DuckdbQualifyClause *q = arena_alloc(pstate, sizeof(DuckdbQualifyClause));
    if (q == NULL) return NULL;
    q->condition = condition;
    return q;
}

DuckdbSampleClause *duckdb_make_sample(DuckdbParseState *pstate,
                                        double amount,
                                        DuckdbSampleMethod method) {
    DuckdbSampleClause *s = arena_alloc(pstate, sizeof(DuckdbSampleClause));
    if (s == NULL) return NULL;
    s->amount = amount;
    s->method = method;
    s->seed = -1;
    return s;
}

/* ------------------------------------------------------------------ */
/*  Statement emit                                                     */
/* ------------------------------------------------------------------ */

void duckdb_emit_stmt(DuckdbParseState *pstate, DuckdbStmt *stmt) {
    pstate->result = stmt;
}

/* ------------------------------------------------------------------ */
/*  AST cleanup (no-ops with arena allocator)                          */
/* ------------------------------------------------------------------ */

void duckdb_expr_free(DuckdbExpr *expr) {
    (void)expr;
}

void duckdb_stmt_free(DuckdbStmt *stmt) {
    (void)stmt;
}
