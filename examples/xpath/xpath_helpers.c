/*
 * xpath_helpers.c
 *    Helper functions for the XPath 1.0 Lime grammar.
 *
 * These functions are used by the Lime-generated parser's reduction
 * actions to build the XPath AST (abstract syntax tree).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "xpath_helpers.h"

/* ======================================================================
 * Internal: allocate a new AST node
 * ====================================================================== */

static XPathNode *make_node(XPathNodeType type)
{
    XPathNode *n = (XPathNode *)calloc(1, sizeof(XPathNode));
    n->type = type;
    return n;
}

static char *str_dup(const char *s, int len)
{
    char *d = (char *)malloc(len + 1);
    memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

/* ======================================================================
 * AST construction helpers -- location paths
 * ====================================================================== */

XPathNode *
xpath_make_root(void)
{
    return make_node(XPATH_AST_ROOT);
}

XPathNode *
xpath_make_absolute_path(XPathNode *rel)
{
    XPathNode *n = make_node(XPATH_AST_ABSOLUTE_PATH);
    n->value.absolute.path = rel;
    return n;
}

XPathNode *
xpath_make_path(XPathNode *left, XPathNode *right)
{
    XPathNode *n = make_node(XPATH_AST_PATH);
    n->value.binary.left = left;
    n->value.binary.right = right;
    return n;
}

XPathNode *
xpath_make_step(XPathAxisType axis, XPathNode *node_test, XPathNode *predicates)
{
    XPathNode *n = make_node(XPATH_AST_STEP);
    n->value.step.axis = axis;
    n->value.step.node_test = node_test;
    n->value.step.predicates = predicates;
    return n;
}

/* ======================================================================
 * AST construction helpers -- node tests
 * ====================================================================== */

XPathNode *
xpath_make_name_test(const char *name, int len)
{
    XPathNode *n = make_node(XPATH_AST_NAME_TEST);
    n->value.name_test.name = str_dup(name, len);
    n->value.name_test.name_len = len;
    return n;
}

XPathNode *
xpath_make_node_type_test(XPathNodeTypeTest type)
{
    XPathNode *n = make_node(XPATH_AST_NODE_TYPE_TEST);
    n->value.node_type_test.node_type = type;
    return n;
}

XPathNode *
xpath_make_pi_test(const char *target, int len)
{
    XPathNode *n = make_node(XPATH_AST_PI_TEST);
    n->value.pi_test.target = str_dup(target, len);
    n->value.pi_test.target_len = len;
    return n;
}

/* ======================================================================
 * AST construction helpers -- operators
 * ====================================================================== */

XPathNode *
xpath_make_binary(XPathOpType op, XPathNode *left, XPathNode *right)
{
    XPathNode *n = make_node(XPATH_AST_BINARY_OP);
    n->value.binary.op = op;
    n->value.binary.left = left;
    n->value.binary.right = right;
    return n;
}

XPathNode *
xpath_make_unary(XPathOpType op, XPathNode *operand)
{
    XPathNode *n = make_node(XPATH_AST_UNARY_OP);
    n->value.unary.op = op;
    n->value.unary.operand = operand;
    return n;
}

/* ======================================================================
 * AST construction helpers -- primary expressions
 * ====================================================================== */

XPathNode *
xpath_make_literal(const char *val, int len)
{
    XPathNode *n = make_node(XPATH_AST_LITERAL);
    n->value.string.val = str_dup(val, len);
    n->value.string.len = len;
    return n;
}

XPathNode *
xpath_make_number(double val)
{
    XPathNode *n = make_node(XPATH_AST_NUMBER);
    n->value.number = val;
    return n;
}

XPathNode *
xpath_make_variable_ref(const char *name, int len)
{
    XPathNode *n = make_node(XPATH_AST_VARIABLE_REF);
    n->value.string.val = str_dup(name, len);
    n->value.string.len = len;
    return n;
}

XPathNode *
xpath_make_function_call(const char *name, int name_len, XPathNodeList *args)
{
    XPathNode *n = make_node(XPATH_AST_FUNCTION_CALL);
    n->value.function_call.name = str_dup(name, name_len);
    n->value.function_call.name_len = name_len;
    n->value.function_call.args = args;
    return n;
}

/* ======================================================================
 * AST construction helpers -- filters and predicates
 * ====================================================================== */

XPathNode *
xpath_make_filter(XPathNode *primary, XPathNode *predicate)
{
    XPathNode *n = make_node(XPATH_AST_FILTER);
    n->value.filter.primary = primary;
    n->value.filter.predicate = predicate;
    return n;
}

XPathNode *
xpath_append_predicate(XPathNode *list, XPathNode *pred)
{
    XPathNode *n = make_node(XPATH_AST_PREDICATE_LIST);
    n->value.pred_list.first = pred;
    n->value.pred_list.rest = NULL;

    if (list == NULL) {
        return n;
    }

    /* Append to the end of the predicate chain */
    XPathNode *tail = list;
    while (tail->type == XPATH_AST_PREDICATE_LIST && tail->value.pred_list.rest != NULL) {
        tail = tail->value.pred_list.rest;
    }
    tail->value.pred_list.rest = n;
    return list;
}

/* ======================================================================
 * Node list helpers (for function arguments)
 * ====================================================================== */

static XPathNodeListCell *make_cell(XPathNode *data)
{
    XPathNodeListCell *cell = (XPathNodeListCell *)malloc(sizeof(XPathNodeListCell));
    cell->data = data;
    cell->next = NULL;
    return cell;
}

XPathNodeList *
xpath_node_list_make1(XPathNode *item)
{
    XPathNodeList *list = (XPathNodeList *)malloc(sizeof(XPathNodeList));
    XPathNodeListCell *cell = make_cell(item);
    list->length = 1;
    list->head = cell;
    list->tail = cell;
    return list;
}

XPathNodeList *
xpath_node_list_append(XPathNodeList *list, XPathNode *item)
{
    if (!list)
        return xpath_node_list_make1(item);
    XPathNodeListCell *cell = make_cell(item);
    list->tail->next = cell;
    list->tail = cell;
    list->length++;
    return list;
}

int
xpath_node_list_length(XPathNodeList *list)
{
    return list ? list->length : 0;
}

/* ======================================================================
 * AST printing
 * ====================================================================== */

static const char *axis_name_str(XPathAxisType axis)
{
    switch (axis) {
        case XPATH_AXIS_CHILD:              return "child";
        case XPATH_AXIS_DESCENDANT:         return "descendant";
        case XPATH_AXIS_PARENT:             return "parent";
        case XPATH_AXIS_ANCESTOR:           return "ancestor";
        case XPATH_AXIS_FOLLOWING_SIBLING:  return "following-sibling";
        case XPATH_AXIS_PRECEDING_SIBLING:  return "preceding-sibling";
        case XPATH_AXIS_FOLLOWING:          return "following";
        case XPATH_AXIS_PRECEDING:          return "preceding";
        case XPATH_AXIS_ATTRIBUTE:          return "attribute";
        case XPATH_AXIS_NAMESPACE:          return "namespace";
        case XPATH_AXIS_SELF:               return "self";
        case XPATH_AXIS_DESCENDANT_OR_SELF: return "descendant-or-self";
        case XPATH_AXIS_ANCESTOR_OR_SELF:   return "ancestor-or-self";
    }
    return "???";
}

static const char *op_name_str(XPathOpType op)
{
    switch (op) {
        case XPATH_OP_OR:       return "or";
        case XPATH_OP_AND:      return "and";
        case XPATH_OP_EQ:       return "=";
        case XPATH_OP_NE:       return "!=";
        case XPATH_OP_LT:       return "<";
        case XPATH_OP_LE:       return "<=";
        case XPATH_OP_GT:       return ">";
        case XPATH_OP_GE:       return ">=";
        case XPATH_OP_ADD:      return "+";
        case XPATH_OP_SUB:      return "-";
        case XPATH_OP_MUL:      return "*";
        case XPATH_OP_DIV:      return "div";
        case XPATH_OP_MOD:      return "mod";
        case XPATH_OP_NEGATE:   return "-";
        case XPATH_OP_UNION:    return "|";
    }
    return "???";
}

static const char *node_type_str(XPathNodeTypeTest nt)
{
    switch (nt) {
        case XPATH_NODE_TYPE_COMMENT:   return "comment()";
        case XPATH_NODE_TYPE_TEXT:       return "text()";
        case XPATH_NODE_TYPE_PI:        return "processing-instruction()";
        case XPATH_NODE_TYPE_NODE:      return "node()";
    }
    return "???";
}

static void indent(int depth)
{
    for (int i = 0; i < depth; i++)
        printf("  ");
}

void
xpath_print_ast(XPathNode *node, int depth)
{
    if (!node) return;

    indent(depth);

    switch (node->type) {
        case XPATH_AST_ROOT:
            printf("Root (/)\n");
            break;

        case XPATH_AST_ABSOLUTE_PATH:
            printf("AbsolutePath\n");
            xpath_print_ast(node->value.absolute.path, depth + 1);
            break;

        case XPATH_AST_PATH:
            printf("Path\n");
            xpath_print_ast(node->value.binary.left, depth + 1);
            xpath_print_ast(node->value.binary.right, depth + 1);
            break;

        case XPATH_AST_STEP:
            printf("Step [%s::]\n", axis_name_str(node->value.step.axis));
            xpath_print_ast(node->value.step.node_test, depth + 1);
            if (node->value.step.predicates)
                xpath_print_ast(node->value.step.predicates, depth + 1);
            break;

        case XPATH_AST_NAME_TEST:
            printf("NameTest: %.*s\n",
                   node->value.name_test.name_len,
                   node->value.name_test.name);
            break;

        case XPATH_AST_NODE_TYPE_TEST:
            printf("NodeTypeTest: %s\n",
                   node_type_str(node->value.node_type_test.node_type));
            break;

        case XPATH_AST_PI_TEST:
            printf("PITest: '%.*s'\n",
                   node->value.pi_test.target_len,
                   node->value.pi_test.target);
            break;

        case XPATH_AST_PREDICATE_LIST: {
            printf("Predicate\n");
            xpath_print_ast(node->value.pred_list.first, depth + 1);
            if (node->value.pred_list.rest)
                xpath_print_ast(node->value.pred_list.rest, depth);
            break;
        }

        case XPATH_AST_LITERAL:
            printf("Literal: \"%.*s\"\n",
                   node->value.string.len,
                   node->value.string.val);
            break;

        case XPATH_AST_NUMBER:
            printf("Number: %g\n", node->value.number);
            break;

        case XPATH_AST_VARIABLE_REF:
            printf("VariableRef: $%.*s\n",
                   node->value.string.len,
                   node->value.string.val);
            break;

        case XPATH_AST_FUNCTION_CALL: {
            printf("FunctionCall: %.*s(",
                   node->value.function_call.name_len,
                   node->value.function_call.name);
            if (node->value.function_call.args)
                printf("%d args", node->value.function_call.args->length);
            printf(")\n");
            if (node->value.function_call.args) {
                XPathNodeListCell *cell = node->value.function_call.args->head;
                int argnum = 0;
                while (cell) {
                    indent(depth + 1);
                    printf("arg[%d]:\n", argnum++);
                    xpath_print_ast(cell->data, depth + 2);
                    cell = cell->next;
                }
            }
            break;
        }

        case XPATH_AST_FILTER:
            printf("Filter\n");
            xpath_print_ast(node->value.filter.primary, depth + 1);
            indent(depth + 1);
            printf("Predicate:\n");
            xpath_print_ast(node->value.filter.predicate, depth + 2);
            break;

        case XPATH_AST_BINARY_OP:
            printf("BinaryOp: %s\n", op_name_str(node->value.binary.op));
            xpath_print_ast(node->value.binary.left, depth + 1);
            xpath_print_ast(node->value.binary.right, depth + 1);
            break;

        case XPATH_AST_UNARY_OP:
            printf("UnaryOp: %s\n", op_name_str(node->value.unary.op));
            xpath_print_ast(node->value.unary.operand, depth + 1);
            break;

        case XPATH_AST_PREDICATE:
            printf("Predicate\n");
            break;
    }
}

/* ======================================================================
 * Memory management
 * ====================================================================== */

void
xpath_free_node(XPathNode *node)
{
    if (!node) return;

    switch (node->type) {
        case XPATH_AST_ROOT:
            break;

        case XPATH_AST_ABSOLUTE_PATH:
            xpath_free_node(node->value.absolute.path);
            break;

        case XPATH_AST_PATH:
        case XPATH_AST_BINARY_OP:
            xpath_free_node(node->value.binary.left);
            xpath_free_node(node->value.binary.right);
            break;

        case XPATH_AST_STEP:
            xpath_free_node(node->value.step.node_test);
            xpath_free_node(node->value.step.predicates);
            break;

        case XPATH_AST_NAME_TEST:
            free(node->value.name_test.name);
            break;

        case XPATH_AST_NODE_TYPE_TEST:
            break;

        case XPATH_AST_PI_TEST:
            free(node->value.pi_test.target);
            break;

        case XPATH_AST_PREDICATE_LIST:
            xpath_free_node(node->value.pred_list.first);
            xpath_free_node(node->value.pred_list.rest);
            break;

        case XPATH_AST_LITERAL:
        case XPATH_AST_VARIABLE_REF:
            free(node->value.string.val);
            break;

        case XPATH_AST_NUMBER:
            break;

        case XPATH_AST_FUNCTION_CALL:
            free(node->value.function_call.name);
            if (node->value.function_call.args) {
                XPathNodeListCell *cell = node->value.function_call.args->head;
                while (cell) {
                    XPathNodeListCell *next = cell->next;
                    xpath_free_node(cell->data);
                    free(cell);
                    cell = next;
                }
                free(node->value.function_call.args);
            }
            break;

        case XPATH_AST_FILTER:
            xpath_free_node(node->value.filter.primary);
            xpath_free_node(node->value.filter.predicate);
            break;

        case XPATH_AST_UNARY_OP:
            xpath_free_node(node->value.unary.operand);
            break;

        case XPATH_AST_PREDICATE:
            break;
    }

    free(node);
}
