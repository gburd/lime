/*
** Shared tokenizer for the calculator example.
**
** Token constants match the %token declaration order in the Lime grammars.
** Both calc.lime and calc_power.lime share codes 1–7; the extension
** appends CARET as code 8.
*/
#ifndef CALC_TOKENIZE_H
#define CALC_TOKENIZE_H

#define TOK_PLUS    1
#define TOK_MINUS   2
#define TOK_TIMES   3
#define TOK_DIVIDE  4
#define TOK_LPAREN  5
#define TOK_RPAREN  6
#define TOK_INTEGER 7
#define TOK_CARET   8

/*
** Scan the next token from input starting at *pos.
**
** Returns the token code (one of the TOK_* constants), or 0 for EOF.
** On TOK_INTEGER, *value receives the parsed integer value.
** *pos is advanced past the consumed characters.
*/
int calc_next_token(const char *input, int *pos, int len, int *value);

#endif /* CALC_TOKENIZE_H */
