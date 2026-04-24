/*
 * tokenize.c
 *    Hand-written tokenizer for the MongoDB query language parser.
 *
 * Recognizes JSON tokens (strings, numbers, booleans, null, braces,
 * brackets, colons, commas) plus MongoDB-specific operator keywords
 * like $eq, $gt, $and, $set, $match, etc.
 *
 * Operator keywords start with '$' and are returned as specific token
 * types. Unrecognized $-prefixed strings are returned as DOLLAR_IDENT.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include "tokenize.h"
#include "mongodb.h"

struct MdbScanner
{
    const char *input;
    int         len;
    int         pos;
    const char *error;
    char        strbuf[4096];   /* buffer for string values */
};

MdbScanner *mdb_scanner_create(const char *input, int len)
{
    MdbScanner *s = (MdbScanner *)calloc(1, sizeof(MdbScanner));
    s->input = input;
    s->len = len;
    s->pos = 0;
    s->error = NULL;
    return s;
}

void mdb_scanner_destroy(MdbScanner *scanner)
{
    free(scanner);
}

const char *mdb_scanner_error(MdbScanner *scanner)
{
    return scanner ? scanner->error : NULL;
}

/* Skip whitespace and comments */
static void skip_ws(MdbScanner *s)
{
    while (s->pos < s->len) {
        char c = s->input[s->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            s->pos++;
        } else if (c == '/' && s->pos + 1 < s->len) {
            /* C-style line comment */
            if (s->input[s->pos + 1] == '/') {
                s->pos += 2;
                while (s->pos < s->len && s->input[s->pos] != '\n')
                    s->pos++;
            }
            /* C-style block comment */
            else if (s->input[s->pos + 1] == '*') {
                s->pos += 2;
                while (s->pos + 1 < s->len) {
                    if (s->input[s->pos] == '*' && s->input[s->pos + 1] == '/') {
                        s->pos += 2;
                        break;
                    }
                    s->pos++;
                }
            } else {
                break;
            }
        } else {
            break;
        }
    }
}

/* Scan a JSON string (double-quoted). Returns length written to buf. */
static int scan_string(MdbScanner *s, char *buf, int bufsize)
{
    int out = 0;
    s->pos++; /* skip opening quote */

    while (s->pos < s->len) {
        char c = s->input[s->pos];
        if (c == '"') {
            s->pos++;
            buf[out] = '\0';
            return out;
        }
        if (c == '\\') {
            s->pos++;
            if (s->pos >= s->len) {
                s->error = "unterminated string escape";
                return -1;
            }
            c = s->input[s->pos];
            switch (c) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'u':
                    /* Simplified: store \uXXXX literally */
                    if (out + 6 < bufsize) {
                        buf[out++] = '\\';
                        buf[out++] = 'u';
                        s->pos++;
                        for (int i = 0; i < 4 && s->pos < s->len; i++) {
                            buf[out++] = s->input[s->pos++];
                        }
                        continue;
                    }
                    s->error = "string too long";
                    return -1;
                default:
                    s->error = "invalid escape character";
                    return -1;
            }
        }
        if (out < bufsize - 1)
            buf[out++] = c;
        s->pos++;
    }

    s->error = "unterminated string";
    return -1;
}

/* Operator keyword lookup table */
typedef struct {
    const char *name;
    int         token;
} MdbKeyword;

