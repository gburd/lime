/*
 * main.c
 *    Standalone JSONPath parser driver.
 *
 * Reads JSONPath expressions from stdin (one per line) or from files
 * given as arguments, parses them using the Lime-generated parser,
 * and prints the resulting AST.
 *
 * Usage:
 *   ./jsonpath_parser                  # interactive, reads from stdin
 *   ./jsonpath_parser "$.store.book[*]"  # parse a single expression
 *   echo '$.x' | ./jsonpath_parser     # pipe mode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jsonpath_internal.h"
#include "jsonpath_gram.h"
#include "tokenize.h"

/* Forward declarations from Lime-generated parser */
void *jsonpathParseAlloc(void *(*mallocProc)(size_t));
void jsonpathParseFree(void *p, void (*freeProc)(void *));
void jsonpathParse(void *p, int yymajor, JsonPathToken yyminor,
                   JsonPathParseState *pstate);

/* ======================================================================
 * AST printing
 * ====================================================================== */

static const char *item_type_name(JsonPathItemType type)
{
    switch (type) {
        case jpiNull:           return "null";
        case jpiBool:           return "bool";
        case jpiNumeric:        return "numeric";
        case jpiString:         return "string";
        case jpiVariable:       return "variable";
        case jpiKey:            return "key";
        case jpiRoot:           return "$";
        case jpiCurrent:        return "@";
        case jpiLast:           return "last";
        case jpiAnyArray:       return "[*]";
        case jpiAnyKey:         return ".*";
        case jpiIndexArray:     return "[]";
        case jpiAny:            return "**";
        case jpiFilter:         return "?()";
        case jpiExists:         return "exists()";
        case jpiNot:            return "!";
        case jpiIsUnknown:      return "is unknown";
        case jpiPlus:           return "+";
        case jpiMinus:          return "-";
        case jpiAdd:            return "+";
        case jpiSub:            return "-";
        case jpiMul:            return "*";
        case jpiDiv:            return "/";
        case jpiMod:            return "%";
        case jpiEqual:          return "==";
        case jpiNotEqual:       return "!=";
        case jpiLess:           return "<";
        case jpiGreater:        return ">";
        case jpiLessOrEqual:    return "<=";
        case jpiGreaterOrEqual: return ">=";
        case jpiAnd:            return "&&";
        case jpiOr:             return "||";
        case jpiStartsWith:     return "starts with";
        case jpiLikeRegex:      return "like_regex";
        case jpiSubscript:      return "subscript";
        case jpiDatetime:       return ".datetime()";
        case jpiTime:           return ".time()";
        case jpiTimeTz:         return ".time_tz()";
        case jpiTimestamp:      return ".timestamp()";
        case jpiTimestampTz:    return ".timestamp_tz()";
        case jpiAbs:            return ".abs()";
        case jpiSize:           return ".size()";
        case jpiType:           return ".type()";
        case jpiFloor:          return ".floor()";
        case jpiDouble:         return ".double()";
        case jpiCeiling:        return ".ceiling()";
        case jpiKeyValue:       return ".keyvalue()";
        case jpiBigint:         return ".bigint()";
        case jpiBoolean:        return ".boolean()";
        case jpiDate:           return ".date()";
        case jpiDecimal:        return ".decimal()";
        case jpiInteger:        return ".integer()";
        case jpiNumber:         return ".number()";
        case jpiStringFunc:     return ".string()";
    }
    return "???";
}

