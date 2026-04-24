/*-------------------------------------------------------------------------
 *
 * repl_tokenize.c
 *    Lexical scanner for the replication protocol parser (Lime version).
 *
 * Converted from: src/backend/replication/repl_scanner.l
 *
 * This hand-written scanner replaces the Flex-generated scanner from
 * the original PostgreSQL replication protocol parser.  It tokenizes
 * replication commands into tokens consumed by the Lime-generated parser.
 *
 * Token patterns (from repl_scanner.l):
 *   identifier = [A-Za-z\200-\377_][A-Za-z\200-\377_0-9$]*
 *   digit      = [0-9]+                              -> UCONST
 *   hexXX/hexXX = [0-9A-Fa-f]+/[0-9A-Fa-f]+         -> RECPTR
 *   'string'   = single-quoted string with '' escapes -> SCONST
 *   "ident"    = double-quoted identifier             -> IDENT
 *
 * Keywords are case-insensitive (matching repl_scanner.l behavior).
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#include "repl_defs.h"
#include "repl_gram.h"

/* ---------------------------------------------------------------
 * Keyword table
 *
 * Keywords are matched case-insensitively.  Sorted alphabetically
 * for binary search.
 * --------------------------------------------------------------- */
typedef struct ReplKeyword {
    const char *name;
    int         token;
} ReplKeyword;

static const ReplKeyword repl_keywords[] = {
    { "ALTER_REPLICATION_SLOT",    K_ALTER_REPLICATION_SLOT },
    { "BASE_BACKUP",              K_BASE_BACKUP },
    { "CREATE_REPLICATION_SLOT",  K_CREATE_REPLICATION_SLOT },
    { "DROP_REPLICATION_SLOT",    K_DROP_REPLICATION_SLOT },
    { "EXPORT_SNAPSHOT",          K_EXPORT_SNAPSHOT },
    { "IDENTIFY_SYSTEM",          K_IDENTIFY_SYSTEM },
    { "LOGICAL",                  K_LOGICAL },
    { "NOEXPORT_SNAPSHOT",        K_NOEXPORT_SNAPSHOT },
    { "PHYSICAL",                 K_PHYSICAL },
    { "READ_REPLICATION_SLOT",    K_READ_REPLICATION_SLOT },
    { "RESERVE_WAL",              K_RESERVE_WAL },
    { "SHOW",                     K_SHOW },
    { "SLOT",                     K_SLOT },
    { "START_REPLICATION",        K_START_REPLICATION },
    { "TEMPORARY",                K_TEMPORARY },
    { "TIMELINE",                 K_TIMELINE },
    { "TIMELINE_HISTORY",         K_TIMELINE_HISTORY },
    { "TWO_PHASE",                K_TWO_PHASE },
    { "UPLOAD_MANIFEST",          K_UPLOAD_MANIFEST },
    { "USE_SNAPSHOT",             K_USE_SNAPSHOT },
    { "WAIT",                     K_WAIT },
    { NULL, 0 }
};

#define NUM_REPL_KEYWORDS (sizeof(repl_keywords) / sizeof(repl_keywords[0]) - 1)

/*
 * Case-insensitive comparison for keyword lookup.
 */
static int
repl_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = toupper((unsigned char)*a);
        int cb = toupper((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return toupper((unsigned char)*a) - toupper((unsigned char)*b);
}

/*
 * Look up a keyword by case-insensitive match.
 * Returns the token code, or -1 if not found.
 */
static int
repl_keyword_lookup(const char *str)
{
    int lo = 0;
    int hi = (int)NUM_REPL_KEYWORDS - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = repl_strcasecmp(str, repl_keywords[mid].name);
        if (cmp < 0)
            hi = mid - 1;
        else if (cmp > 0)
            lo = mid + 1;
        else
            return repl_keywords[mid].token;
    }
    return -1;
}

/* ---------------------------------------------------------------
 * Character classification helpers
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
is_hexdigit(unsigned char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

static int
is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
}

/* ---------------------------------------------------------------
 * Literal buffer helpers (for quoted strings)
 * --------------------------------------------------------------- */

static void
litbuf_start(ReplParseState *pstate)
{
    pstate->litbuf_len = 0;
    if (pstate->litbuf_cap == 0) {
        pstate->litbuf_cap = 64;
        pstate->litbuf = (char *)malloc(pstate->litbuf_cap);
    }
}

static void
litbuf_addchar(ReplParseState *pstate, char c)
{
    if (pstate->litbuf_len + 1 >= pstate->litbuf_cap) {
        pstate->litbuf_cap *= 2;
        pstate->litbuf = (char *)realloc(pstate->litbuf, pstate->litbuf_cap);
    }
    pstate->litbuf[pstate->litbuf_len++] = c;
}

static char *
litbuf_finish(ReplParseState *pstate)
{
    litbuf_addchar(pstate, '\0');
    return strdup(pstate->litbuf);
}

/* ---------------------------------------------------------------
 * Downcase an identifier string (matching PG behavior).
 * Returns a malloc'd copy.
 * --------------------------------------------------------------- */
static char *
downcase_identifier(const char *src, int len)
{
    char *result = (char *)malloc(len + 1);
    for (int i = 0; i < len; i++)
        result[i] = (char)tolower((unsigned char)src[i]);
    result[len] = '\0';
    return result;
}

/* ---------------------------------------------------------------
 * Main scanner function
 *
 * Returns token type (from repl_gram.h).
 * Returns 0 on EOF.
 * Token value is stored in *tok.
 * --------------------------------------------------------------- */
