/*
** main.c -- COBOL parser driver.
**
** Reads a COBOL source file from argv[1] (or stdin if no argv), runs
** the tokenizer + Lime-generated parser, prints a summary of what
** it saw (programs, paragraphs, statements, data items), and exits
** non-zero on any syntax error.
*/

#include "tokenize.h"
#include "cobol_grammar.h"
#include "cobol_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Generated parser entry points -- prefixed by %name CobolParser. */
void *CobolParserAlloc(void *(*mallocProc)(size_t));
void  CobolParserFree(void *p, void (*freeProc)(void *));
void  CobolParser(void *yyp, int yymajor, int yyminor,
                  struct cobol_parse_state *cps);

static char *slurp(FILE *fp, size_t *out_len) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) return NULL;
    for (;;) {
        if (len + 4096 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (nb == NULL) {
                free(buf);
                return NULL;
            }
            buf = nb;
        }
        size_t n = fread(buf + len, 1, cap - len - 1, fp);
        len += n;
        if (n == 0) break;
    }
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : NULL;
    FILE *fp = path ? fopen(path, "rb") : stdin;
    if (path && fp == NULL) {
        fprintf(stderr, "cobol_parser: cannot open %s\n", path);
        return 1;
    }

    size_t srclen = 0;
    char *src = slurp(fp, &srclen);
    if (path) fclose(fp);
    if (src == NULL) {
        fprintf(stderr, "cobol_parser: out of memory\n");
        return 1;
    }

    CobolTokenizer *tk = cobol_tokenize_init(src, srclen, COBOL_FORMAT_AUTO);
    if (tk == NULL) {
        fprintf(stderr, "cobol_parser: tokenizer init failed\n");
        free(src);
        return 1;
    }

    struct cobol_parse_state cps = {0};
    void *parser = CobolParserAlloc(malloc);
    if (parser == NULL) {
        fprintf(stderr, "cobol_parser: parser alloc failed\n");
        cobol_tokenize_free(tk);
        free(src);
        return 1;
    }

    int ntokens = 0;
    for (;;) {
        CobolToken t = cobol_tokenize_next(tk);
        if (t.code == 0) {
            CobolParser(parser, 0, 0, &cps);
            break;
        }
        CobolParser(parser, t.code, (int)t.ivalue, &cps);
        ntokens++;
    }

    CobolParserFree(parser, free);
    cobol_tokenize_free(tk);

    printf("cobol_parser: %s%s\n",
           path ? path : "<stdin>",
           cps.errors ? "  (with syntax errors)" : "");
    printf("  tokens read    : %d\n", ntokens);
    printf("  programs       : %d\n", cps.programs);
    printf("  paragraphs     : %d\n", cps.paragraphs);
    printf("  statements     : %d\n", cps.statements);
    printf("  data items     : %d\n", cps.data_items);
    printf("  syntax errors  : %d\n", cps.errors);

    free(src);
    return cps.errors > 0 ? 1 : 0;
}
