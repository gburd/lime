/*-------------------------------------------------------------------------
 *
 * boot_tokenize.c
 *    Lexical scanner for the BKI bootstrap parser (Lime version).
 *
 * Converted from: src/backend/bootstrap/bootscanner.l
 *
 * This hand-written scanner replaces the Flex-generated scanner from
 * the original PostgreSQL bootstrap parser. It tokenizes BKI input
 * into tokens consumed by the Lime-generated parser.
 *
 * Token patterns (from bootscanner.l):
 *   id   = [-A-Za-z0-9_]+
 *   sid  = '([^']|'')*'        (single-quoted string with '' escapes)
 *
 * Keywords are case-sensitive. Most are lowercase, but some (OID,
 * FORCE, NOT, NULL) are uppercase per the original grammar.
 *
 * Comments start with '#' at the beginning of a line and extend to EOL.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#include "boot_gram_defs.h"
#include "boot_grammar.h"

/* Forward declaration */
static int scan_identifier_or_keyword(BootParseState *pstate, BootToken *tok);
static int scan_quoted_string(BootParseState *pstate, BootToken *tok);

/*
 * Keyword table: maps keyword strings to token codes.
 *
 * Keywords are case-sensitive in the BKI format.
 * Note: _null_ is handled specially (NULLVAL token).
 */
typedef struct BootKeyword {
    const char *name;
    int         token;
} BootKeyword;

static const BootKeyword boot_keywords[] = {
    { "FORCE",           XFORCE },
    { "NOT",             XNOT },
    { "NULL",            XNULL },
    { "OID",             OBJ_ID },
    { "_null_",          NULLVAL },
    { "bootstrap",       XBOOTSTRAP },
    { "build",           XBUILD },
    { "close",           XCLOSE },
    { "create",          XCREATE },
    { "declare",         XDECLARE },
    { "index",           INDEX },
    { "indices",         INDICES },
    { "insert",          INSERT_TUPLE },
    { "on",              ON },
    { "open",            OPEN },
    { "rowtype_oid",     XROWTYPE_OID },
    { "shared_relation", XSHARED_RELATION },
    { "toast",           XTOAST },
    { "unique",          UNIQUE },
    { "using",           USING },
    { NULL, 0 }
};

#define NUM_BOOT_KEYWORDS (sizeof(boot_keywords) / sizeof(boot_keywords[0]) - 1)

/*
 * Look up a keyword by exact string match.
 * Returns the token code, or -1 if not found.
 */
static int
boot_keyword_lookup(const char *str)
{
    /* Binary search (keywords are sorted alphabetically) */
    int lo = 0;
    int hi = (int)NUM_BOOT_KEYWORDS - 1;

    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(str, boot_keywords[mid].name);

        if (cmp < 0)
            hi = mid - 1;
        else if (cmp > 0)
            lo = mid + 1;
        else
            return boot_keywords[mid].token;
    }
    return -1;
}

/*
 * Return the keyword string for a given token code, or NULL.
 */
static const char *
boot_keyword_string(int token)
{
    for (int i = 0; boot_keywords[i].name != NULL; i++)
    {
        if (boot_keywords[i].token == token)
            return boot_keywords[i].name;
    }
    return NULL;
}

/* ---------------------------------------------------------------
 * Character classification helpers
 * --------------------------------------------------------------- */

