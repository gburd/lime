/*
** Stub implementations for library functions not yet fully implemented.
**
** These allow the parse-manager CLI tool to link and test its command
** infrastructure. When the full library is ready, remove this file
** from the build (set USE_STUBS=0 in the Makefile).
**
** Functions already provided by snapshot.c and version.c are NOT
** stubbed here to avoid multiple-definition errors.
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Forward declarations matching include/parser.h and src/snapshot.h */
typedef struct ParserSnapshot ParserSnapshot;
typedef struct ParseContext ParseContext;

/* --- Parse Context API stubs (src/parse_context.c not yet linkable) --- */

ParseContext *parse_begin(ParserSnapshot *snap) {
    (void)snap;
    fprintf(stderr, "stub: parse_begin not yet implemented\n");
    return NULL;
}

int parse_token(ParseContext *ctx, int token_code, void *token_value) {
    (void)ctx; (void)token_code; (void)token_value;
    return -1;
}

void parse_end(ParseContext *ctx) {
    (void)ctx;
}

ParseContext *parse_get_snapshot(ParseContext *ctx) {
    (void)ctx;
    return NULL;
}

/* --- JIT stubs --- */

bool lime_jit_available(void) {
    return false;
}

int lime_jit_compile(ParserSnapshot *snap) {
    (void)snap;
    return -1;
}

void jit_detach_from_snapshot(ParserSnapshot *snap) {
    (void)snap;
}

/* --- Extension registry stubs --- */

bool lemon_extension_registry_init(void) {
    return true;
}

void lemon_extension_registry_destroy(void) {
}
