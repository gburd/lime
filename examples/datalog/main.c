/*
 * main.c
 *    Standalone Datalog/EDN parser driver.
 *
 * Usage:
 *   ./datalog_parser [-q] [-v] [file.dl ...]
 *   ./datalog_parser -e "parent(tom, bob)."
 *
 *   -q   Quiet mode (only report success/failure)
 *   -v   Verbose mode (print AST after parsing)
 *   -e   Parse an inline expression instead of a file
 *
 * Exit codes:
 *   0 = success (all inputs parsed without error)
 *   1 = parse error
 *   2 = usage error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "datalog_defs.h"
#include "datalog_helpers.h"
#include "datalog_tokenize.h"
#include "datalog.h"   /* generated: datalogParse, datalogParseAlloc, etc. */

/* Prototypes from the generated parser */
void *datalogParseAlloc(void *(*mallocProc)(size_t));
void  datalogParseFree(void *parser, void (*freeProc)(void *));
void  datalogParse(void *parser, int tokenCode, DatalogToken value,
                    DatalogParseState *pstate);

static char *read_file(const char *path, int *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "error: out of memory reading '%s'\n", path);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    *out_len = (int)n;
    return buf;
}

static int parse_string(const char *input, int len,
                         int quiet, int verbose, const char *source_name)
{
    DatalogParseState pstate;
    memset(&pstate, 0, sizeof(pstate));

    void *parser = datalogParseAlloc(malloc);
    if (!parser) {
        fprintf(stderr, "error: failed to allocate parser\n");
        return 1;
    }

    DlTokenizer *tok = dl_tokenizer_create(input, len);
    DatalogToken value;
    int code;

    while ((code = dl_tokenizer_next(tok, &value)) != 0) {
        if (code < 0) {
            if (!quiet)
                fprintf(stderr, "%s: tokenizer error\n", source_name);
            dl_tokenizer_destroy(tok);
            datalogParseFree(parser, free);
            return 1;
        }
        datalogParse(parser, code, value, &pstate);
        if (pstate.error) break;
    }

    /* Signal end of input */
    if (!pstate.error) {
        memset(&value, 0, sizeof(value));
        datalogParse(parser, 0, value, &pstate);
    }

    dl_tokenizer_destroy(tok);

    int result = 0;
    if (pstate.error) {
        if (!quiet)
            fprintf(stderr, "%s: %s\n", source_name,
                    pstate.errMsg ? pstate.errMsg : "parse error");
        result = 1;
    } else {
        if (!quiet) {
            fprintf(stdout, "%s: OK", source_name);
            if (pstate.result) {
                fprintf(stdout, " (%d facts, %d rules, %d queries)",
                        pstate.result->nfacts,
                        pstate.result->nrules,
                        pstate.result->nqueries);
            }
            fprintf(stdout, "\n");
        }
        if (verbose && pstate.result) {
            dl_print_program(pstate.result, stdout);
        }
    }

    if (pstate.result) {
        dl_free_program(pstate.result);
    }

    datalogParseFree(parser, free);
    return result;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-q] [-v] [-e expr] [file.dl ...]\n", prog);
    fprintf(stderr, "  -q        Quiet mode\n");
    fprintf(stderr, "  -v        Verbose mode (print AST)\n");
    fprintf(stderr, "  -e expr   Parse inline expression\n");
}

int main(int argc, char *argv[])
{
    int quiet = 0;
    int verbose = 0;
    const char *inline_expr = NULL;
    int errors = 0;
    int i;

    /* Parse options */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-e") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            inline_expr = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    /* Inline expression mode */
    if (inline_expr) {
        return parse_string(inline_expr, (int)strlen(inline_expr),
                             quiet, verbose, "<expr>");
    }

    /* File mode */
    if (i >= argc) {
        /* Read from stdin */
        char buf[65536];
        size_t total = 0;
        size_t n;
        while ((n = fread(buf + total, 1, sizeof(buf) - total - 1, stdin)) > 0) {
            total += n;
            if (total >= sizeof(buf) - 1) break;
        }
        buf[total] = '\0';
        return parse_string(buf, (int)total, quiet, verbose, "<stdin>");
    }

    for (; i < argc; i++) {
        int flen;
        char *content = read_file(argv[i], &flen);
        if (!content) {
            errors++;
            continue;
        }
        errors += parse_string(content, flen, quiet, verbose, argv[i]);
        free(content);
    }

    return errors > 0 ? 1 : 0;
}