int
repl_scan_next(ReplParseState *pstate, ReplToken *tok)
{
    const char *input = pstate->input;
    int         pos   = pstate->pos;
    int         len   = pstate->length;

    memset(tok, 0, sizeof(*tok));

    /* Skip whitespace */
    while (pos < len && is_space((unsigned char)input[pos]))
        pos++;

    if (pos >= len) {
        pstate->pos = pos;
        return 0;  /* EOF */
    }

    unsigned char ch = (unsigned char)input[pos];

    /* ----------------------------------------------------------
     * Single-quoted string: SCONST
     * ---------------------------------------------------------- */
    if (ch == '\'') {
        pos++;  /* skip opening quote */
        litbuf_start(pstate);

        while (pos < len) {
            if (input[pos] == '\'') {
                /* Check for escaped quote '' */
                if (pos + 1 < len && input[pos + 1] == '\'') {
                    litbuf_addchar(pstate, '\'');
                    pos += 2;
                } else {
                    pos++;  /* skip closing quote */
                    break;
                }
            } else {
                litbuf_addchar(pstate, input[pos]);
                pos++;
            }
        }

        tok->str = litbuf_finish(pstate);
        pstate->pos = pos;
        return SCONST;
    }

    /* ----------------------------------------------------------
     * Double-quoted identifier: IDENT
     * ---------------------------------------------------------- */
    if (ch == '"') {
        pos++;  /* skip opening quote */
        litbuf_start(pstate);

        while (pos < len) {
            if (input[pos] == '"') {
                /* Check for escaped quote "" */
                if (pos + 1 < len && input[pos + 1] == '"') {
                    litbuf_addchar(pstate, '"');
                    pos += 2;
                } else {
                    pos++;  /* skip closing quote */
                    break;
                }
            } else {
                litbuf_addchar(pstate, input[pos]);
                pos++;
            }
        }

        tok->str = litbuf_finish(pstate);
        pstate->pos = pos;
        return IDENT;
    }

    /* ----------------------------------------------------------
     * Numeric: UCONST or RECPTR
     *
     * RECPTR pattern: hexdigits/hexdigits  (e.g., 0/0 or A1B2/CDEF0123)
     * UCONST pattern: digits
     *
     * Strategy: if we see hex digits, check for '/' followed by more
     * hex digits to decide RECPTR vs UCONST.
     * ---------------------------------------------------------- */
    if (is_hexdigit(ch)) {
        int start = pos;
        bool all_decimal = true;

        /* Scan hex digits */
        while (pos < len && is_hexdigit((unsigned char)input[pos])) {
            if (!is_digit((unsigned char)input[pos]))
                all_decimal = false;
            pos++;
        }

        /* Check for RECPTR: hex / hex */
        if (pos < len && input[pos] == '/') {
            int slash_pos = pos;
            pos++;  /* skip slash */
            int hex_start = pos;

            while (pos < len && is_hexdigit((unsigned char)input[pos]))
                pos++;

            if (pos > hex_start) {
                /* Valid RECPTR */
                char buf[64];
                int total_len = pos - start;
                if (total_len >= (int)sizeof(buf))
                    total_len = (int)sizeof(buf) - 1;
                memcpy(buf, input + start, total_len);
                buf[total_len] = '\0';

                uint32 hi = 0, lo = 0;
                if (sscanf(buf, "%X/%X", &hi, &lo) == 2) {
                    tok->recptr = ((uint64_t)hi) << 32 | lo;
                    pstate->pos = pos;
                    return RECPTR;
                }
            }

            /* Not a valid RECPTR, backtrack to before the slash */
            pos = slash_pos;
        }

        /* Not a RECPTR; if all decimal digits, it's a UCONST */
        if (all_decimal || (!all_decimal && pos == start)) {
            /* Re-scan as pure digits */
            pos = start;
            while (pos < len && is_digit((unsigned char)input[pos]))
                pos++;

            char numbuf[32];
            int nlen = pos - start;
            if (nlen >= (int)sizeof(numbuf))
                nlen = (int)sizeof(numbuf) - 1;
            memcpy(numbuf, input + start, nlen);
            numbuf[nlen] = '\0';

            tok->uintval = (uint32)strtoul(numbuf, NULL, 10);
            pstate->pos = pos;
            return UCONST;
        }

        /* Had hex-only digits with no slash: treat as identifier start */
        pos = start;
        /* fall through to identifier scanning */
    }

    /* ----------------------------------------------------------
     * Identifier or keyword
     * ---------------------------------------------------------- */
    if (is_ident_start(ch)) {
        int start = pos;
        pos++;

        while (pos < len && is_ident_cont((unsigned char)input[pos]))
            pos++;

        int ident_len = pos - start;
        char *ident = (char *)malloc(ident_len + 1);
        memcpy(ident, input + start, ident_len);
        ident[ident_len] = '\0';

        /* Check for keyword (case-insensitive) */
        int kw = repl_keyword_lookup(ident);
        if (kw >= 0) {
            free(ident);
            pstate->pos = pos;
            return kw;
        }

        /* Regular identifier: downcase */
        tok->str = downcase_identifier(input + start, ident_len);
        free(ident);
        pstate->pos = pos;
        return IDENT;
    }

    /* ----------------------------------------------------------
     * Single-character tokens
     * ---------------------------------------------------------- */
    pos++;
    pstate->pos = pos;

    switch (ch) {
        case '(':  return LPAREN;
        case ')':  return RPAREN;
        case ',':  return COMMA;
        case ';':  return SEMI;
        case '.':  return DOT;
        default:
            /* Unknown character: return as-is (parser will report error) */
            repl_yyerror(pstate, "unexpected character in replication command");
            return 0;
    }
}
