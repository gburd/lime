/*
 * lsp_json.c -- minimal JSON parser and serializer.
 *
 * Implements RFC 8259 with two pragmatic shortcuts:
 *
 *   - Numbers parse via strtod; scientific notation is accepted.
 *     Integers up to 2^53 round-trip exactly through `double`.
 *   - We don't validate that object keys are unique on parse;
 *     `json_get` returns the first match.
 *
 * Strings are stored as UTF-8 (LSP wire format).  The parser
 * decodes \uXXXX escapes and re-emits them as UTF-8 bytes; pairs
 * of surrogates are stitched back together.  The serializer
 * emits non-ASCII bytes verbatim (LSP requires UTF-8).
 */

#include "lsp_json.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- parser state --------------------------------------------------- */

typedef struct {
    const char *src;
    size_t      len;
    size_t      pos;
} parser_t;

static void skip_ws(parser_t *p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static int peek(parser_t *p) {
    return p->pos < p->len ? (unsigned char)p->src[p->pos] : -1;
}

static int eat(parser_t *p, const char *s) {
    size_t n = strlen(s);
    if (p->pos + n > p->len) return 0;
    if (memcmp(p->src + p->pos, s, n) != 0) return 0;
    p->pos += n;
    return 1;
}

static json_value *parse_value(parser_t *p);

/* ---- string buffer -------------------------------------------------- */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} sbuf_t;

static int sbuf_reserve(sbuf_t *b, size_t need) {
    if (b->cap >= need) return 1;
    size_t nc = b->cap ? b->cap : 16;
    while (nc < need) nc *= 2;
    char *nd = (char *)realloc(b->data, nc);
    if (!nd) return 0;
    b->data = nd;
    b->cap  = nc;
    return 1;
}

static int sbuf_putc(sbuf_t *b, char c) {
    if (!sbuf_reserve(b, b->len + 1)) return 0;
    b->data[b->len++] = c;
    return 1;
}

static int sbuf_put(sbuf_t *b, const char *s, size_t n) {
    if (!sbuf_reserve(b, b->len + n)) return 0;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    return 1;
}

static int encode_utf8(uint32_t cp, sbuf_t *out) {
    if (cp < 0x80) {
        return sbuf_putc(out, (char)cp);
    } else if (cp < 0x800) {
        return sbuf_putc(out, (char)(0xC0 | (cp >> 6))) &&
               sbuf_putc(out, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        return sbuf_putc(out, (char)(0xE0 | (cp >> 12))) &&
               sbuf_putc(out, (char)(0x80 | ((cp >> 6) & 0x3F))) &&
               sbuf_putc(out, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x110000) {
        return sbuf_putc(out, (char)(0xF0 | (cp >> 18))) &&
               sbuf_putc(out, (char)(0x80 | ((cp >> 12) & 0x3F))) &&
               sbuf_putc(out, (char)(0x80 | ((cp >>  6) & 0x3F))) &&
               sbuf_putc(out, (char)(0x80 | (cp & 0x3F)));
    }
    return 0;
}

static int parse_hex4(parser_t *p, uint32_t *out) {
    if (p->pos + 4 > p->len) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)p->src[p->pos + i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') v |= 10 + c - 'A';
        else return 0;
    }
    p->pos += 4;
    *out = v;
    return 1;
}

static char *parse_string_raw(parser_t *p, size_t *out_len) {
    if (peek(p) != '"') return NULL;
    p->pos++;
    sbuf_t b = {0};
    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->src[p->pos++];
        if (c == '"') {
            if (!sbuf_putc(&b, 0)) goto fail;
            *out_len = b.len - 1;
            return b.data;
        }
        if (c == '\\') {
            if (p->pos >= p->len) goto fail;
            unsigned char e = (unsigned char)p->src[p->pos++];
            switch (e) {
                case '"':  if (!sbuf_putc(&b, '"'))  goto fail; break;
                case '\\': if (!sbuf_putc(&b, '\\')) goto fail; break;
                case '/':  if (!sbuf_putc(&b, '/'))  goto fail; break;
                case 'b':  if (!sbuf_putc(&b, '\b')) goto fail; break;
                case 'f':  if (!sbuf_putc(&b, '\f')) goto fail; break;
                case 'n':  if (!sbuf_putc(&b, '\n')) goto fail; break;
                case 'r':  if (!sbuf_putc(&b, '\r')) goto fail; break;
                case 't':  if (!sbuf_putc(&b, '\t')) goto fail; break;
                case 'u': {
                    uint32_t cp;
                    if (!parse_hex4(p, &cp)) goto fail;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        /* high surrogate; expect low surrogate */
                        if (p->pos + 2 > p->len) goto fail;
                        if (p->src[p->pos] != '\\' || p->src[p->pos+1] != 'u')
                            goto fail;
                        p->pos += 2;
                        uint32_t lo;
                        if (!parse_hex4(p, &lo)) goto fail;
                        if (lo < 0xDC00 || lo > 0xDFFF) goto fail;
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                    }
                    if (!encode_utf8(cp, &b)) goto fail;
                    break;
                }
                default: goto fail;
            }
        } else if (c < 0x20) {
            goto fail;  /* unescaped control char */
        } else {
            if (!sbuf_putc(&b, (char)c)) goto fail;
        }
    }
