/*
 * main.c
 *    Standalone XQuery 1.0 parser driver.
 *
 * Reads XQuery expressions from stdin or arguments, parses them using
 * the Lime-generated parser, and prints the resulting AST.
 *
 * Usage:
 *   ./xquery_parser 'for $x in //book return $x/title'
 *   echo '1 + 2' | ./xquery_parser
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xquery_internal.h"
#include "xquery.h"
#include "xquery_helpers.h"
#include "tokenize.h"

/* Forward declarations from Lime-generated parser */
void *xqueryParseAlloc(void *(*mallocProc)(size_t));
void xqueryParseFree(void *p, void (*freeProc)(void *));
void xqueryParse(void *p, int yymajor, XQToken yyminor,
                 XQParseState *pstate);

static int parse_xquery(const char *input, int len, int verbose)
{
    XQParseState pstate;
    memset(&pstate, 0, sizeof(pstate));

    void *parser = xqueryParseAlloc(malloc);
    if (!parser) {
        fprintf(stderr, "ERROR: failed to allocate parser\n");
        return 1;
    }

    XQScanner *scanner = xq_scanner_create(input, len);
    if (!scanner) {
        fprintf(stderr, "ERROR: failed to allocate scanner\n");
        xqueryParseFree(parser, free);
        return 1;
    }

    XQToken val;
    int tok;
    while ((tok = xq_scan(scanner, &val)) > 0) {
        xqueryParse(parser, tok, val, &pstate);
        if (pstate.error) break;
    }

    if (tok < 0) {
        fprintf(stderr, "ERROR: lexical error: %s\n",
                xq_scanner_error(scanner) ? xq_scanner_error(scanner) : "unknown");
        xq_scanner_destroy(scanner);
        xqueryParseFree(parser, free);
        return 1;
    }

    if (!pstate.error) {
        memset(&val, 0, sizeof(val));
        xqueryParse(parser, 0, val, &pstate);
    }

    xq_scanner_destroy(scanner);
    xqueryParseFree(parser, free);

    if (pstate.error) {
        fprintf(stderr, "ERROR: %s\n",
                pstate.errMsg ? pstate.errMsg : "parse error");
        return 1;
    }

    if (pstate.result) {
        if (verbose) {
            printf("AST:\n");
            xq_print_ast(pstate.result, 1);
        } else {
            printf("OK\n");
        }
    } else {
        if (verbose)
            printf("(empty input)\n");
        else
            printf("OK\n");
    }

    return 0;
}

int main(int argc, char **argv)
{
    int verbose = 0;
    int errors = 0;
    int processed = 0;

    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "--") == 0) { argi++; break; }
        else if (strcmp(argv[argi], "-v") == 0 ||
                 strcmp(argv[argi], "--verbose") == 0) { verbose = 1; }
        else if (strcmp(argv[argi], "-h") == 0 ||
                 strcmp(argv[argi], "--help") == 0) {
            printf("Usage: %s [-v] [--] [EXPR ...]\n", argv[0]);
            printf("  -v, --verbose   Print AST details\n");
            printf("  -h, --help      Show this help\n");
            printf("\nWithout arguments, reads XQuery expressions from stdin.\n");
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[argi]);
            return 1;
        }
        argi++;
    }

    if (argi < argc) {
        for (int i = argi; i < argc; i++) {
            if (verbose)
                printf("--- %s ---\n", argv[i]);
            processed++;
            if (parse_xquery(argv[i], (int)strlen(argv[i]), verbose) != 0)
                errors++;
        }
    } else {
        char line[8192];
        while (fgets(line, sizeof(line), stdin)) {
            int len = (int)strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
            if (len == 0) continue;

            if (verbose)
                printf("--- %s ---\n", line);
            processed++;
            if (parse_xquery(line, len, verbose) != 0)
                errors++;
        }
    }

    if (processed > 0 && !verbose)
        printf("Parsed %d expression(s), %d error(s)\n", processed, errors);

    return errors > 0 ? 1 : 0;
}
