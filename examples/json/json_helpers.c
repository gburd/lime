/*
** json_helpers.c -- AST node constructors / destructor / pretty printer
** for the examples/json Lime parser.  Memory model: callers transfer
** ownership of heap-allocated strings (number / string literals, object
** keys) via the *_take entry points; json_free walks the tree and
** releases everything.
*/
#include "json.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static JsonValue *alloc(JsonType t) {
    JsonValue *v = calloc(1, sizeof(*v));
    if (v == NULL) {
        fprintf(stderr, "json: out of memory\n");
        exit(1);
    }
    v->type = t;
    return v;
}

JsonValue *json_null(void) {
    return alloc(JSON_T_NULL);
}

JsonValue *json_bool(int b) {
    JsonValue *v = alloc(JSON_T_BOOL);
    v->b = b;
    return v;
}

JsonValue *json_number_take(char *literal) {
    JsonValue *v = alloc(JSON_T_NUMBER);
    v->n = strtod(literal, NULL);
    free(literal);
    return v;
}

JsonValue *json_string_take(char *literal) {
    JsonValue *v = alloc(JSON_T_STRING);
    v->s = literal; /* takes ownership */
    return v;
}

static void array_grow(JsonValue *arr) {
    if (arr->a.count + 1 > arr->a.cap) {
        size_t new_cap = arr->a.cap == 0 ? 4 : arr->a.cap * 2;
        arr->a.items = realloc(arr->a.items, new_cap * sizeof(JsonValue *));
        arr->a.cap = new_cap;
    }
}

JsonValue *json_array_new(void) {
    return alloc(JSON_T_ARRAY);
}

void json_array_append(JsonValue *arr, JsonValue *item) {
    assert(arr != NULL && arr->type == JSON_T_ARRAY);
    array_grow(arr);
    arr->a.items[arr->a.count++] = item;
}

JsonValue *json_object_new(void) {
    return alloc(JSON_T_OBJECT);
}

JsonValue *json_pair(char *key, JsonValue *val) {
    JsonValue *p = alloc(JSON_T_PAIR);
    p->p.key = key;
    p->p.val = val;
    return p;
}

void json_object_add_pair(JsonValue *obj, JsonValue *pair) {
    assert(obj != NULL && obj->type == JSON_T_OBJECT);
    assert(pair != NULL && pair->type == JSON_T_PAIR);
    if (obj->o.count + 1 > obj->o.cap) {
        size_t new_cap = obj->o.cap == 0 ? 4 : obj->o.cap * 2;
        obj->o.pairs = realloc(obj->o.pairs, new_cap * sizeof(JsonValue *));
        obj->o.cap = new_cap;
    }
    obj->o.pairs[obj->o.count++] = pair;
}

void json_free(JsonValue *v) {
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
            /* not normally encountered as a top-level value */
            fprintf(out, "<pair %s>", v->p.key);
            break;
    }
}
