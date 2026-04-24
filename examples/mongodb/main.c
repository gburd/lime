/*
 * main.c
 *    Standalone MongoDB query parser driver.
 *
 * Reads MongoDB query expressions from stdin (one per line) or from
 * arguments, parses them using the Lime-generated parser, and prints
 * the resulting AST.
 *
 * Usage:
 *   ./mongodb_parser                                # interactive
 *   ./mongodb_parser '{ "age": { "$gt": 25 } }'    # parse one query
 *   echo '{ "x": 1 }' | ./mongodb_parser           # pipe mode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mongodb_internal.h"
#include "mongodb.h"
#include "tokenize.h"

/* Forward declarations from Lime-generated parser */
void *mongodbParseAlloc(void *(*mallocProc)(size_t));
void mongodbParseFree(void *p, void (*freeProc)(void *));
void mongodbParse(void *p, int yymajor, MdbToken yyminor,
                  MdbParseState *pstate);

/* ======================================================================
 * AST node type names
 * ====================================================================== */

static const char *node_type_name(MdbNodeType type)
{
    switch (type) {
        case MDB_NULL:          return "null";
        case MDB_BOOL:          return "bool";
        case MDB_INT:           return "int";
        case MDB_DOUBLE:        return "double";
        case MDB_STRING:        return "string";
        case MDB_REGEX:         return "regex";
        case MDB_OBJECT_ID:     return "ObjectId";
        case MDB_DOCUMENT:      return "document";
        case MDB_ARRAY:         return "array";
        case MDB_PAIR:          return "pair";
        case MDB_FIELD_PATH:    return "field_path";
        case MDB_OP_EQ:         return "$eq";
        case MDB_OP_NE:         return "$ne";
        case MDB_OP_GT:         return "$gt";
        case MDB_OP_GTE:        return "$gte";
        case MDB_OP_LT:         return "$lt";
        case MDB_OP_LTE:        return "$lte";
        case MDB_OP_IN:         return "$in";
        case MDB_OP_NIN:        return "$nin";
        case MDB_OP_AND:        return "$and";
        case MDB_OP_OR:         return "$or";
        case MDB_OP_NOT:        return "$not";
        case MDB_OP_NOR:        return "$nor";
        case MDB_OP_EXISTS:     return "$exists";
        case MDB_OP_TYPE:       return "$type";
        case MDB_OP_REGEX:      return "$regex";
        case MDB_OP_MOD:        return "$mod";
        case MDB_OP_ALL:        return "$all";
        case MDB_OP_ELEMMATCH:  return "$elemMatch";
        case MDB_OP_SIZE:       return "$size";
        case MDB_UP_SET:        return "$set";
        case MDB_UP_UNSET:      return "$unset";
        case MDB_UP_INC:        return "$inc";
        case MDB_UP_MUL:        return "$mul";
        case MDB_UP_RENAME:     return "$rename";
        case MDB_UP_MIN:        return "$min";
        case MDB_UP_MAX:        return "$max";
        case MDB_UP_CURRENTDATE:return "$currentDate";
        case MDB_UP_SETONDISERT:return "$setOnInsert";
        case MDB_UP_PUSH:       return "$push";
        case MDB_UP_PULL:       return "$pull";
        case MDB_UP_ADDTOSET:   return "$addToSet";
        case MDB_UP_POP:        return "$pop";
        case MDB_AGG_MATCH:     return "$match";
        case MDB_AGG_GROUP:     return "$group";
        case MDB_AGG_PROJECT:   return "$project";
        case MDB_AGG_SORT:      return "$sort";
        case MDB_AGG_LIMIT:     return "$limit";
        case MDB_AGG_SKIP:      return "$skip";
        case MDB_AGG_UNWIND:    return "$unwind";
        case MDB_AGG_LOOKUP:    return "$lookup";
        case MDB_AGG_OUT:       return "$out";
        case MDB_AGG_COUNT:     return "$count";
        case MDB_AGG_SUM:       return "$sum";
        case MDB_AGG_AVG:       return "$avg";
        case MDB_AGG_FIRST:     return "$first";
        case MDB_AGG_LAST:      return "$last";
        case MDB_FIND:          return "find";
        case MDB_UPDATE:        return "update";
        case MDB_AGGREGATE:     return "aggregate";
        case MDB_PIPELINE:      return "pipeline";
    }
    return "???";
}

