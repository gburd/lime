/*
 * tokenize.c
 *    Hand-written tokenizer for the XQuery 1.0 parser.
 *
 * Extends the XPath 1.0 tokenization rules with XQuery keywords
 * (for, let, where, order, by, return, if, then, else, some, every,
 * satisfies, declare, function, variable, namespace, etc.) and
 * additional operators (<<, >>, :=, ||).
 *
 * Follows the XPath/XQuery disambiguation rules from the W3C spec.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "tokenize.h"
#include "xquery.h"

struct XQScanner
{
    const char *input;
    int         len;
    int         pos;
    int         prev_token;
    const char *error_msg;
};

XQScanner *xq_scanner_create(const char *input, int len)
{
    XQScanner *s = (XQScanner *)calloc(1, sizeof(XQScanner));
    s->input = input;
    s->len = len;
    s->pos = 0;
    s->prev_token = 0;
    s->error_msg = NULL;
    return s;
}

void xq_scanner_destroy(XQScanner *scanner)
{
    free(scanner);
}

const char *xq_scanner_error(XQScanner *scanner)
{
    return scanner ? scanner->error_msg : NULL;
}

static int is_ncname_start(int c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static int is_ncname_char(int c)
{
    return is_ncname_start(c) ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '.';
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static void skip_ws(XQScanner *s)
{
    while (s->pos < s->len) {
        char c = s->input[s->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            s->pos++;
        } else if (c == '(' && s->pos + 1 < s->len && s->input[s->pos + 1] == ':') {
            /* XQuery comment (: ... :) */
            s->pos += 2;
            int depth = 1;
            while (s->pos + 1 < s->len && depth > 0) {
                if (s->input[s->pos] == '(' && s->input[s->pos + 1] == ':') {
                    depth++;
                    s->pos += 2;
                } else if (s->input[s->pos] == ':' && s->input[s->pos + 1] == ')') {
                    depth--;
                    s->pos += 2;
                } else {
                    s->pos++;
                }
            }
        } else {
            break;
        }
    }
}

/*
 * Is the previous token an "operand" token?
 * Used for disambiguation of * (wildcard vs multiply) and
 * / (path vs standalone root).
 */
static bool prev_is_operand(XQScanner *s)
{
    switch (s->prev_token) {
        case RPAREN:
        case RBRACKET:
        case LITERAL:
        case NUMBER:
        case NAME_TEST:
        case VARIABLE_REF:
        case DOT:
        case DOUBLE_DOT:
        case STAR:
            return true;
        default:
            return false;
    }
}

/* Scan a quoted string (single or double quotes). */
static int scan_string(XQScanner *s, char *buf, int bufsize)
{
    char quote = s->input[s->pos];
    s->pos++;
    int out = 0;

    while (s->pos < s->len) {
        char c = s->input[s->pos];
        if (c == quote) {
            /* Check for escaped quote (doubled) */
            if (s->pos + 1 < s->len && s->input[s->pos + 1] == quote) {
                if (out < bufsize - 1) buf[out++] = c;
                s->pos += 2;
                continue;
            }
            s->pos++;
            buf[out] = '\0';
            return out;
        }
        if (out < bufsize - 1) buf[out++] = c;
        s->pos++;
    }

    s->error_msg = "unterminated string literal";
    return -1;
}

/* Keyword lookup table */
typedef struct { const char *name; int token; } KW;

