/*
 * xquery_internal.h
 *    Internal type definitions for the standalone XQuery 1.0 parser.
 *
 * Extends the XPath data structures with XQuery-specific AST node types
 * for FLWOR expressions, element/attribute constructors, function
 * declarations, module structure, quantified expressions, etc.
 *
 * Based on the W3C XQuery 1.0 Recommendation.
 */

#ifndef XQUERY_INTERNAL_H
#define XQUERY_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * XPath axis types (inherited from XPath 1.0)
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
 * XPath node type tests (inherited from XPath 1.0)
 * ====================================================================== */

typedef enum XPathNodeTypeTest
{
    XPATH_NODE_TYPE_COMMENT = 0,
    XPATH_NODE_TYPE_TEXT,
    XPATH_NODE_TYPE_PI,
    XPATH_NODE_TYPE_NODE,
    XPATH_NODE_TYPE_DOCUMENT_NODE,
    XPATH_NODE_TYPE_ELEMENT,
    XPATH_NODE_TYPE_ATTRIBUTE,
    XPATH_NODE_TYPE_SCHEMA_ELEMENT,
    XPATH_NODE_TYPE_SCHEMA_ATTRIBUTE
} XPathNodeTypeTest;

/* ======================================================================
 * XQuery AST node types
 *
 * Extends XPath AST node types with XQuery-specific constructs.
 * ====================================================================== */

typedef enum XQNodeType
{
    /* --- XPath expression nodes (from XPath 1.0) --- */
    XQ_AST_ROOT = 0,
    XQ_AST_ABSOLUTE_PATH,
    XQ_AST_PATH,
    XQ_AST_STEP,
    XQ_AST_NAME_TEST,
    XQ_AST_NODE_TYPE_TEST,
    XQ_AST_PI_TEST,
    XQ_AST_PREDICATE,
    XQ_AST_PREDICATE_LIST,
    XQ_AST_LITERAL,
    XQ_AST_NUMBER,
    XQ_AST_VARIABLE_REF,
    XQ_AST_FUNCTION_CALL,
    XQ_AST_FILTER,
    XQ_AST_BINARY_OP,
    XQ_AST_UNARY_OP,

    /* --- XQuery-specific nodes --- */

    /* Module structure */
    XQ_AST_MODULE,          /* top-level: prolog + body */
    XQ_AST_PROLOG,          /* declarations list */
    XQ_AST_QUERY_BODY,      /* the main expression */

    /* Prolog declarations */
    XQ_AST_NS_DECL,         /* declare namespace prefix = "uri" */
    XQ_AST_DEFAULT_NS_DECL, /* declare default element/function namespace */
    XQ_AST_FUNCTION_DECL,   /* declare function name(...) as type { body } */
    XQ_AST_VARIABLE_DECL,   /* declare variable $x := expr */
    XQ_AST_OPTION_DECL,     /* declare option name "value" */
    XQ_AST_IMPORT_SCHEMA,   /* import schema ... */
    XQ_AST_IMPORT_MODULE,   /* import module ... */

    /* FLWOR expression */
    XQ_AST_FLWOR,           /* for/let/where/order by/return */
    XQ_AST_FOR_CLAUSE,      /* for $var in expr */
    XQ_AST_LET_CLAUSE,      /* let $var := expr */
    XQ_AST_WHERE_CLAUSE,    /* where expr */
    XQ_AST_ORDER_BY_CLAUSE, /* order by spec-list */
    XQ_AST_ORDER_SPEC,      /* sort spec: expr ascending/descending */
    XQ_AST_RETURN_CLAUSE,   /* return expr */

    /* Quantified expressions */
    XQ_AST_SOME_EXPR,       /* some $var in expr satisfies expr */
    XQ_AST_EVERY_EXPR,      /* every $var in expr satisfies expr */
    XQ_AST_QUANT_BINDING,   /* $var in expr (binding in quantified) */

    /* Conditional */
    XQ_AST_IF_EXPR,         /* if (expr) then expr else expr */

    /* Element constructors */
    XQ_AST_DIRECT_ELEM,     /* <tag attr="val">content</tag> */
    XQ_AST_COMP_ELEM,       /* element name { content } */
    XQ_AST_COMP_ATTR,       /* attribute name { value } */
    XQ_AST_COMP_DOC,        /* document { content } */
    XQ_AST_COMP_TEXT,       /* text { value } */
    XQ_AST_COMP_COMMENT,    /* comment { value } */
    XQ_AST_COMP_PI,         /* processing-instruction name { value } */

    /* Enclosed expression */
    XQ_AST_ENCLOSED_EXPR,   /* { expr } */

    /* Type expressions */
    XQ_AST_TYPESWITCH,      /* typeswitch (expr) case ... default ... */
    XQ_AST_CASE_CLAUSE,     /* case type return expr */
    XQ_AST_INSTANCE_OF,     /* expr instance of type */
    XQ_AST_TREAT_AS,        /* expr treat as type */
    XQ_AST_CASTABLE_AS,     /* expr castable as type */
    XQ_AST_CAST_AS,         /* expr cast as type */

    /* Sequence types */
    XQ_AST_SEQUENCE_TYPE,   /* type? / type* / type+ */
    XQ_AST_EMPTY_SEQUENCE,  /* empty-sequence() */

    /* Sequence expression */
    XQ_AST_SEQUENCE,        /* (expr, expr, ...) */

    /* Parameter in function declarations */
    XQ_AST_PARAM,           /* $name as type */

    /* String concat operator */
    XQ_AST_STRING_CONCAT    /* expr || expr (XQuery string concatenation) */
} XQNodeType;

