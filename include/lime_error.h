/*
** Error reporting utilities for Lime-generated parsers.
**
** Provides expected-token reporting and structured error accumulation
** for improved parser diagnostics.
*/
#ifndef LIME_ERROR_H
#define LIME_ERROR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef LIME_LOCATION_H
typedef struct LimeLocation LimeLocation;
#endif

/*
** Accumulated parse error with location and expected-token information.
*/
typedef struct LimeError {
    uint32_t line;
    uint32_t column;
    const char *filename;
    char *message;           /* Human-readable error message */
    char *expected;          /* Comma-separated list of expected tokens */
    struct LimeError *next;  /* Linked list of errors */
} LimeError;

/*
** Free a linked list of LimeError nodes.
** Passing NULL is safe.
*/
void lime_error_free(LimeError *err);

#ifdef __cplusplus
}
#endif

#endif /* LIME_ERROR_H */
