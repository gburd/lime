/*
** main.c -- example driver for the Lime JSON parser.  Reads JSON
** from stdin (or argv[1]), parses it into a JsonValue tree, prints
** the round-tripped result, and frees everything.
**
** Usage:
**     ./json_parser           # reads from stdin
**     ./json_parser file.json
**
** Exits 0 on success, non-zero on parse error.
*/
#include "json.h"
#include "json_tokenize.h"
#include "json_grammar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern void *JsonAlloc(void *(*)(size_t));
extern void  JsonFree(void *, void (*)(void *));
extern void  Json(void *, int, void *, JsonValue **);

static char *slurp(FILE *f, size_t *out_len) {
    size_t cap = 4096;
    size_t n = 0;
    char *buf = malloc(cap);
    if (buf == NULL) return NULL;
    while (!feof(f)) {
        if (n + 1024 > cap) { cap *= 2; buf = realloc(buf, cap); }
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        n += r;
        if (r == 0) break;
    }
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

int main(int argc, char **argv) {
    FILE *f = stdin;
    if (argc > 1) {
        f = fopen(argv[1], "r");
        if (f == NULL) {
            fprintf(stderr, "json: cannot open %s\n", argv[1]);
            return 1;
        }
    }

    size_t len = 0;
    char *input = slurp(f, &len);
    if (f != stdin) fclose(f);
    if (input == NULL) {
        fprintf(stderr, "json: failed to read input\n");
        return 1;
    }

    JsonScanner sc;
    json_scanner_init(&sc, input, len);

    void *parser = JsonAlloc(malloc);
    JsonValue *root = NULL;

    int tok;
    void *value;
    while ((tok = json_scan(&sc, &value)) > 0) {
        Json(parser, tok, value, &root);
    }
    Json(parser, 0, NULL, &root);
    JsonFree(parser, free);

    free(input);

    if (tok < 0) {
        fprintf(stderr, "json: lex error\n");
        json_free(root);
        return 1;
    }
    if (root == NULL) {
        fprintf(stderr, "json: empty / no value parsed\n");
        return 1;
    }

    json_print(stdout, root, 0);
    fputc('\n', stdout);
    json_free(root);
    return 0;
}
