/*
** Shared tokenizer for the calculator example.
*/
#include "tokenize.h"
#include <ctype.h>

int calc_next_token(const char *input, int *pos, int len, int *value) {
    /* Skip whitespace */
    while (*pos < len && isspace((unsigned char)input[*pos])) {
        (*pos)++;
    }
    if (*pos >= len) return 0; /* EOF */

    char c = input[*pos];

    switch (c) {
    case '+': (*pos)++; return TOK_PLUS;
    case '-': (*pos)++; return TOK_MINUS;
    case '*': (*pos)++; return TOK_TIMES;
    case '/': (*pos)++; return TOK_DIVIDE;
    case '(': (*pos)++; return TOK_LPAREN;
    case ')': (*pos)++; return TOK_RPAREN;
    case '^': (*pos)++; return TOK_CARET;
    default:
        break;
    }

    /* Integer literal */
    if (isdigit((unsigned char)c)) {
        int v = 0;
        while (*pos < len && isdigit((unsigned char)input[*pos])) {
            v = v * 10 + (input[*pos] - '0');
            (*pos)++;
        }
        *value = v;
        return TOK_INTEGER;
    }

    /* Unknown character — skip it and return 0 */
    (*pos)++;
    return 0;
}