fail:
    free(b.data);
    return NULL;
}

static json_value *parse_string(parser_t *p) {
    size_t n;
    char *s = parse_string_raw(p, &n);
    if (!s) return NULL;
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    if (!v) { free(s); return NULL; }
    v->type = JSON_STRING;
    v->u.string.data = s;
    v->u.string.len  = n;
    return v;
}

static json_value *parse_number(parser_t *p) {
    size_t start = p->pos;
    if (peek(p) == '-') p->pos++;
    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->src[p->pos];
        if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
            c == '+' || c == '-') {
            p->pos++;
        } else break;
    }
    if (p->pos == start) return NULL;
    char buf[64];
    size_t n = p->pos - start;
    if (n >= sizeof(buf)) return NULL;
    memcpy(buf, p->src + start, n);
    buf[n] = 0;
    char *end;
    double d = strtod(buf, &end);
    if (end == buf) return NULL;
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = JSON_NUMBER;
    v->u.number = d;
    return v;
}

static json_value *parse_array(parser_t *p) {
    if (peek(p) != '[') return NULL;
    p->pos++;
    json_value *arr = json_make_array();
    if (!arr) return NULL;
    skip_ws(p);
    if (peek(p) == ']') { p->pos++; return arr; }
    while (1) {
        skip_ws(p);
        json_value *item = parse_value(p);
        if (!item) goto fail;
        if (!json_array_push(arr, item)) { json_free(item); goto fail; }
        skip_ws(p);
        int c = peek(p);
        if (c == ',') { p->pos++; continue; }
        if (c == ']') { p->pos++; return arr; }
        goto fail;
    }
fail:
    json_free(arr);
    return NULL;
}

static json_value *parse_object(parser_t *p) {
    if (peek(p) != '{') return NULL;
    p->pos++;
    json_value *obj = json_make_object();
    if (!obj) return NULL;
    skip_ws(p);
    if (peek(p) == '}') { p->pos++; return obj; }
    while (1) {
        skip_ws(p);
        size_t klen;
        char *key = parse_string_raw(p, &klen);
        if (!key) goto fail;
        skip_ws(p);
        if (peek(p) != ':') { free(key); goto fail; }
        p->pos++;
        skip_ws(p);
        json_value *val = parse_value(p);
        if (!val) { free(key); goto fail; }
        if (!json_object_set(obj, key, val)) {
            free(key);
            json_free(val);
            goto fail;
        }
        free(key);
        skip_ws(p);
        int c = peek(p);
        if (c == ',') { p->pos++; continue; }
        if (c == '}') { p->pos++; return obj; }
        goto fail;
    }
fail:
    json_free(obj);
    return NULL;
}

static json_value *parse_value(parser_t *p) {
    skip_ws(p);
    int c = peek(p);
    if (c == '{') return parse_object(p);
    if (c == '[') return parse_array(p);
    if (c == '"') return parse_string(p);
    if (c == 't') {
        if (eat(p, "true")) return json_make_bool(1);
        return NULL;
    }
    if (c == 'f') {
        if (eat(p, "false")) return json_make_bool(0);
        return NULL;
    }
    if (c == 'n') {
        if (eat(p, "null")) return json_make_null();
        return NULL;
    }
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(p);
    return NULL;
}