/* ======================================================================
 * AST printing
 * ====================================================================== */

static void indent(int depth)
{
    for (int i = 0; i < depth; i++)
        printf("  ");
}

static void print_node(MdbNode *node, int depth)
{
    if (!node) return;

    indent(depth);
    printf("%s", node_type_name(node->type));

    switch (node->type) {
        case MDB_NULL:
            printf("\n");
            break;

        case MDB_BOOL:
            printf(": %s\n", node->value.boolean ? "true" : "false");
            break;

        case MDB_INT:
            printf(": %lld\n", (long long)node->value.integer);
            break;

        case MDB_DOUBLE:
            printf(": %g\n", node->value.floating);
            break;

        case MDB_STRING:
        case MDB_FIELD_PATH:
        case MDB_OBJECT_ID:
            printf(": \"%.*s\"\n", node->value.string.len,
                   node->value.string.val);
            break;

        case MDB_REGEX:
            printf(": /%.*s/", node->value.regex.pattern.len,
                   node->value.regex.pattern.val);
            if (node->value.regex.flags.val)
                printf("%.*s", node->value.regex.flags.len,
                       node->value.regex.flags.val);
            printf("\n");
            break;

        case MDB_DOCUMENT:
        case MDB_ARRAY:
        case MDB_PIPELINE:
            printf("\n");
            if (node->value.elements) {
                MdbNodeCell *cell = node->value.elements->head;
                while (cell) {
                    print_node(cell->data, depth + 1);
                    cell = cell->next;
                }
            }
            break;

        case MDB_PAIR:
            printf(": \"%.*s\"\n", node->value.pair.key.len,
                   node->value.pair.key.val);
            print_node(node->value.pair.value, depth + 1);
            break;

        /* Query/update operators */
        case MDB_OP_EQ: case MDB_OP_NE: case MDB_OP_GT: case MDB_OP_GTE:
        case MDB_OP_LT: case MDB_OP_LTE: case MDB_OP_IN: case MDB_OP_NIN:
        case MDB_OP_AND: case MDB_OP_OR: case MDB_OP_NOT: case MDB_OP_NOR:
        case MDB_OP_EXISTS: case MDB_OP_TYPE: case MDB_OP_REGEX:
        case MDB_OP_MOD: case MDB_OP_ALL: case MDB_OP_ELEMMATCH:
        case MDB_OP_SIZE:
            if (node->value.op.field.val)
                printf(" [field: \"%.*s\"]", node->value.op.field.len,
                       node->value.op.field.val);
            printf("\n");
            print_node(node->value.op.operand, depth + 1);
            break;

        /* Update operators */
        case MDB_UP_SET: case MDB_UP_UNSET: case MDB_UP_INC:
        case MDB_UP_MUL: case MDB_UP_RENAME: case MDB_UP_MIN:
        case MDB_UP_MAX: case MDB_UP_CURRENTDATE: case MDB_UP_SETONDISERT:
        case MDB_UP_PUSH: case MDB_UP_PULL: case MDB_UP_ADDTOSET:
        case MDB_UP_POP:
            printf("\n");
            print_node(node->value.op.operand, depth + 1);
            break;

        /* Aggregation stages */
        case MDB_AGG_MATCH: case MDB_AGG_GROUP: case MDB_AGG_PROJECT:
        case MDB_AGG_SORT: case MDB_AGG_LIMIT: case MDB_AGG_SKIP:
        case MDB_AGG_UNWIND: case MDB_AGG_LOOKUP: case MDB_AGG_OUT:
        case MDB_AGG_COUNT:
            printf("\n");
            print_node(node->value.stage.spec, depth + 1);
            break;

        /* Aggregation accumulators */
        case MDB_AGG_SUM: case MDB_AGG_AVG:
        case MDB_AGG_FIRST: case MDB_AGG_LAST:
            printf("\n");
            print_node(node->value.op.operand, depth + 1);
            break;

        case MDB_FIND:
            printf("\n");
            indent(depth + 1); printf("query:\n");
            print_node(node->value.find.query, depth + 2);
            if (node->value.find.projection) {
                indent(depth + 1); printf("projection:\n");
                print_node(node->value.find.projection, depth + 2);
            }
            break;

        case MDB_UPDATE:
            printf("\n");
            indent(depth + 1); printf("query:\n");
            print_node(node->value.update.query, depth + 2);
            indent(depth + 1); printf("update:\n");
            print_node(node->value.update.update, depth + 2);
            break;

        case MDB_AGGREGATE:
            printf("\n");
            indent(depth + 1); printf("pipeline:\n");
            print_node(node->value.aggregate.pipeline, depth + 2);
            break;
    }
}

