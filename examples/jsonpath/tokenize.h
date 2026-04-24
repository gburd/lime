/*
 * tokenize.h
 *    JSONPath tokenizer -- public interface
 *
 * Hand-written tokenizer that replaces the Flex-generated scanner from
 * PostgreSQL's jsonpath_scan.l.  Produces token codes defined in the
 * Lime-generated jsonpath_gram.h.
 *
 * Usage:
 *   JpScanner *s = jp_scanner_create(input, strlen(input));
 *   JsonPathToken val;
 *   int tok;
 *   while ((tok = jp_scan(s, &val)) > 0) {
 *       jsonpathParse(parser, tok, val, pstate);
 *   }
 *   jsonpathParse(parser, 0, empty_val, pstate);  // EOF
 *   jp_scanner_destroy(s);
 */

#ifndef JP_TOKENIZE_H
#define JP_TOKENIZE_H

#include "jsonpath_internal.h"
#include "jsonpath_gram.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque scanner handle */
typedef struct JpScanner JpScanner;

/* Create a scanner for the given JSONPath input string.
 * The input must remain valid for the lifetime of the scanner.
 * Returns NULL on allocation failure. */
JpScanner *jp_scanner_create(const char *input, int length);

/* Destroy a scanner and free resources.
 * Passing NULL is safe (no-op). */
void jp_scanner_destroy(JpScanner *s);

/* Extract the next token from the input.
 *
 * Returns:
 *   > 0  Token code from jsonpath_gram.h
 *   0    End of input
 *   -1   Lexical error (call jp_scanner_error() for message)
 *
 * On success, *val is filled with the semantic value. */
int jp_scan(JpScanner *s, JsonPathToken *val);

/* Return a human-readable error message after jp_scan returns -1.
 * Returns NULL if no error. */
const char *jp_scanner_error(JpScanner *s);

#ifdef __cplusplus
}
#endif

#endif /* JP_TOKENIZE_H */