static void print_item(JsonPathParseItem *item, int depth)
{
    if (!item) return;

    for (int i = 0; i < depth; i++)
        printf("  ");

    printf("%s", item_type_name(item->type));

    switch (item->type) {
        case jpiString:
        case jpiKey:
            printf(": \"%.*s\"", item->value.string.len, item->value.string.val);
            break;
        case jpiVariable:
            printf(": $%.*s", item->value.string.len, item->value.string.val);
            break;
        case jpiBool:
            printf(": %s", item->value.boolean ? "true" : "false");
            break;
        case jpiNumeric:
            printf(": %s", item->value.numeric);
            break;
        case jpiAny:
            if (item->value.anybounds.first == UINT32_MAX)
                printf(": {any}");
            else if (item->value.anybounds.first == item->value.anybounds.last)
                printf(": {%u}", item->value.anybounds.first);
            else
                printf(": {%u to %u}", item->value.anybounds.first,
                       item->value.anybounds.last);
            break;
        case jpiLikeRegex:
            printf(": \"%.*s\"", item->value.like_regex.patternlen,
                   item->value.like_regex.pattern);
            if (item->value.like_regex.flags)
                printf(" flags=0x%x", item->value.like_regex.flags);
            break;
        default:
            break;
    }
    printf("\n");

    /* Print children */
    switch (item->type) {
        case jpiAnd:
        case jpiOr:
        case jpiAdd:
        case jpiSub:
        case jpiMul:
        case jpiDiv:
        case jpiMod:
        case jpiEqual:
        case jpiNotEqual:
        case jpiLess:
        case jpiGreater:
        case jpiLessOrEqual:
        case jpiGreaterOrEqual:
        case jpiStartsWith:
        case jpiDecimal:
            print_item(item->value.args.left, depth + 1);
            print_item(item->value.args.right, depth + 1);
            break;
        case jpiNot:
        case jpiIsUnknown:
        case jpiExists:
        case jpiFilter:
        case jpiPlus:
        case jpiMinus:
        case jpiDatetime:
        case jpiTime:
        case jpiTimeTz:
        case jpiTimestamp:
        case jpiTimestampTz:
            print_item(item->value.arg, depth + 1);
            break;
        case jpiLikeRegex:
            print_item(item->value.like_regex.expr, depth + 1);
            break;
        case jpiIndexArray:
            for (int i = 0; i < item->value.array.nelems; i++) {
                for (int j = 0; j < depth + 1; j++) printf("  ");
                printf("[%d]:\n", i);
                print_item(item->value.array.elems[i].from, depth + 2);
                if (item->value.array.elems[i].to)
                    print_item(item->value.array.elems[i].to, depth + 2);
            }
            break;
        default:
            break;
    }

    /* Print chained next */
    if (item->next)
        print_item(item->next, depth);
}

/* ======================================================================
 * Parse a single JSONPath expression
 * ====================================================================== */

static int parse_jsonpath(const char *input, int len, int verbose)
{
    JsonPathParseState pstate;
    memset(&pstate, 0, sizeof(pstate));

    void *parser = jsonpathParseAlloc(malloc);
    if (!parser) {
        fprintf(stderr, "ERROR: failed to allocate parser\n");
        return 1;
    }

    JpScanner *scanner = jp_scanner_create(input, len);
    if (!scanner) {
        fprintf(stderr, "ERROR: failed to allocate scanner\n");
        jsonpathParseFree(parser, free);
        return 1;
    }

    JsonPathToken val;
    int tok;
    while ((tok = jp_scan(scanner, &val)) > 0) {
        jsonpathParse(parser, tok, val, &pstate);
        if (pstate.error) break;
    }

    if (tok < 0) {
        fprintf(stderr, "ERROR: lexical error: %s\n",
                jp_scanner_error(scanner) ? jp_scanner_error(scanner) : "unknown");
        jp_scanner_destroy(scanner);
        jsonpathParseFree(parser, free);
        return 1;
    }

    /* Send EOF */
    if (!pstate.error) {
        memset(&val, 0, sizeof(val));
        jsonpathParse(parser, 0, val, &pstate);
    }

    jp_scanner_destroy(scanner);
    jsonpathParseFree(parser, free);

    if (pstate.error) {
        fprintf(stderr, "ERROR: %s\n",
                pstate.errMsg ? pstate.errMsg : "parse error");
        return 1;
    }

    if (pstate.result) {
        if (verbose) {
            printf("Mode: %s\n", pstate.result->lax ? "lax" : "strict");
            printf("AST:\n");
            print_item(pstate.result->expr, 1);
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
            printf("\nWithout arguments, reads JSONPath expressions from stdin.\n");
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
            if (parse_jsonpath(argv[i], (int)strlen(argv[i]), verbose) != 0)
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
            if (parse_jsonpath(line, len, verbose) != 0)
                errors++;
        }
    }

    if (processed > 0 && !verbose) {
        printf("Parsed %d expression(s), %d error(s)\n", processed, errors);
    }

    return errors > 0 ? 1 : 0;
}