static const KW keywords[] = {
    /* XPath operators */
    { "or",         KW_OR },
    { "and",        KW_AND },
    { "div",        KW_DIV },
    { "mod",        KW_MOD },

    /* XQuery value comparison */
    { "eq",         KW_EQ },
    { "ne",         KW_NE },
    { "lt",         KW_LT },
    { "le",         KW_LE },
    { "gt",         KW_GT },
    { "ge",         KW_GE },

    /* XQuery keywords */
    { "for",        KW_FOR },
    { "let",        KW_LET },
    { "where",      KW_WHERE },
    { "order",      KW_ORDER },
    { "by",         KW_BY },
    { "return",     KW_RETURN },
    { "in",         KW_IN },
    { "at",         KW_AT_WORD },
    { "if",         KW_IF },
    { "then",       KW_THEN },
    { "else",       KW_ELSE },
    { "some",       KW_SOME },
    { "every",      KW_EVERY },
    { "satisfies",  KW_SATISFIES },
    { "is",         KW_IS },
    { "to",         KW_TO },
    { "union",      KW_UNION },
    { "intersect",  KW_INTERSECT },
    { "except",     KW_EXCEPT },
    { "instance",   KW_INSTANCE },
    { "of",         KW_OF },
    { "treat",      KW_TREAT },
    { "as",         KW_AS },
    { "castable",   KW_CASTABLE },
    { "cast",       KW_CAST },
    { "idiv",       KW_IDIV },

    /* Declaration keywords */
    { "declare",    KW_DECLARE },
    { "function",   KW_FUNCTION },
    { "variable",   KW_VARIABLE },
    { "namespace",  KW_NAMESPACE },
    { "default",    KW_DEFAULT },
    { "element",    KW_ELEMENT },
    { "import",     KW_IMPORT },
    { "module",     KW_MODULE },
    { "stable",     KW_STABLE },

    /* Computed constructors */
    { "attribute",  KW_ATTRIBUTE },
    { "document",   KW_DOCUMENT },
    { "text",       KW_TEXT },
    { "comment",    KW_COMMENT },
    { "processing-instruction", KW_PI },

    /* Sort order */
    { "ascending",  KW_ASCENDING },
    { "descending", KW_DESCENDING },

    /* Typeswitch */
    { "typeswitch", KW_TYPESWITCH },
    { "case",       KW_CASE },

    /* empty-sequence is handled specially */

    { NULL, 0 }
};

/* XPath axis lookup */
static const KW axis_names[] = {
    { "child",              AXIS_CHILD },
    { "descendant",         AXIS_DESCENDANT },
    { "parent",             AXIS_PARENT },
    { "ancestor",           AXIS_ANCESTOR },
    { "following-sibling",  AXIS_FOLLOWING_SIBLING },
    { "preceding-sibling",  AXIS_PRECEDING_SIBLING },
    { "following",          AXIS_FOLLOWING },
    { "preceding",          AXIS_PRECEDING },
    { "attribute",          AXIS_ATTRIBUTE },
    { "namespace",          AXIS_NAMESPACE },
    { "self",               AXIS_SELF },
    { "descendant-or-self", AXIS_DESCENDANT_OR_SELF },
    { "ancestor-or-self",   AXIS_ANCESTOR_OR_SELF },
    { NULL, 0 }
};

/* Node type test names */
static const KW node_type_names[] = {
    { "comment",                  NODE_TYPE_COMMENT },
    { "text",                     NODE_TYPE_TEXT },
    { "processing-instruction",   NODE_TYPE_PI },
    { "node",                     NODE_TYPE_NODE },
    { NULL, 0 }
};

static int lookup_keyword(const char *s, int len, const KW *table)
{
    for (const KW *kw = table; kw->name; kw++) {
        if ((int)strlen(kw->name) == len && strncmp(kw->name, s, len) == 0)
            return kw->token;
    }
    return 0;
}

static char strbuf[4096];