/* ======================================================================
 * XPath/XQuery operators
 * ====================================================================== */

typedef enum XQOpType
{
    /* Logical */
    XQ_OP_OR = 0,
    XQ_OP_AND,

    /* Equality (value comparison) */
    XQ_OP_EQ,
    XQ_OP_NE,

    /* General comparison */
    XQ_OP_GEN_EQ,      /* = */
    XQ_OP_GEN_NE,      /* != */
    XQ_OP_GEN_LT,      /* < */
    XQ_OP_GEN_LE,      /* <= */
    XQ_OP_GEN_GT,      /* > */
    XQ_OP_GEN_GE,      /* >= */

    /* Value comparison */
    XQ_OP_VAL_EQ,      /* eq */
    XQ_OP_VAL_NE,      /* ne */
    XQ_OP_VAL_LT,      /* lt */
    XQ_OP_VAL_LE,      /* le */
    XQ_OP_VAL_GT,      /* gt */
    XQ_OP_VAL_GE,      /* ge */

    /* Node comparison */
    XQ_OP_IS,          /* is */
    XQ_OP_PRECEDES,    /* << */
    XQ_OP_FOLLOWS,     /* >> */

    /* Arithmetic */
    XQ_OP_ADD,
    XQ_OP_SUB,
    XQ_OP_MUL,
    XQ_OP_DIV,
    XQ_OP_IDIV,        /* idiv */
    XQ_OP_MOD,

    /* Unary */
    XQ_OP_NEGATE,
    XQ_OP_POSITIVE,

    /* Union / intersect / except */
    XQ_OP_UNION,
    XQ_OP_INTERSECT,
    XQ_OP_EXCEPT,

    /* Sequence operators */
    XQ_OP_TO,           /* to (range) */
    XQ_OP_STRING_CONCAT /* || */
} XQOpType;

/* ======================================================================
 * Sort order for order-by
 * ====================================================================== */

typedef enum XQSortOrder
{
    XQ_SORT_ASCENDING = 0,
    XQ_SORT_DESCENDING
} XQSortOrder;

/* ======================================================================
 * XQString -- string with length
 * ====================================================================== */

typedef struct XQString
{
    char *val;
    int   len;
} XQString;

/* ======================================================================
 * XQNode -- AST node
 * ====================================================================== */

typedef struct XQNode XQNode;

/* Linked list of nodes (for function args, FLWOR clauses, etc.) */
typedef struct XQNodeListCell
{
    XQNode                *data;
    struct XQNodeListCell *next;
} XQNodeListCell;

typedef struct XQNodeList
{
    int             length;
    XQNodeListCell *head;
    XQNodeListCell *tail;
} XQNodeList;

struct XQNode
{
    XQNodeType type;

    union
    {
        /* XQ_AST_STEP */
        struct {
            XPathAxisType  axis;
            XQNode        *node_test;
            XQNode        *predicates;
        } step;

        /* XQ_AST_NAME_TEST */
        struct {
            char *name;
            int   name_len;
        } name_test;

        /* XQ_AST_NODE_TYPE_TEST */
        struct {
            XPathNodeTypeTest node_type;
        } node_type_test;

        /* XQ_AST_PI_TEST */
        struct {
            char *target;
            int   target_len;
        } pi_test;

        /* XQ_AST_PATH, XQ_AST_BINARY_OP */
        struct {
            XQOpType  op;
            XQNode   *left;
            XQNode   *right;
        } binary;

        /* XQ_AST_UNARY_OP */
        struct {
            XQOpType  op;
            XQNode   *operand;
        } unary;

        /* XQ_AST_LITERAL, XQ_AST_VARIABLE_REF */
        struct {
            char *val;
            int   len;
        } string;

        /* XQ_AST_NUMBER */
        double number;

        /* XQ_AST_FUNCTION_CALL */
        struct {
            char       *name;
            int         name_len;
            XQNodeList *args;
        } function_call;

        /* XQ_AST_ABSOLUTE_PATH */
        struct {
            XQNode *path;
        } absolute;

        /* XQ_AST_FILTER */
        struct {
            XQNode *primary;
            XQNode *predicate;
        } filter;

        /* XQ_AST_PREDICATE_LIST */
        struct {
            XQNode *first;
            XQNode *rest;
        } pred_list;

        /* --- XQuery-specific --- */

        /* XQ_AST_MODULE */
        struct {
            XQNode *prolog;
            XQNode *body;
        } module;

