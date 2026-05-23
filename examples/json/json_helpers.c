/*
** json_helpers.c -- AST node constructors / destructor / pretty
** printer with a pluggable allocator.  See json.h for the allocator
** model.
*/
#include "json.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Allocator state                                                    */
/* ------------------------------------------------------------------ */

static JsonAllocMode g_mode  = JSON_ALLOC_MALLOC;
static JsonArena    *g_arena = NULL;

void json_set_alloc_mode(JsonAllocMode m) { g_mode = m; }
JsonAllocMode json_get_alloc_mode(void)   { return g_mode; }
void json_set_arena(JsonArena *a)         { g_arena = a; }
JsonArena *json_get_arena(void)           { return g_arena; }

/* ------------------------------------------------------------------ */
/*  Arena                                                              */
/* ------------------------------------------------------------------ */

void json_arena_init(JsonArena *a, size_t cap) {
    a->base  = malloc(cap);
    a->cap   = cap;
    a->used  = 0;
    a->escapes = NULL;
    a->escapes_count = 0;
    a->escapes_cap = 0;
}

void json_arena_destroy(JsonArena *a) {
    free(a->base);
    a->base = NULL;
    for (size_t i = 0; i < a->escapes_count; i++) free(a->escapes[i]);
    free(a->escapes);
    a->escapes = NULL;
    a->escapes_count = 0;
    a->escapes_cap = 0;
    a->cap = a->used = 0;
}

void json_arena_reset(JsonArena *a) {
    /* Free escape allocations (resizable arrays inside JsonValues
    ** that overflow into heap) and reset bump pointer to 0. */
    for (size_t i = 0; i < a->escapes_count; i++) free(a->escapes[i]);
    a->escapes_count = 0;
    a->used = 0;
}

static void *arena_alloc(JsonArena *a, size_t bytes) {
    /* 8-byte align */
    size_t aligned = (bytes + 7) & ~(size_t)7;
    if (a->used + aligned > a->cap) {
        /* Out of arena: spill to malloc and remember to free in reset. */
        void *p = calloc(1, bytes);
        if (a->escapes_count == a->escapes_cap) {
            size_t nc = a->escapes_cap == 0 ? 8 : a->escapes_cap * 2;
            a->escapes = realloc(a->escapes, nc * sizeof(void *));
            a->escapes_cap = nc;
        }
        a->escapes[a->escapes_count++] = p;
        return p;
    }
    void *p = a->base + a->used;
    memset(p, 0, bytes);
    a->used += aligned;
    return p;
}

/* ------------------------------------------------------------------ */
/*  Allocation dispatch                                                */
/* ------------------------------------------------------------------ */

void *json_alloc(size_t bytes) {
    switch (g_mode) {
        case JSON_ALLOC_MALLOC:
        case JSON_ALLOC_MALLOC_NOFREE: {
            void *p = malloc(bytes);
            if (p) memset(p, 0, bytes);
            return p;
        }
        case JSON_ALLOC_ARENA:
            assert(g_arena != NULL);
            return arena_alloc(g_arena, bytes);
    }
    return NULL;
}

static void *value_alloc(JsonType t) {
    JsonValue *v = json_alloc(sizeof(*v));
    if (v == NULL) {
        fprintf(stderr, "json: out of memory\n");
        exit(1);
    }
    v->type = t;
    return v;
}

JsonValue *json_null(void) {
    return value_alloc(JSON_T_NULL);
}

JsonValue *json_bool(int b) {
    JsonValue *v = value_alloc(JSON_T_BOOL);
    v->b = b;
    return v;
}

JsonValue *json_number_take(char *literal) {
    JsonValue *v = value_alloc(JSON_T_NUMBER);
    v->n = strtod(literal, NULL);
    /* In MALLOC mode the tokenizer used real malloc and we now own
    ** that memory; free it.  In NOFREE mode we deliberately leak.
    ** In ARENA mode the literal came from the arena and is freed
    ** by arena_reset / arena_destroy (no per-value action needed). */
    if (g_mode == JSON_ALLOC_MALLOC) free(literal);
    return v;
}

JsonValue *json_string_take(char *literal) {
    JsonValue *v = value_alloc(JSON_T_STRING);
    v->s = literal; /* ownership moved into the tree */
    return v;
}