int xq_scan(XQScanner *s, XQToken *val)
{
    memset(val, 0, sizeof(*val));
    skip_ws(s);

    if (s->pos >= s->len) return 0;

    char c = s->input[s->pos];

    /* Two-character operators */
    if (s->pos + 1 < s->len) {
        char c2 = s->input[s->pos + 1];

        if (c == ':' && c2 == '=') { s->pos += 2; s->prev_token = ASSIGN; return ASSIGN; }
        if (c == '!' && c2 == '=') { s->pos += 2; s->prev_token = NOT_EQUALS; return NOT_EQUALS; }
        if (c == '<' && c2 == '=') { s->pos += 2; s->prev_token = LESS_EQUAL; return LESS_EQUAL; }
        if (c == '>' && c2 == '=') { s->pos += 2; s->prev_token = GREATER_EQUAL; return GREATER_EQUAL; }
        if (c == '<' && c2 == '<') { s->pos += 2; s->prev_token = NODE_PRECEDES; return NODE_PRECEDES; }
        if (c == '>' && c2 == '>') { s->pos += 2; s->prev_token = NODE_FOLLOWS; return NODE_FOLLOWS; }
        if (c == '/' && c2 == '/') { s->pos += 2; s->prev_token = DOUBLE_SLASH; return DOUBLE_SLASH; }
        if (c == '.' && c2 == '.') { s->pos += 2; s->prev_token = DOUBLE_DOT; return DOUBLE_DOT; }
        if (c == ':' && c2 == ':') { s->pos += 2; s->prev_token = COLON_COLON; return COLON_COLON; }
        if (c == '|' && c2 == '|') { s->pos += 2; s->prev_token = CONCAT_OP; return CONCAT_OP; }
    }

    /* Single-character operators */
    switch (c) {
        case '/': s->pos++; s->prev_token = SLASH; return SLASH;
        case '.': s->pos++; s->prev_token = DOT; return DOT;
        case '@': s->pos++; s->prev_token = AT; return AT;
        case ',': s->pos++; s->prev_token = COMMA; return COMMA;
        case '(': s->pos++; s->prev_token = LPAREN; return LPAREN;
        case ')': s->pos++; s->prev_token = RPAREN; return RPAREN;
        case '[': s->pos++; s->prev_token = LBRACKET; return LBRACKET;
        case ']': s->pos++; s->prev_token = RBRACKET; return RBRACKET;
        case '{': s->pos++; s->prev_token = LBRACE; return LBRACE;
        case '}': s->pos++; s->prev_token = RBRACE; return RBRACE;
        case '+': s->pos++; s->prev_token = PLUS; return PLUS;
        case '-': s->pos++; s->prev_token = MINUS; return MINUS;
        case '=': s->pos++; s->prev_token = EQUALS; return EQUALS;
        case '<': s->pos++; s->prev_token = LESS_THAN; return LESS_THAN;
        case '>': s->pos++; s->prev_token = GREATER_THAN; return GREATER_THAN;
        case '|': s->pos++; s->prev_token = PIPE; return PIPE;
        case ';': s->pos++; s->prev_token = SEMICOLON; return SEMICOLON;
    }

    /* Multiply operator vs wildcard */
    if (c == '*') {
        s->pos++;
        if (prev_is_operand(s)) {
            s->prev_token = STAR;
            return STAR;
        }
        s->prev_token = STAR;
        return STAR;
    }

    /* String literal */
    if (c == '"' || c == '\'') {
        int slen = scan_string(s, strbuf, sizeof(strbuf));
        if (slen < 0) return -1;
        val->str.val = strdup(strbuf);
        val->str.len = slen;
        s->prev_token = LITERAL;
        return LITERAL;
    }

    /* Number */
    if (is_digit((unsigned char)c) ||
        (c == '.' && s->pos + 1 < s->len && is_digit((unsigned char)s->input[s->pos + 1]))) {
        const char *start = s->input + s->pos;
        char *end;

        while (s->pos < s->len && is_digit((unsigned char)s->input[s->pos]))
            s->pos++;
        if (s->pos < s->len && s->input[s->pos] == '.') {
            s->pos++;
            while (s->pos < s->len && is_digit((unsigned char)s->input[s->pos]))
                s->pos++;
        }
        /* XQuery also allows 'e' exponent */
        if (s->pos < s->len && (s->input[s->pos] == 'e' || s->input[s->pos] == 'E')) {
            s->pos++;
            if (s->pos < s->len && (s->input[s->pos] == '+' || s->input[s->pos] == '-'))
                s->pos++;
            while (s->pos < s->len && is_digit((unsigned char)s->input[s->pos]))
                s->pos++;
        }

        val->numval = strtod(start, &end);

        int nlen = (int)(s->input + s->pos - start);
        if (nlen > 0 && nlen < (int)sizeof(strbuf)) {
            memcpy(strbuf, start, nlen);
            strbuf[nlen] = '\0';
            val->str.val = strdup(strbuf);
            val->str.len = nlen;
        }

        s->prev_token = NUMBER;
        return NUMBER;
    }

    /* Variable reference: $QName */
    if (c == '$') {
        s->pos++;
        int start = s->pos;
        if (s->pos < s->len && is_ncname_start((unsigned char)s->input[s->pos])) {
            s->pos++;
            while (s->pos < s->len && (is_ncname_char((unsigned char)s->input[s->pos]) ||
                                        s->input[s->pos] == ':'))
                s->pos++;
        }
        int vlen = s->pos - start;
        if (vlen > 0) {
            val->str.val = strndup(s->input + start, vlen);
            val->str.len = vlen;
            s->prev_token = VARIABLE_REF;
            return VARIABLE_REF;
        }
        s->error_msg = "expected variable name after $";
        return -1;
    }

    /* NCName -- could be axis, node-type, keyword, function name, or name test */
    if (is_ncname_start((unsigned char)c)) {
        int start = s->pos;
        s->pos++;
        while (s->pos < s->len && is_ncname_char((unsigned char)s->input[s->pos]))
            s->pos++;

        int nlen = s->pos - start;
        const char *nstart = s->input + start;

        /* Check for compound names with hyphens: following-sibling, etc. */
        /* Save position; check if "xxx-yyy" forms an axis name */
        int save_pos = s->pos;
        while (s->pos < s->len && s->input[s->pos] == '-') {
            int dash_pos = s->pos;
            s->pos++;
            if (s->pos < s->len && is_ncname_start((unsigned char)s->input[s->pos])) {
                while (s->pos < s->len && is_ncname_char((unsigned char)s->input[s->pos]))
                    s->pos++;
            } else {
                s->pos = dash_pos;
                break;
            }
            /* Check if extended name is a known axis/node-type */
            int elen = s->pos - start;
            int tok = lookup_keyword(nstart, elen, axis_names);
            if (tok) {
                nlen = elen;
                save_pos = s->pos;
            } else {
                tok = lookup_keyword(nstart, elen, node_type_names);
                if (tok) {
                    nlen = elen;
                    save_pos = s->pos;
                } else {
                    /* Also check keywords like "processing-instruction", "empty-sequence" */
                    tok = lookup_keyword(nstart, elen, keywords);
                    if (tok) {
                        nlen = elen;
                        save_pos = s->pos;
                    } else {
                        s->pos = dash_pos;
                        break;
                    }
                }
            }
        }
        s->pos = save_pos;

        /* Look ahead for :: (axis specifier) */
        int la = s->pos;
        while (la < s->len && (s->input[la] == ' ' || s->input[la] == '\t'))
            la++;

        if (la + 1 < s->len && s->input[la] == ':' && s->input[la + 1] == ':') {
            int tok = lookup_keyword(nstart, nlen, axis_names);
            if (tok) {
                val->str.val = strndup(nstart, nlen);
                val->str.len = nlen;
                s->prev_token = tok;
                return tok;
            }
        }

        /* Look ahead for ( (function call or node type test) */
        if (la < s->len && s->input[la] == '(') {
            int tok = lookup_keyword(nstart, nlen, node_type_names);
            if (tok) {
                val->str.val = strndup(nstart, nlen);
                val->str.len = nlen;
                s->prev_token = tok;
                return tok;
            }
            /* Check for XQuery keywords that can precede '(' */
            tok = lookup_keyword(nstart, nlen, keywords);
            if (tok) {
                val->str.val = strndup(nstart, nlen);
                val->str.len = nlen;
                s->prev_token = tok;
                return tok;
            }
            /* Treat as function name */
            val->str.val = strndup(nstart, nlen);
            val->str.len = nlen;
            s->prev_token = FUNCTION_NAME;
            return FUNCTION_NAME;
        }

        /* Check for keywords (not followed by ':' which would make it a QName prefix) */
        if (!(s->pos < s->len && s->input[s->pos] == ':')) {
            int tok = lookup_keyword(nstart, nlen, keywords);
            if (tok) {
                val->str.val = strndup(nstart, nlen);
                val->str.len = nlen;
                s->prev_token = tok;
                return tok;
            }
        }

        /* Check for NCName:NCName (qualified name) or NCName:* */
        if (s->pos < s->len && s->input[s->pos] == ':') {
            s->pos++;
            if (s->pos < s->len && s->input[s->pos] == '*') {
                /* NCName:* wildcard */
                s->pos++;
                nlen = s->pos - start;
            } else if (s->pos < s->len && is_ncname_start((unsigned char)s->input[s->pos])) {
                s->pos++;
                while (s->pos < s->len && is_ncname_char((unsigned char)s->input[s->pos]))
                    s->pos++;
                nlen = s->pos - start;
            } else {
                /* Just a colon after the name, push it back */
                s->pos--;
            }
        }

        /* After qualified name expansion, check for '(' (function call) */
        la = s->pos;
        while (la < s->len && (s->input[la] == ' ' || s->input[la] == '\t'))
            la++;
        if (la < s->len && s->input[la] == '(') {
            val->str.val = strndup(nstart, nlen);
            val->str.len = nlen;
            s->prev_token = FUNCTION_NAME;
            return FUNCTION_NAME;
        }

        val->str.val = strndup(nstart, nlen);
        val->str.len = nlen;
        s->prev_token = NAME_TEST;
        return NAME_TEST;
    }

    s->error_msg = "unexpected character";
    return -1;
}
