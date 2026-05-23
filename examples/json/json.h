/*
** json.h -- public AST types and helper functions for the
** examples/json Lime parser.
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
