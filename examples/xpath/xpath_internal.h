/*
 * xpath_internal.h
 *    Internal type definitions for the standalone XPath 1.0 parser.
 *
 * Provides the data structures needed by both the Lime-generated parser
 * and the hand-written tokenizer.
 */

#ifndef XPATH_INTERNAL_H
#define XPATH_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * XPath axis types
 *
 * All 13 axes defined by the XPath 1.0 specification.
 * ====================================================================== */

typedef enum XPathAxisType
{
    XPATH_AXIS_CHILD = 0,
    XPATH_AXIS_DESCENDANT,
    XPATH_AXIS_PARENT,
    XPATH_AXIS_ANCESTOR,
    XPATH_AXIS_FOLLOWING_SIBLING,
    XPATH_AXIS_PRECEDING_SIBLING,
    XPATH_AXIS_FOLLOWING,
    XPATH_AXIS_PRECEDING,
    XPATH_AXIS_ATTRIBUTE,
    XPATH_AXIS_NAMESPACE,
    XPATH_AXIS_SELF,
    XPATH_AXIS_DESCENDANT_OR_SELF,
    XPATH_AXIS_ANCESTOR_OR_SELF
} XPathAxisType;

/* ======================================================================
 * XPath node type tests
 * ====================================================================== */

typedef enum XPathNodeTypeTest
{
    XPATH_NODE_TYPE_COMMENT = 0,
    XPATH_NODE_TYPE_TEXT,
    XPATH_NODE_TYPE_PI,
    XPATH_NODE_TYPE_NODE
} XPathNodeTypeTest;

/* ======================================================================
 * XPath AST node types
 * ====================================================================== */

typedef enum XPathNodeType
{
    /* Location path nodes */
    XPATH_AST_ROOT = 0,         /* document root (/) */
    XPATH_AST_ABSOLUTE_PATH,    /* /relative-path */
    XPATH_AST_PATH,             /* step/step (binary: left/right) */
    XPATH_AST_STEP,             /* axis::node-test[pred]* */
    XPATH_AST_NAME_TEST,        /* name test (NCName, *, prefix:*) */
    XPATH_AST_NODE_TYPE_TEST,   /* comment(), text(), pi(), node() */
    XPATH_AST_PI_TEST,          /* processing-instruction('target') */
    XPATH_AST_PREDICATE,        /* [expr] */
    XPATH_AST_PREDICATE_LIST,   /* chain of predicates */

    /* Expressions */
    XPATH_AST_LITERAL,          /* "string" or 'string' */
    XPATH_AST_NUMBER,           /* numeric literal */
    XPATH_AST_VARIABLE_REF,     /* $varname */
    XPATH_AST_FUNCTION_CALL,    /* func(args) */
    XPATH_AST_FILTER,           /* primary-expr[predicate] */

    /* Binary operators */
    XPATH_AST_BINARY_OP,

    /* Unary operators */
    XPATH_AST_UNARY_OP
} XPathNodeType;

/* ======================================================================
 * XPath operators
 * ====================================================================== */

typedef enum XPathOpType
{
    /* Logical */
    XPATH_OP_OR = 0,
    XPATH_OP_AND,

    /* Equality */
    XPATH_OP_EQ,
    XPATH_OP_NE,

    /* Relational */
    XPATH_OP_LT,
    XPATH_OP_LE,
    XPATH_OP_GT,
    XPATH_OP_GE,

    /* Arithmetic */
    XPATH_OP_ADD,
    XPATH_OP_SUB,
    XPATH_OP_MUL,
    XPATH_OP_DIV,
    XPATH_OP_MOD,

    /* Unary */
    XPATH_OP_NEGATE,

    /* Union */
    XPATH_OP_UNION
} XPathOpType;

/* ======================================================================
 * XPathString -- string with length
 * ====================================================================== */

typedef struct XPathString
{
    char *val;
    int   len;
} XPathString;

/* ======================================================================
 * XPathNode -- AST node
 *
 * Each node has a type discriminator and a union of type-specific fields.
 * ====================================================================== */

typedef struct XPathNode XPathNode;

struct XPathNode
{
    XPathNodeType type;

    union
    {
        /* XPATH_AST_STEP */
        struct
        {
            XPathAxisType  axis;
            XPathNode     *node_test;   /* name_test or node_type_test */
            XPathNode     *predicates;  /* predicate_list or NULL */
        } step;

        /* XPATH_AST_NAME_TEST */
        struct
        {
            char *name;
            int   name_len;
        } name_test;

        /* XPATH_AST_NODE_TYPE_TEST */
        struct
        {
            XPathNodeTypeTest node_type;
        } node_type_test;

        /* XPATH_AST_PI_TEST */
        struct
        {
            char *target;
            int   target_len;
        } pi_test;

        /* XPATH_AST_PATH, XPATH_AST_BINARY_OP */
        struct
        {
            XPathOpType  op;
            XPathNode   *left;
            XPathNode   *right;
        } binary;

        /* XPATH_AST_UNARY_OP */
        struct
        {
            XPathOpType  op;
            XPathNode   *operand;
        } unary;

        /* XPATH_AST_LITERAL, XPATH_AST_VARIABLE_REF */
        struct
        {
            char *val;
            int   len;
        } string;

        /* XPATH_AST_NUMBER */
        double number;

        /* XPATH_AST_FUNCTION_CALL */
        struct
        {
            char              *name;
            int                name_len;
            struct XPathNodeList *args;
        } function_call;

        /* XPATH_AST_ABSOLUTE_PATH */
        struct
        {
            XPathNode *path;    /* the relative path below root */
        } absolute;

        /* XPATH_AST_FILTER */
        struct
        {
            XPathNode *primary;
            XPathNode *predicate;
        } filter;

        /* XPATH_AST_PREDICATE_LIST */
        struct
        {
            XPathNode *first;   /* first predicate expr */
            XPathNode *rest;    /* next predicate_list node, or NULL */
        } pred_list;

    } value;
};

/* ======================================================================
 * XPathNodeList -- simple linked list of AST nodes (for function args)
 * ====================================================================== */

typedef struct XPathNodeListCell
{
    XPathNode              *data;
    struct XPathNodeListCell *next;
} XPathNodeListCell;

typedef struct XPathNodeList
{
    int               length;
    XPathNodeListCell *head;
    XPathNodeListCell *tail;
} XPathNodeList;

/* ======================================================================
 * XPathToken -- semantic value carried by each token
 *
 * Used as Lime's %token_type.
 * ====================================================================== */

typedef struct XPathToken
{
    XPathString str;        /* string value (for LITERAL, NAME_TEST, etc.) */
    double      numval;     /* numeric value (for NUMBER) */
} XPathToken;

/* ======================================================================
 * XPathParseState -- extra argument to the Lime parser
 * ====================================================================== */

typedef struct XPathParseState
{
    XPathNode  *result;
    int         error;
    const char *errMsg;
} XPathParseState;

#ifdef __cplusplus
}
#endif

#endif /* XPATH_INTERNAL_H */
