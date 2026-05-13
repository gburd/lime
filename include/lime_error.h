/*
** Error reporting utilities for Lime-generated parsers.
**
** Provides expected-token reporting and structured error accumulation
** for improved parser diagnostics.
*/
#ifndef LIME_ERROR_H
#define LIME_ERROR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef LIME_LOCATION_H
typedef struct LimeLocation LimeLocation;
#endif

/**
 * @brief Accumulated parse error with location and expected-token info.
 *
 * The fields are owned by the LimeError node; lime_error_free() calls
 * free() on @ref message and @ref expected.  @ref filename is borrowed
 * (not duplicated) -- the caller must ensure it outlives the error
 * chain or pass NULL.
 */
typedef struct LimeError {
    uint32_t line;            /**< 1-based source line */
    uint32_t column;          /**< 1-based source column */
    const char *filename;     /**< Borrowed filename, or NULL */
    char *message;            /**< Human-readable error message (owned) */
    char *expected;           /**< Comma-separated expected-token list (owned) */
    struct LimeError *next;   /**< Next entry in linked list of errors */
} LimeError;

/*
** Append a new error to the end of `list` and return the (possibly new)
** list head.
**
**   list     -- existing list head, or NULL to start a new list
**   message  -- error text (duplicated; may be NULL)
**   expected -- expected-token string (duplicated; may be NULL)
**   line, column -- 1-based source location
**   filename -- optional filename (borrowed, not duplicated)
**
** Returns the list head, or NULL on allocation failure (in which case
** `list` is left unchanged and the caller still owns it).
*/
LimeError *lime_error_append(LimeError *list,
                             const char *message,
                             const char *expected,
                             uint32_t line,
                             uint32_t column,
                             const char *filename);

/*
** Count the number of errors in `list`.  Safe on NULL.
*/
size_t lime_error_count(const LimeError *list);

/*
** Free a linked list of LimeError nodes.
** Passing NULL is safe.
*/
void lime_error_free(LimeError *err);

#ifdef __cplusplus
}
#endif

#endif /* LIME_ERROR_H */
