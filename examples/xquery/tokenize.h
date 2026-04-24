/*
 * tokenize.h
 *    Tokenizer interface for the XQuery 1.0 parser.
 */

#ifndef XQUERY_TOKENIZE_H
#define XQUERY_TOKENIZE_H

#include "xquery_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XQScanner XQScanner;

XQScanner  *xq_scanner_create(const char *input, int len);
void        xq_scanner_destroy(XQScanner *scanner);
int         xq_scan(XQScanner *scanner, XQToken *val);
const char *xq_scanner_error(XQScanner *scanner);

#ifdef __cplusplus
}
#endif

#endif /* XQUERY_TOKENIZE_H */
