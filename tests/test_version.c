/*
** Basic test to verify the build system works and the
** lime_parser_version() entry point returns a string that
** matches the project version.
**
** The expected value is computed from the meson-injected
** LIME_VERSION_STRING macro (see meson.build add_project_arguments)
** so a version bump that updates meson.build / lime.c / src/version.c
** does not also need a hardcoded constant in this test.
*/
#include <stdio.h>
#include <string.h>
#include "parser.h"

#ifndef LIME_VERSION_STRING
#  define LIME_VERSION_STRING "0.3.0"
#endif

int main(void) {
    const char *ver = lime_parser_version();
    if (ver == NULL) {
        fprintf(stderr, "FAIL: lime_parser_version() returned NULL\n");
        return 1;
    }
    if (strcmp(ver, LIME_VERSION_STRING) != 0) {
        fprintf(stderr, "FAIL: expected version '%s', got '%s'\n",
                LIME_VERSION_STRING, ver);
        return 1;
    }
    printf("PASS: version = %s\n", ver);
    return 0;
}