/* Characters valid in a BKI identifier: [-A-Za-z0-9_] */
static int
is_id_char(int c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

/* ---------------------------------------------------------------
 * Main scanner entry point
 * --------------------------------------------------------------- */
int
boot_scan_next(BootParseState *pstate, BootToken *tok)
{
    const char *input = pstate->input;
    int         pos   = pstate->pos;
    int         len   = pstate->length;

    /* Initialize token value */
    tok->str = NULL;
    tok->kw  = NULL;
    tok->ival = 0;

restart:
    /* Skip whitespace (space, tab, carriage return) */
    while (pos < len && (input[pos] == ' ' || input[pos] == '\t' ||
                         input[pos] == '\r'))
    {
        pos++;
    }

    /* Handle newlines */
    if (pos < len && input[pos] == '\n')
    {
        pstate->lineno++;
        pos++;

        /* Check for comment at start of next line */
        if (pos < len && input[pos] == '#')
        {
            /* Skip to end of line */
            while (pos < len && input[pos] != '\n')
                pos++;
            goto restart;
        }
        goto restart;
    }

    /* Check for comment at start of line (or after whitespace at start) */
    /* In the original scanner: ^\#[^\n]* */
    /* We approximate: '#' at start of input or after a newline */
    if (pos < len && input[pos] == '#')
    {
        /* Skip to end of line */
        while (pos < len && input[pos] != '\n')
            pos++;
        goto restart;
    }

    /* End of input */
    if (pos >= len)
    {
        pstate->pos = pos;
        return 0;  /* EOF */
    }

    /* Single-character punctuation */
    switch (input[pos])
    {
        case ',':
            pstate->pos = pos + 1;
            return COMMA;
        case '=':
            pstate->pos = pos + 1;
            return EQUALS;
        case '(':
            pstate->pos = pos + 1;
            return LPAREN;
        case ')':
            pstate->pos = pos + 1;
            return RPAREN;
        default:
            break;
    }

    /* Single-quoted string */
    if (input[pos] == '\'')
    {
        pstate->pos = pos;
        return scan_quoted_string(pstate, tok);
    }

    /* Identifier or keyword */
    if (is_id_char(input[pos]))
    {
        pstate->pos = pos;
        return scan_identifier_or_keyword(pstate, tok);
    }

    /* Unexpected character */
    {
        char errbuf[128];
        snprintf(errbuf, sizeof(errbuf),
                 "syntax error at line %d: unexpected character '%c'",
                 pstate->lineno, input[pos]);
        boot_yyerror(pstate, errbuf);
        pstate->pos = pos + 1;
        goto restart;
    }
}

/*
 * Scan an identifier or keyword token.
 *
 * Pattern: [-A-Za-z0-9_]+
 */
static int
scan_identifier_or_keyword(BootParseState *pstate, BootToken *tok)
{
    const char *input = pstate->input;
    int         start = pstate->pos;
    int         pos   = start;
    int         len   = pstate->length;
    int         kwtoken;
    char       *ident;
    int         ident_len;

    while (pos < len && is_id_char(input[pos]))
        pos++;

    ident_len = pos - start;
    ident = boot_alloc(pstate, ident_len + 1);
    memcpy(ident, input + start, ident_len);
    ident[ident_len] = '\0';

    pstate->pos = pos;

    /* Check if this is a keyword */
    kwtoken = boot_keyword_lookup(ident);
    if (kwtoken >= 0)
    {
        if (kwtoken == NULLVAL)
        {
            /* _null_ is a reserved keyword with no string value */
            return NULLVAL;
        }
        /* Unreserved keyword: store the keyword string */
        tok->kw = boot_keyword_string(kwtoken);
        return kwtoken;
    }

    /* Regular identifier */
    tok->str = ident;
    return ID;
}

/*
 * Scan a single-quoted string.
 *
 * Pattern: '([^']|'')*'
 *
 * Two consecutive single quotes ('') are an escape for one quote.
 */
static int
scan_quoted_string(BootParseState *pstate, BootToken *tok)
{
    const char *input = pstate->input;
    int         pos   = pstate->pos + 1;  /* skip opening quote */
    int         len   = pstate->length;
    int         buf_size = 64;
    char       *buf;
    int         buf_pos = 0;

    buf = boot_alloc(pstate, buf_size);

    while (pos < len)
    {
        if (input[pos] == '\'')
        {
            /* Check for escaped quote ('') */
            if (pos + 1 < len && input[pos + 1] == '\'')
            {
                /* Escaped quote: emit one quote */
                if (buf_pos + 1 >= buf_size)
                {
                    buf_size *= 2;
                    char *newbuf = boot_alloc(pstate, buf_size);
                    memcpy(newbuf, buf, buf_pos);
                    buf = newbuf;
                }
                buf[buf_pos++] = '\'';
                pos += 2;
            }
            else
            {
                /* End of string */
                pos++;
                break;
            }
        }
        else
        {
            if (buf_pos + 1 >= buf_size)
            {
                buf_size *= 2;
                char *newbuf = boot_alloc(pstate, buf_size);
                memcpy(newbuf, buf, buf_pos);
                buf = newbuf;
            }
            buf[buf_pos++] = input[pos++];
        }
    }

    buf[buf_pos] = '\0';
    pstate->pos = pos;

    tok->str = buf;
    return ID;
}
