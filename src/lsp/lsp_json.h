/*
 * lsp_json.h -- minimal JSON value tree for the Lime LSP server.
 *
 * Hand-rolled because LSP traffic is small (kilobytes per
 * exchange), well-structured, and we want zero runtime
 * dependencies.  Pulling in cJSON / jansson would dwarf the rest
 * of the server.  Roughly 300 LOC of parser + serializer covers
 * everything `vscode-languageserver` ever sends us.
 *
 * The value tree owns its own storage; `json_free` walks it
 * recursively.  Builder helpers (`json_make_object`, etc.) and
 * mutators (`json_object_set`, `json_array_push`) take ownership
 * of any child values passed in -- callers must NOT free them
 * separately.
 */
#ifndef LIME_LSP_JSON_H
#define LIME_LSP_JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

typedef struct json_value json_value;
typedef struct json_member json_member;

struct json_value {
    json_type_t type;
    union {
        int boolean;
        double number;
        struct {
            char  *data;     /* NUL-terminated, heap */
            size_t len;
        } string;
        struct {
            json_value **items;
            size_t       count;
            size_t       cap;
        } array;
        struct {
            json_member *members;
            size_t       count;
            size_t       cap;
        } object;
    } u;
};

struct json_member {
    char       *key;        /* NUL-terminated, heap */
    json_value *value;
};

/* Parse JSON from a buffer.  `len` is the byte length; the buffer
 * does not need to be NUL-terminated.  Returns NULL on parse
 * error.  Caller frees with json_free.
 */
json_value *json_parse(const char *src, size_t len);

/* Free a value tree (handles NULL gracefully). */
void json_free(json_value *v);

/* Serialize a value tree to a freshly-allocated NUL-terminated
 * string.  Returns NULL on OOM.  Caller frees with free().
 */
char *json_serialize(const json_value *v);

/* Builders (heap-allocated; ownership transfers to parent on
 * insert).
 */
json_value *json_make_null(void);
json_value *json_make_bool(int b);
json_value *json_make_number(double n);
json_value *json_make_int(long long n);
json_value *json_make_string(const char *s);          /* copies */
json_value *json_make_string_n(const char *s, size_t n);
json_value *json_make_array(void);
json_value *json_make_object(void);

/* Mutators -- v takes ownership of `child`. */
int json_array_push(json_value *arr, json_value *child);
int json_object_set(json_value *obj, const char *key, json_value *child);

/* Accessors -- return NULL / 0 if shape is wrong.  Returned
 * pointers alias the tree; do not free.
 */
const json_value *json_get(const json_value *obj, const char *key);
const json_value *json_at(const json_value *arr, size_t idx);
const char       *json_string(const json_value *v);   /* NULL if not string */
size_t            json_string_len(const json_value *v);
double            json_number(const json_value *v);
long long         json_int(const json_value *v);
int               json_bool(const json_value *v);
size_t            json_array_size(const json_value *arr);
size_t            json_object_size(const json_value *obj);

#endif /* LIME_LSP_JSON_H */
