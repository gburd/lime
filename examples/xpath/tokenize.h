/*
 * tokenize.h
 *    XPath 1.0 tokenizer interface.
 *
 * The tokenizer converts an XPath expression string into a stream of
 * tokens suitable for feeding to the Lime-generated parser.
 */

#ifndef XPATH_TOKENIZE_H
#define XPATH_TOKENIZE_H

#include "xpath_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque scanner state */
typedef struct XPathScanner XPathScanner;

/* Create a scanner for the given input string (not null-terminated ok). */
XPathScanner *xpath_scanner_create(const char *input, int len);

/* Destroy a scanner and free resources. */
void xpath_scanner_destroy(XPathScanner *scanner);

/*
 * Scan the next token.
 * Returns the token code (> 0), 0 for EOF, or -1 for error.
 * On success, fills in *val with the token's semantic value.
 */
int xpath_scan(XPathScanner *scanner, XPathToken *val);

/* Return the last error message, or NULL if no error. */
const char *xpath_scanner_error(XPathScanner *scanner);

#ifdef __cplusplus
}
#endif

#endif /* XPATH_TOKENIZE_H */
