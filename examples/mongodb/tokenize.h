/*
 * tokenize.h
 *    Tokenizer interface for the MongoDB query parser.
 */

#ifndef MONGODB_TOKENIZE_H
#define MONGODB_TOKENIZE_H

#include "mongodb_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque scanner handle */
typedef struct MdbScanner MdbScanner;

/* Create a scanner for the given input string */
MdbScanner *mdb_scanner_create(const char *input, int len);

/* Destroy a scanner */
void mdb_scanner_destroy(MdbScanner *scanner);

/* Get the next token. Returns token ID (>0), 0 for EOF, -1 for error. */
int mdb_scan(MdbScanner *scanner, MdbToken *val);

/* Get the last error message (NULL if none) */
const char *mdb_scanner_error(MdbScanner *scanner);

#ifdef __cplusplus
}
#endif

#endif /* MONGODB_TOKENIZE_H */
