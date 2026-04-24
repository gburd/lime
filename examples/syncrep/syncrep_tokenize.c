/*-------------------------------------------------------------------------
 *
 * syncrep_tokenize.c
 *    Lexical scanner for the synchronous replication config parser.
 *
 * Converted from: src/backend/replication/syncrep_scanner.l
 *
 * Token patterns (from syncrep_scanner.l):
 *   identifier = [A-Za-z\200-\377_][A-Za-z\200-\377_0-9$]*
 *   number     = [0-9]+
 *   dquoted    = "([^"]|"")*"   (double-quoted identifier)
 *   *          = wildcard name
 *
 * Keywords (case-insensitive):
 *   ANY, FIRST
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#include "syncrep_defs.h"
#include "syncrep_gram.h"

/* ---------------------------------------------------------------
 * Character classification helpers
 * --------------------------------------------------------------- */

static int
is_ident_start(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= 0x80) ||  /* high-byte characters */
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
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\f' || c == '\v';
}

/* ---------------------------------------------------------------
 * Case-insensitive keyword check
 * --------------------------------------------------------------- */

static int
ci_streq(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* ---------------------------------------------------------------
 * Main scanner
 * --------------------------------------------------------------- */

int
syncrep_scan_next(SyncRepParseState *pstate, SyncRepToken *tok)
{
    const char     *input = pstate->input;
    int             pos   = pstate->pos;
    int             len   = pstate->length;

    tok->str = NULL;

    /* Skip whitespace */
    while (pos < len && is_space((unsigned char)input[pos]))
        pos++;

    /* End of input */
    if (pos >= len)
    {
        pstate->pos = pos;
        return 0;
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
            /* Wildcard: treated as NAME with value "*" */
            tok->str = syncrep_pstrdup(pstate, "*");
            pstate->pos = pos + 1;
            return NAME;
        default:
            break;
    }

    /* Double-quoted identifier */
    if (input[pos] == '"')
    {
        int buf_size = 64;
        int buf_pos = 0;
        char *buf = syncrep_alloc(pstate, buf_size);

        pos++; /* skip opening quote */

        while (pos < len)
        {
            if (input[pos] == '"')
            {
                if (pos + 1 < len && input[pos + 1] == '"')
                {
                    /* Escaped double quote */
                    if (buf_pos + 1 >= buf_size)
                    {
                        buf_size *= 2;
                        char *newbuf = syncrep_alloc(pstate, buf_size);
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
            else
            {
                if (buf_pos + 1 >= buf_size)
                {
                    buf_size *= 2;
                    char *newbuf = syncrep_alloc(pstate, buf_size);
                    memcpy(newbuf, buf, buf_pos);
                    buf = newbuf;
                }
                buf[buf_pos++] = input[pos++];
            }
        }

        buf[buf_pos] = '\0';
        tok->str = buf;
        pstate->pos = pos;
        return NAME;
    }

    /* Number: [0-9]+ */
    if (is_digit((unsigned char)input[pos]))
    {
        int start = pos;
        while (pos < len && is_digit((unsigned char)input[pos]))
            pos++;

        int slen = pos - start;
        char *num = syncrep_alloc(pstate, slen + 1);
        memcpy(num, input + start, slen);
        num[slen] = '\0';

        tok->str = num;
        pstate->pos = pos;
        return NUM;
    }

    /* Identifier: [A-Za-z\200-\377_][A-Za-z\200-\377_0-9$]* */
    if (is_ident_start((unsigned char)input[pos]))
    {
        int start = pos;
        while (pos < len && is_ident_cont((unsigned char)input[pos]))
            pos++;

        int slen = pos - start;
        char *ident = syncrep_alloc(pstate, slen + 1);
        memcpy(ident, input + start, slen);
        ident[slen] = '\0';

        pstate->pos = pos;

        /* Check for case-insensitive keywords */
        if (ci_streq(ident, "any"))
        {
            tok->str = ident;
            return ANY;
        }
        if (ci_streq(ident, "first"))
        {
            tok->str = ident;
            return FIRST;
        }

        tok->str = ident;
        return NAME;
    }

    /* Unrecognized character */
    pstate->pos = pos + 1;
    return JUNK;
}