        /* XQ_AST_PROLOG */
        struct {
            XQNodeList *declarations;
        } prolog;

        /* XQ_AST_QUERY_BODY */
        struct {
            XQNode *expr;
        } query_body;

        /* XQ_AST_NS_DECL, XQ_AST_DEFAULT_NS_DECL */
        struct {
            char *prefix;
            int   prefix_len;
            char *uri;
            int   uri_len;
        } ns_decl;

        /* XQ_AST_FUNCTION_DECL */
        struct {
            char       *name;
            int         name_len;
            XQNodeList *params;
            XQNode     *return_type;   /* may be NULL */
            XQNode     *body;
        } func_decl;

        /* XQ_AST_VARIABLE_DECL */
        struct {
            char   *name;
            int     name_len;
            XQNode *type_decl;  /* may be NULL */
            XQNode *init_expr;
        } var_decl;

        /* XQ_AST_PARAM */
        struct {
            char   *name;
            int     name_len;
            XQNode *type_decl;  /* may be NULL */
        } param;

        /* XQ_AST_FLWOR */
        struct {
            XQNodeList *clauses;    /* for/let clauses */
            XQNode     *where;      /* where clause (may be NULL) */
            XQNode     *order_by;   /* order-by clause (may be NULL) */
            XQNode     *ret;        /* return expression */
        } flwor;

        /* XQ_AST_FOR_CLAUSE */
        struct {
            char   *var_name;
            int     var_name_len;
            XQNode *in_expr;
            char   *pos_var;        /* optional positional variable */
            int     pos_var_len;
        } for_clause;

        /* XQ_AST_LET_CLAUSE */
        struct {
            char   *var_name;
            int     var_name_len;
            XQNode *bind_expr;
        } let_clause;

        /* XQ_AST_WHERE_CLAUSE */
        struct {
            XQNode *condition;
        } where_clause;

        /* XQ_AST_ORDER_BY_CLAUSE */
        struct {
            XQNodeList *specs;
            bool        stable;
        } order_by;

        /* XQ_AST_ORDER_SPEC */
        struct {
            XQNode     *expr;
            XQSortOrder order;
        } order_spec;

        /* XQ_AST_SOME_EXPR, XQ_AST_EVERY_EXPR */
        struct {
            XQNodeList *bindings;
            XQNode     *satisfies;
        } quantified;

        /* XQ_AST_QUANT_BINDING */
        struct {
            char   *var_name;
            int     var_name_len;
            XQNode *in_expr;
        } quant_binding;

        /* XQ_AST_IF_EXPR */
        struct {
            XQNode *condition;
            XQNode *then_expr;
            XQNode *else_expr;
        } if_expr;

        /* XQ_AST_COMP_ELEM, XQ_AST_DIRECT_ELEM */
        struct {
            char       *tag_name;
            int         tag_name_len;
            XQNodeList *attributes;
            XQNode     *content;
        } element;

        /* XQ_AST_COMP_ATTR */
        struct {
            char   *attr_name;
            int     attr_name_len;
            XQNode *attr_value;
        } comp_attr;

        /* XQ_AST_COMP_DOC, XQ_AST_COMP_TEXT, XQ_AST_COMP_COMMENT,
         * XQ_AST_ENCLOSED_EXPR, XQ_AST_RETURN_CLAUSE */
        struct {
            XQNode *content;
        } enclosed;

        /* XQ_AST_COMP_PI */
        struct {
            char   *pi_target;
            int     pi_target_len;
            XQNode *pi_content;
        } comp_pi;

        /* XQ_AST_INSTANCE_OF, XQ_AST_TREAT_AS, XQ_AST_CASTABLE_AS, XQ_AST_CAST_AS */
        struct {
            XQNode *expr;
            XQNode *type;
        } type_expr;

        /* XQ_AST_TYPESWITCH */
        struct {
            XQNode     *operand;
            XQNodeList *cases;
            XQNode     *default_expr;
            char       *default_var;
            int         default_var_len;
        } typeswitch;

        /* XQ_AST_CASE_CLAUSE */
        struct {
            char   *var_name;
            int     var_name_len;
            XQNode *case_type;
            XQNode *case_expr;
        } case_clause;

        /* XQ_AST_SEQUENCE_TYPE */
        struct {
            char *type_name;
            int   type_name_len;
            int   occurrence;   /* 0=exactly-one, '?', '*', '+' */
        } seq_type;

        /* XQ_AST_SEQUENCE */
        struct {
            XQNodeList *items;
        } sequence;

    } value;
};

/* ======================================================================
 * XQToken -- semantic value carried by each token
 * ====================================================================== */

typedef struct XQToken
{
    XQString str;
    double   numval;
} XQToken;

/* ======================================================================
 * XQParseState -- extra argument to the Lime parser
 * ====================================================================== */

typedef struct XQParseState
{
    XQNode     *result;
    int         error;
    const char *errMsg;
} XQParseState;

#ifdef __cplusplus
}
#endif

#endif /* XQUERY_INTERNAL_H */
