/*
** EDN Literals Extension - Semantic Actions
**
** Implements the AST construction functions called by the EDN grammar
** reduction actions.  Each function allocates an AST node and fills in
** the appropriate fields.
**
** Memory management uses a simple arena allocator attached to the parse
** state.  All allocations are freed in one shot when the parse state is
** destroyed, which avoids per-node free() calls and simplifies error
** handling in the parser.
*/

#include "edn_semantics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Arena allocator                                                    */
/* ------------------------------------------------------------------ */

#define ARENA_INITIAL_SIZE  (32 * 1024)   /* 32 KB */
#define ARENA_ALIGN         8

static void *arena_alloc(EdnParseState *pstate, size_t size) {
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

static char *arena_strdup(EdnParseState *pstate, const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = arena_alloc(pstate, len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

/* ------------------------------------------------------------------ */
/*  Parse state management                                             */
/* ------------------------------------------------------------------ */

EdnParseState *edn_parse_state_create(void) {
    EdnParseState *ps = calloc(1, sizeof(EdnParseState));
    return ps;
}

void edn_parse_state_destroy(EdnParseState *pstate) {
    if (pstate == NULL) return;
    free(pstate->arena);
    free(pstate->error_msg);
    free(pstate);
}

void edn_parse_error(EdnParseState *pstate, const char *msg) {
    if (pstate->has_error) return;  /* keep first error */
    pstate->has_error = true;
    free(pstate->error_msg);
    pstate->error_msg = msg ? strdup(msg) : NULL;
}

/* ------------------------------------------------------------------ */
/*  EDN value constructors -- primitives                               */
/* ------------------------------------------------------------------ */

EdnValue *edn_make_nil(EdnParseState *pstate) {
    EdnValue *v = arena_alloc(pstate, sizeof(EdnValue));
    if (v == NULL) return NULL;
    v->type = EDN_VAL_NIL;
    return v;
}

EdnValue *edn_make_bool(EdnParseState *pstate, bool val) {
    EdnValue *v = arena_alloc(pstate, sizeof(EdnValue));
    if (v == NULL) return NULL;
    v->type = EDN_VAL_BOOL;
    v->u.bval = val;
    return v;
}

EdnValue *edn_make_integer(EdnParseState *pstate, int64_t val) {
    EdnValue *v = arena_alloc(pstate, sizeof(EdnValue));
    if (v == NULL) return NULL;
    v->type = EDN_VAL_INTEGER;
    v->u.ival = val;
    return v;
}

EdnValue *edn_make_float(EdnParseState *pstate, double val) {
    EdnValue *v = arena_alloc(pstate, sizeof(EdnValue));
    if (v == NULL) return NULL;
    v->type = EDN_VAL_FLOAT;
    v->u.fval = val;
    return v;
}

EdnValue *edn_make_string(EdnParseState *pstate, const char *val) {
    EdnValue *v = arena_alloc(pstate, sizeof(EdnValue));
    if (v == NULL) return NULL;
    v->type = EDN_VAL_STRING;
    v->u.string.value = arena_strdup(pstate, val);
    return v;
}

/* ------------------------------------------------------------------ */
/*  EDN value constructors -- keywords and symbols                     */
/* ------------------------------------------------------------------ */

/*
** Parse a keyword token string.  The token sval includes the leading
** colon, e.g. ":active" or ":db/id".
**
** For namespaced keywords like ":db/id", we split on the slash:
**   ns   = "db"
**   name = "id"
**
** For simple keywords like ":active":
**   ns   = NULL
**   name = "active"
*/
EdnValue *edn_make_keyword(EdnParseState *pstate, const char *raw) {
    EdnValue *v = arena_alloc(pstate, sizeof(EdnValue));
    if (v == NULL) return NULL;
    v->type = EDN_VAL_KEYWORD;

    /* Skip leading colon */
    const char *name_start = raw;
    if (raw != NULL && raw[0] == ':') {
        name_start = raw + 1;
    }

    /* Check for namespace separator */
    const char *slash = name_start ? strchr(name_start, '/') : NULL;
    if (slash != NULL) {
        size_t ns_len = (size_t)(slash - name_start);
        char *ns = arena_alloc(pstate, ns_len + 1);
        if (ns != NULL) {
            memcpy(ns, name_start, ns_len);
            ns[ns_len] = '\0';
        }
        v->u.keyword.ns = ns;
        v->u.keyword.name = arena_strdup(pstate, slash + 1);
    } else {
        v->u.keyword.ns = NULL;
        v->u.keyword.name = arena_strdup(pstate, name_start);
    }

    return v;
}

EdnValue *edn_make_ns_keyword(EdnParseState *pstate, const char *ns,
                               const char *name) {
    EdnValue *v = arena_alloc(pstate, sizeof(EdnValue));
    if (v == NULL) return NULL;
    v->type = EDN_VAL_KEYWORD;
    v->u.keyword.ns = arena_strdup(pstate, ns);
    v->u.keyword.name = arena_strdup(pstate, name);
    return v;
}

EdnValue *edn_make_symbol(EdnParseState *pstate, const char *name) {
    EdnValue *v = arena_alloc(pstate, sizeof(EdnValue));
    if (v == NULL) return NULL;
    v->type = EDN_VAL_SYMBOL;
    v->u.symbol.ns = NULL;
    v->u.symbol.name = arena_strdup(pstate, name);
    return v;
}

/* ------------------------------------------------------------------ */
/*  EDN value constructors -- collections                              */
/* ------------------------------------------------------------------ */

EdnValue *edn_make_vector(EdnParseState *pstate, EdnValueList *elements) {
    EdnValue *v = arena_alloc(pstate, sizeof(EdnValue));
    if (v == NULL) return NULL;
    v->type = EDN_VAL_VECTOR;
    v->u.vector = elements;
    return v;
}

EdnValue *edn_make_map(EdnParseState *pstate, EdnMap *entries) {
    EdnValue *v = arena_alloc(pstate, sizeof(EdnValue));
    if (v == NULL) return NULL;
    v->type = EDN_VAL_MAP;
    v->u.map = entries;
    return v;
}

EdnValue *edn_make_set(EdnParseState *pstate, EdnValueList *elements) {
    EdnValue *v = arena_alloc(pstate, sizeof(EdnValue));
    if (v == NULL) return NULL;
    v->type = EDN_VAL_SET;
    v->u.set = elements;
    return v;
}

/* ------------------------------------------------------------------ */
/*  Value list builders (for vectors and sets)                         */
/* ------------------------------------------------------------------ */

EdnValueList *edn_value_list_new(EdnParseState *pstate, EdnValue *first) {
    EdnValueList *list = arena_alloc(pstate, sizeof(EdnValueList));
    if (list == NULL) return NULL;

    list->capacity = 8;
    list->items = arena_alloc(pstate, list->capacity * sizeof(EdnValue *));
    if (list->items == NULL) return NULL;

    list->items[0] = first;
    list->count = 1;
    return list;
}

EdnValueList *edn_value_list_append(EdnParseState *pstate,
                                     EdnValueList *list, EdnValue *item) {
    if (list == NULL || item == NULL) return list;

    if (list->count >= list->capacity) {
        int new_cap = list->capacity * 2;
        EdnValue **new_items = arena_alloc(pstate,
                                            new_cap * sizeof(EdnValue *));
        if (new_items == NULL) return list;
        memcpy(new_items, list->items, list->count * sizeof(EdnValue *));
        list->items = new_items;
        list->capacity = new_cap;
    }

    list->items[list->count++] = item;
    return list;
}

/* ------------------------------------------------------------------ */
/*  Map builders                                                       */
/* ------------------------------------------------------------------ */

EdnMap *edn_map_new(EdnParseState *pstate, EdnValue *key, EdnValue *value) {
    EdnMap *map = arena_alloc(pstate, sizeof(EdnMap));
    if (map == NULL) return NULL;

    EdnMapEntry *entry = arena_alloc(pstate, sizeof(EdnMapEntry));
    if (entry == NULL) return NULL;

    entry->key = key;
    entry->value = value;
    entry->next = NULL;

    map->head = map->tail = entry;
    map->count = 1;
    return map;
}

EdnMap *edn_map_append(EdnParseState *pstate, EdnMap *map,
                        EdnValue *key, EdnValue *value) {
    if (map == NULL) return edn_map_new(pstate, key, value);

    EdnMapEntry *entry = arena_alloc(pstate, sizeof(EdnMapEntry));
    if (entry == NULL) return map;

    entry->key = key;
    entry->value = value;
    entry->next = NULL;

    map->tail->next = entry;
    map->tail = entry;
    map->count++;
    return map;
}

/* ------------------------------------------------------------------ */
/*  SQL expression constructors                                        */
/* ------------------------------------------------------------------ */

EdnExpr *edn_make_literal_expr(EdnParseState *pstate, EdnValue *val) {
    EdnExpr *e = arena_alloc(pstate, sizeof(EdnExpr));
    if (e == NULL) return NULL;
    e->type = EDN_EXPR_LITERAL;
    e->u.literal = val;
    return e;
}

EdnExpr *edn_make_keyword_ref(EdnParseState *pstate, const char *name) {
    EdnExpr *e = arena_alloc(pstate, sizeof(EdnExpr));
    if (e == NULL) return NULL;
    e->type = EDN_EXPR_KEYWORD_REF;
    /* Skip leading colon if present */
    const char *n = (name && name[0] == ':') ? name + 1 : name;
    e->u.keyword_ref.name = arena_strdup(pstate, n);
    return e;
}

EdnExpr *edn_make_contains_expr(EdnParseState *pstate,
                                 EdnExpr *left, EdnValue *right) {
    EdnExpr *e = arena_alloc(pstate, sizeof(EdnExpr));
    if (e == NULL) return NULL;
    e->type = EDN_EXPR_CONTAINS;
    e->u.binop.left = left;
    e->u.binop.right = right;
    return e;
}

EdnExpr *edn_make_overlap_expr(EdnParseState *pstate,
                                EdnExpr *left, EdnValue *right) {
    EdnExpr *e = arena_alloc(pstate, sizeof(EdnExpr));
    if (e == NULL) return NULL;
    e->type = EDN_EXPR_OVERLAP;
    e->u.binop.left = left;
    e->u.binop.right = right;
    return e;
}

EdnExpr *edn_make_equals_expr(EdnParseState *pstate,
                               EdnExpr *left, EdnValue *right) {
    EdnExpr *e = arena_alloc(pstate, sizeof(EdnExpr));
    if (e == NULL) return NULL;
    e->type = EDN_EXPR_EQUALS;
    e->u.binop.left = left;
    e->u.binop.right = right;
    return e;
}

EdnExpr *edn_make_in_list_expr(EdnParseState *pstate,
                                EdnExpr *expr, EdnValueList *list) {
    EdnExpr *e = arena_alloc(pstate, sizeof(EdnExpr));
    if (e == NULL) return NULL;
    e->type = EDN_EXPR_IN_LIST;
    e->u.in_list.expr = expr;
    e->u.in_list.list = list;
    return e;
}

/* ------------------------------------------------------------------ */
/*  Emit final result                                                  */
/* ------------------------------------------------------------------ */

void edn_emit_value(EdnParseState *pstate, EdnValue *val) {
    pstate->result = val;
}

void edn_emit_expr(EdnParseState *pstate, EdnExpr *expr) {
    pstate->sql_result = expr;
}

/* ------------------------------------------------------------------ */
/*  AST cleanup (no-ops when using arena allocator)                    */
/* ------------------------------------------------------------------ */

/*
** When the arena allocator is used, individual node frees are no-ops.
** The entire AST is freed when edn_parse_state_destroy() releases
** the arena.  These functions exist to satisfy the interface and to
** support non-arena usage in tests.
*/

void edn_value_free(EdnValue *val) {
    (void)val;
}

void edn_expr_free(EdnExpr *expr) {
    (void)expr;
}