static void array_grow(JsonValue *arr) {
    if (arr->a.count + 1 > arr->a.cap) {
        size_t new_cap = arr->a.cap == 0 ? 4 : arr->a.cap * 2;
        if (g_mode == JSON_ALLOC_ARENA) {
            /* Resizing a backing array from a bump arena is awkward;
            ** route through the arena's escape list so it still gets
            ** released on reset. */
            JsonValue **fresh = json_alloc(new_cap * sizeof(JsonValue *));
            if (arr->a.items) memcpy(fresh, arr->a.items, arr->a.count * sizeof(JsonValue *));
            arr->a.items = fresh;
        } else {
            arr->a.items = realloc(arr->a.items, new_cap * sizeof(JsonValue *));
        }
        arr->a.cap = new_cap;
    }
}

JsonValue *json_array_new(void) {
    return value_alloc(JSON_T_ARRAY);
}

void json_array_append(JsonValue *arr, JsonValue *item) {
    assert(arr != NULL && arr->type == JSON_T_ARRAY);
    array_grow(arr);
    arr->a.items[arr->a.count++] = item;
}

JsonValue *json_object_new(void) {
    return value_alloc(JSON_T_OBJECT);
}

JsonValue *json_pair(char *key, JsonValue *val) {
    JsonValue *p = value_alloc(JSON_T_PAIR);
    p->p.key = key;
    p->p.val = val;
    return p;
}

void json_object_add_pair(JsonValue *obj, JsonValue *pair) {
    assert(obj != NULL && obj->type == JSON_T_OBJECT);
    assert(pair != NULL && pair->type == JSON_T_PAIR);
    if (obj->o.count + 1 > obj->o.cap) {
        size_t new_cap = obj->o.cap == 0 ? 4 : obj->o.cap * 2;
        if (g_mode == JSON_ALLOC_ARENA) {
            JsonValue **fresh = json_alloc(new_cap * sizeof(JsonValue *));
            if (obj->o.pairs) memcpy(fresh, obj->o.pairs, obj->o.count * sizeof(JsonValue *));
            obj->o.pairs = fresh;
        } else {
            obj->o.pairs = realloc(obj->o.pairs, new_cap * sizeof(JsonValue *));
        }
        obj->o.cap = new_cap;
    }
    obj->o.pairs[obj->o.count++] = pair;
}

void json_free(JsonValue *v) {
    /* In NOFREE / ARENA modes we don't release per-node.  The
    ** caller is responsible for resetting / destroying the arena
    ** (or accepting the leak). */
    if (g_mode != JSON_ALLOC_MALLOC) return;
    if (v == NULL) return;
    switch (v->type) {
        case JSON_T_NULL:
        case JSON_T_BOOL:
        case JSON_T_NUMBER:
            break;
        case JSON_T_STRING:
            free(v->s);
            break;
        case JSON_T_ARRAY:
            for (size_t i = 0; i < v->a.count; i++) json_free(v->a.items[i]);
            free(v->a.items);
            break;
        case JSON_T_OBJECT:
            for (size_t i = 0; i < v->o.count; i++) json_free(v->o.pairs[i]);
            free(v->o.pairs);
            break;
        case JSON_T_PAIR:
            free(v->p.key);
            json_free(v->p.val);
            break;
    }
    free(v);
}

static void print_indent(FILE *out, int n) {
    for (int i = 0; i < n; i++) fputs("  ", out);
}

void json_print(FILE *out, const JsonValue *v, int indent) {
    if (v == NULL) {
        fputs("null", out);
        return;
    }
    switch (v->type) {
        case JSON_T_NULL:   fputs("null", out); break;
        case JSON_T_BOOL:   fputs(v->b ? "true" : "false", out); break;
        case JSON_T_NUMBER: fprintf(out, "%g", v->n); break;
        case JSON_T_STRING: fprintf(out, "\"%s\"", v->s ? v->s : ""); break;
        case JSON_T_ARRAY:
            if (v->a.count == 0) { fputs("[]", out); break; }
            fputs("[\n", out);
            for (size_t i = 0; i < v->a.count; i++) {
                print_indent(out, indent + 1);
                json_print(out, v->a.items[i], indent + 1);
                fputs(i + 1 < v->a.count ? ",\n" : "\n", out);
            }
            print_indent(out, indent); fputc(']', out);
            break;
        case JSON_T_OBJECT:
            if (v->o.count == 0) { fputs("{}", out); break; }
            fputs("{\n", out);
            for (size_t i = 0; i < v->o.count; i++) {
                print_indent(out, indent + 1);
                JsonValue *p = v->o.pairs[i];
                fprintf(out, "\"%s\": ", p->p.key);
                json_print(out, p->p.val, indent + 1);
                fputs(i + 1 < v->o.count ? ",\n" : "\n", out);
            }
            print_indent(out, indent); fputc('}', out);
            break;
        case JSON_T_PAIR:
            fprintf(out, "<pair %s>", v->p.key);
            break;
    }
}