json_value *json_parse(const char *src, size_t len) {
    parser_t p = { src, len, 0 };
    json_value *v = parse_value(&p);
    if (!v) return NULL;
    skip_ws(&p);
    if (p.pos != len) {
        /* trailing garbage tolerated only if it's whitespace */
        json_free(v);
        return NULL;
    }
    return v;
}

/* ---- builders ------------------------------------------------------- */

json_value *json_make_null(void) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    if (v) v->type = JSON_NULL;
    return v;
}

json_value *json_make_bool(int b) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = JSON_BOOL;
    v->u.boolean = b ? 1 : 0;
    return v;
}

json_value *json_make_number(double n) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = JSON_NUMBER;
    v->u.number = n;
    return v;
}

json_value *json_make_int(long long n) {
    return json_make_number((double)n);
}

json_value *json_make_string_n(const char *s, size_t n) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = JSON_STRING;
    v->u.string.data = (char *)malloc(n + 1);
    if (!v->u.string.data) { free(v); return NULL; }
    if (n) memcpy(v->u.string.data, s, n);
    v->u.string.data[n] = 0;
    v->u.string.len = n;
    return v;
}

json_value *json_make_string(const char *s) {
    if (!s) return json_make_null();
    return json_make_string_n(s, strlen(s));
}

json_value *json_make_array(void) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    if (v) v->type = JSON_ARRAY;
    return v;
}

json_value *json_make_object(void) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    if (v) v->type = JSON_OBJECT;
    return v;
}

int json_array_push(json_value *arr, json_value *child) {
    if (!arr || arr->type != JSON_ARRAY || !child) return 0;
    if (arr->u.array.count == arr->u.array.cap) {
        size_t nc = arr->u.array.cap ? arr->u.array.cap * 2 : 8;
        json_value **ni = (json_value **)realloc(arr->u.array.items,
                                                 nc * sizeof(*ni));
        if (!ni) return 0;
        arr->u.array.items = ni;
        arr->u.array.cap = nc;
    }
    arr->u.array.items[arr->u.array.count++] = child;
    return 1;
}

int json_object_set(json_value *obj, const char *key, json_value *child) {
    if (!obj || obj->type != JSON_OBJECT || !key || !child) return 0;
    /* replace existing key if present */
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.members[i].key, key) == 0) {
            json_free(obj->u.object.members[i].value);
            obj->u.object.members[i].value = child;
            return 1;
        }
    }
    if (obj->u.object.count == obj->u.object.cap) {
        size_t nc = obj->u.object.cap ? obj->u.object.cap * 2 : 8;
        json_member *nm = (json_member *)realloc(obj->u.object.members,
                                                 nc * sizeof(*nm));
        if (!nm) return 0;
        obj->u.object.members = nm;
        obj->u.object.cap = nc;
    }
    char *kc = strdup(key);
    if (!kc) return 0;
    obj->u.object.members[obj->u.object.count].key   = kc;
    obj->u.object.members[obj->u.object.count].value = child;
    obj->u.object.count++;
    return 1;
}

/* ---- accessors ------------------------------------------------------ */

const json_value *json_get(const json_value *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.members[i].key, key) == 0) {
            return obj->u.object.members[i].value;
        }
    }
    return NULL;
}

const json_value *json_at(const json_value *arr, size_t idx) {
    if (!arr || arr->type != JSON_ARRAY) return NULL;
    if (idx >= arr->u.array.count) return NULL;
    return arr->u.array.items[idx];
}

const char *json_string(const json_value *v) {
    return (v && v->type == JSON_STRING) ? v->u.string.data : NULL;
}

size_t json_string_len(const json_value *v) {
    return (v && v->type == JSON_STRING) ? v->u.string.len : 0;
}

double json_number(const json_value *v) {
    if (!v) return 0;
    if (v->type == JSON_NUMBER) return v->u.number;
    if (v->type == JSON_BOOL)   return v->u.boolean;
    return 0;
}

