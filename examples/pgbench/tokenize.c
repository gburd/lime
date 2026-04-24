/*
 * pgbench Expression Tokenizer for Lime Parser
 *
 * Hand-written tokenizer implementing the lexical analysis from
 * PostgreSQL's src/bin/pgbench/exprscan.l (EXPR state only),
 * adapted for use with the Lime-generated pgbench expression parser.
 *
 * See tokenize.h for the public API.
 */

#define _POSIX_C_SOURCE 200809L

#include "tokenize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>


/* ======================================================================
 * Scanner state
 * ====================================================================== */

struct PgbenchScanner {
    const char *input;      /* Input expression string */
    size_t      length;     /* Length of input */
    size_t      pos;        /* Current byte offset */

    /* Error state */
    const char *errmsg;
    int         errloc;
};


/* ======================================================================
 * Character classification helpers
 * ====================================================================== */

static inline bool is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\f' || c == '\v';
}

static inline bool is_alpha(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '_' || c >= 0x80;
}

static inline bool is_alnum(unsigned char c) {
    return is_alpha(c) || (c >= '0' && c <= '9');
}

static inline bool is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

static inline unsigned char to_lower(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}


/* ======================================================================
 * Case-insensitive keyword matching
 *
 * The keywords are short enough that a simple linear scan with
 * case-insensitive comparison is efficient.
 * ====================================================================== */

typedef struct {
    const char *keyword;
    int         token;
} Keyword;

static const Keyword keywords[] = {
    { "and",     AND_OP },
    { "case",    CASE_KW },
    { "else",    ELSE_KW },
    { "end",     END_KW },
    { "false",   BOOLEAN_CONST },
    { "is",      IS_OP },
    { "isnull",  ISNULL_OP },
    { "not",     NOT_OP },
    { "notnull", NOTNULL_OP },
    { "null",    NULL_CONST },
    { "or",      OR_OP },
    { "then",    THEN_KW },
    { "true",    BOOLEAN_CONST },
    { "when",    WHEN_KW },
    { NULL,      0 }
};

/*
 * Case-insensitive comparison of input[0..len-1] against a lowercase keyword.
 */
static bool keyword_match(const char *input, size_t len, const char *kw) {
    size_t i;
    for (i = 0; i < len; i++) {
        if (kw[i] == '\0') return false;  /* keyword is shorter */
        if (to_lower((unsigned char)input[i]) != (unsigned char)kw[i])
            return false;
    }
    return kw[i] == '\0';  /* keyword must end here too */
}

/*
 * Look up a word as a keyword.
 * Returns the token code if found, or 0 if it is not a keyword.
 * For BOOLEAN_CONST, sets *bval to true/false.
 */
static int keyword_lookup(const char *text, size_t len, bool *bval) {
    for (int i = 0; keywords[i].keyword != NULL; i++) {
        if (keyword_match(text, len, keywords[i].keyword)) {
            if (keywords[i].token == BOOLEAN_CONST) {
                *bval = (to_lower((unsigned char)text[0]) == 't');
            }
            return keywords[i].token;
        }
    }
    return 0;
}


/* ======================================================================
 * Whitespace and line-continuation skipping
 * ====================================================================== */

static void skip_whitespace(PgbenchScanner *s) {
    while (s->pos < s->length) {
        unsigned char c = (unsigned char)s->input[s->pos];
        if (is_space(c)) {
            s->pos++;
            continue;
        }
        /* Line continuation: backslash followed by optional \r and \n */
        if (c == '\\' && s->pos + 1 < s->length) {
            size_t next = s->pos + 1;
            if (s->input[next] == '\r') next++;
            if (next < s->length && s->input[next] == '\n') {
                s->pos = next + 1;
                continue;
            }
        }
        break;
    }
}


/* ======================================================================
 * Main scanner function
 * ====================================================================== */

