/*-------------------------------------------------------------------------
 *
 * isolation_tokenize.c
 *    Lexical scanner for the isolation test spec parser.
 *
 * Converted from: src/test/isolation/specscanner.l
 *
 * Token patterns:
 *   identifier  = [A-Za-z\200-\377_][A-Za-z\200-\377_0-9$]*
 *   qident      = "([^"]|"")*"       (double-quoted identifier)
 *   sqlblock    = { ... }            (SQL between braces)
 *   integer     = [0-9]+
 *   comment     = # to end of line
 *
 * Keywords (case-sensitive):
 *   notices, permutation, session, setup, step, teardown
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#include "isolation_defs.h"
#include "isolation_gram.h"

/* ---------------------------------------------------------------
 * Character classification
 * --------------------------------------------------------------- */

static int
is_ident_start(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= 0x80) ||
           c == '_';
}

static int
is_ident_cont(unsigned char c)
{
    return is_ident_start(c) ||
           (c >= '0' && c <= '9') ||
           c == '$';
}

static int
is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static int
is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

/* ---------------------------------------------------------------
 * Keyword table (case-sensitive, sorted for binary search)
 * --------------------------------------------------------------- */

typedef struct IsolKeyword {
    const char *name;
    int         token;
} IsolKeyword;

static const IsolKeyword isol_keywords[] = {
    { "notices",     NOTICES },
    { "permutation", PERMUTATION },
    { "session",     SESSION },
    { "setup",       SETUP },
    { "step",        STEP },
    { "teardown",    TEARDOWN },
    { "test",        TEST },
    { NULL, 0 }
};

#define NUM_ISOL_KEYWORDS (sizeof(isol_keywords) / sizeof(isol_keywords[0]) - 1)

static int
isol_keyword_lookup(const char *str)
{
    int lo = 0;
    int hi = (int)NUM_ISOL_KEYWORDS - 1;

    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(str, isol_keywords[mid].name);

        if (cmp < 0)
            hi = mid - 1;
        else if (cmp > 0)
            lo = mid + 1;
        else
            return isol_keywords[mid].token;
    }
    return -1;
}

/* ---------------------------------------------------------------
 * SQL block scanner
 *
 * Scans everything between { and }, handling nesting and trimming
 * leading whitespace after the opening brace.
 * --------------------------------------------------------------- */
static int
scan_sqlblock(IsolParseState *pstate, IsolToken *tok)
{
    const char *input = pstate->input;
    int         pos   = pstate->pos;
    int         len   = pstate->length;
    int         buf_size = 256;
    int         buf_pos = 0;
    char       *buf;
    int         brace_depth = 1;

    /* Skip '{' */
    pos++;

    /* Skip leading whitespace */
    while (pos < len && is_space((unsigned char)input[pos]))
        pos++;

    buf = isol_alloc(pstate, buf_size);

    while (pos < len && brace_depth > 0)
    {
        char c = input[pos];

        if (c == '{')
        {
            brace_depth++;
        }
        else if (c == '}')
        {
            brace_depth--;
            if (brace_depth == 0)
            {
                pos++;
                break;
            }
        }

        if (brace_depth > 0)
        {
            if (c == '\n')
                pstate->lineno++;

            if (buf_pos + 1 >= buf_size)
            {
                buf_size *= 2;
                char *newbuf = isol_alloc(pstate, buf_size);
                memcpy(newbuf, buf, buf_pos);
                buf = newbuf;
            }
            buf[buf_pos++] = c;
        }
        pos++;
    }

    /* Trim trailing whitespace */
    while (buf_pos > 0 && (buf[buf_pos - 1] == ' ' || buf[buf_pos - 1] == '\t' ||
                            buf[buf_pos - 1] == '\r' || buf[buf_pos - 1] == '\f' ||
                            buf[buf_pos - 1] == '\v'))
        buf_pos--;

    buf[buf_pos] = '\0';
    pstate->pos = pos;

    tok->str = buf;
    return SQLBLOCK;
}

/* ---------------------------------------------------------------
 * Double-quoted identifier scanner
 * --------------------------------------------------------------- */
