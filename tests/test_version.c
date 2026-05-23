/*
** Basic test to verify the build system works.
*/
#include <stdio.h>
#include <string.h>
#include "parser.h"

int main(void) {
    const char *ver = lime_parser_version();
    if (ver == NULL) {
        fprintf(stderr, "FAIL: lime_parser_version() returned NULL\n");
        return 1;
    }
    if (strcmp(ver, "0.1.0") != 0) {
        fprintf(stderr, "FAIL: expected version '0.1.0', got '%s'\n", ver);
        return 1;
    }
    printf("PASS: version = %s\n", ver);
    return 0;
}