static const MdbKeyword dollar_keywords[] = {
    /* Comparison */
    { "$eq",            OP_EQ },
    { "$ne",            OP_NE },
    { "$gt",            OP_GT },
    { "$gte",           OP_GTE },
    { "$lt",            OP_LT },
    { "$lte",           OP_LTE },
    { "$in",            OP_IN },
    { "$nin",           OP_NIN },

    /* Logical */
    { "$and",           OP_AND },
    { "$or",            OP_OR },
    { "$not",           OP_NOT },
    { "$nor",           OP_NOR },

    /* Element */
    { "$exists",        OP_EXISTS },
    { "$type",          OP_TYPE },

    /* Evaluation */
    { "$regex",         OP_REGEX },
    { "$options",       OP_OPTIONS },
    { "$mod",           OP_MOD },

    /* Array query */
    { "$all",           OP_ALL },
    { "$elemMatch",     OP_ELEMMATCH },
    { "$size",          OP_SIZE },

    /* Update -- field */
    { "$set",           UP_SET },
    { "$unset",         UP_UNSET },
    { "$inc",           UP_INC },
    { "$mul",           UP_MUL },
    { "$rename",        UP_RENAME },
    { "$min",           UP_MIN },
    { "$max",           UP_MAX },
    { "$currentDate",   UP_CURRENTDATE },
    { "$setOnInsert",   UP_SETONINSERT },

    /* Update -- array */
    { "$push",          UP_PUSH },
    { "$pull",          UP_PULL },
    { "$addToSet",      UP_ADDTOSET },
    { "$pop",           UP_POP },

    /* Aggregation stages */
    { "$match",         AGG_MATCH },
    { "$group",         AGG_GROUP },
    { "$project",       AGG_PROJECT },
    { "$sort",          AGG_SORT },
    { "$limit",         AGG_LIMIT },
    { "$skip",          AGG_SKIP },
    { "$unwind",        AGG_UNWIND },
    { "$lookup",        AGG_LOOKUP },
    { "$out",           AGG_OUT },
    { "$count",         AGG_COUNT },

    /* Aggregation accumulator expressions */
    { "$sum",           AGG_SUM },
    { "$avg",           AGG_AVG },
    { "$first",         AGG_FIRST },
    { "$last",          AGG_LAST },

    { NULL, 0 }
};

static int lookup_dollar_keyword(const char *s, int len)
{
    for (const MdbKeyword *kw = dollar_keywords; kw->name; kw++) {
        if ((int)strlen(kw->name) == len && strncmp(kw->name, s, len) == 0)
            return kw->token;
    }
    return 0;
}