/* ======================================================================
 * Parse a single MongoDB expression
 * ====================================================================== */

static int parse_mongodb(const char *input, int len, int verbose)
{
    MdbParseState pstate;
    memset(&pstate, 0, sizeof(pstate));

    void *parser = mongodbParseAlloc(malloc);
    if (!parser) {
        fprintf(stderr, "ERROR: failed to allocate parser\n");
        return 1;
    }

    MdbScanner *scanner = mdb_scanner_create(input, len);
    if (!scanner) {
        fprintf(stderr, "ERROR: failed to allocate scanner\n");
        mongodbParseFree(parser, free);
        return 1;
    }

    MdbToken val;
    int tok;
    while ((tok = mdb_scan(scanner, &val)) > 0) {
        mongodbParse(parser, tok, val, &pstate);
        if (pstate.error) break;
    }

    if (tok < 0) {
        fprintf(stderr, "ERROR: lexical error: %s\n",
                mdb_scanner_error(scanner) ? mdb_scanner_error(scanner)
                                           : "unknown");
        mdb_scanner_destroy(scanner);
        mongodbParseFree(parser, free);
        return 1;
    }

    /* Send EOF */
    if (!pstate.error) {
        memset(&val, 0, sizeof(val));
        mongodbParse(parser, 0, val, &pstate);
    }

    mdb_scanner_destroy(scanner);
    mongodbParseFree(parser, free);

    if (pstate.error) {
        fprintf(stderr, "ERROR: %s\n",
                pstate.errMsg ? pstate.errMsg : "parse error");
        return 1;
    }

    if (pstate.result) {
        if (verbose) {
            printf("AST:\n");
            print_node(pstate.result, 1);
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

    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "--") == 0) {
            argi++;
            break;
        } else if (strcmp(argv[argi], "-v") == 0 ||
                   strcmp(argv[argi], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[argi], "-h") == 0 ||
                   strcmp(argv[argi], "--help") == 0) {
            printf("Usage: %s [-v] [--] [EXPR ...]\n", argv[0]);
            printf("  -v, --verbose   Print AST details\n");
            printf("  -h, --help      Show this help\n");
            printf("  --              End of options\n");
            printf("\nWithout arguments, reads MongoDB queries from stdin.\n");
            printf("\nExamples:\n");
            printf("  %s '{ \"age\": { \"$gt\": 25 } }'\n", argv[0]);
            printf("  %s '{ \"$set\": { \"status\": \"active\" } }'\n", argv[0]);
            printf("  %s '[ { \"$match\": { \"x\": 1 } } ]'\n", argv[0]);
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
            if (parse_mongodb(argv[i], (int)strlen(argv[i]), verbose) != 0)
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
            if (parse_mongodb(line, len, verbose) != 0)
                errors++;
        }
    }

    if (processed > 0 && !verbose) {
        printf("Parsed %d expression(s), %d error(s)\n", processed, errors);
    }

    return errors > 0 ? 1 : 0;
}