int pgbench_scan(PgbenchScanner *s, TokenValue *val) {
    unsigned char c;

    memset(val, 0, sizeof(*val));
    skip_whitespace(s);

    if (s->pos >= s->length) {
        return 0;  /* EOF */
    }

    c = (unsigned char)s->input[s->pos];

    /* ---- Newline: end of expression ---- */
    if (c == '\n') {
        return 0;
    }

    /* ---- Single-character operators and punctuation ---- */
    switch (c) {
        case '(':  s->pos++; return LPAREN;
        case ')':  s->pos++; return RPAREN;
        case ',':  s->pos++; return COMMA;
        case '~':  s->pos++; return TILDE;
        case '+':  s->pos++; return PLUS;
        case '-':  s->pos++; return MINUS;
        case '*':  s->pos++; return STAR;
        case '/':  s->pos++; return SLASH;
        case '%':  s->pos++; return PERCENT;
        case '&':  s->pos++; return AMP;
        case '|':  s->pos++; return PIPE;
        case '#':  s->pos++; return HASH;
        default: break;
    }

    /* ---- Multi-character operators ---- */

    /* <> or <= or << or < */
    if (c == '<') {
        s->pos++;
        if (s->pos < s->length) {
            if (s->input[s->pos] == '>') { s->pos++; return NE_OP; }
            if (s->input[s->pos] == '=') { s->pos++; return LE_OP; }
            if (s->input[s->pos] == '<') { s->pos++; return LS_OP; }
        }
        return LT;
    }

    /* >= or >> or > */
    if (c == '>') {
        s->pos++;
        if (s->pos < s->length) {
            if (s->input[s->pos] == '=') { s->pos++; return GE_OP; }
            if (s->input[s->pos] == '>') { s->pos++; return RS_OP; }
        }
        return GT;
    }

    /* != or = */
    if (c == '!') {
        if (s->pos + 1 < s->length && s->input[s->pos + 1] == '=') {
            s->pos += 2;
            return NE_OP;
        }
        /* Bare '!' is not valid in pgbench expressions */
        s->errmsg = "unexpected character '!'";
        s->errloc = (int)s->pos;
        s->pos++;
        return -1;
    }

    if (c == '=') {
        s->pos++;
        return EQ;
    }

    /* ---- Variable: :name ---- */
    if (c == ':') {
        s->pos++;  /* skip ':' */
        size_t start = s->pos;
        while (s->pos < s->length && is_alnum((unsigned char)s->input[s->pos]))
            s->pos++;
        if (s->pos == start) {
            s->errmsg = "missing variable name after ':'";
            s->errloc = (int)(s->pos - 1);
            return -1;
        }
        size_t len = s->pos - start;
        val->str = strndup(s->input + start, len);
        return VARIABLE;
    }

    /* ---- Identifiers and keywords ---- */
    if (is_alpha(c)) {
        size_t start = s->pos;
        s->pos++;
        while (s->pos < s->length && is_alnum((unsigned char)s->input[s->pos]))
            s->pos++;
        size_t len = s->pos - start;

        /* Check for keywords */
        bool bval = false;
        int kw = keyword_lookup(s->input + start, len, &bval);
        if (kw == BOOLEAN_CONST) {
            val->bval = bval;
            return BOOLEAN_CONST;
        }
        if (kw > 0) {
            return kw;
        }

        /* Not a keyword: it is a function name */
        val->str = strndup(s->input + start, len);
        return FUNCTION;
    }

    /* ---- Numeric constants ---- */
    if (is_digit(c)) {
        size_t start = s->pos;

        /* Check for the special INT64_MIN literal */
        if (c == '9') {
            const char *maxint_str = "9223372036854775808";
            size_t maxint_len = strlen(maxint_str);
            if (s->pos + maxint_len <= s->length &&
                memcmp(s->input + s->pos, maxint_str, maxint_len) == 0) {
                /* Verify the next character is not a digit or dot */
                size_t after = s->pos + maxint_len;
                if (after >= s->length ||
                    (!is_digit((unsigned char)s->input[after]) &&
                     s->input[after] != '.')) {
                    s->pos += maxint_len;
                    return MAXINT_PLUS_ONE_CONST;
                }
            }
        }

        /* Scan integer part */
        while (s->pos < s->length && is_digit((unsigned char)s->input[s->pos]))
            s->pos++;

        bool is_float = false;

        /* Check for decimal point */
        if (s->pos < s->length && s->input[s->pos] == '.') {
            is_float = true;
            s->pos++;
            while (s->pos < s->length && is_digit((unsigned char)s->input[s->pos]))
                s->pos++;
        }

        /* Check for exponent */
        if (s->pos < s->length &&
            (s->input[s->pos] == 'e' || s->input[s->pos] == 'E')) {
            is_float = true;
            s->pos++;
            if (s->pos < s->length &&
                (s->input[s->pos] == '+' || s->input[s->pos] == '-'))
                s->pos++;
            if (s->pos >= s->length || !is_digit((unsigned char)s->input[s->pos])) {
                s->errmsg = "invalid numeric constant (missing exponent digits)";
                s->errloc = (int)start;
                return -1;
            }
            while (s->pos < s->length && is_digit((unsigned char)s->input[s->pos]))
                s->pos++;
        }

        size_t numlen = s->pos - start;
        char *numstr = strndup(s->input + start, numlen);

        if (is_float) {
            char *end;
            errno = 0;
            val->dval = strtod(numstr, &end);
            if (errno == ERANGE) {
                s->errmsg = "double constant overflow";
                s->errloc = (int)start;
                free(numstr);
                return -1;
            }
            free(numstr);
            return DOUBLE_CONST;
        } else {
            char *end;
            errno = 0;
            long long llval = strtoll(numstr, &end, 10);
            if (errno == ERANGE) {
                s->errmsg = "bigint constant overflow";
                s->errloc = (int)start;
                free(numstr);
                return -1;
            }
            val->ival = (int64_t)llval;
            free(numstr);
            return INTEGER_CONST;
        }
    }

    /* ---- Numbers starting with dot: .NNN ---- */
    if (c == '.' && s->pos + 1 < s->length &&
        is_digit((unsigned char)s->input[s->pos + 1])) {
        size_t start = s->pos;
        s->pos++;  /* skip '.' */
        while (s->pos < s->length && is_digit((unsigned char)s->input[s->pos]))
            s->pos++;

        /* Check for exponent */
        if (s->pos < s->length &&
            (s->input[s->pos] == 'e' || s->input[s->pos] == 'E')) {
            s->pos++;
            if (s->pos < s->length &&
                (s->input[s->pos] == '+' || s->input[s->pos] == '-'))
                s->pos++;
            if (s->pos >= s->length || !is_digit((unsigned char)s->input[s->pos])) {
                s->errmsg = "invalid numeric constant";
                s->errloc = (int)start;
                return -1;
            }
            while (s->pos < s->length && is_digit((unsigned char)s->input[s->pos]))
                s->pos++;
        }

        size_t numlen = s->pos - start;
        char *numstr = strndup(s->input + start, numlen);
        char *end;
        errno = 0;
        val->dval = strtod(numstr, &end);
        if (errno == ERANGE) {
            s->errmsg = "double constant overflow";
            s->errloc = (int)start;
            free(numstr);
            return -1;
        }
        free(numstr);
        return DOUBLE_CONST;
    }

    /* ---- Unknown character ---- */
    s->errmsg = "unexpected character";
    s->errloc = (int)s->pos;
    s->pos++;
    return -1;
}


