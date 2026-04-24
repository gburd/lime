/*
 * mongodb_internal.h
 *    Internal type definitions for the standalone MongoDB query parser.
 *
 * Provides the data structures needed by both the Lime-generated parser
 * and the hand-written tokenizer to represent MongoDB query documents,
 * update documents, and aggregation pipeline stages as an AST.
 */

#ifndef MONGODB_INTERNAL_H
#define MONGODB_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * MongoDB AST node types
 * ====================================================================== */

typedef enum MdbNodeType
{
    /* Literals */
    MDB_NULL = 0,
    MDB_BOOL,
    MDB_INT,
    MDB_DOUBLE,
    MDB_STRING,
    MDB_REGEX,
    MDB_OBJECT_ID,

    /* Structural */
    MDB_DOCUMENT,       /* { key: value, ... } */
    MDB_ARRAY,          /* [ value, ... ] */
    MDB_PAIR,           /* key: value */
    MDB_FIELD_PATH,     /* "field" or "field.subfield" (dot notation) */

    /* Query operators -- comparison */
    MDB_OP_EQ,          /* $eq */
    MDB_OP_NE,          /* $ne */
    MDB_OP_GT,          /* $gt */
    MDB_OP_GTE,         /* $gte */
    MDB_OP_LT,          /* $lt */
    MDB_OP_LTE,         /* $lte */
    MDB_OP_IN,          /* $in */
    MDB_OP_NIN,         /* $nin */

    /* Query operators -- logical */
    MDB_OP_AND,         /* $and */
    MDB_OP_OR,          /* $or */
    MDB_OP_NOT,         /* $not */
    MDB_OP_NOR,         /* $nor */

    /* Query operators -- element */
    MDB_OP_EXISTS,      /* $exists */
    MDB_OP_TYPE,        /* $type */

    /* Query operators -- evaluation */
    MDB_OP_REGEX,       /* $regex */
    MDB_OP_MOD,         /* $mod */

    /* Query operators -- array */
    MDB_OP_ALL,         /* $all */
    MDB_OP_ELEMMATCH,   /* $elemMatch */
    MDB_OP_SIZE,        /* $size */

    /* Update operators -- field */
    MDB_UP_SET,         /* $set */
    MDB_UP_UNSET,       /* $unset */
    MDB_UP_INC,         /* $inc */
    MDB_UP_MUL,         /* $mul */
    MDB_UP_RENAME,      /* $rename */
    MDB_UP_MIN,         /* $min */
    MDB_UP_MAX,         /* $max */
    MDB_UP_CURRENTDATE, /* $currentDate */
    MDB_UP_SETONDISERT, /* $setOnInsert */

    /* Update operators -- array */
    MDB_UP_PUSH,        /* $push */
    MDB_UP_PULL,        /* $pull */
    MDB_UP_ADDTOSET,    /* $addToSet */
    MDB_UP_POP,         /* $pop */

    /* Aggregation stages */
    MDB_AGG_MATCH,      /* $match */
    MDB_AGG_GROUP,      /* $group */
    MDB_AGG_PROJECT,    /* $project */
    MDB_AGG_SORT,       /* $sort */
    MDB_AGG_LIMIT,      /* $limit */
    MDB_AGG_SKIP,       /* $skip */
    MDB_AGG_UNWIND,     /* $unwind */
    MDB_AGG_LOOKUP,     /* $lookup */
    MDB_AGG_OUT,        /* $out */
    MDB_AGG_COUNT,      /* $count */

    /* Aggregation expressions */
    MDB_AGG_SUM,        /* $sum */
    MDB_AGG_AVG,        /* $avg */
    MDB_AGG_FIRST,      /* $first */
    MDB_AGG_LAST,       /* $last */

    /* Top-level statement types */
    MDB_FIND,           /* db.collection.find(query, projection) */
    MDB_UPDATE,         /* db.collection.update(query, update) */
    MDB_AGGREGATE,      /* db.collection.aggregate(pipeline) */
    MDB_PIPELINE        /* [ stage, stage, ... ] */
} MdbNodeType;

/* ======================================================================
 * MdbString -- string with length
 * ====================================================================== */

typedef struct MdbString
{
    int     len;
    char   *val;
} MdbString;

/* ======================================================================
 * MdbNode -- AST node
 *
 * Each node has a type and a union of type-specific fields.
 * ====================================================================== */

typedef struct MdbNode MdbNode;

/* Linked list of nodes (for document pairs, array elements, etc.) */
typedef struct MdbNodeList
{
    int               length;
    struct MdbNodeCell *head;
    struct MdbNodeCell *tail;
} MdbNodeList;

typedef struct MdbNodeCell
{
    MdbNode            *data;
    struct MdbNodeCell *next;
} MdbNodeCell;

struct MdbNode
{
    MdbNodeType type;

    union
    {
        /* MDB_BOOL */
        bool boolean;

        /* MDB_INT */
        int64_t integer;

        /* MDB_DOUBLE */
        double floating;

        /* MDB_STRING, MDB_FIELD_PATH, MDB_OBJECT_ID */
        MdbString string;

        /* MDB_REGEX */
        struct {
            MdbString pattern;
            MdbString flags;
        } regex;

        /* MDB_DOCUMENT, MDB_ARRAY, MDB_PIPELINE */
        MdbNodeList *elements;

        /* MDB_PAIR */
        struct {
            MdbString  key;
            MdbNode   *value;
        } pair;

        /* Query/update operators with a single operand */
        struct {
            MdbString  field;    /* field name (may be empty for logical ops) */
            MdbNode   *operand;  /* value or sub-expression */
        } op;

        /* Aggregation stage */
        struct {
            MdbNode *spec;       /* stage specification document */
        } stage;

        /* MDB_FIND */
        struct {
            MdbNode *query;
            MdbNode *projection;
        } find;

        /* MDB_UPDATE */
        struct {
            MdbNode *query;
            MdbNode *update;
        } update;

        /* MDB_AGGREGATE */
        struct {
            MdbNode *pipeline;
        } aggregate;

    } value;
};

/* ======================================================================
 * MdbToken -- semantic value carried by each token.
 * This is used as Lime's %token_type.
 * ====================================================================== */

typedef struct MdbToken
{
    MdbString str;      /* string value for identifiers, strings, numbers */
    int64_t   ival;     /* integer value */
    double    dval;     /* floating point value */
} MdbToken;

/* ======================================================================
 * MdbParseState -- extra argument to the Lime parser
 * ====================================================================== */

typedef struct MdbParseState
{
    MdbNode    *result;
    int         error;
    const char *errMsg;
} MdbParseState;

#ifdef __cplusplus
}
#endif

#endif /* MONGODB_INTERNAL_H */
