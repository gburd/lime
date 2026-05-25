/*
** main.c -- example binary for the SQL+JSON multi-grammar example.
**
** Reads a single SQL statement from argv[1] (or a sensible default
** if no argument is given), runs the multi-grammar parser, and
** prints the resulting AST.  See multi_driver.c for the actual
** boundary-detection logic.
*/
#include "multi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *input;
    if (argc > 1) {
        input = argv[1];
    } else {
        input = "SELECT id, json '{\"a\":1, \"b\":[2,3]}' FROM t WHERE id = 5;";
    }

    fprintf(stderr, "input: %s\n\n", input);

    SqlSelect *ast = NULL;
    MultiParseStatus status = multi_parse_sql(input, /*register_json_trigger=*/true, &ast);
    if (status != MULTI_OK) {
        fprintf(stderr, "parse failed: status=%d\n", (int)status);
        return 1;
    }

    sql_select_print(stdout, ast);
    sql_select_destroy(ast);
    return 0;
}
