/*
 * tokenize.c
 *    Hand-written JSONPath tokenizer for the Lime-generated parser.
 *
 * Replaces the Flex-generated scanner from PostgreSQL's jsonpath_scan.l.
 * Implements the same lexical rules: quoted/unquoted strings, variables,
 * numbers (decimal/hex/octal/binary/real), multi-character operators,
 * C-style comments, and keyword recognition.
 *
 * Original Copyright (c) 2019-2026, PostgreSQL Global Development Group
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "tokenize.h"

/* ======================================================================
 * Keyword table
 *
 * Sorted by length then alphabetically, matching the original.
 * "lowercase" means the keyword must be compared case-sensitively
 * (for null, true, false which are JSON literals).
 * ====================================================================== */

typedef struct JpKeyword
{
    int         len;
    bool        lowercase;
    int         token;
    const char *keyword;
} JpKeyword;

static const JpKeyword keywords[] = {
    { 2, false, IS_P,          "is"},
    { 2, false, TO_P,          "to"},
    { 3, false, ABS_P,         "abs"},
    { 3, false, LAX_P,         "lax"},
    { 4, false, DATE_P,        "date"},
    { 4, false, FLAG_P,        "flag"},
    { 4, false, LAST_P,        "last"},
    { 4, true,  NULL_P,        "null"},
    { 4, false, SIZE_P,        "size"},
    { 4, false, TIME_P,        "time"},
    { 4, true,  TRUE_P,        "true"},
    { 4, false, TYPE_P,        "type"},
    { 4, false, WITH_P,        "with"},
    { 5, true,  FALSE_P,       "false"},
    { 5, false, FLOOR_P,       "floor"},
    { 6, false, BIGINT_P,      "bigint"},
    { 6, false, DOUBLE_P,      "double"},
    { 6, false, EXISTS_P,      "exists"},
    { 6, false, NUMBER_P,      "number"},
    { 6, false, STARTS_P,      "starts"},
    { 6, false, STRICT_P,      "strict"},
    { 6, false, STRINGFUNC_P,  "string"},
    { 7, false, BOOLEAN_P,     "boolean"},
    { 7, false, CEILING_P,     "ceiling"},
    { 7, false, DECIMAL_P,     "decimal"},
    { 7, false, INTEGER_P,     "integer"},
    { 7, false, TIME_TZ_P,     "time_tz"},
    { 7, false, UNKNOWN_P,     "unknown"},
    { 8, false, DATETIME_P,    "datetime"},
    { 8, false, KEYVALUE_P,    "keyvalue"},
    { 9, false, TIMESTAMP_P,   "timestamp"},
    {10, false, LIKE_REGEX_P,  "like_regex"},
    {12, false, TIMESTAMP_TZ_P,"timestamp_tz"},
};

#define NUM_KEYWORDS (sizeof(keywords) / sizeof(keywords[0]))

/* ======================================================================
 * Scanner state
 * ====================================================================== */

struct JpScanner
{
    const char *input;
    int         length;
    int         pos;            /* current read position */

    /* String accumulation buffer */
    char       *buf;
    int         buf_len;
    int         buf_alloc;

    const char *error;
};

/* ======================================================================
 * Buffer helpers
 * ====================================================================== */

static void buf_init(JpScanner *s)
{
    s->buf_len = 0;
    if (s->buf_alloc == 0) {
        s->buf_alloc = 64;
        s->buf = (char *)malloc(s->buf_alloc);
    }
}

static void buf_ensure(JpScanner *s, int extra)
{
    while (s->buf_len + extra >= s->buf_alloc) {
        s->buf_alloc *= 2;
        s->buf = (char *)realloc(s->buf, s->buf_alloc);
    }
}

static void buf_addchar(JpScanner *s, char c)
{
    buf_ensure(s, 1);
    s->buf[s->buf_len++] = c;
}

static void buf_addstring(JpScanner *s, const char *str, int len)
{
    buf_ensure(s, len);
    memcpy(s->buf + s->buf_len, str, len);
    s->buf_len += len;
}

