/*
** MySQL SQL Compatibility -- Semantic Actions
**
** Implements the AST construction functions called by the MySQL grammar's
** reduction actions.  Uses a simple arena allocator for all AST nodes.
*/
#define _GNU_SOURCE
#include "mysql_semantics.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Portable strdup for strict C11 */
static char *local_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

/* ------------------------------------------------------------------ */
/*  Arena allocator                                                     */
/* ------------------------------------------------------------------ */

#define ARENA_INITIAL_SIZE (64 * 1024)

static void *arena_alloc(MysqlParseState *ps, size_t size) {
    if (ps->arena == NULL) {
        ps->arena_size = ARENA_INITIAL_SIZE;
        ps->arena = malloc(ps->arena_size);
        ps->arena_used = 0;
        if (ps->arena == NULL) return NULL;
    }

    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;

    if (ps->arena_used + size > ps->arena_size) {
        /* Fall back to malloc for oversized allocations */
        return calloc(1, size);
    }

    void *p = (char *)ps->arena + ps->arena_used;
    ps->arena_used += size;
    memset(p, 0, size);
    return p;
}

static char *arena_strdup(MysqlParseState *ps, const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = arena_alloc(ps, len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

/* ------------------------------------------------------------------ */
/*  Parse state                                                         */
/* ------------------------------------------------------------------ */

MysqlParseState *mysql_parse_state_create(void) {
    MysqlParseState *ps = calloc(1, sizeof(MysqlParseState));
    return ps;
}

void mysql_parse_state_destroy(MysqlParseState *ps) {
    if (ps == NULL) return;
    free(ps->arena);
    free(ps->error_msg);
    free(ps);
}

void mysql_parse_error(MysqlParseState *ps, const char *msg) {
    if (ps == NULL) return;
    ps->has_error = true;
    free(ps->error_msg);
    ps->error_msg = msg ? local_strdup(msg) : NULL;
}

/* ------------------------------------------------------------------ */
/*  Statement constructors                                              */
/* ------------------------------------------------------------------ */

void mysql_emit_stmt(MysqlParseState *ps, MysqlStmt *stmt) {
    if (ps) ps->result = stmt;
}

MysqlStmt *mysql_make_select(MysqlParseState *ps,
                              MysqlSelectList *targets,
                              MysqlFromList *from,
                              MysqlExpr *where,
                              MysqlOrderList *order,
                              MysqlExpr *limit) {
    MysqlStmt *s = arena_alloc(ps, sizeof(MysqlStmt));
    if (!s) return NULL;
    s->type = MYSQL_STMT_SELECT;
    s->u.select.targets = targets;
    s->u.select.from = from;
    s->u.select.where = where;
    s->u.select.order = order;
    s->u.select.limit = limit;
    return s;
}

MysqlStmt *mysql_make_create_table(MysqlParseState *ps,
                                    const char *name,
                                    MysqlColumnDef *cols,
                                    MysqlTableOption *opts,
                                    bool if_not_exists) {
    MysqlStmt *s = arena_alloc(ps, sizeof(MysqlStmt));
    if (!s) return NULL;
    s->type = MYSQL_STMT_CREATE_TABLE;
    s->u.create_table.table_name = arena_strdup(ps, name);
    s->u.create_table.columns = cols;
    s->u.create_table.options = opts;
    s->u.create_table.if_not_exists = if_not_exists;
    return s;
}

MysqlStmt *mysql_make_insert(MysqlParseState *ps,
                              const char *table,
                              char **cols, int ncols,
                              MysqlExpr **vals, int nvals,
                              MysqlUpsertClause *on_dup) {
    MysqlStmt *s = arena_alloc(ps, sizeof(MysqlStmt));
    if (!s) return NULL;
    s->type = MYSQL_STMT_INSERT;
    s->u.insert.table_name = arena_strdup(ps, table);
    s->u.insert.columns = cols;
    s->u.insert.ncolumns = ncols;
    s->u.insert.values = vals;
    s->u.insert.nvalues = nvals;
    s->u.insert.on_dup = on_dup;
    return s;
}

MysqlStmt *mysql_make_show(MysqlParseState *ps,
                            MysqlShowType show_type,
                            const char *filter,
                            const char *from_db) {
    MysqlStmt *s = arena_alloc(ps, sizeof(MysqlStmt));
    if (!s) return NULL;
    s->type = MYSQL_STMT_SHOW;
    s->u.show.show_type = show_type;
    s->u.show.filter = arena_strdup(ps, filter);
    s->u.show.from_db = arena_strdup(ps, from_db);
    return s;
}

/* ------------------------------------------------------------------ */
/*  Select list                                                         */
/* ------------------------------------------------------------------ */

MysqlSelectList *mysql_select_list_new(MysqlParseState *ps,
                                        MysqlSelectItem *item) {
    MysqlSelectList *l = arena_alloc(ps, sizeof(MysqlSelectList));
    if (!l) return NULL;
    l->head = l->tail = item;
    l->count = item ? 1 : 0;
    return l;
}

MysqlSelectList *mysql_select_list_append(MysqlParseState *ps,
                                           MysqlSelectList *list,
                                           MysqlSelectItem *item) {
    (void)ps;
    if (!list || !item) return list;
    if (list->tail) list->tail->next = item;
    else list->head = item;
    list->tail = item;
    list->count++;
    return list;
}

MysqlSelectItem *mysql_select_item_new(MysqlParseState *ps,
                                        MysqlExpr *expr, const char *alias) {
    MysqlSelectItem *it = arena_alloc(ps, sizeof(MysqlSelectItem));
    if (!it) return NULL;
    it->expr = expr;
    it->alias = arena_strdup(ps, alias);
    return it;
}

MysqlSelectItem *mysql_select_item_star(MysqlParseState *ps) {
    MysqlSelectItem *it = arena_alloc(ps, sizeof(MysqlSelectItem));
    if (it) it->is_star = true;
    return it;
}

/* ------------------------------------------------------------------ */
/*  From list                                                           */
/* ------------------------------------------------------------------ */

MysqlFromList *mysql_from_list_new(MysqlParseState *ps,
                                    MysqlFromItem *item) {
    MysqlFromList *l = arena_alloc(ps, sizeof(MysqlFromList));
    if (!l) return NULL;
    l->head = l->tail = item;
    l->count = item ? 1 : 0;
    return l;
}

MysqlFromList *mysql_from_list_append(MysqlParseState *ps,
                                       MysqlFromList *list,
                                       MysqlFromItem *item) {
    (void)ps;
    if (!list || !item) return list;
    if (list->tail) list->tail->next = item;
    else list->head = item;
    list->tail = item;
    list->count++;
    return list;
}

MysqlFromItem *mysql_from_item_table(MysqlParseState *ps,
                                      const char *name, const char *alias) {
    MysqlFromItem *it = arena_alloc(ps, sizeof(MysqlFromItem));
    if (!it) return NULL;
    it->table_name = arena_strdup(ps, name);
    it->alias = arena_strdup(ps, alias);
    return it;
}

/* ------------------------------------------------------------------ */
/*  Expression constructors                                             */
/* ------------------------------------------------------------------ */

MysqlExpr *mysql_make_ident(MysqlParseState *ps, const char *name) {
    MysqlExpr *e = arena_alloc(ps, sizeof(MysqlExpr));
    if (!e) return NULL;
    e->type = MYSQL_EXPR_IDENT;
    e->u.ident.name = arena_strdup(ps, name);
    return e;
}

MysqlExpr *mysql_make_backtick_ident(MysqlParseState *ps, const char *name) {
    MysqlExpr *e = arena_alloc(ps, sizeof(MysqlExpr));
    if (!e) return NULL;
    e->type = MYSQL_EXPR_BACKTICK_IDENT;
    e->u.ident.name = arena_strdup(ps, name);
    return e;
}

MysqlExpr *mysql_make_iconst(MysqlParseState *ps, int64_t val) {
    MysqlExpr *e = arena_alloc(ps, sizeof(MysqlExpr));
    if (!e) return NULL;
    e->type = MYSQL_EXPR_ICONST;
    e->u.iconst = val;
    return e;
}

MysqlExpr *mysql_make_fconst(MysqlParseState *ps, double val) {
    MysqlExpr *e = arena_alloc(ps, sizeof(MysqlExpr));
    if (!e) return NULL;
    e->type = MYSQL_EXPR_FCONST;
    e->u.fconst = val;
    return e;
}

MysqlExpr *mysql_make_sconst(MysqlParseState *ps, const char *val) {
    MysqlExpr *e = arena_alloc(ps, sizeof(MysqlExpr));
    if (!e) return NULL;
    e->type = MYSQL_EXPR_SCONST;
    e->u.sconst.value = arena_strdup(ps, val);
    return e;
}

MysqlExpr *mysql_make_null(MysqlParseState *ps) {
    MysqlExpr *e = arena_alloc(ps, sizeof(MysqlExpr));
    if (e) e->type = MYSQL_EXPR_NULL;
    return e;
}

MysqlExpr *mysql_make_binop(MysqlParseState *ps, MysqlBinOp op,
                              MysqlExpr *left, MysqlExpr *right) {
    MysqlExpr *e = arena_alloc(ps, sizeof(MysqlExpr));
    if (!e) return NULL;
    e->type = MYSQL_EXPR_BINOP;
    e->u.binop.op = op;
    e->u.binop.left = left;
    e->u.binop.right = right;
    return e;
}

MysqlExpr *mysql_make_ifnull(MysqlParseState *ps,
                              MysqlExpr *expr1, MysqlExpr *expr2) {
    MysqlExpr *e = arena_alloc(ps, sizeof(MysqlExpr));
    if (!e) return NULL;
    e->type = MYSQL_EXPR_IFNULL;
    e->u.ifnull.expr1 = expr1;
    e->u.ifnull.expr2 = expr2;
    return e;
}

MysqlExpr *mysql_make_if_func(MysqlParseState *ps,
                               MysqlExpr *cond,
                               MysqlExpr *then_expr,
                               MysqlExpr *else_expr) {
    MysqlExpr *e = arena_alloc(ps, sizeof(MysqlExpr));
    if (!e) return NULL;
    e->type = MYSQL_EXPR_IF_FUNC;
    e->u.if_func.cond = cond;
    e->u.if_func.then_expr = then_expr;
    e->u.if_func.else_expr = else_expr;
    return e;
}

MysqlExpr *mysql_make_limit(MysqlParseState *ps,
                             MysqlExpr *count, MysqlExpr *offset) {
    MysqlExpr *e = arena_alloc(ps, sizeof(MysqlExpr));
    if (!e) return NULL;
    e->type = MYSQL_EXPR_LIMIT;
    e->u.limit.count = count;
    e->u.limit.offset = offset;
    return e;
}

MysqlExpr *mysql_make_interval(MysqlParseState *ps,
                                MysqlExpr *value, MysqlIntervalUnit unit) {
    MysqlExpr *e = arena_alloc(ps, sizeof(MysqlExpr));
    if (!e) return NULL;
    e->type = MYSQL_EXPR_INTERVAL;
    e->u.interval.value = value;
    e->u.interval.unit = unit;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Column definition                                                   */
/* ------------------------------------------------------------------ */

MysqlColumnDef *mysql_make_column_def(MysqlParseState *ps,
                                       const char *name,
                                       const char *type_name,
                                       int type_size) {
    MysqlColumnDef *c = arena_alloc(ps, sizeof(MysqlColumnDef));
    if (!c) return NULL;
    c->name = arena_strdup(ps, name);
    c->type_name = arena_strdup(ps, type_name);
    c->type_size = type_size;
    return c;
}

/* ------------------------------------------------------------------ */
/*  Table options                                                       */
/* ------------------------------------------------------------------ */

MysqlTableOption *mysql_make_engine_option(MysqlParseState *ps,
                                            MysqlEngine engine) {
    MysqlTableOption *o = arena_alloc(ps, sizeof(MysqlTableOption));
    if (!o) return NULL;
    o->type = MYSQL_TOPT_ENGINE;
    o->u.engine = engine;
    return o;
}

MysqlTableOption *mysql_make_charset_option(MysqlParseState *ps,
                                             const char *charset) {
    MysqlTableOption *o = arena_alloc(ps, sizeof(MysqlTableOption));
    if (!o) return NULL;
    o->type = MYSQL_TOPT_CHARSET;
    o->u.charset = arena_strdup(ps, charset);
    return o;
}

MysqlTableOption *mysql_make_collate_option(MysqlParseState *ps,
                                             const char *collate) {
    MysqlTableOption *o = arena_alloc(ps, sizeof(MysqlTableOption));
    if (!o) return NULL;
    o->type = MYSQL_TOPT_COLLATE;
    o->u.collate = arena_strdup(ps, collate);
    return o;
}

/* ------------------------------------------------------------------ */
/*  UPSERT                                                              */
/* ------------------------------------------------------------------ */

MysqlUpsertClause *mysql_make_upsert(MysqlParseState *ps,
                                      MysqlUpsertAssign *assigns) {
    MysqlUpsertClause *u = arena_alloc(ps, sizeof(MysqlUpsertClause));
    if (!u) return NULL;
    u->assignments = assigns;
    int n = 0;
    for (MysqlUpsertAssign *a = assigns; a; a = a->next) n++;
    u->nassignments = n;
    return u;
}

MysqlUpsertAssign *mysql_make_upsert_assign(MysqlParseState *ps,
                                             const char *column,
                                             MysqlExpr *value) {
    MysqlUpsertAssign *a = arena_alloc(ps, sizeof(MysqlUpsertAssign));
    if (!a) return NULL;
    a->column = arena_strdup(ps, column);
    a->value = value;
    return a;
}

/* ------------------------------------------------------------------ */
/*  Argument list                                                       */
/* ------------------------------------------------------------------ */

MysqlArgList *mysql_arg_list_new(MysqlParseState *ps, MysqlExpr *expr) {
    MysqlArgList *l = arena_alloc(ps, sizeof(MysqlArgList));
    if (!l) return NULL;
    l->capacity = 8;
    l->args = arena_alloc(ps, (size_t)l->capacity * sizeof(MysqlExpr *));
    if (!l->args) return NULL;
    l->args[0] = expr;
    l->count = 1;
    return l;
}

MysqlArgList *mysql_arg_list_append(MysqlParseState *ps,
                                     MysqlArgList *list, MysqlExpr *expr) {
    if (!list || !expr) return list;
    if (list->count >= list->capacity) {
        int new_cap = list->capacity * 2;
        MysqlExpr **new_args = arena_alloc(ps,
            (size_t)new_cap * sizeof(MysqlExpr *));
        if (!new_args) return list;
        memcpy(new_args, list->args, (size_t)list->count * sizeof(MysqlExpr *));
        list->args = new_args;
        list->capacity = new_cap;
    }
    list->args[list->count++] = expr;
    return list;
}

/* ------------------------------------------------------------------ */
/*  AST cleanup (for non-arena allocations)                             */
/* ------------------------------------------------------------------ */

void mysql_expr_free(MysqlExpr *expr) {
    /* When using the arena, individual frees are no-ops.
    ** The arena is freed in mysql_parse_state_destroy(). */
    (void)expr;
}

void mysql_stmt_free(MysqlStmt *stmt) {
    (void)stmt;
}