long long json_int(const json_value *v) {
    return (long long)json_number(v);
}

int json_bool(const json_value *v) {
    if (!v) return 0;
    if (v->type == JSON_BOOL)   return v->u.boolean;
    if (v->type == JSON_NUMBER) return v->u.number != 0;
    return 0;
}

size_t json_array_size(const json_value *arr) {
    return (arr && arr->type == JSON_ARRAY) ? arr->u.array.count : 0;
}

size_t json_object_size(const json_value *obj) {
    return (obj && obj->type == JSON_OBJECT) ? obj->u.object.count : 0;
}

void json_free(json_value *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING: free(v->u.string.data); break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->u.array.count; i++)
                json_free(v->u.array.items[i]);
            free(v->u.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->u.object.count; i++) {
                free(v->u.object.members[i].key);
                json_free(v->u.object.members[i].value);
            }
            free(v->u.object.members);
            break;
        default: break;
    }
    free(v);
}

/* ---- serializer ----------------------------------------------------- */

static int ser_string(const char *s, size_t len, sbuf_t *out) {
    if (!sbuf_putc(out, '"')) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  if (!sbuf_put(out, "\\\"", 2)) return 0; break;
            case '\\': if (!sbuf_put(out, "\\\\", 2)) return 0; break;
            case '\b': if (!sbuf_put(out, "\\b",  2)) return 0; break;
            case '\f': if (!sbuf_put(out, "\\f",  2)) return 0; break;
            case '\n': if (!sbuf_put(out, "\\n",  2)) return 0; break;
            case '\r': if (!sbuf_put(out, "\\r",  2)) return 0; break;
            case '\t': if (!sbuf_put(out, "\\t",  2)) return 0; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    if (!sbuf_put(out, buf, 6)) return 0;
                } else {
                    /* UTF-8 bytes pass through unchanged */
                    if (!sbuf_putc(out, (char)c)) return 0;
                }
        }
    }
    return sbuf_putc(out, '"');
}

static int ser_value(const json_value *v, sbuf_t *out) {
    if (!v) return sbuf_put(out, "null", 4);
    switch (v->type) {
        case JSON_NULL:   return sbuf_put(out, "null", 4);
        case JSON_BOOL:   return sbuf_put(out, v->u.boolean ? "true" : "false",
                                          v->u.boolean ? 4 : 5);
        case JSON_NUMBER: {
            char buf[64];
            double n = v->u.number;
            int len;
            /* Emit integers without decimal point when possible. */
            if (n == (double)(long long)n && n >= -1e15 && n <= 1e15) {
                len = snprintf(buf, sizeof(buf), "%lld", (long long)n);
            } else {
                len = snprintf(buf, sizeof(buf), "%.17g", n);
            }
            if (len < 0) return 0;
            return sbuf_put(out, buf, (size_t)len);
        }
        case JSON_STRING:
            return ser_string(v->u.string.data, v->u.string.len, out);
        case JSON_ARRAY:
            if (!sbuf_putc(out, '[')) return 0;
            for (size_t i = 0; i < v->u.array.count; i++) {
                if (i && !sbuf_putc(out, ',')) return 0;
                if (!ser_value(v->u.array.items[i], out)) return 0;
            }
            return sbuf_putc(out, ']');
        case JSON_OBJECT:
            if (!sbuf_putc(out, '{')) return 0;
            for (size_t i = 0; i < v->u.object.count; i++) {
                if (i && !sbuf_putc(out, ',')) return 0;
                const char *k = v->u.object.members[i].key;
                if (!ser_string(k, strlen(k), out)) return 0;
                if (!sbuf_putc(out, ':')) return 0;
                if (!ser_value(v->u.object.members[i].value, out)) return 0;
            }
            return sbuf_putc(out, '}');
    }
    return 0;
}

char *json_serialize(const json_value *v) {
    sbuf_t out = {0};
    if (!ser_value(v, &out)) {
        free(out.data);
        return NULL;
    }
    if (!sbuf_putc(&out, 0)) {
        free(out.data);
        return NULL;
    }
    return out.data;
}