/* ======================================================================
 * Public API
 * ====================================================================== */

PgbenchScanner *pgbench_scanner_create(const char *input, size_t length) {
    PgbenchScanner *s = (PgbenchScanner *)calloc(1, sizeof(PgbenchScanner));
    if (!s) return NULL;
    s->input = input;
    s->length = length;
    s->pos = 0;
    s->errmsg = NULL;
    s->errloc = -1;
    return s;
}

void pgbench_scanner_destroy(PgbenchScanner *s) {
    if (!s) return;
    free(s);
}

const char *pgbench_scanner_error(const PgbenchScanner *s) {
    return s->errmsg;
}

int pgbench_scanner_error_location(const PgbenchScanner *s) {
    return s->errloc;
}

const char *pgbench_token_name(int token) {
    switch (token) {
        case 0:                      return "EOF";
        case NULL_CONST:             return "NULL_CONST";
        case INTEGER_CONST:          return "INTEGER_CONST";
        case MAXINT_PLUS_ONE_CONST:  return "MAXINT_PLUS_ONE_CONST";
        case DOUBLE_CONST:           return "DOUBLE_CONST";
        case BOOLEAN_CONST:          return "BOOLEAN_CONST";
        case VARIABLE:               return "VARIABLE";
        case FUNCTION:               return "FUNCTION";
        case AND_OP:                 return "AND_OP";
        case OR_OP:                  return "OR_OP";
        case NOT_OP:                 return "NOT_OP";
        case NE_OP:                  return "NE_OP";
        case LE_OP:                  return "LE_OP";
        case GE_OP:                  return "GE_OP";
        case LS_OP:                  return "LS_OP";
        case RS_OP:                  return "RS_OP";
        case IS_OP:                  return "IS_OP";
        case ISNULL_OP:              return "ISNULL_OP";
        case NOTNULL_OP:             return "NOTNULL_OP";
        case CASE_KW:               return "CASE_KW";
        case WHEN_KW:               return "WHEN_KW";
        case THEN_KW:               return "THEN_KW";
        case ELSE_KW:               return "ELSE_KW";
        case END_KW:                return "END_KW";
        case PLUS:                   return "PLUS";
        case MINUS:                  return "MINUS";
        case STAR:                   return "STAR";
        case SLASH:                  return "SLASH";
        case PERCENT:                return "PERCENT";
        case LT:                     return "LT";
        case GT:                     return "GT";
        case EQ:                     return "EQ";
        case AMP:                    return "AMP";
        case PIPE:                   return "PIPE";
        case HASH:                   return "HASH";
        case TILDE:                  return "TILDE";
        case LPAREN:                 return "LPAREN";
        case RPAREN:                 return "RPAREN";
        case COMMA:                  return "COMMA";
        default:                     return "???";
    }
}


