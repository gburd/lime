/*
 * datalog_tokenize.h
 *    Public interface for the Datalog/EDN tokenizer.
 */

#ifndef DATALOG_TOKENIZE_H
#define DATALOG_TOKENIZE_H

#include "datalog_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DlTokenizer DlTokenizer;

DlTokenizer *dl_tokenizer_create(const char *input, int len);
void         dl_tokenizer_destroy(DlTokenizer *t);

/*
 * Get the next token.
 *
 * Returns the token code (>0 for valid tokens, 0 for EOF, -1 for error).
 * Fills in *value with the token's semantic value.
 */
int dl_tokenizer_next(DlTokenizer *t, DatalogToken *value);

#ifdef __cplusplus
}
#endif

#endif /* DATALOG_TOKENIZE_H */
