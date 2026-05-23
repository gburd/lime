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
    /* On entry, *cursor == '"'. */
    consume(s);

    size_t cap = 32;
    size_t len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) return NULL;

    while (s->cursor < s->end && *s->cursor != '"') {
        if (len + 4 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        int c = consume(s);
        if (c == '\\' && s->cursor < s->end) {
            int e = consume(s);
            switch (e) {
                case '"': buf[len++] = '"'; break;
                case '\\': buf[len++] = '\\'; break;
                case '/': buf[len++] = '/'; break;
                case 'b': buf[len++] = '\b'; break;
                case 'f': buf[len++] = '\f'; break;
                case 'n': buf[len++] = '\n'; break;
                case 'r': buf[len++] = '\r'; break;
                case 't': buf[len++] = '\t'; break;
                /* Unicode escapes \uXXXX are accepted at the lexer
                ** level but not decoded here -- keep the example
                ** small.  Pass through as-is. */
                case 'u':
                    buf[len++] = '\\';
                    buf[len++] = 'u';
                    for (int i = 0; i < 4 && s->cursor < s->end; i++) {
                        buf[len++] = consume(s);
                    }
                    break;
                default:
                    /* unknown escape; pass through */
                    buf[len++] = '\\';
                    buf[len++] = (char)e;
                    break;
            }
        } else {
            buf[len++] = (char)c;
        }
    }
    if (s->cursor < s->end && *s->cursor == '"') consume(s);
    buf[len] = '\0';
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
    char *buf = malloc(n + 1);
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