/* ======================================================================
 * Standalone test drivers
 *
 * Compile with -DPGBENCH_TOKENIZE_MAIN for tokenizer-only test.
 * Compile with -DPGBENCH_PARSE_MAIN for full parse test.
 * ====================================================================== */

#ifdef PGBENCH_TOKENIZE_MAIN

static char *read_stdin(size_t *out_len) {
    size_t alloc = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(alloc);
    if (!buf) return NULL;
    size_t n;
    while ((n = fread(buf + len, 1, alloc - len, stdin)) > 0) {
        len += n;
        if (len >= alloc) {
            alloc *= 2;
            char *newbuf = (char *)realloc(buf, alloc);
            if (!newbuf) { free(buf); return NULL; }
            buf = newbuf;
        }
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

int main(void) {
    size_t len;
    char *input = read_stdin(&len);
    if (!input) {
        fprintf(stderr, "Failed to read input\n");
        return 1;
    }

    PgbenchScanner *scanner = pgbench_scanner_create(input, len);
    if (!scanner) {
        fprintf(stderr, "Failed to create scanner\n");
        free(input);
        return 1;
    }

    TokenValue val;
    int token;

    while ((token = pgbench_scan(scanner, &val)) != 0) {
        if (token == -1) {
            fprintf(stderr, "Error at byte %d: %s\n",
                    pgbench_scanner_error_location(scanner),
                    pgbench_scanner_error(scanner));
            break;
        }

        printf("%-25s", pgbench_token_name(token));

        if (token == INTEGER_CONST) {
            printf(" val=%ld", (long)val.ival);
        } else if (token == DOUBLE_CONST) {
            printf(" val=%g", val.dval);
        } else if (token == BOOLEAN_CONST) {
            printf(" val=%s", val.bval ? "true" : "false");
        } else if (token == VARIABLE || token == FUNCTION) {
            printf(" val=\"%s\"", val.str);
            free(val.str);
        }

        printf("\n");
    }

    pgbench_scanner_destroy(scanner);
    free(input);
    return (token == -1) ? 1 : 0;
}

#endif /* PGBENCH_TOKENIZE_MAIN */


#ifdef PGBENCH_PARSE_MAIN

/* Pull in the Lime-generated parser */
#include "pgbench_expr.c"

int main(int argc, char **argv) {
    size_t alloc = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(alloc);
    if (!buf) { fprintf(stderr, "malloc failed\n"); return 1; }
    size_t n;
    while ((n = fread(buf + len, 1, alloc - len, stdin)) > 0) {
        len += n;
        if (len >= alloc) {
            alloc *= 2;
            char *newbuf = (char *)realloc(buf, alloc);
            if (!newbuf) { free(buf); return 1; }
            buf = newbuf;
        }
    }
    buf[len] = '\0';

    /* Create parser */
    void *parser = pgbenchExprAlloc(malloc);
    if (!parser) { fprintf(stderr, "parser alloc failed\n"); free(buf); return 1; }

    ParseState pstate;
    memset(&pstate, 0, sizeof(pstate));

    /* Enable tracing if -t flag is given */
    if (argc > 1 && strcmp(argv[1], "-t") == 0) {
        pgbenchExprTrace(stdout, "PARSE: ");
    }

    /* Create scanner */
    PgbenchScanner *scanner = pgbench_scanner_create(buf, len);
    if (!scanner) {
        fprintf(stderr, "scanner alloc failed\n");
        pgbenchExprFree(parser, free);
        free(buf);
        return 1;
    }

    /* Feed tokens to parser */
    TokenValue val;
    int token;

    while ((token = pgbench_scan(scanner, &val)) != 0) {
        if (token == -1) {
            fprintf(stderr, "Lexical error at byte %d: %s\n",
                    pgbench_scanner_error_location(scanner),
                    pgbench_scanner_error(scanner));
            pgbench_scanner_destroy(scanner);
            pgbenchExprFree(parser, free);
            free(buf);
            return 1;
        }
        pgbenchExpr(parser, token, val, &pstate);
    }

    /* Signal end of input */
    TokenValue empty_val;
    memset(&empty_val, 0, sizeof(empty_val));
    pgbenchExpr(parser, 0, empty_val, &pstate);

    /* Check results */
    if (pstate.errors > 0) {
        fprintf(stderr, "Parse failed with %d error(s)\n", pstate.errors);
        pgbench_scanner_destroy(scanner);
        pgbenchExprFree(parser, free);
        free(buf);
        return 1;
    }

    if (pstate.result) {
        printf("Parse succeeded. AST:\n");
        print_expr(pstate.result, 0);
        free_expr(pstate.result);
    } else {
        printf("Parse succeeded (no result).\n");
    }

    pgbench_scanner_destroy(scanner);
    pgbenchExprFree(parser, free);
    free(buf);
    return 0;
}

#endif /* PGBENCH_PARSE_MAIN */
