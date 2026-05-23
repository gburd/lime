/*
** json_tokenize.h -- minimal JSON tokenizer for the Lime example.
**
** Produces a stream of (token_id, value) pairs where `value` is:
**   - heap-allocated decoded char* for STRING (caller frees via
**     json_free of the surrounding AST)
**   - heap-allocated literal char* for NUMBER (will be parsed via
**     strtod inside the action)
**   - NULL for everything else
**
** Tokenizer is single-pass, no buffering -- caller drives it
** character by character.
*/
#ifndef LIME_EXAMPLES_JSON_TOKENIZE_H
#define LIME_EXAMPLES_JSON_TOKENIZE_H

#include <stddef.h>

typedef struct JsonScanner {
    const char *cursor;
    const char *end;
} JsonScanner;

void  json_scanner_init(JsonScanner *s, const char *input, size_t len);

/*
** Returns the next token id (one of JSON_LBRACE etc. from the
** generated json_parser.h), 0 on EOF, -1 on lex error.  On STRING /
** NUMBER, *out_value is filled with a heap-allocated char* the
** parser owns; otherwise *out_value is set to NULL.
*/
int json_scan(JsonScanner *s, void **out_value);

#endif
