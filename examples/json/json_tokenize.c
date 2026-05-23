/*
** json_tokenize.c -- minimal JSON tokenizer.  Decodes string escapes
** into a heap-allocated char* on STRING tokens; passes number
** literals through as heap-allocated char* (the grammar's action
** body parses them via strtod).
**
** Not a faithful JSON validator -- accepts what RFC 8259 calls
** "well-formed" without enforcing every constraint (e.g. doesn't
** validate UTF-8 sequences inside strings).  Sufficient for the
** example.
*/
#include "json_tokenize.h"
#include "json.h"          /* json_alloc honors the current allocator */
#include "json_grammar.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void json_scanner_init(JsonScanner *s, const char *input, size_t len) {
    s->cursor = input;
    s->end    = input + len;
}

static int peek(const JsonScanner *s) {
    return s->cursor < s->end ? (unsigned char)*s->cursor : -1;
}

static int consume(JsonScanner *s) {
    return s->cursor < s->end ? (unsigned char)*s->cursor++ : -1;
}

static void skip_ws(JsonScanner *s) {
    while (s->cursor < s->end) {
        unsigned char c = (unsigned char)*s->cursor;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') s->cursor++;
        else break;
    }
}

static char *scan_string_decoded(JsonScanner *s) {
    /* On entry, *cursor == '"'.  We can't bump-allocate a string
    ** while it grows since we don't know the final size up-front,
    ** so first pass: count bytes; second pass: copy.  This keeps
    ** the arena allocator linear (one bump-alloc per string). */
    consume(s);
    const char *start = s->cursor;
    size_t out_len = 0;
    while (s->cursor < s->end && *s->cursor != '"') {
        if (*s->cursor == '\\' && s->cursor + 1 < s->end) {
            char e = s->cursor[1];
            if (e == 'u') {
                /* \uXXXX -- pass through as 6 raw bytes (no decode) */
                size_t skip = 2;
                for (int i = 0; i < 4 && s->cursor + 2 + i < s->end; i++) skip++;
                out_len += skip;
                s->cursor += skip;
            } else {
                out_len += 1;
                s->cursor += 2;
            }
        } else {
            out_len++;
            s->cursor++;
        }
    }
    size_t consumed = (size_t)(s->cursor - start);
    if (s->cursor < s->end && *s->cursor == '"') consume(s);

    char *buf = json_alloc(out_len + 1);
    if (buf == NULL) return NULL;

    /* Second pass: rewind and copy. */
    const char *p = start;
    size_t i = 0;
    size_t end_off = consumed;
    while ((size_t)(p - start) < end_off) {
        if (*p == '\\' && (size_t)(p - start) + 1 < end_off) {
            char e = p[1];
            switch (e) {
                case '"':  buf[i++] = '"';  p += 2; break;
                case '\\': buf[i++] = '\\'; p += 2; break;
                case '/':  buf[i++] = '/';  p += 2; break;
                case 'b':  buf[i++] = '\b'; p += 2; break;
                case 'f':  buf[i++] = '\f'; p += 2; break;
                case 'n':  buf[i++] = '\n'; p += 2; break;
                case 'r':  buf[i++] = '\r'; p += 2; break;
                case 't':  buf[i++] = '\t'; p += 2; break;
                case 'u':
                    /* Pass through 6 bytes \\uXXXX */
                    buf[i++] = '\\';
                    buf[i++] = 'u';
                    p += 2;
                    for (int k = 0; k < 4 && p < start + end_off; k++) {
                        buf[i++] = *p++;
                    }
                    break;
                default:
                    buf[i++] = '\\';
                    buf[i++] = e;
                    p += 2;
                    break;
            }
        } else {
            buf[i++] = *p++;
        }
    }
    buf[i] = '\0';
    return buf;
}

static char *scan_number(JsonScanner *s) {
    const char *start = s->cursor;
    if (peek(s) == '-') consume(s);
    while (peek(s) >= '0' && peek(s) <= '9') consume(s);
    if (peek(s) == '.') {
        consume(s);
        while (peek(s) >= '0' && peek(s) <= '9') consume(s);
    }
    int p = peek(s);
    if (p == 'e' || p == 'E') {
        consume(s);
        p = peek(s);
        if (p == '+' || p == '-') consume(s);
        while (peek(s) >= '0' && peek(s) <= '9') consume(s);
    }
    size_t n = (size_t)(s->cursor - start);
    char *buf = json_alloc(n + 1);
    if (buf == NULL) return NULL;
    memcpy(buf, start, n);
    buf[n] = '\0';
    return buf;
}

int json_scan(JsonScanner *s, void **out_value) {
    skip_ws(s);
    if (out_value) *out_value = NULL;
    if (s->cursor >= s->end) return 0;

    int c = peek(s);
    switch (c) {
        case '{': consume(s); return JSON_LBRACE;
        case '}': consume(s); return JSON_RBRACE;
        case '[': consume(s); return JSON_LBRACKET;
        case ']': consume(s); return JSON_RBRACKET;
        case ',': consume(s); return JSON_COMMA;
        case ':': consume(s); return JSON_COLON;
        case '"':
            *out_value = scan_string_decoded(s);
            return JSON_STRING;
        case 't':
            if (s->end - s->cursor >= 4 && memcmp(s->cursor, "true", 4) == 0) {
                s->cursor += 4;
                return JSON_TRUE;
            }
            return -1;
        case 'f':
            if (s->end - s->cursor >= 5 && memcmp(s->cursor, "false", 5) == 0) {
                s->cursor += 5;
                return JSON_FALSE;
            }
            return -1;
        case 'n':
            if (s->end - s->cursor >= 4 && memcmp(s->cursor, "null", 4) == 0) {
                s->cursor += 4;
                return JSON_NULL;
            }
            return -1;
        default:
            if (c == '-' || (c >= '0' && c <= '9')) {
                *out_value = scan_number(s);
                return JSON_NUMBER;
            }
            consume(s);
            return -1;
    }
}
