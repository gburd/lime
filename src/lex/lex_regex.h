/*
** src/lex/lex_regex.h -- regex parser for the Lime .lex DFA
** compiler (M2.1 of the lexer subsystem).
**
** Parses the POSIX-extended regex subset documented in
** docs/LEXER_DESIGN.md v0.2:
**
**   regex      := alt
**   alt        := concat ('|' concat)*
**   concat     := term*
**   term       := atom quantifier?
**   quantifier := '*' | '+' | '?' | '{' n (',' m?)? '}'
**   atom       := literal | escape | char_class | '.' | '^' | '$'
**                | '(' regex ')'
**   char_class := '[' '^'? class_item* ']'
**   class_item := char | char '-' char | escape
**
** No PCRE features (back-references, named captures, lookahead,
** inline flags).  Backslash escapes recognized: \n \t \r \f \v
** \0 \\ \" \' and \xHH for arbitrary bytes.  Inside a character
** class, escapes work the same way.
**
** AST is heap-allocated; release with lime_lex_regex_free.
*/
#ifndef LIME_LEX_REGEX_H
#define LIME_LEX_REGEX_H

#include <stddef.h>

/* AST node kinds.  Stable for the lifetime of the project; tests
** assert on specific values. */
typedef enum {
    LIME_RE_LITERAL = 1,  /* a single byte (literal char) */
    LIME_RE_CHAR_CLASS,   /* [abc-z] character class (256-bit bitmap) */
    LIME_RE_ANY,          /* . wildcard (any byte except '\n') */
    LIME_RE_CONCAT,       /* ab (left then right) */
    LIME_RE_ALT,          /* a|b (left or right) */
    LIME_RE_STAR,         /* a* */
    LIME_RE_PLUS,         /* a+ */
    LIME_RE_QUESTION,     /* a? */
    LIME_RE_REPEAT,       /* a{n,m} */
    LIME_RE_ANCHOR_START, /* ^ -- begin of input */
    LIME_RE_ANCHOR_END,   /* $ -- end of input */
    LIME_RE_EMPTY         /* (empty regex) */
} LimeReKind;

typedef struct LimeReNode LimeReNode;
struct LimeReNode {
    LimeReKind kind;
    union {
        unsigned char literal;      /* LITERAL */
        struct {                    /* CHAR_CLASS */
            unsigned char bits[32]; /* 256-bit bitmap; bit i set
                                            ** = byte i is in the class */
            int negate;             /* if true, bitmap is complemented */
        } char_class;
        struct { /* CONCAT, ALT */
            LimeReNode *left;
            LimeReNode *right;
        } binary;
        struct { /* STAR, PLUS, QUESTION */
            LimeReNode *child;
        } unary;
        struct { /* REPEAT */
            LimeReNode *child;
            int min;
            int max; /* -1 means unbounded */
        } repeat;
    } u;
};

/* Parse a NUL-terminated regex source string into an AST.
**
** Returns the root node on success; NULL on parse error.  When
** non-NULL `err_out` is supplied and parsing fails, *err_out is
** set to a heap-allocated diagnostic message (caller frees);
** otherwise *err_out is left NULL.  When parsing succeeds,
** *err_out (if supplied) is left NULL.
**
** The empty regex `""` parses successfully and yields a
** LIME_RE_EMPTY node.
*/
LimeReNode *lime_lex_regex_parse(const char *src, char **err_out);

/* Free an AST.  NULL-safe. */
void lime_lex_regex_free(LimeReNode *root);

/* Set bit `byte` in a 256-bit bitmap.  Helper exposed for tests. */
void lime_lex_regex_class_set(unsigned char *bits, unsigned char byte);

/* Test whether bit `byte` is set in a 256-bit bitmap.  Helper for
** tests. */
int lime_lex_regex_class_has(const unsigned char *bits, unsigned char byte);

#endif /* LIME_LEX_REGEX_H */