int mdb_scan(MdbScanner *s, MdbToken *val)
{
    memset(val, 0, sizeof(*val));
    skip_ws(s);

    if (s->pos >= s->len)
        return 0; /* EOF */

    char c = s->input[s->pos];

    /* Single-character tokens */
    switch (c) {
        case '{': s->pos++; return LBRACE;
        case '}': s->pos++; return RBRACE;
        case '[': s->pos++; return LBRACKET;
        case ']': s->pos++; return RBRACKET;
        case ':': s->pos++; return COLON;
        case ',': s->pos++; return COMMA;
        case '(': s->pos++; return LPAREN;
        case ')': s->pos++; return RPAREN;
        case '.': s->pos++; return DOT;
    }

    /* String */
    if (c == '"') {
        int slen = scan_string(s, s->strbuf, sizeof(s->strbuf));
        if (slen < 0)
            return -1;

        /* Check if the string content is a $-operator when used as key */
        val->str.val = strdup(s->strbuf);
        val->str.len = slen;

        /* If the string starts with '$', check for operator keyword */
        if (slen > 1 && s->strbuf[0] == '$') {
            int tok = lookup_dollar_keyword(s->strbuf, slen);
            if (tok)
                return tok;
            return DOLLAR_IDENT;
        }

        return STRING;
    }

    /* Single-quoted strings (MongoDB shell allows them) */
    if (c == '\'') {
        s->pos++; /* skip opening quote */
        int out = 0;
        while (s->pos < s->len && s->input[s->pos] != '\'') {
            if (s->input[s->pos] == '\\' && s->pos + 1 < s->len) {
                s->pos++;
            }
            if (out < (int)sizeof(s->strbuf) - 1)
                s->strbuf[out++] = s->input[s->pos];
            s->pos++;
        }
        if (s->pos < s->len)
            s->pos++; /* skip closing quote */
        s->strbuf[out] = '\0';
        val->str.val = strdup(s->strbuf);
        val->str.len = out;

        if (out > 1 && s->strbuf[0] == '$') {
            int tok = lookup_dollar_keyword(s->strbuf, out);
            if (tok)
                return tok;
            return DOLLAR_IDENT;
        }

        return STRING;
    }

    /* Number */
    if (c == '-' || c == '+' || isdigit((unsigned char)c)) {
        const char *start = s->input + s->pos;
        char *end;
        bool is_float = false;

        if (c == '-' || c == '+')
            s->pos++;

        while (s->pos < s->len && isdigit((unsigned char)s->input[s->pos]))
            s->pos++;

        if (s->pos < s->len && s->input[s->pos] == '.') {
            is_float = true;
            s->pos++;
            while (s->pos < s->len && isdigit((unsigned char)s->input[s->pos]))
                s->pos++;
        }

        if (s->pos < s->len && (s->input[s->pos] == 'e' || s->input[s->pos] == 'E')) {
            is_float = true;
            s->pos++;
            if (s->pos < s->len && (s->input[s->pos] == '+' || s->input[s->pos] == '-'))
                s->pos++;
            while (s->pos < s->len && isdigit((unsigned char)s->input[s->pos]))
                s->pos++;
        }

        int nlen = (int)(s->input + s->pos - start);
        if (nlen > 0 && nlen < (int)sizeof(s->strbuf)) {
            memcpy(s->strbuf, start, nlen);
            s->strbuf[nlen] = '\0';
            val->str.val = strdup(s->strbuf);
            val->str.len = nlen;
        }

        if (is_float) {
            errno = 0;
            val->dval = strtod(start, &end);
            return FLOAT_LIT;
        } else {
            errno = 0;
            val->ival = strtoll(start, &end, 10);
            return INT_LIT;
        }
    }

    /* Keywords: true, false, null, and identifiers */
    if (isalpha((unsigned char)c) || c == '_') {
        int start = s->pos;
        while (s->pos < s->len &&
               (isalnum((unsigned char)s->input[s->pos]) ||
                s->input[s->pos] == '_'))
            s->pos++;
        int ilen = s->pos - start;

        if (ilen == 4 && strncmp(s->input + start, "true", 4) == 0)
            return KW_TRUE;
        if (ilen == 5 && strncmp(s->input + start, "false", 5) == 0)
            return KW_FALSE;
        if (ilen == 4 && strncmp(s->input + start, "null", 4) == 0)
            return KW_NULL;

        /* db.collection.method() syntax support */
        if (ilen == 2 && strncmp(s->input + start, "db", 2) == 0)
            return KW_DB;
        if (ilen == 4 && strncmp(s->input + start, "find", 4) == 0)
            return KW_FIND;
        if (ilen == 6 && strncmp(s->input + start, "update", 6) == 0)
            return KW_UPDATE;
        if (ilen == 9 && strncmp(s->input + start, "aggregate", 9) == 0)
            return KW_AGGREGATE;
        if (ilen == 8 && strncmp(s->input + start, "ObjectId", 8) == 0)
            return KW_OBJECTID;

        /* Generic identifier */
        if (ilen < (int)sizeof(s->strbuf)) {
            memcpy(s->strbuf, s->input + start, ilen);
            s->strbuf[ilen] = '\0';
            val->str.val = strdup(s->strbuf);
            val->str.len = ilen;
        }
        return IDENT;
    }

    /* Unquoted $-prefixed identifier (e.g., in aggregation field references) */
    if (c == '$') {
        int start = s->pos;
        s->pos++;
        while (s->pos < s->len &&
               (isalnum((unsigned char)s->input[s->pos]) ||
                s->input[s->pos] == '_'))
            s->pos++;
        int dlen = s->pos - start;

        if (dlen < (int)sizeof(s->strbuf)) {
            memcpy(s->strbuf, s->input + start, dlen);
            s->strbuf[dlen] = '\0';
            val->str.val = strdup(s->strbuf);
            val->str.len = dlen;
        }

        if (dlen > 1) {
            int tok = lookup_dollar_keyword(s->strbuf, dlen);
            if (tok)
                return tok;
        }

        return DOLLAR_IDENT;
    }

    s->error = "unexpected character";
    return -1;
}
