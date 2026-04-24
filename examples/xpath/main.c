/*
 * main.c
 *    Standalone XPath 1.0 parser driver.
 *
 * Reads XPath expressions from stdin (one per line) or from arguments,
 * parses them using the Lime-generated parser, and prints the resulting AST.
 *
 * Usage:
 *   ./xpath_parser                          # interactive, reads from stdin
 *   ./xpath_parser "//book[@lang='en']"     # parse a single expression
 *   echo '/root/child' | ./xpath_parser     # pipe mode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xpath_internal.h"
#include "xpath.h"
#include "xpath_helpers.h"
#include "tokenize.h"

/* Forward declarations from Lime-generated parser */
void *xpathParseAlloc(void *(*mallocProc)(size_t));
void xpathParseFree(void *p, void (*freeProc)(void *));
void xpathParse(void *p, int yymajor, XPathToken yyminor,
                XPathParseState *pstate);

/* ======================================================================
 * Parse a single XPath expression
 * ====================================================================== */

static int parse_xpath(const char *input, int len, int verbose)
{
    XPathParseState pstate;
    memset(&pstate, 0, sizeof(pstate));

    void *parser = xpathParseAlloc(malloc);
    if (!parser) {
        fprintf(stderr, "ERROR: failed to allocate parser\n");
        return 1;
    }

    XPathScanner *scanner = xpath_scanner_create(input, len);
    if (!scanner) {
        fprintf(stderr, "ERROR: failed to allocate scanner\n");
        xpathParseFree(parser, free);
        return 1;
    }

    XPathToken val;
    int tok;
    while ((tok = xpath_scan(scanner, &val)) > 0) {
        xpathParse(parser, tok, val, &pstate);
        if (pstate.error) break;
    }

    if (tok < 0) {
        fprintf(stderr, "ERROR: lexical error: %s\n",
                xpath_scanner_error(scanner) ? xpath_scanner_error(scanner) : "unknown");
        xpath_scanner_destroy(scanner);
        xpathParseFree(parser, free);
        return 1;
    }

    /* Send EOF */
    if (!pstate.error) {
        memset(&val, 0, sizeof(val));
        xpathParse(parser, 0, val, &pstate);
    }

    xpath_scanner_destroy(scanner);
    xpathParseFree(parser, free);

    if (pstate.error) {
        fprintf(stderr, "ERROR: %s\n",
                pstate.errMsg ? pstate.errMsg : "parse error");
        return 1;
    }

    if (pstate.result) {
        if (verbose) {
            printf("AST:\n");
            xpath_print_ast(pstate.result, 1);
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

/* ======================================================================
 * Main entry point
 * ====================================================================== */

int main(int argc, char **argv)
{
    int verbose = 0;
    int errors = 0;
    int processed = 0;

    /* Check for flags */
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' &&
           argv[argi][1] != '\0' &&
           (argv[argi][1] == '-' || (argv[argi][1] >= 'a' && argv[argi][1] <= 'z') ||
            (argv[argi][1] >= 'A' && argv[argi][1] <= 'Z'))) {
        if (strcmp(argv[argi], "--") == 0) {
            argi++;
            break;
        } else if (strcmp(argv[argi], "-v") == 0 || strcmp(argv[argi], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[argi], "-h") == 0 || strcmp(argv[argi], "--help") == 0) {
            printf("Usage: %s [-v] [--] [EXPR ...]\n", argv[0]);
            printf("  -v, --verbose   Print AST details\n");
            printf("  -h, --help      Show this help\n");
            printf("  --              End of options\n");
            printf("\nWithout arguments, reads XPath expressions from stdin.\n");
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[argi]);
            return 1;
        }
        argi++;
    }

    /* Parse expressions from command line */
    if (argi < argc) {
        for (int i = argi; i < argc; i++) {
            if (verbose)
                printf("--- %s ---\n", argv[i]);
            processed++;
            if (parse_xpath(argv[i], (int)strlen(argv[i]), verbose) != 0)
                errors++;
        }
    } else {
        /* Read from stdin */
        char line[4096];
        while (fgets(line, sizeof(line), stdin)) {
            /* Strip trailing newline */
            int len = (int)strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
            if (len == 0) continue;  /* skip empty lines */

            if (verbose)
                printf("--- %s ---\n", line);
            processed++;
            if (parse_xpath(line, len, verbose) != 0)
                errors++;
        }
    }

    if (processed > 0 && !verbose) {
        printf("Parsed %d expression(s), %d error(s)\n", processed, errors);
    }

    return errors > 0 ? 1 : 0;
}