static int
scan_quoted_ident(IsolParseState *pstate, IsolToken *tok)
{
    const char *input = pstate->input;
    int         pos   = pstate->pos + 1; /* skip opening quote */
    int         len   = pstate->length;
    int         buf_size = 64;
    int         buf_pos = 0;
    char       *buf;

    buf = isol_alloc(pstate, buf_size);

    while (pos < len)
    {
        if (input[pos] == '"')
        {
            if (pos + 1 < len && input[pos + 1] == '"')
            {
                /* Escaped quote */
                if (buf_pos + 1 >= buf_size)
                {
                    buf_size *= 2;
                    char *newbuf = isol_alloc(pstate, buf_size);
                    memcpy(newbuf, buf, buf_pos);
                    buf = newbuf;
                }
                buf[buf_pos++] = '"';
                pos += 2;
            }
            else
            {
                /* End of quoted identifier */
                pos++;
                break;
            }
        }
        else if (input[pos] == '\n')
        {
            isol_yyerror(pstate, "unexpected newline in quoted identifier");
            pos++;
            break;
        }
        else
        {
            if (buf_pos + 1 >= buf_size)
            {
                buf_size *= 2;
                char *newbuf = isol_alloc(pstate, buf_size);
                memcpy(newbuf, buf, buf_pos);
                buf = newbuf;
            }
            buf[buf_pos++] = input[pos++];
        }
    }

    buf[buf_pos] = '\0';
    pstate->pos = pos;

    tok->str = buf;
    return IDENTIFIER;
}

/* ---------------------------------------------------------------
 * Main scanner
 * --------------------------------------------------------------- */
int
isol_scan_next(IsolParseState *pstate, IsolToken *tok)
{
    const char *input = pstate->input;
    int         pos   = pstate->pos;
    int         len   = pstate->length;

    tok->str = NULL;
    tok->ival = 0;

restart:
    /* Skip whitespace */
    while (pos < len && is_space((unsigned char)input[pos]))
        pos++;

    /* Handle newlines */
    if (pos < len && input[pos] == '\n')
    {
        pstate->lineno++;
        pos++;
        goto restart;
    }

    /* End of input */
    if (pos >= len)
    {
        pstate->pos = pos;
        return 0;
    }

    /* Comments: # to end of line */
    if (input[pos] == '#')
    {
        while (pos < len && input[pos] != '\n')
            pos++;
        goto restart;
    }

    /* SQL block: { ... } */
    if (input[pos] == '{')
    {
        pstate->pos = pos;
        return scan_sqlblock(pstate, tok);
    }

    /* Double-quoted identifier */
    if (input[pos] == '"')
    {
        pstate->pos = pos;
        return scan_quoted_ident(pstate, tok);
    }

    /* Punctuation */
    switch (input[pos])
    {
        case ',':
            pstate->pos = pos + 1;
            return COMMA;
        case '(':
            pstate->pos = pos + 1;
            return LPAREN;
        case ')':
            pstate->pos = pos + 1;
            return RPAREN;
        case '*':
            pstate->pos = pos + 1;
            return STAR;
        default:
            break;
    }

    /* Number: [0-9]+ */
    if (is_digit((unsigned char)input[pos]))
    {
        int start = pos;
        while (pos < len && is_digit((unsigned char)input[pos]))
            pos++;

        int slen = pos - start;
        char *num = isol_alloc(pstate, slen + 1);
        memcpy(num, input + start, slen);
        num[slen] = '\0';

        tok->ival = atoi(num);
        pstate->pos = pos;
        return INTEGER;
    }

    /* Identifier or keyword */
    if (is_ident_start((unsigned char)input[pos]))
    {
        int start = pos;
        int kwtoken;

        while (pos < len && is_ident_cont((unsigned char)input[pos]))
            pos++;

        int slen = pos - start;
        char *ident = isol_alloc(pstate, slen + 1);
        memcpy(ident, input + start, slen);
        ident[slen] = '\0';

        pstate->pos = pos;

        /* Check for keyword */
        kwtoken = isol_keyword_lookup(ident);
        if (kwtoken >= 0)
            return kwtoken;

        /* Regular identifier */
        tok->str = ident;
        return IDENTIFIER;
    }

    /* Unexpected character */
    {
        char errbuf[128];
        snprintf(errbuf, sizeof(errbuf),
                 "unexpected character '%c' at line %d",
                 input[pos], pstate->lineno);
        isol_yyerror(pstate, errbuf);
        pstate->pos = pos + 1;
        goto restart;
    }
}
