/*
 * xquery_helpers.h
 *    Declarations for XQuery grammar helper functions.
 *
 * These functions are used by the Lime-generated parser's reduction
 * actions to build the XQuery AST. Includes both inherited XPath
 * constructors and XQuery-specific constructors.
 */

#ifndef XQUERY_HELPERS_H
#define XQUERY_HELPERS_H

#include "xquery_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- List helpers --- */
XQNodeList *xq_list_make1(XQNode *item);
XQNodeList *xq_list_append(XQNodeList *list, XQNode *item);
int         xq_list_length(XQNodeList *list);

/* --- XPath expression constructors (inherited) --- */
XQNode *xq_make_root(void);
XQNode *xq_make_absolute_path(XQNode *rel);
XQNode *xq_make_path(XQNode *left, XQNode *right);
XQNode *xq_make_step(XPathAxisType axis, XQNode *node_test, XQNode *predicates);
XQNode *xq_make_name_test(const char *name, int len);
XQNode *xq_make_node_type_test(XPathNodeTypeTest type);
XQNode *xq_make_pi_test(const char *target, int len);
XQNode *xq_make_binary(XQOpType op, XQNode *left, XQNode *right);
XQNode *xq_make_unary(XQOpType op, XQNode *operand);
XQNode *xq_make_literal(const char *val, int len);
XQNode *xq_make_number(double val);
XQNode *xq_make_variable_ref(const char *name, int len);
XQNode *xq_make_function_call(const char *name, int name_len, XQNodeList *args);
XQNode *xq_make_filter(XQNode *primary, XQNode *predicate);
XQNode *xq_append_predicate(XQNode *list, XQNode *pred);

/* --- XQuery module structure --- */
XQNode *xq_make_module(XQNode *prolog, XQNode *body);
XQNode *xq_make_prolog(XQNodeList *decls);
XQNode *xq_make_query_body(XQNode *expr);

/* --- Prolog declarations --- */
XQNode *xq_make_ns_decl(const char *prefix, int prefix_len,
                         const char *uri, int uri_len);
XQNode *xq_make_default_ns_decl(const char *uri, int uri_len);
XQNode *xq_make_function_decl(const char *name, int name_len,
                               XQNodeList *params, XQNode *ret_type,
                               XQNode *body);
XQNode *xq_make_variable_decl(const char *name, int name_len,
                               XQNode *type_decl, XQNode *init_expr);
XQNode *xq_make_param(const char *name, int name_len, XQNode *type_decl);

/* --- FLWOR expressions --- */
XQNode *xq_make_flwor(XQNodeList *clauses, XQNode *where,
                       XQNode *order_by, XQNode *ret);
XQNode *xq_make_for_clause(const char *var, int var_len, XQNode *in_expr,
                            const char *pos_var, int pos_var_len);
XQNode *xq_make_let_clause(const char *var, int var_len, XQNode *bind_expr);
XQNode *xq_make_where_clause(XQNode *condition);
XQNode *xq_make_order_by(XQNodeList *specs, bool stable);
XQNode *xq_make_order_spec(XQNode *expr, XQSortOrder order);

/* --- Quantified expressions --- */
XQNode *xq_make_some_expr(XQNodeList *bindings, XQNode *satisfies);
XQNode *xq_make_every_expr(XQNodeList *bindings, XQNode *satisfies);
XQNode *xq_make_quant_binding(const char *var, int var_len, XQNode *in_expr);

/* --- Conditional --- */
XQNode *xq_make_if_expr(XQNode *cond, XQNode *then_expr, XQNode *else_expr);

/* --- Constructors --- */
XQNode *xq_make_comp_elem(const char *name, int name_len, XQNode *content);
XQNode *xq_make_comp_attr(const char *name, int name_len, XQNode *value);
XQNode *xq_make_comp_doc(XQNode *content);
XQNode *xq_make_comp_text(XQNode *value);
XQNode *xq_make_comp_comment(XQNode *value);
XQNode *xq_make_comp_pi(const char *target, int target_len, XQNode *value);
XQNode *xq_make_enclosed_expr(XQNode *expr);

/* --- Type expressions --- */
XQNode *xq_make_instance_of(XQNode *expr, XQNode *type);
XQNode *xq_make_treat_as(XQNode *expr, XQNode *type);
XQNode *xq_make_castable_as(XQNode *expr, XQNode *type);
XQNode *xq_make_cast_as(XQNode *expr, XQNode *type);
XQNode *xq_make_sequence_type(const char *name, int name_len, int occurrence);
XQNode *xq_make_empty_sequence(void);

/* --- Typeswitch --- */
XQNode *xq_make_typeswitch(XQNode *operand, XQNodeList *cases,
                            XQNode *default_expr,
                            const char *default_var, int default_var_len);
XQNode *xq_make_case_clause(const char *var, int var_len,
                             XQNode *case_type, XQNode *case_expr);

/* --- Sequence --- */
XQNode *xq_make_sequence(XQNodeList *items);

/* --- AST printing --- */
void xq_print_ast(XQNode *node, int depth);

#ifdef __cplusplus
}
#endif

#endif /* XQUERY_HELPERS_H */
