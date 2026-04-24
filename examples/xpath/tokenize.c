/*
 * tokenize.c
 *    XPath 1.0 tokenizer (lexer).
 *
 * Implements the XPath 1.0 tokenization rules from the W3C Recommendation,
 * Section 3.7 "Lexical Structure".
 *
 * The XPath lexer has context-dependent disambiguation rules:
 *
 *   - If the token before a NCName is an operator or nothing, and the
 *     NCName is followed by '::', it is an AxisName.
 *   - If the token before a NCName is an operator or nothing, and the
 *     NCName is followed by '(', it is a NodeType or FunctionName.
 *   - '*' is a multiply operator if the previous token was a closing
 *     bracket, paren, number, literal, or NCName (i.e., an operand).
 *     Otherwise it is a name test wildcard.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tokenize.h"
#include "xpath.h"

/* ======================================================================
 * Scanner state
 * ====================================================================== */

struct XPathScanner
{
    const char *input;
    int         len;
    int         pos;
    int         prev_token;     /* token type of the previously returned token */
    const char *error_msg;
};

/* ======================================================================
 * Character classification helpers
 * ====================================================================== */

static int is_ncname_start(int c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
    /* Full XPath/XML allows Unicode letters too; we handle ASCII subset */
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

static void skip_whitespace(XPathScanner *s)
{
    while (s->pos < s->len) {
        char c = s->input[s->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            s->pos++;
        else
            break;
    }
}

/*
 * Peek at a character at offset from current position.
 * Returns 0 for out-of-bounds.  Currently unused but retained for
 * potential tokenizer extensions.
 */
#if 0
static int peek(XPathScanner *s, int offset)
{
    int p = s->pos + offset;
    if (p >= 0 && p < s->len)
        return (unsigned char)s->input[p];
    return 0;
}
#endif

/*
 * Determine if the previous token indicates the current context is
 * an "operator position".  In operator position:
 *   - NCName can be an axis name or node type
 *   - '*' is a wildcard name test
 *
 * NOT in operator position (previous token is an operand):
 *   - NCName after an operand is not possible in valid XPath
 *   - '*' is the multiply operator
 *
 * Per XPath spec Section 3.7:
 * A token is NOT preceded by an operator when it follows:
 *   @, ::, (, [, ,, or an Operator (and, or, mod, div, *, /, //, |,
 *   +, -, =, !=, <, <=, >, >=)
 * Or when it is the first token.
 */
static int is_operator_context(int prev_token)
{
    switch (prev_token) {
        case 0:                 /* start of expression */
        case AT:
        case COLON_COLON:
        case LPAREN:
        case LBRACKET:
        case COMMA:
        case KW_AND:
        case KW_OR:
        case KW_DIV:
        case KW_MOD:
        case STAR:              /* when STAR was a multiply op, but we recheck */
        case SLASH:
        case DOUBLE_SLASH:
        case PIPE:
        case PLUS:
        case MINUS:
        case EQUALS:
        case NOT_EQUALS:
        case LESS_THAN:
        case LESS_EQUAL:
        case GREATER_THAN:
        case GREATER_EQUAL:
            return 1;
        default:
            return 0;
    }
}

/* ======================================================================
 * Axis name lookup
 * ====================================================================== */

typedef struct {
    const char *name;
    int         len;
    int         token;
} AxisEntry;

static const AxisEntry axis_table[] = {
    {"ancestor",            8,  AXIS_ANCESTOR},
    {"ancestor-or-self",   16,  AXIS_ANCESTOR_OR_SELF},
    {"attribute",           9,  AXIS_ATTRIBUTE},
    {"child",               5,  AXIS_CHILD},
    {"descendant",         10,  AXIS_DESCENDANT},
    {"descendant-or-self", 18,  AXIS_DESCENDANT_OR_SELF},
    {"following",           9,  AXIS_FOLLOWING},
    {"following-sibling",  17,  AXIS_FOLLOWING_SIBLING},
    {"namespace",           9,  AXIS_NAMESPACE},
    {"parent",              6,  AXIS_PARENT},
    {"preceding",           9,  AXIS_PRECEDING},
    {"preceding-sibling",  17,  AXIS_PRECEDING_SIBLING},
    {"self",                4,  AXIS_SELF},
    {NULL, 0, 0}
};

static int lookup_axis(const char *name, int len)
{
    for (const AxisEntry *e = axis_table; e->name != NULL; e++) {
        if (e->len == len && memcmp(e->name, name, len) == 0)
            return e->token;
    }
    return 0;
}

/* ======================================================================
 * Node type keyword lookup
 * ====================================================================== */

typedef struct {
    const char *name;
    int         len;
    int         token;
} NodeTypeEntry;

static const NodeTypeEntry node_type_table[] = {
    {"comment",                  7, NODE_TYPE_COMMENT},
    {"text",                     4, NODE_TYPE_TEXT},
    {"processing-instruction",  22, NODE_TYPE_PI},
    {"node",                     4, NODE_TYPE_NODE},
    {NULL, 0, 0}
};

static int lookup_node_type(const char *name, int len)
{
    for (const NodeTypeEntry *e = node_type_table; e->name != NULL; e++) {
        if (e->len == len && memcmp(e->name, name, len) == 0)
            return e->token;
    }
    return 0;
}

/* ======================================================================
 * Operator keyword lookup
 * ====================================================================== */

static int lookup_operator_keyword(const char *name, int len)
{
    if (len == 2 && memcmp(name, "or", 2) == 0) return KW_OR;
    if (len == 3 && memcmp(name, "and", 3) == 0) return KW_AND;
    if (len == 3 && memcmp(name, "div", 3) == 0) return KW_DIV;
    if (len == 3 && memcmp(name, "mod", 3) == 0) return KW_MOD;
    return 0;
}

/* ======================================================================
 * Scanner API
 * ====================================================================== */

XPathScanner *
xpath_scanner_create(const char *input, int len)
{
    XPathScanner *s = (XPathScanner *)calloc(1, sizeof(XPathScanner));
    s->input = input;
    s->len = len;
    s->pos = 0;
    s->prev_token = 0;
    s->error_msg = NULL;
    return s;
}

void
xpath_scanner_destroy(XPathScanner *scanner)
{
    free(scanner);
}

const char *
xpath_scanner_error(XPathScanner *scanner)
{
    return scanner ? scanner->error_msg : NULL;
}

/*
 * Scan an NCName starting at s->pos.
 * An NCName (Non-Colonized Name) is: NCNameStartChar (NCNameChar)*
 * For XPath, we also handle names with hyphens (following-sibling, etc.)
 * but NOT colons -- colons are separate for the :: axis separator
 * and for namespace prefixes.
 */
static int scan_ncname(XPathScanner *s, const char **start)
{
    int p = s->pos;
    if (p >= s->len || !is_ncname_start((unsigned char)s->input[p]))
        return 0;
    *start = &s->input[p];
    p++;
    while (p < s->len && is_ncname_char((unsigned char)s->input[p]))
        p++;
    /*
     * NCName should not end with '-' or '.' (those are only valid as
     * interior characters in the name). However, for axis names like
     * "following-sibling" the '-' is interior, which is correct.
     * The scan greedily takes the longest NCName.
     */
    return p - s->pos;
}

/*
 * After scanning the first NCName, check for ':' followed by NCName or '*'
 * to handle prefixed name tests like "prefix:localname" or "prefix:*".
 */
static int scan_qname_or_wildcard(XPathScanner *s, const char **start,
                                  int ncname_len)
{
    (void)start;  /* start was set by scan_ncname; we only extend the length */
    int total = ncname_len;
    int p = s->pos + ncname_len;

    if (p < s->len && s->input[p] == ':') {
        /* Check for :: (axis separator) -- do NOT consume */
        if (p + 1 < s->len && s->input[p + 1] == ':')
            return total;

        /* prefix:* */
        if (p + 1 < s->len && s->input[p + 1] == '*') {
            total = (p + 2) - s->pos;
            return total;
        }

        /* prefix:localname */
        if (p + 1 < s->len && is_ncname_start((unsigned char)s->input[p + 1])) {
            int q = p + 2;
            while (q < s->len && is_ncname_char((unsigned char)s->input[q]))
                q++;
            total = q - s->pos;
            return total;
        }
    }

    return total;
}

int
xpath_scan(XPathScanner *s, XPathToken *val)
{
    memset(val, 0, sizeof(*val));
    skip_whitespace(s);

    if (s->pos >= s->len)
        return 0;  /* EOF */

    int c = (unsigned char)s->input[s->pos];
    int tok;

    /* ---- Single and double character operators ---- */

    switch (c) {
        case '(':
            s->pos++;
            tok = LPAREN;
            goto done;
        case ')':
            s->pos++;
            tok = RPAREN;
            goto done;
        case '[':
            s->pos++;
            tok = LBRACKET;
            goto done;
        case ']':
            s->pos++;
            tok = RBRACKET;
            goto done;
        case ',':
            s->pos++;
            tok = COMMA;
            goto done;
        case '+':
            s->pos++;
            tok = PLUS;
            goto done;
        case '-':
            /*
             * '-' could be part of a name like "following-sibling".
             * But at the top level of scan dispatch, if we got here
             * then the character is not part of an NCName in progress.
             * So it is the minus operator.
             */
            s->pos++;
            tok = MINUS;
            goto done;
        case '|':
            s->pos++;
            tok = PIPE;
            goto done;
        case '=':
            s->pos++;
            tok = EQUALS;
            goto done;
        case '@':
            s->pos++;
            tok = AT;
            goto done;
        case '/':
            if (s->pos + 1 < s->len && s->input[s->pos + 1] == '/') {
                s->pos += 2;
                tok = DOUBLE_SLASH;
            } else {
                s->pos++;
                tok = SLASH;
            }
            goto done;
        case '!':
            if (s->pos + 1 < s->len && s->input[s->pos + 1] == '=') {
                s->pos += 2;
                tok = NOT_EQUALS;
                goto done;
            }
            s->error_msg = "unexpected character '!'";
            return -1;
        case '<':
            if (s->pos + 1 < s->len && s->input[s->pos + 1] == '=') {
                s->pos += 2;
                tok = LESS_EQUAL;
            } else {
                s->pos++;
                tok = LESS_THAN;
            }
            goto done;
        case '>':
            if (s->pos + 1 < s->len && s->input[s->pos + 1] == '=') {
                s->pos += 2;
                tok = GREATER_EQUAL;
            } else {
                s->pos++;
                tok = GREATER_THAN;
            }
            goto done;
        case '.':
            if (s->pos + 1 < s->len && s->input[s->pos + 1] == '.') {
                s->pos += 2;
                tok = DOUBLE_DOT;
                goto done;
            }
            /* Could be start of a number (.5) or a single dot */
            if (s->pos + 1 < s->len && is_digit((unsigned char)s->input[s->pos + 1])) {
                /* Fall through to number scanning */
                break;
            }
            s->pos++;
            tok = DOT;
            goto done;
        case '$': {
            /* Variable reference: $QName */
            s->pos++;  /* skip $ */
            const char *vstart;
            int vlen = scan_ncname(s, &vstart);
            if (vlen == 0) {
                s->error_msg = "expected variable name after '$'";
                return -1;
            }
            vlen = scan_qname_or_wildcard(s, &vstart, vlen);
            val->str.val = (char *)vstart;
            val->str.len = vlen;
            s->pos += vlen;
            tok = VARIABLE_REF;
            goto done;
        }
        case '*':
            /* Disambiguation: multiply operator vs. wildcard */
            s->pos++;
            if (!is_operator_context(s->prev_token)) {
                tok = STAR;  /* multiply */
            } else {
                tok = STAR;  /* wildcard name test -- same token, parser handles it */
            }
            goto done;
        default:
            break;
    }

    /* ---- String literals ---- */

    if (c == '"' || c == '\'') {
        int quote = c;
        s->pos++;  /* skip opening quote */
        int start = s->pos;
        while (s->pos < s->len && s->input[s->pos] != quote)
            s->pos++;
        if (s->pos >= s->len) {
            s->error_msg = "unterminated string literal";
            return -1;
        }
        val->str.val = (char *)&s->input[start];
        val->str.len = s->pos - start;
        s->pos++;  /* skip closing quote */
        tok = LITERAL;
        goto done;
    }

    /* ---- Numbers ---- */

    if (is_digit(c) || (c == '.' && s->pos + 1 < s->len &&
                        is_digit((unsigned char)s->input[s->pos + 1]))) {
        int start = s->pos;
        /* Integer part */
        while (s->pos < s->len && is_digit((unsigned char)s->input[s->pos]))
            s->pos++;
        /* Decimal part */
        if (s->pos < s->len && s->input[s->pos] == '.') {
            s->pos++;
            while (s->pos < s->len && is_digit((unsigned char)s->input[s->pos]))
                s->pos++;
        }
        /* Convert to double */
        char buf[64];
        int numlen = s->pos - start;
        if (numlen >= (int)sizeof(buf)) numlen = (int)sizeof(buf) - 1;
        memcpy(buf, &s->input[start], numlen);
        buf[numlen] = '\0';
        val->numval = strtod(buf, NULL);
        tok = NUMBER;
        goto done;
    }

    /* ---- NCName-based tokens ---- */

    if (is_ncname_start(c)) {
        const char *name_start;
        int name_len = scan_ncname(s, &name_start);

        if (name_len == 0) {
            s->error_msg = "internal error: expected NCName";
            return -1;
        }

        /*
         * Now we need context-dependent disambiguation (XPath 1.0 Section 3.7):
         *
         * 1. If followed by '::', it is an AxisName
         * 2. If followed by '(', it is a NodeType or FunctionName
         * 3. If preceded by an operator (or nothing), it could be:
         *    a. An operator keyword (and, or, div, mod) -- only in NON-operator context
         *    b. A NameTest
         * 4. Otherwise it is a NameTest
         */

        /* Save position and look ahead past any whitespace for '::' or '(' */
        int saved_pos = s->pos;
        int ahead = s->pos + name_len;

        /* Skip whitespace for lookahead */
        while (ahead < s->len &&
               (s->input[ahead] == ' ' || s->input[ahead] == '\t' ||
                s->input[ahead] == '\n' || s->input[ahead] == '\r'))
            ahead++;

        /* Check for :: (axis name) */
        if (ahead + 1 < s->len && s->input[ahead] == ':' && s->input[ahead + 1] == ':') {
            int axis_tok = lookup_axis(name_start, name_len);
            if (axis_tok) {
                s->pos += name_len;
                /* skip whitespace to the :: */
                skip_whitespace(s);
                /* consume :: */
                s->pos += 2;
                tok = axis_tok;
                /* Return COLON_COLON is handled implicitly by grammar
                 * which expects axis_name COLON_COLON.  But our grammar
                 * has axis tokens that already include the axis semantics.
                 * The grammar has: axis_specifier ::= axis_name COLON_COLON
                 * So we return the axis token and then COLON_COLON separately.
                 *
                 * Actually, let's return axis_name first, then the next call
                 * will return COLON_COLON.
                 */
                /* Undo: just return axis name, let :: be returned next time */
                s->pos = saved_pos + name_len;
                tok = axis_tok;
                goto done;
            }
            /* Not a recognized axis name -- treat as name test */
        }

        /* Check for '(' (node type test or function call) */
        if (ahead < s->len && s->input[ahead] == '(') {
            /* Check if it's a node type */
            int nt_tok = lookup_node_type(name_start, name_len);
            if (nt_tok) {
                s->pos += name_len;
                tok = nt_tok;
                goto done;
            }
            /* It is a function name */
            val->str.val = (char *)name_start;
            val->str.len = name_len;
            s->pos += name_len;
            tok = FUNCTION_NAME;
            goto done;
        }

        /*
         * Not followed by :: or (.
         * If we are NOT in operator context (previous token was an operand),
         * check for operator keywords.
         */
        if (!is_operator_context(s->prev_token)) {
            int kw_tok = lookup_operator_keyword(name_start, name_len);
            if (kw_tok) {
                s->pos += name_len;
                tok = kw_tok;
                goto done;
            }
        }

        /*
         * It is a NameTest.  Could be NCName, NCName:NCName, or NCName:*.
         */
        int qname_len = scan_qname_or_wildcard(s, &name_start, name_len);
        val->str.val = (char *)name_start;
        val->str.len = qname_len;
        s->pos += qname_len;
        tok = NAME_TEST;
        goto done;
    }

    /* ---- Colon (for ::) ---- */

    if (c == ':' && s->pos + 1 < s->len && s->input[s->pos + 1] == ':') {
        s->pos += 2;
        tok = COLON_COLON;
        goto done;
    }

    /* ---- Unrecognized character ---- */

    s->error_msg = "unexpected character";
    return -1;

done:
    s->prev_token = tok;
    return tok;
}
