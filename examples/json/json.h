/*
** json.h -- public AST types and helper functions for the
** examples/json Lime parser.
**
** Allocator model (relevant for benchmarking)
** -------------------------------------------
** All AST nodes and decoded strings flow through json_alloc(),
** which dispatches based on a global JsonAllocMode:
**
**   JSON_ALLOC_MALLOC         (default) -- one malloc per node, json_free
**                             walks and releases everything.  This is
**                             the "honest" mode; the example uses it.
**
**   JSON_ALLOC_MALLOC_NOFREE  -- malloc per node, json_free is a
**                             no-op.  Simulates "leak everything";
**                             useful for measuring parse-only cost
**                             without paying the deallocator side.
**
**   JSON_ALLOC_ARENA          -- bump-pointer from a pre-allocated
**                             JsonArena.  Reuses the arena across
**                             parses by resetting the bump pointer
**                             between iterations (no malloc, no free
**                             in steady state).  Mirrors simdjson's
**                             internal allocation model: two big
**                             buffers allocated once per parser,
**                             reset between parses.
**
** The bench_simdjson_compare driver uses all three modes to give a
** fair-ish comparison against simdjson.
*/
#ifndef LIME_EXAMPLES_JSON_H
#define LIME_EXAMPLES_JSON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    JSON_T_NULL,
    JSON_T_BOOL,
    JSON_T_NUMBER,
    JSON_T_STRING,
    JSON_T_ARRAY,
    JSON_T_OBJECT,
    JSON_T_PAIR,    /* internal: a key/value bound together */
} JsonType;

typedef struct JsonValue {
    JsonType type;
    union {
        int b;
        double n;
        char *s; /* heap-owned, decoded */
        struct {
            struct JsonValue **items;
            size_t count;
            size_t cap;
        } a;
        struct {
            struct JsonValue **pairs; /* each item is a JSON_T_PAIR */
            size_t count;
            size_t cap;
        } o;
        struct {
            char *key;            /* heap-owned */
            struct JsonValue *val;
        } p;
    };
} JsonValue;

/* ------------------------------------------------------------------ */
/*  Allocator dispatch                                                 */
/* ------------------------------------------------------------------ */

typedef enum {
    JSON_ALLOC_MALLOC = 0,
    JSON_ALLOC_MALLOC_NOFREE,
    JSON_ALLOC_ARENA,
} JsonAllocMode;

typedef struct JsonArena {
    char *base;
    size_t cap;
    size_t used;
    /* Sub-allocations made through this arena that need their *own*
    ** allocations destroyed (resizable arrays inside JsonValues).
    ** When the arena is reset / destroyed, these are freed. */
    void **escapes;
    size_t escapes_count;
    size_t escapes_cap;
} JsonArena;

void json_arena_init(JsonArena *a, size_t cap);
void json_arena_destroy(JsonArena *a);
void json_arena_reset(JsonArena *a); /* bump back to 0; does not free */

void          json_set_alloc_mode(JsonAllocMode m);
JsonAllocMode json_get_alloc_mode(void);
void          json_set_arena(JsonArena *a); /* used in JSON_ALLOC_ARENA */
JsonArena    *json_get_arena(void);

/* Generic allocation primitive used by the helpers and the tokenizer.
** Honors the current mode; in MALLOC mode is calloc-equivalent (zeroed). */
void *json_alloc(size_t bytes);

/* ------------------------------------------------------------------ */
/*  AST constructors                                                   */
/* ------------------------------------------------------------------ */

JsonValue *json_null(void);
JsonValue *json_bool(int b);
JsonValue *json_number_take(char *literal); /* takes ownership of literal */
JsonValue *json_string_take(char *literal); /* takes ownership of literal */
JsonValue *json_array_new(void);
void       json_array_append(JsonValue *arr, JsonValue *item);
JsonValue *json_object_new(void);
JsonValue *json_pair(char *key, JsonValue *val); /* takes ownership of key */
void       json_object_add_pair(JsonValue *obj, JsonValue *pair);

void json_free(JsonValue *v);
void json_print(FILE *out, const JsonValue *v, int indent);

#endif /* LIME_EXAMPLES_JSON_H */