static char *buf_finish(JpScanner *s, int *out_len)
{
    buf_addchar(s, '\0');
    char *result = (char *)malloc(s->buf_len);
    memcpy(result, s->buf, s->buf_len);
    if (out_len)
        *out_len = s->buf_len - 1;  /* exclude NUL */
    return result;
}

/* ======================================================================
 * Character classification helpers
 * ====================================================================== */

static bool is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static bool is_special(char c)
{
    return c == '?' || c == '%' || c == '$' || c == '.' ||
           c == '[' || c == ']' || c == '{' || c == '}' ||
           c == '(' || c == ')' || c == '|' || c == '&' ||
           c == '!' || c == '=' || c == '<' || c == '>' ||
           c == '@' || c == '#' || c == ',' || c == '*' ||
           c == ':' || c == '-' || c == '+' || c == '/';
}

static bool is_other(char c)
{
    return c != '\0' && !is_special(c) && !is_blank(c) &&
           c != '\\' && c != '"';
}

static bool is_hexdigit(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ======================================================================
 * Keyword lookup (binary search matching the original)
 * ====================================================================== */

static int check_keyword(const char *str, int len)
{
    int lo = 0, hi = NUM_KEYWORDS;

    if (len > (int)keywords[NUM_KEYWORDS - 1].len)
        return IDENT_P;

    while (lo < hi) {
        int mid = lo + ((hi - lo) >> 1);
        int diff;

        if (keywords[mid].len == len) {
            /* Case-insensitive comparison */
            diff = 0;
            for (int i = 0; i < len; i++) {
                char a = keywords[mid].keyword[i];
                char b = str[i];
                if (b >= 'A' && b <= 'Z') b += ('a' - 'A');
                if (a != b) { diff = a - b; break; }
            }
        } else {
            diff = keywords[mid].len - len;
        }

        if (diff < 0)
            lo = mid + 1;
        else if (diff > 0)
            hi = mid;
        else {
            /* For lowercase-sensitive keywords, do exact compare */
            if (keywords[mid].lowercase) {
                if (strncmp(keywords[mid].keyword, str, len) != 0)
                    return IDENT_P;
            }
            return keywords[mid].token;
        }
    }

    return IDENT_P;
}

/* ======================================================================
 * Escape sequence handling
 * ====================================================================== */

static bool scan_escape(JpScanner *s)
{
    if (s->pos >= s->length) {
        s->error = "unexpected end after backslash";
        return false;
    }

    char c = s->input[s->pos++];
    switch (c) {
        case 'b': buf_addchar(s, '\b'); return true;
        case 'f': buf_addchar(s, '\f'); return true;
        case 'n': buf_addchar(s, '\n'); return true;
        case 'r': buf_addchar(s, '\r'); return true;
        case 't': buf_addchar(s, '\t'); return true;
        case 'v': buf_addchar(s, '\v'); return true;
        case 'u': {
            /* Unicode escape: \uXXXX or \u{XXXXXX} */
            if (s->pos >= s->length) {
                s->error = "invalid Unicode escape sequence";
                return false;
            }
            if (s->input[s->pos] == '{') {
                /* \u{XXXXXX} form */
                s->pos++;
                uint32_t ch = 0;
                int digits = 0;
                while (s->pos < s->length && s->input[s->pos] != '}') {
                    if (!is_hexdigit(s->input[s->pos])) {
                        s->error = "invalid Unicode escape sequence";
                        return false;
                    }
                    ch = (ch << 4) | hexval(s->input[s->pos++]);
                    digits++;
                    if (digits > 6) {
                        s->error = "invalid Unicode escape sequence";
                        return false;
                    }
                }
                if (s->pos >= s->length || s->input[s->pos] != '}') {
                    s->error = "invalid Unicode escape sequence";
                    return false;
                }
                s->pos++;  /* skip '}' */
                /* Simple ASCII-only handling for standalone parser */
                if (ch == 0) {
                    s->error = "\\u0000 cannot be converted to text";
                    return false;
                }
                if (ch < 0x80) {
                    buf_addchar(s, (char)ch);
                } else if (ch < 0x800) {
                    buf_addchar(s, (char)(0xC0 | (ch >> 6)));
                    buf_addchar(s, (char)(0x80 | (ch & 0x3F)));
                } else if (ch < 0x10000) {
                    buf_addchar(s, (char)(0xE0 | (ch >> 12)));
                    buf_addchar(s, (char)(0x80 | ((ch >> 6) & 0x3F)));
                    buf_addchar(s, (char)(0x80 | (ch & 0x3F)));
                } else if (ch < 0x110000) {
                    buf_addchar(s, (char)(0xF0 | (ch >> 18)));
                    buf_addchar(s, (char)(0x80 | ((ch >> 12) & 0x3F)));
                    buf_addchar(s, (char)(0x80 | ((ch >> 6) & 0x3F)));
                    buf_addchar(s, (char)(0x80 | (ch & 0x3F)));
                } else {
                    s->error = "invalid Unicode code point";
                    return false;
                }
            } else {
                /* \uXXXX form */
                if (s->pos + 4 > s->length) {
                    s->error = "invalid Unicode escape sequence";
                    return false;
                }
                uint32_t ch = 0;
                for (int i = 0; i < 4; i++) {
                    if (!is_hexdigit(s->input[s->pos])) {
                        s->error = "invalid Unicode escape sequence";
                        return false;
                    }
                    ch = (ch << 4) | hexval(s->input[s->pos++]);
                }
                /* Handle surrogate pairs */
                if (ch >= 0xD800 && ch <= 0xDBFF) {
                    /* High surrogate -- expect \uDCxx */
                    if (s->pos + 6 > s->length ||
                        s->input[s->pos] != '\\' ||
                        s->input[s->pos + 1] != 'u') {
                        s->error = "Unicode low surrogate must follow a high surrogate";
                        return false;
                    }
                    s->pos += 2;
                    uint32_t lo = 0;
                    for (int i = 0; i < 4; i++) {
                        if (!is_hexdigit(s->input[s->pos])) {
                            s->error = "invalid Unicode escape sequence";
                            return false;
                        }
                        lo = (lo << 4) | hexval(s->input[s->pos++]);
                    }
                    if (lo < 0xDC00 || lo > 0xDFFF) {
                        s->error = "Unicode low surrogate must follow a high surrogate";
                        return false;
                    }
                    ch = 0x10000 + ((ch - 0xD800) << 10) + (lo - 0xDC00);
                } else if (ch >= 0xDC00 && ch <= 0xDFFF) {
                    s->error = "Unicode low surrogate must follow a high surrogate";
                    return false;
                }
                if (ch == 0) {
                    s->error = "\\u0000 cannot be converted to text";
                    return false;
                }
                /* Encode as UTF-8 */
                if (ch < 0x80) {
                    buf_addchar(s, (char)ch);
                } else if (ch < 0x800) {
                    buf_addchar(s, (char)(0xC0 | (ch >> 6)));
                    buf_addchar(s, (char)(0x80 | (ch & 0x3F)));
                } else if (ch < 0x10000) {
                    buf_addchar(s, (char)(0xE0 | (ch >> 12)));
                    buf_addchar(s, (char)(0x80 | ((ch >> 6) & 0x3F)));
                    buf_addchar(s, (char)(0x80 | (ch & 0x3F)));
                } else {
                    buf_addchar(s, (char)(0xF0 | (ch >> 18)));
                    buf_addchar(s, (char)(0x80 | ((ch >> 12) & 0x3F)));
                    buf_addchar(s, (char)(0x80 | ((ch >> 6) & 0x3F)));
                    buf_addchar(s, (char)(0x80 | (ch & 0x3F)));
                }
            }
            return true;
        }
        case 'x': {
            /* Hex escape: \xHH */
            if (s->pos + 2 > s->length ||
                !is_hexdigit(s->input[s->pos]) ||
                !is_hexdigit(s->input[s->pos + 1])) {
                s->error = "invalid hexadecimal character sequence";
                return false;
            }
            int ch = (hexval(s->input[s->pos]) << 4) | hexval(s->input[s->pos + 1]);
            s->pos += 2;
            if (ch == 0) {
                s->error = "\\x00 cannot be converted to text";
                return false;
            }
            buf_addchar(s, (char)ch);
            return true;
        }
        default:
            buf_addchar(s, c);
            return true;
    }
}

/* ======================================================================
 * Number scanning
 * ====================================================================== */

static int scan_number(JpScanner *s, JsonPathToken *val)
{
    int start = s->pos;
    bool is_int = true;

    /* Check for hex/octal/binary prefix */
    if (s->input[s->pos] == '0' && s->pos + 1 < s->length) {
        char next = s->input[s->pos + 1];
        if (next == 'x' || next == 'X') {
            /* Hex integer */
            s->pos += 2;
            if (s->pos >= s->length || !is_hexdigit(s->input[s->pos])) {
                s->error = "invalid numeric literal";
                return -1;
            }
            while (s->pos < s->length &&
                   (is_hexdigit(s->input[s->pos]) || s->input[s->pos] == '_'))
                s->pos++;
            goto finish_int;
        }
        if (next == 'o' || next == 'O') {
            /* Octal integer */
            s->pos += 2;
            if (s->pos >= s->length || s->input[s->pos] < '0' || s->input[s->pos] > '7') {
                s->error = "invalid numeric literal";
                return -1;
            }
            while (s->pos < s->length &&
                   ((s->input[s->pos] >= '0' && s->input[s->pos] <= '7') ||
                    s->input[s->pos] == '_'))
                s->pos++;
            goto finish_int;
        }
        if (next == 'b' || next == 'B') {
            /* Binary integer */
            s->pos += 2;
            if (s->pos >= s->length || (s->input[s->pos] != '0' && s->input[s->pos] != '1')) {
                s->error = "invalid numeric literal";
                return -1;
            }
            while (s->pos < s->length &&
                   (s->input[s->pos] == '0' || s->input[s->pos] == '1' ||
                    s->input[s->pos] == '_'))
                s->pos++;
            goto finish_int;
        }
    }

    /* Decimal integer or floating point */
    /* Consume leading digits */
    while (s->pos < s->length &&
           (s->input[s->pos] >= '0' && s->input[s->pos] <= '9'))
        s->pos++;

    /* Check for underscore-separated digits */
    while (s->pos < s->length && s->input[s->pos] == '_') {
        s->pos++;
        if (s->pos >= s->length || s->input[s->pos] < '0' || s->input[s->pos] > '9') {
            s->error = "trailing junk after numeric literal";
            return -1;
        }
        while (s->pos < s->length &&
               (s->input[s->pos] >= '0' && s->input[s->pos] <= '9'))
            s->pos++;
    }

    /* Decimal point? */
    if (s->pos < s->length && s->input[s->pos] == '.') {
        is_int = false;
        s->pos++;
        while (s->pos < s->length &&
               ((s->input[s->pos] >= '0' && s->input[s->pos] <= '9') ||
                s->input[s->pos] == '_'))
            s->pos++;
    }

    /* Exponent? */
    if (s->pos < s->length && (s->input[s->pos] == 'e' || s->input[s->pos] == 'E')) {
        is_int = false;
        s->pos++;
        if (s->pos < s->length && (s->input[s->pos] == '+' || s->input[s->pos] == '-'))
            s->pos++;
        if (s->pos >= s->length || s->input[s->pos] < '0' || s->input[s->pos] > '9') {
            s->error = "invalid numeric literal";
            return -1;
        }
        while (s->pos < s->length &&
               ((s->input[s->pos] >= '0' && s->input[s->pos] <= '9') ||
                s->input[s->pos] == '_'))
            s->pos++;
    }

    /* Check for trailing junk */
    if (s->pos < s->length && is_other(s->input[s->pos])) {
        s->error = "trailing junk after numeric literal";
        return -1;
    }

finish_int:
    /* Check for trailing junk on non-decimal integers */
    if (is_int && s->pos < s->length && is_other(s->input[s->pos])) {
        s->error = "trailing junk after numeric literal";
        return -1;
    }

    {
        int len = s->pos - start;
        val->str.val = (char *)malloc(len + 1);
        memcpy(val->str.val, s->input + start, len);
        val->str.val[len] = '\0';
        val->str.len = len;
    }

    return is_int ? INT_P : NUMERIC_P;
}

/* ======================================================================
 * Public API
 * ====================================================================== */

JpScanner *jp_scanner_create(const char *input, int length)
{
    JpScanner *s = (JpScanner *)calloc(1, sizeof(JpScanner));
    if (!s) return NULL;
    s->input = input;
    s->length = (length > 0) ? length : (int)strlen(input);
    s->pos = 0;
    s->buf = NULL;
    s->buf_len = 0;
    s->buf_alloc = 0;
    s->error = NULL;
    return s;
}

void jp_scanner_destroy(JpScanner *s)
{
    if (!s) return;
    free(s->buf);
    free(s);
}

const char *jp_scanner_error(JpScanner *s)
{
    return s ? s->error : NULL;
}

int jp_scan(JpScanner *s, JsonPathToken *val)
{
    memset(val, 0, sizeof(*val));

    /* Skip whitespace */
restart:
    while (s->pos < s->length && is_blank(s->input[s->pos]))
        s->pos++;

    if (s->pos >= s->length)
        return 0;

    char c = s->input[s->pos];

    /* C-style comments */
    if (c == '/' && s->pos + 1 < s->length && s->input[s->pos + 1] == '*') {
        s->pos += 2;
        while (s->pos < s->length) {
            if (s->input[s->pos] == '*' && s->pos + 1 < s->length &&
                s->input[s->pos + 1] == '/') {
                s->pos += 2;
                goto restart;
            }
            s->pos++;
        }
        s->error = "unexpected end of comment";
        return -1;
    }

    /* Multi-character operators */
    if (c == '&' && s->pos + 1 < s->length && s->input[s->pos + 1] == '&') {
        s->pos += 2;
        return AND_P;
    }
    if (c == '|' && s->pos + 1 < s->length && s->input[s->pos + 1] == '|') {
        s->pos += 2;
        return OR_P;
    }
    if (c == '!' && s->pos + 1 < s->length && s->input[s->pos + 1] == '=') {
        s->pos += 2;
        return NOTEQUAL_P;
    }
    if (c == '!') {
        s->pos++;
        return NOT_P;
    }
    if (c == '*' && s->pos + 1 < s->length && s->input[s->pos + 1] == '*') {
        s->pos += 2;
        return ANY_P;
    }
    if (c == '<' && s->pos + 1 < s->length && s->input[s->pos + 1] == '=') {
        s->pos += 2;
        return LESSEQUAL_P;
    }
    if (c == '<' && s->pos + 1 < s->length && s->input[s->pos + 1] == '>') {
        s->pos += 2;
        return NOTEQUAL_P;
    }
    if (c == '<') {
        s->pos++;
        return LESS_P;
    }
    if (c == '>' && s->pos + 1 < s->length && s->input[s->pos + 1] == '=') {
        s->pos += 2;
        return GREATEREQUAL_P;
    }
    if (c == '>') {
        s->pos++;
        return GREATER_P;
    }
    if (c == '=' && s->pos + 1 < s->length && s->input[s->pos + 1] == '=') {
        s->pos += 2;
        return EQUAL_P;
    }

    /* Variable: $identifier or $"quoted" */
    if (c == '$') {
        s->pos++;
        if (s->pos < s->length && s->input[s->pos] == '"') {
            /* Quoted variable name: $"..." */
            s->pos++;
            buf_init(s);
            while (s->pos < s->length) {
                if (s->input[s->pos] == '"') {
                    s->pos++;
                    val->str.val = buf_finish(s, &val->str.len);
                    return VARIABLE_P;
                }
                if (s->input[s->pos] == '\\') {
                    s->pos++;
                    if (!scan_escape(s)) return -1;
                } else {
                    buf_addchar(s, s->input[s->pos++]);
                }
            }
            s->error = "unterminated quoted string";
            return -1;
        }
        if (s->pos < s->length && is_other(s->input[s->pos])) {
            /* Unquoted variable name: $identifier */
            buf_init(s);
            while (s->pos < s->length && is_other(s->input[s->pos]))
                buf_addchar(s, s->input[s->pos++]);
            val->str.val = buf_finish(s, &val->str.len);
            return VARIABLE_P;
        }
        /* Bare $ */
        return DOLLAR;
    }

    /* Single-character specials */
    if (c == '@') { s->pos++; return AT; }
    if (c == '.') { s->pos++; return DOT; }
    if (c == '[') { s->pos++; return LBRACKET; }
    if (c == ']') { s->pos++; return RBRACKET; }
    if (c == '{') { s->pos++; return LBRACE; }
    if (c == '}') { s->pos++; return RBRACE; }
    if (c == '(') { s->pos++; return LPAREN; }
    if (c == ')') { s->pos++; return RPAREN; }
    if (c == '?') { s->pos++; return QUESTION; }
    if (c == ',') { s->pos++; return COMMA; }
    if (c == '*') { s->pos++; return STAR; }
    if (c == ':') { s->pos++; return COLON; }
    if (c == '+') { s->pos++; return PLUS; }
    if (c == '-') { s->pos++; return MINUS; }
    if (c == '/') { s->pos++; return SLASH; }
    if (c == '%') { s->pos++; return PERCENT; }
    if (c == '#') { s->pos++; return 0; /* unused in jsonpath */ }

    /* Numeric literals */
    if ((c >= '0' && c <= '9') ||
        (c == '.' && s->pos + 1 < s->length &&
         s->input[s->pos + 1] >= '0' && s->input[s->pos + 1] <= '9')) {
        return scan_number(s, val);
    }

    /* Quoted string */
    if (c == '"') {
        s->pos++;
        buf_init(s);
        while (s->pos < s->length) {
            if (s->input[s->pos] == '"') {
                s->pos++;
                val->str.val = buf_finish(s, &val->str.len);
                return STRING_P;
            }
            if (s->input[s->pos] == '\\') {
                s->pos++;
                if (!scan_escape(s)) return -1;
            } else {
                buf_addchar(s, s->input[s->pos++]);
            }
        }
        s->error = "unterminated quoted string";
        return -1;
    }

    /* Backslash-started unquoted string */
    if (c == '\\') {
        buf_init(s);
        s->pos++;
        if (!scan_escape(s)) return -1;
        /* Continue reading "other" chars and escapes */
        while (s->pos < s->length) {
            if (s->input[s->pos] == '\\') {
                s->pos++;
                if (!scan_escape(s)) return -1;
            } else if (is_other(s->input[s->pos])) {
                buf_addchar(s, s->input[s->pos++]);
            } else {
                break;
            }
        }
        val->str.val = buf_finish(s, &val->str.len);
        return check_keyword(val->str.val, val->str.len);
    }

    /* Unquoted identifier/keyword */
    if (is_other(c)) {
        buf_init(s);
        while (s->pos < s->length && is_other(s->input[s->pos]))
            buf_addchar(s, s->input[s->pos++]);

        /* Continue if there are escape sequences */
        while (s->pos < s->length && s->input[s->pos] == '\\') {
            s->pos++;
            if (!scan_escape(s)) return -1;
            while (s->pos < s->length && is_other(s->input[s->pos]))
                buf_addchar(s, s->input[s->pos++]);
        }

        val->str.val = buf_finish(s, &val->str.len);
        return check_keyword(val->str.val, val->str.len);
    }

    /* Unknown character */
    s->error = "unexpected character in jsonpath";
    return -1;
}
