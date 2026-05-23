/*
** tokenize.h -- COBOL tokenizer interface for the example parser.
**
** Hand-written rather than generated from a .lex file because COBOL's
** lexical grammar is unusually irregular: PICTURE clauses run a
** custom mini-tokenizer, fixed-format source layout strips columns
** 1-6 and treats column 7 specially, hyphenated identifiers must
** match against a 200+ keyword table case-insensitively.
*/

#ifndef COBOL_TOKENIZE_H
#define COBOL_TOKENIZE_H

#include <stddef.h>

#include "cobol_grammar.h"

/*
** Source-format mode.  COBOL has lived with two formats for decades:
**
**   FIXED  -- columns 1-6 = sequence numbers (ignored), column 7 =
**             indicator (`*` = comment, `-` = continuation,
**             `D` = debug, `/` = form-feed), columns 8-72 = code.
**             Anything beyond column 72 is also ignored.
**   FREE   -- the entire line is code; comments are introduced by
**             `*>` (modern) or `*` at the start of a line.
**
** The tokenizer picks the mode at startup.  In practice, code that
** starts with six digits in columns 1-6 is fixed-format; anything
** else is free-format.
*/
typedef enum {
    COBOL_FORMAT_AUTO = 0,
    COBOL_FORMAT_FIXED,
    COBOL_FORMAT_FREE,
} CobolSrcFormat;

typedef struct {
    int code;          /* CB_* token code (0 for EOF) */
    const char *text;  /* pointer into the source buffer; NOT NUL-terminated */
    size_t      len;
    int         line; /* 1-based */
    int         col;  /* 1-based */
    long        ivalue; /* for NUMBER tokens; 0 otherwise */
} CobolToken;

typedef struct CobolTokenizer CobolTokenizer;

CobolTokenizer *cobol_tokenize_init(const char *src, size_t len, CobolSrcFormat fmt);
void            cobol_tokenize_free(CobolTokenizer *tk);

/*
** Pull the next token from the stream.  On EOF returns a token with
** code == 0.  Subsequent calls keep returning code == 0.
*/
CobolToken      cobol_tokenize_next(CobolTokenizer *tk);

#endif /* COBOL_TOKENIZE_H */
