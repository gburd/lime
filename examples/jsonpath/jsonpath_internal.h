/*
 * jsonpath_internal.h
 *    Internal type definitions for the standalone JSONPath parser.
 *
 * This is a standalone replacement for PostgreSQL's jsonpath_internal.h,
 * providing the data structures needed by both the Lime-generated parser
 * and the hand-written tokenizer without requiring PostgreSQL headers.
 *
 * Original structures are based on PostgreSQL's jsonpath.h and
 * jsonpath_internal.h.
 */

#ifndef JSONPATH_INTERNAL_H
#define JSONPATH_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * JsonPath item types
 *
 * These correspond to PostgreSQL's JsonPathItemType enum.
 * ====================================================================== */

typedef enum JsonPathItemType
{
    jpiNull = 0,
    jpiBool,
    jpiNumeric,
    jpiString,
    jpiVariable,
    jpiKey,
    jpiRoot,
    jpiCurrent,
    jpiLast,
    jpiAnyArray,
    jpiAnyKey,
    jpiIndexArray,
    jpiAny,
    jpiFilter,
    jpiExists,
    jpiNot,
    jpiIsUnknown,
    jpiPlus,
    jpiMinus,
    jpiAdd,
    jpiSub,
    jpiMul,
    jpiDiv,
    jpiMod,
    jpiEqual,
    jpiNotEqual,
    jpiLess,
    jpiGreater,
    jpiLessOrEqual,
    jpiGreaterOrEqual,
    jpiAnd,
    jpiOr,
    jpiStartsWith,
    jpiLikeRegex,
    jpiSubscript,
    jpiDatetime,
    jpiTime,
    jpiTimeTz,
    jpiTimestamp,
    jpiTimestampTz,
    jpiAbs,
    jpiSize,
    jpiType,
    jpiFloor,
    jpiDouble,
    jpiCeiling,
    jpiKeyValue,
    jpiBigint,
    jpiBoolean,
    jpiDate,
    jpiDecimal,
    jpiInteger,
    jpiNumber,
    jpiStringFunc
} JsonPathItemType;

/* ======================================================================
 * JsonPathString -- string with length
 * ====================================================================== */

typedef struct JsonPathString
{
    int     len;
    int     total;   /* allocated size (used by tokenizer for buffer mgmt) */
    char   *val;
} JsonPathString;

/* ======================================================================
 * JsonPathParseItem -- AST node
 *
 * Each node has a type and a next pointer (for chaining accessor
 * operations), plus a union of type-specific fields.
 * ====================================================================== */

typedef struct JsonPathParseItem JsonPathParseItem;

struct JsonPathParseItem
{
    JsonPathItemType type;
    JsonPathParseItem *next;    /* next accessor in chain */

    union
    {
        /* jpiString, jpiVariable, jpiKey */
        struct
        {
            char   *val;
            int     len;
        } string;

        /* jpiBool */
        bool boolean;

        /* jpiNumeric -- stored as string for simplicity in standalone mode */
        char *numeric;

        /* jpiAnd, jpiOr, jpiAdd, jpiSub, jpiMul, jpiDiv, jpiMod,
         * jpiEqual, jpiNotEqual, jpiLess, jpiGreater, jpiLessOrEqual,
         * jpiGreaterOrEqual, jpiStartsWith, jpiSubscript, jpiDecimal */
        struct
        {
            JsonPathParseItem *left;
            JsonPathParseItem *right;
        } args;

        /* jpiNot, jpiIsUnknown, jpiExists, jpiFilter, jpiPlus, jpiMinus,
         * jpiDatetime, jpiTime, jpiTimeTz, jpiTimestamp, jpiTimestampTz */
        JsonPathParseItem *arg;

        /* jpiIndexArray */
        struct
        {
            int     nelems;
            struct
            {
                JsonPathParseItem *from;
                JsonPathParseItem *to;
            }      *elems;
        } array;

        /* jpiAny */
        struct
        {
            uint32_t first;
            uint32_t last;
        } anybounds;

        /* jpiLikeRegex */
        struct
        {
            JsonPathParseItem *expr;
            char   *pattern;
            int     patternlen;
            uint32_t flags;
        } like_regex;

    } value;
};

/* ======================================================================
 * Regex flag constants (matching PostgreSQL's JSP_REGEX_* values)
 * ====================================================================== */

#define JSP_REGEX_ICASE   0x01
#define JSP_REGEX_DOTALL  0x02
#define JSP_REGEX_MLINE   0x04
#define JSP_REGEX_WSPACE  0x08
#define JSP_REGEX_QUOTE   0x10

/* ======================================================================
 * JsonPathParseResult -- top-level parse result
 * ====================================================================== */

typedef struct JsonPathParseResult
{
    JsonPathParseItem  *expr;
    bool                lax;    /* true = lax mode, false = strict */
} JsonPathParseResult;

/* ======================================================================
 * JsonPathToken -- semantic value carried by each token.
 * This replaces the Bison %union and is used as Lime's %token_type.
 * ====================================================================== */

typedef struct JsonPathToken
{
    JsonPathString  str;    /* string value + length (for STRING_P, IDENT_P, etc.) */
    int             ival;   /* integer value (for INT_P used as level) */
} JsonPathToken;

/* ======================================================================
 * JsonPathList -- simple linked list (replaces PostgreSQL's List)
 * ====================================================================== */

typedef struct JsonPathListCell
{
    void                   *data;
    struct JsonPathListCell *next;
} JsonPathListCell;

typedef struct JsonPathList
{
    int               length;
    JsonPathListCell  *head;
    JsonPathListCell  *tail;
} JsonPathList;

/* ======================================================================
 * JsonPathParseState -- extra argument to the Lime parser
 *
 * Replaces the multiple %parse-param/%lex-param from Bison.
 * ====================================================================== */

typedef struct JsonPathParseState
{
    JsonPathParseResult *result;
    int                  error;
    const char          *errMsg;
} JsonPathParseState;

/* ======================================================================
 * Utility function used in grammar actions
 * ====================================================================== */

static inline int jp_strtoint32(const char *s)
{
    return (int)strtol(s, NULL, 10);
}

#ifdef __cplusplus
}
#endif

#endif /* JSONPATH_INTERNAL_H */
