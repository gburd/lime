/*
** EDN Literals Extension - Semantic Types and Action Prototypes
**
** Defines the AST node types and semantic action function prototypes
** used by the EDN grammar extension.  EDN (Extensible Data Notation)
** is Clojure's data format; this extension adds first-class support
** for EDN literals in SQL queries.
**
** EDN types supported:
**   - Keywords    (:name, :active, :theme)
**   - Maps        {:key val :key2 val2}
**   - Vectors     [1 2 3]
**   - Sets        #{:read :write :delete}
**   - Strings     "hello"
**   - Integers    42
**   - Floats      3.14
**   - Booleans    true / false
**   - nil
**   - Nested combinations of the above
*/
#ifndef EDN_SEMANTICS_H
#define EDN_SEMANTICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Token value                                                        */
/* ------------------------------------------------------------------ */

typedef struct EdnToken {
    int64_t     ival;
    double      fval;
    const char *sval;
    int         line;
    int         col;
} EdnToken;

/* ------------------------------------------------------------------ */
/*  EDN value types                                                    */
/* ------------------------------------------------------------------ */

typedef enum EdnValueType {
    EDN_VAL_NIL,
    EDN_VAL_BOOL,
    EDN_VAL_INTEGER,
    EDN_VAL_FLOAT,
    EDN_VAL_STRING,
    EDN_VAL_KEYWORD,
    EDN_VAL_SYMBOL,
    EDN_VAL_VECTOR,
    EDN_VAL_MAP,
    EDN_VAL_SET,
} EdnValueType;

/* ------------------------------------------------------------------ */
/*  EDN AST nodes                                                      */
/* ------------------------------------------------------------------ */

typedef struct EdnValue EdnValue;

/*
** A list of EDN values (used for vector elements, set elements, and
** map entries stored as alternating key-value pairs).
*/
typedef struct EdnValueList {
    EdnValue **items;
    int count;
    int capacity;
} EdnValueList;

/*
** A key-value pair in an EDN map.
*/
typedef struct EdnMapEntry {
    EdnValue *key;
    EdnValue *value;
    struct EdnMapEntry *next;
} EdnMapEntry;

/*
** An EDN map: ordered sequence of key-value pairs.
*/
typedef struct EdnMap {
    EdnMapEntry *head;
    EdnMapEntry *tail;
    int count;
} EdnMap;

/*
** Core EDN value node.
*/
struct EdnValue {
    EdnValueType type;
    union {
        /* EDN_VAL_BOOL */
        bool bval;

        /* EDN_VAL_INTEGER */
        int64_t ival;

        /* EDN_VAL_FLOAT */
        double fval;

        /* EDN_VAL_STRING */
        struct { char *value; } string;

        /* EDN_VAL_KEYWORD (includes leading colon in name) */
        struct { char *name; char *ns; } keyword;

        /* EDN_VAL_SYMBOL */
        struct { char *name; char *ns; } symbol;

        /* EDN_VAL_VECTOR */
        EdnValueList *vector;

        /* EDN_VAL_MAP */
        EdnMap *map;

        /* EDN_VAL_SET */
        EdnValueList *set;
    } u;
};

/* ------------------------------------------------------------------ */
/*  SQL integration node types                                         */
/* ------------------------------------------------------------------ */

/*
** Represents an EDN literal embedded in a SQL expression.
** The SQL parser produces these when it encounters EDN syntax.
*/
typedef enum EdnExprType {
    EDN_EXPR_LITERAL,         /* Standalone EDN value as SQL expr    */
    EDN_EXPR_KEYWORD_REF,     /* :keyword used as column/enum ref    */
    EDN_EXPR_CONTAINS,        /* @> containment with EDN operand     */
    EDN_EXPR_OVERLAP,         /* && overlap with EDN operand         */
    EDN_EXPR_EQUALS,          /* = comparison with EDN operand       */
    EDN_EXPR_IN_LIST,         /* IN (:a :b :c) keyword list          */
} EdnExprType;

