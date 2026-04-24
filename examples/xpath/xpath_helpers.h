/*
 * xpath_helpers.h
 *    Declarations for XPath grammar helper functions.
 *
 * These functions are used by the Lime-generated parser's reduction
 * actions to build the XPath AST.
 */

#ifndef XPATH_HELPERS_H
#define XPATH_HELPERS_H

#include "xpath_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AST construction helpers */
XPathNode *xpath_make_root(void);
XPathNode *xpath_make_absolute_path(XPathNode *rel);
XPathNode *xpath_make_path(XPathNode *left, XPathNode *right);
XPathNode *xpath_make_step(XPathAxisType axis, XPathNode *node_test,
                           XPathNode *predicates);
XPathNode *xpath_make_name_test(const char *name, int len);
XPathNode *xpath_make_node_type_test(XPathNodeTypeTest type);
XPathNode *xpath_make_pi_test(const char *target, int len);
XPathNode *xpath_make_binary(XPathOpType op, XPathNode *left, XPathNode *right);
XPathNode *xpath_make_unary(XPathOpType op, XPathNode *operand);
XPathNode *xpath_make_literal(const char *val, int len);
XPathNode *xpath_make_number(double val);
XPathNode *xpath_make_variable_ref(const char *name, int len);
XPathNode *xpath_make_function_call(const char *name, int name_len,
                                    XPathNodeList *args);
XPathNode *xpath_make_filter(XPathNode *primary, XPathNode *predicate);
XPathNode *xpath_append_predicate(XPathNode *list, XPathNode *pred);

/* List helpers for function arguments */
XPathNodeList *xpath_node_list_make1(XPathNode *item);
XPathNodeList *xpath_node_list_append(XPathNodeList *list, XPathNode *item);
int xpath_node_list_length(XPathNodeList *list);

/* AST printing */
void xpath_print_ast(XPathNode *node, int depth);

/* Memory management */
void xpath_free_node(XPathNode *node);

#ifdef __cplusplus
}
#endif

#endif /* XPATH_HELPERS_H */