typedef struct EdnExpr {
    EdnExprType type;
    union {
        /* EDN_EXPR_LITERAL */
        EdnValue *literal;

        /* EDN_EXPR_KEYWORD_REF */
        struct { char *name; } keyword_ref;

        /* EDN_EXPR_CONTAINS / EDN_EXPR_OVERLAP / EDN_EXPR_EQUALS */
        struct {
            struct EdnExpr *left;   /* SQL expression (column ref etc.) */
            EdnValue *right;        /* EDN value */
        } binop;

        /* EDN_EXPR_IN_LIST */
        struct {
            struct EdnExpr *expr;   /* SQL expression being tested */
            EdnValueList *list;     /* List of EDN keywords/values */
        } in_list;
    } u;
} EdnExpr;

/* ------------------------------------------------------------------ */
/*  Parse state                                                        */
/* ------------------------------------------------------------------ */

typedef struct EdnParseState {
    EdnValue *result;          /* Final parsed EDN value              */
    EdnExpr  *sql_result;      /* SQL expression containing EDN       */
    char     *error_msg;       /* Non-NULL on error                   */
    bool      has_error;

    /* Memory arena for AST allocations */
    void   *arena;
    size_t  arena_used;
    size_t  arena_size;
} EdnParseState;

/* ------------------------------------------------------------------ */
/*  Semantic action prototypes                                         */
/* ------------------------------------------------------------------ */

/* Parse state management */
EdnParseState *edn_parse_state_create(void);
void edn_parse_state_destroy(EdnParseState *pstate);
void edn_parse_error(EdnParseState *pstate, const char *msg);

/* EDN value constructors */
EdnValue *edn_make_nil(EdnParseState *pstate);
EdnValue *edn_make_bool(EdnParseState *pstate, bool val);
EdnValue *edn_make_integer(EdnParseState *pstate, int64_t val);
EdnValue *edn_make_float(EdnParseState *pstate, double val);
EdnValue *edn_make_string(EdnParseState *pstate, const char *val);
EdnValue *edn_make_keyword(EdnParseState *pstate, const char *name);
EdnValue *edn_make_ns_keyword(EdnParseState *pstate, const char *ns,
                               const char *name);
EdnValue *edn_make_symbol(EdnParseState *pstate, const char *name);

/* Collection constructors */
EdnValue *edn_make_vector(EdnParseState *pstate, EdnValueList *elements);
EdnValue *edn_make_map(EdnParseState *pstate, EdnMap *entries);
EdnValue *edn_make_set(EdnParseState *pstate, EdnValueList *elements);

/* List builders (for vector/set elements) */
EdnValueList *edn_value_list_new(EdnParseState *pstate, EdnValue *first);
EdnValueList *edn_value_list_append(EdnParseState *pstate,
                                     EdnValueList *list, EdnValue *item);

/* Map builders */
EdnMap *edn_map_new(EdnParseState *pstate, EdnValue *key, EdnValue *value);
EdnMap *edn_map_append(EdnParseState *pstate, EdnMap *map,
                        EdnValue *key, EdnValue *value);

/* SQL expression constructors */
EdnExpr *edn_make_literal_expr(EdnParseState *pstate, EdnValue *val);
EdnExpr *edn_make_keyword_ref(EdnParseState *pstate, const char *name);
EdnExpr *edn_make_contains_expr(EdnParseState *pstate,
                                 EdnExpr *left, EdnValue *right);
EdnExpr *edn_make_overlap_expr(EdnParseState *pstate,
                                EdnExpr *left, EdnValue *right);
EdnExpr *edn_make_equals_expr(EdnParseState *pstate,
                               EdnExpr *left, EdnValue *right);
EdnExpr *edn_make_in_list_expr(EdnParseState *pstate,
                                EdnExpr *expr, EdnValueList *list);

/* Emit final result */
void edn_emit_value(EdnParseState *pstate, EdnValue *val);
void edn_emit_expr(EdnParseState *pstate, EdnExpr *expr);

/* AST cleanup (no-ops with arena allocator) */
void edn_value_free(EdnValue *val);
void edn_expr_free(EdnExpr *expr);

#ifdef __cplusplus
}
#endif

#endif /* EDN_SEMANTICS_H */
