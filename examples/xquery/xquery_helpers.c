/*
 * xquery_helpers.c
 *    Helper functions for the XQuery 1.0 Lime grammar.
 *
 * These functions are used by the Lime-generated parser's reduction
 * actions to build the XQuery AST. Includes both inherited XPath
 * constructors and XQuery-specific constructors for FLWOR, element
 * constructors, function declarations, etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "xquery_helpers.h"

/* ======================================================================
 * Internal allocation helpers
 * ====================================================================== */

static XQNode *make_node(XQNodeType type)
{
    XQNode *n = (XQNode *)calloc(1, sizeof(XQNode));
    n->type = type;
    return n;
}

static char *str_dup(const char *s, int len)
{
    if (!s || len <= 0) return NULL;
    char *d = (char *)malloc(len + 1);
    memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

/* ======================================================================
 * List helpers
 * ====================================================================== */

static XQNodeListCell *make_cell(XQNode *data)
{
    XQNodeListCell *cell = (XQNodeListCell *)malloc(sizeof(XQNodeListCell));
    cell->data = data;
    cell->next = NULL;
    return cell;
}

XQNodeList *xq_list_make1(XQNode *item)
{
    XQNodeList *list = (XQNodeList *)malloc(sizeof(XQNodeList));
    XQNodeListCell *cell = make_cell(item);
    list->length = 1;
    list->head = cell;
    list->tail = cell;
    return list;
}

XQNodeList *xq_list_append(XQNodeList *list, XQNode *item)
{
    if (!list) return xq_list_make1(item);
    XQNodeListCell *cell = make_cell(item);
    list->tail->next = cell;
    list->tail = cell;
    list->length++;
    return list;
}

int xq_list_length(XQNodeList *list)
{
    return list ? list->length : 0;
}

/* ======================================================================
 * XPath expression constructors (inherited from XPath 1.0)
 * ====================================================================== */

XQNode *xq_make_root(void)
{
    return make_node(XQ_AST_ROOT);
}

XQNode *xq_make_absolute_path(XQNode *rel)
{
    XQNode *n = make_node(XQ_AST_ABSOLUTE_PATH);
    n->value.absolute.path = rel;
    return n;
}

XQNode *xq_make_path(XQNode *left, XQNode *right)
{
    XQNode *n = make_node(XQ_AST_PATH);
    n->value.binary.left = left;
    n->value.binary.right = right;
    return n;
}

XQNode *xq_make_step(XPathAxisType axis, XQNode *node_test, XQNode *predicates)
{
    XQNode *n = make_node(XQ_AST_STEP);
    n->value.step.axis = axis;
    n->value.step.node_test = node_test;
    n->value.step.predicates = predicates;
    return n;
}

XQNode *xq_make_name_test(const char *name, int len)
{
    XQNode *n = make_node(XQ_AST_NAME_TEST);
    n->value.name_test.name = str_dup(name, len);
    n->value.name_test.name_len = len;
    return n;
}

XQNode *xq_make_node_type_test(XPathNodeTypeTest type)
{
    XQNode *n = make_node(XQ_AST_NODE_TYPE_TEST);
    n->value.node_type_test.node_type = type;
    return n;
}

XQNode *xq_make_pi_test(const char *target, int len)
{
    XQNode *n = make_node(XQ_AST_PI_TEST);
    n->value.pi_test.target = str_dup(target, len);
    n->value.pi_test.target_len = len;
    return n;
}

XQNode *xq_make_binary(XQOpType op, XQNode *left, XQNode *right)
{
    XQNode *n = make_node(XQ_AST_BINARY_OP);
    n->value.binary.op = op;
    n->value.binary.left = left;
    n->value.binary.right = right;
    return n;
}

XQNode *xq_make_unary(XQOpType op, XQNode *operand)
{
    XQNode *n = make_node(XQ_AST_UNARY_OP);
    n->value.unary.op = op;
    n->value.unary.operand = operand;
    return n;
}

XQNode *xq_make_literal(const char *val, int len)
{
    XQNode *n = make_node(XQ_AST_LITERAL);
    n->value.string.val = str_dup(val, len);
    n->value.string.len = len;
    return n;
}

XQNode *xq_make_number(double val)
{
    XQNode *n = make_node(XQ_AST_NUMBER);
    n->value.number = val;
    return n;
}

XQNode *xq_make_variable_ref(const char *name, int len)
{
    XQNode *n = make_node(XQ_AST_VARIABLE_REF);
    n->value.string.val = str_dup(name, len);
    n->value.string.len = len;
    return n;
}

XQNode *xq_make_function_call(const char *name, int name_len, XQNodeList *args)
{
    XQNode *n = make_node(XQ_AST_FUNCTION_CALL);
    n->value.function_call.name = str_dup(name, name_len);
    n->value.function_call.name_len = name_len;
    n->value.function_call.args = args;
    return n;
}

XQNode *xq_make_filter(XQNode *primary, XQNode *predicate)
{
    XQNode *n = make_node(XQ_AST_FILTER);
    n->value.filter.primary = primary;
    n->value.filter.predicate = predicate;
    return n;
}

XQNode *xq_append_predicate(XQNode *list, XQNode *pred)
{
    XQNode *n = make_node(XQ_AST_PREDICATE_LIST);
    n->value.pred_list.first = pred;
    n->value.pred_list.rest = NULL;

    if (list == NULL) return n;

    XQNode *tail = list;
    while (tail->type == XQ_AST_PREDICATE_LIST && tail->value.pred_list.rest != NULL)
        tail = tail->value.pred_list.rest;
    tail->value.pred_list.rest = n;
    return list;
}

/* ======================================================================
 * XQuery module structure
 * ====================================================================== */

XQNode *xq_make_module(XQNode *prolog, XQNode *body)
{
    XQNode *n = make_node(XQ_AST_MODULE);
    n->value.module.prolog = prolog;
    n->value.module.body = body;
    return n;
}

XQNode *xq_make_prolog(XQNodeList *decls)
{
    XQNode *n = make_node(XQ_AST_PROLOG);
    n->value.prolog.declarations = decls;
    return n;
}

XQNode *xq_make_query_body(XQNode *expr)
{
    XQNode *n = make_node(XQ_AST_QUERY_BODY);
    n->value.query_body.expr = expr;
    return n;
}

/* ======================================================================
 * Prolog declarations
 * ====================================================================== */

XQNode *xq_make_ns_decl(const char *prefix, int prefix_len,
                         const char *uri, int uri_len)
{
    XQNode *n = make_node(XQ_AST_NS_DECL);
    n->value.ns_decl.prefix = str_dup(prefix, prefix_len);
    n->value.ns_decl.prefix_len = prefix_len;
    n->value.ns_decl.uri = str_dup(uri, uri_len);
    n->value.ns_decl.uri_len = uri_len;
    return n;
}

XQNode *xq_make_default_ns_decl(const char *uri, int uri_len)
{
    XQNode *n = make_node(XQ_AST_DEFAULT_NS_DECL);
    n->value.ns_decl.prefix = NULL;
    n->value.ns_decl.prefix_len = 0;
    n->value.ns_decl.uri = str_dup(uri, uri_len);
    n->value.ns_decl.uri_len = uri_len;
    return n;
}

XQNode *xq_make_function_decl(const char *name, int name_len,
                               XQNodeList *params, XQNode *ret_type,
                               XQNode *body)
{
    XQNode *n = make_node(XQ_AST_FUNCTION_DECL);
    n->value.func_decl.name = str_dup(name, name_len);
    n->value.func_decl.name_len = name_len;
    n->value.func_decl.params = params;
    n->value.func_decl.return_type = ret_type;
    n->value.func_decl.body = body;
    return n;
}

XQNode *xq_make_variable_decl(const char *name, int name_len,
                               XQNode *type_decl, XQNode *init_expr)
{
    XQNode *n = make_node(XQ_AST_VARIABLE_DECL);
    n->value.var_decl.name = str_dup(name, name_len);
    n->value.var_decl.name_len = name_len;
    n->value.var_decl.type_decl = type_decl;
    n->value.var_decl.init_expr = init_expr;
    return n;
}

XQNode *xq_make_param(const char *name, int name_len, XQNode *type_decl)
{
    XQNode *n = make_node(XQ_AST_PARAM);
    n->value.param.name = str_dup(name, name_len);
    n->value.param.name_len = name_len;
    n->value.param.type_decl = type_decl;
    return n;
}

/* ======================================================================
 * FLWOR expressions
 * ====================================================================== */

XQNode *xq_make_flwor(XQNodeList *clauses, XQNode *where,
                       XQNode *order_by, XQNode *ret)
{
    XQNode *n = make_node(XQ_AST_FLWOR);
    n->value.flwor.clauses = clauses;
    n->value.flwor.where = where;
    n->value.flwor.order_by = order_by;
    n->value.flwor.ret = ret;
    return n;
}

XQNode *xq_make_for_clause(const char *var, int var_len, XQNode *in_expr,
                            const char *pos_var, int pos_var_len)
{
    XQNode *n = make_node(XQ_AST_FOR_CLAUSE);
    n->value.for_clause.var_name = str_dup(var, var_len);
    n->value.for_clause.var_name_len = var_len;
    n->value.for_clause.in_expr = in_expr;
    n->value.for_clause.pos_var = str_dup(pos_var, pos_var_len);
    n->value.for_clause.pos_var_len = pos_var_len;
    return n;
}

XQNode *xq_make_let_clause(const char *var, int var_len, XQNode *bind_expr)
{
    XQNode *n = make_node(XQ_AST_LET_CLAUSE);
    n->value.let_clause.var_name = str_dup(var, var_len);
    n->value.let_clause.var_name_len = var_len;
    n->value.let_clause.bind_expr = bind_expr;
    return n;
}

XQNode *xq_make_where_clause(XQNode *condition)
{
    XQNode *n = make_node(XQ_AST_WHERE_CLAUSE);
    n->value.where_clause.condition = condition;
    return n;
}

XQNode *xq_make_order_by(XQNodeList *specs, bool stable)
{
    XQNode *n = make_node(XQ_AST_ORDER_BY_CLAUSE);
    n->value.order_by.specs = specs;
    n->value.order_by.stable = stable;
    return n;
}

XQNode *xq_make_order_spec(XQNode *expr, XQSortOrder order)
{
    XQNode *n = make_node(XQ_AST_ORDER_SPEC);
    n->value.order_spec.expr = expr;
    n->value.order_spec.order = order;
    return n;
}

/* ======================================================================
 * Quantified expressions
 * ====================================================================== */

XQNode *xq_make_some_expr(XQNodeList *bindings, XQNode *satisfies)
{
    XQNode *n = make_node(XQ_AST_SOME_EXPR);
    n->value.quantified.bindings = bindings;
    n->value.quantified.satisfies = satisfies;
    return n;
}

XQNode *xq_make_every_expr(XQNodeList *bindings, XQNode *satisfies)
{
    XQNode *n = make_node(XQ_AST_EVERY_EXPR);
    n->value.quantified.bindings = bindings;
    n->value.quantified.satisfies = satisfies;
    return n;
}

XQNode *xq_make_quant_binding(const char *var, int var_len, XQNode *in_expr)
{
    XQNode *n = make_node(XQ_AST_QUANT_BINDING);
    n->value.quant_binding.var_name = str_dup(var, var_len);
    n->value.quant_binding.var_name_len = var_len;
    n->value.quant_binding.in_expr = in_expr;
    return n;
}

/* ======================================================================
 * Conditional
 * ====================================================================== */

XQNode *xq_make_if_expr(XQNode *cond, XQNode *then_expr, XQNode *else_expr)
{
    XQNode *n = make_node(XQ_AST_IF_EXPR);
    n->value.if_expr.condition = cond;
    n->value.if_expr.then_expr = then_expr;
    n->value.if_expr.else_expr = else_expr;
    return n;
}

/* ======================================================================
 * Constructors
 * ====================================================================== */

XQNode *xq_make_comp_elem(const char *name, int name_len, XQNode *content)
{
    XQNode *n = make_node(XQ_AST_COMP_ELEM);
    n->value.element.tag_name = str_dup(name, name_len);
    n->value.element.tag_name_len = name_len;
    n->value.element.content = content;
    return n;
}

XQNode *xq_make_comp_attr(const char *name, int name_len, XQNode *value)
{
    XQNode *n = make_node(XQ_AST_COMP_ATTR);
    n->value.comp_attr.attr_name = str_dup(name, name_len);
    n->value.comp_attr.attr_name_len = name_len;
    n->value.comp_attr.attr_value = value;
    return n;
}

XQNode *xq_make_comp_doc(XQNode *content)
{
    XQNode *n = make_node(XQ_AST_COMP_DOC);
    n->value.enclosed.content = content;
    return n;
}

XQNode *xq_make_comp_text(XQNode *value)
{
    XQNode *n = make_node(XQ_AST_COMP_TEXT);
    n->value.enclosed.content = value;
    return n;
}

XQNode *xq_make_comp_comment(XQNode *value)
{
    XQNode *n = make_node(XQ_AST_COMP_COMMENT);
    n->value.enclosed.content = value;
    return n;
}

XQNode *xq_make_comp_pi(const char *target, int target_len, XQNode *value)
{
    XQNode *n = make_node(XQ_AST_COMP_PI);
    n->value.comp_pi.pi_target = str_dup(target, target_len);
    n->value.comp_pi.pi_target_len = target_len;
    n->value.comp_pi.pi_content = value;
    return n;
}

XQNode *xq_make_enclosed_expr(XQNode *expr)
{
    XQNode *n = make_node(XQ_AST_ENCLOSED_EXPR);
    n->value.enclosed.content = expr;
    return n;
}

/* ======================================================================
 * Type expressions
 * ====================================================================== */

XQNode *xq_make_instance_of(XQNode *expr, XQNode *type)
{
    XQNode *n = make_node(XQ_AST_INSTANCE_OF);
    n->value.type_expr.expr = expr;
    n->value.type_expr.type = type;
    return n;
}

XQNode *xq_make_treat_as(XQNode *expr, XQNode *type)
{
    XQNode *n = make_node(XQ_AST_TREAT_AS);
    n->value.type_expr.expr = expr;
    n->value.type_expr.type = type;
    return n;
}

XQNode *xq_make_castable_as(XQNode *expr, XQNode *type)
{
    XQNode *n = make_node(XQ_AST_CASTABLE_AS);
    n->value.type_expr.expr = expr;
    n->value.type_expr.type = type;
    return n;
}

XQNode *xq_make_cast_as(XQNode *expr, XQNode *type)
{
    XQNode *n = make_node(XQ_AST_CAST_AS);
    n->value.type_expr.expr = expr;
    n->value.type_expr.type = type;
    return n;
}

XQNode *xq_make_sequence_type(const char *name, int name_len, int occurrence)
{
    XQNode *n = make_node(XQ_AST_SEQUENCE_TYPE);
    n->value.seq_type.type_name = str_dup(name, name_len);
    n->value.seq_type.type_name_len = name_len;
    n->value.seq_type.occurrence = occurrence;
    return n;
}

XQNode *xq_make_empty_sequence(void)
{
    return make_node(XQ_AST_EMPTY_SEQUENCE);
}

/* ======================================================================
 * Typeswitch
 * ====================================================================== */

XQNode *xq_make_typeswitch(XQNode *operand, XQNodeList *cases,
                            XQNode *default_expr,
                            const char *default_var, int default_var_len)
{
    XQNode *n = make_node(XQ_AST_TYPESWITCH);
    n->value.typeswitch.operand = operand;
    n->value.typeswitch.cases = cases;
    n->value.typeswitch.default_expr = default_expr;
    n->value.typeswitch.default_var = str_dup(default_var, default_var_len);
    n->value.typeswitch.default_var_len = default_var_len;
    return n;
}

XQNode *xq_make_case_clause(const char *var, int var_len,
                             XQNode *case_type, XQNode *case_expr)
{
    XQNode *n = make_node(XQ_AST_CASE_CLAUSE);
    n->value.case_clause.var_name = str_dup(var, var_len);
    n->value.case_clause.var_name_len = var_len;
    n->value.case_clause.case_type = case_type;
    n->value.case_clause.case_expr = case_expr;
    return n;
}

/* ======================================================================
 * Sequence
 * ====================================================================== */

XQNode *xq_make_sequence(XQNodeList *items)
{
    XQNode *n = make_node(XQ_AST_SEQUENCE);
    n->value.sequence.items = items;
    return n;
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

static const char *op_name_str(XQOpType op)
{
    switch (op) {
        case XQ_OP_OR:       return "or";
        case XQ_OP_AND:      return "and";
        case XQ_OP_EQ: case XQ_OP_GEN_EQ: case XQ_OP_VAL_EQ: return "eq";
        case XQ_OP_NE: case XQ_OP_GEN_NE: case XQ_OP_VAL_NE: return "ne";
        case XQ_OP_GEN_LT: case XQ_OP_VAL_LT: return "lt";
        case XQ_OP_GEN_LE: case XQ_OP_VAL_LE: return "le";
        case XQ_OP_GEN_GT: case XQ_OP_VAL_GT: return "gt";
        case XQ_OP_GEN_GE: case XQ_OP_VAL_GE: return "ge";
        case XQ_OP_IS:           return "is";
        case XQ_OP_PRECEDES:     return "<<";
        case XQ_OP_FOLLOWS:      return ">>";
        case XQ_OP_ADD:          return "+";
        case XQ_OP_SUB:          return "-";
        case XQ_OP_MUL:          return "*";
        case XQ_OP_DIV:          return "div";
        case XQ_OP_IDIV:         return "idiv";
        case XQ_OP_MOD:          return "mod";
        case XQ_OP_NEGATE:       return "unary-";
        case XQ_OP_POSITIVE:     return "unary+";
        case XQ_OP_UNION:        return "union";
        case XQ_OP_INTERSECT:    return "intersect";
        case XQ_OP_EXCEPT:       return "except";
        case XQ_OP_TO:           return "to";
        case XQ_OP_STRING_CONCAT:return "||";
    }
    return "???";
}

static const char *node_type_str(XPathNodeTypeTest nt)
{
    switch (nt) {
        case XPATH_NODE_TYPE_COMMENT:           return "comment()";
        case XPATH_NODE_TYPE_TEXT:               return "text()";
        case XPATH_NODE_TYPE_PI:                return "processing-instruction()";
        case XPATH_NODE_TYPE_NODE:              return "node()";
        case XPATH_NODE_TYPE_DOCUMENT_NODE:     return "document-node()";
        case XPATH_NODE_TYPE_ELEMENT:           return "element()";
        case XPATH_NODE_TYPE_ATTRIBUTE:         return "attribute()";
        case XPATH_NODE_TYPE_SCHEMA_ELEMENT:    return "schema-element()";
        case XPATH_NODE_TYPE_SCHEMA_ATTRIBUTE:  return "schema-attribute()";
    }
    return "???";
}

static void do_indent(int depth)
{
    for (int i = 0; i < depth; i++)
        printf("  ");
}

static void print_list(const char *label, XQNodeList *list, int depth)
{
    if (!list) return;
    XQNodeListCell *cell = list->head;
    int idx = 0;
    while (cell) {
        do_indent(depth);
        printf("%s[%d]:\n", label, idx++);
        xq_print_ast(cell->data, depth + 1);
        cell = cell->next;
    }
}

void xq_print_ast(XQNode *node, int depth)
{
    if (!node) return;

    do_indent(depth);

    switch (node->type) {
        case XQ_AST_ROOT:
            printf("Root (/)\n");
            break;

        case XQ_AST_ABSOLUTE_PATH:
            printf("AbsolutePath\n");
            xq_print_ast(node->value.absolute.path, depth + 1);
            break;

        case XQ_AST_PATH:
            printf("Path\n");
            xq_print_ast(node->value.binary.left, depth + 1);
            xq_print_ast(node->value.binary.right, depth + 1);
            break;

        case XQ_AST_STEP:
            printf("Step [%s::]\n", axis_name_str(node->value.step.axis));
            xq_print_ast(node->value.step.node_test, depth + 1);
            if (node->value.step.predicates)
                xq_print_ast(node->value.step.predicates, depth + 1);
            break;

        case XQ_AST_NAME_TEST:
            printf("NameTest: %.*s\n", node->value.name_test.name_len,
                   node->value.name_test.name);
            break;

        case XQ_AST_NODE_TYPE_TEST:
            printf("NodeTypeTest: %s\n",
                   node_type_str(node->value.node_type_test.node_type));
            break;

        case XQ_AST_PI_TEST:
            printf("PITest: '%.*s'\n", node->value.pi_test.target_len,
                   node->value.pi_test.target);
            break;

        case XQ_AST_PREDICATE_LIST:
            printf("Predicate\n");
            xq_print_ast(node->value.pred_list.first, depth + 1);
            if (node->value.pred_list.rest)
                xq_print_ast(node->value.pred_list.rest, depth);
            break;

        case XQ_AST_LITERAL:
            printf("Literal: \"%.*s\"\n", node->value.string.len,
                   node->value.string.val);
            break;

        case XQ_AST_NUMBER:
            printf("Number: %g\n", node->value.number);
            break;

        case XQ_AST_VARIABLE_REF:
            printf("VarRef: $%.*s\n", node->value.string.len,
                   node->value.string.val);
            break;

        case XQ_AST_FUNCTION_CALL:
            printf("FunctionCall: %.*s(%d args)\n",
                   node->value.function_call.name_len,
                   node->value.function_call.name,
                   xq_list_length(node->value.function_call.args));
            if (node->value.function_call.args)
                print_list("arg", node->value.function_call.args, depth + 1);
            break;

        case XQ_AST_FILTER:
            printf("Filter\n");
            xq_print_ast(node->value.filter.primary, depth + 1);
            do_indent(depth + 1); printf("Predicate:\n");
            xq_print_ast(node->value.filter.predicate, depth + 2);
            break;

        case XQ_AST_BINARY_OP:
            printf("BinaryOp: %s\n", op_name_str(node->value.binary.op));
            xq_print_ast(node->value.binary.left, depth + 1);
            xq_print_ast(node->value.binary.right, depth + 1);
            break;

        case XQ_AST_UNARY_OP:
            printf("UnaryOp: %s\n", op_name_str(node->value.unary.op));
            xq_print_ast(node->value.unary.operand, depth + 1);
            break;

        /* --- XQuery-specific --- */

        case XQ_AST_MODULE:
            printf("Module\n");
            xq_print_ast(node->value.module.prolog, depth + 1);
            xq_print_ast(node->value.module.body, depth + 1);
            break;

        case XQ_AST_PROLOG:
            printf("Prolog\n");
            if (node->value.prolog.declarations)
                print_list("decl", node->value.prolog.declarations, depth + 1);
            break;

        case XQ_AST_QUERY_BODY:
            printf("QueryBody\n");
            xq_print_ast(node->value.query_body.expr, depth + 1);
            break;

        case XQ_AST_NS_DECL:
            printf("NsDecl: %.*s = \"%.*s\"\n",
                   node->value.ns_decl.prefix_len,
                   node->value.ns_decl.prefix ? node->value.ns_decl.prefix : "",
                   node->value.ns_decl.uri_len,
                   node->value.ns_decl.uri);
            break;

        case XQ_AST_DEFAULT_NS_DECL:
            printf("DefaultNsDecl: \"%.*s\"\n",
                   node->value.ns_decl.uri_len,
                   node->value.ns_decl.uri);
            break;

        case XQ_AST_FUNCTION_DECL:
            printf("FunctionDecl: %.*s\n",
                   node->value.func_decl.name_len,
                   node->value.func_decl.name);
            if (node->value.func_decl.params)
                print_list("param", node->value.func_decl.params, depth + 1);
            if (node->value.func_decl.return_type) {
                do_indent(depth + 1); printf("returns:\n");
                xq_print_ast(node->value.func_decl.return_type, depth + 2);
            }
            if (node->value.func_decl.body) {
                do_indent(depth + 1); printf("body:\n");
                xq_print_ast(node->value.func_decl.body, depth + 2);
            }
            break;

        case XQ_AST_VARIABLE_DECL:
            printf("VarDecl: $%.*s\n",
                   node->value.var_decl.name_len,
                   node->value.var_decl.name);
            if (node->value.var_decl.init_expr) {
                do_indent(depth + 1); printf("init:\n");
                xq_print_ast(node->value.var_decl.init_expr, depth + 2);
            }
            break;

        case XQ_AST_PARAM:
            printf("Param: $%.*s\n",
                   node->value.param.name_len,
                   node->value.param.name);
            if (node->value.param.type_decl)
                xq_print_ast(node->value.param.type_decl, depth + 1);
            break;

        case XQ_AST_FLWOR:
            printf("FLWOR\n");
            if (node->value.flwor.clauses)
                print_list("clause", node->value.flwor.clauses, depth + 1);
            if (node->value.flwor.where) {
                do_indent(depth + 1); printf("where:\n");
                xq_print_ast(node->value.flwor.where, depth + 2);
            }
            if (node->value.flwor.order_by) {
                do_indent(depth + 1); printf("order-by:\n");
                xq_print_ast(node->value.flwor.order_by, depth + 2);
            }
            do_indent(depth + 1); printf("return:\n");
            xq_print_ast(node->value.flwor.ret, depth + 2);
            break;

        case XQ_AST_FOR_CLAUSE:
            printf("ForClause: $%.*s",
                   node->value.for_clause.var_name_len,
                   node->value.for_clause.var_name);
            if (node->value.for_clause.pos_var)
                printf(" at $%.*s", node->value.for_clause.pos_var_len,
                       node->value.for_clause.pos_var);
            printf("\n");
            do_indent(depth + 1); printf("in:\n");
            xq_print_ast(node->value.for_clause.in_expr, depth + 2);
            break;

        case XQ_AST_LET_CLAUSE:
            printf("LetClause: $%.*s\n",
                   node->value.let_clause.var_name_len,
                   node->value.let_clause.var_name);
            do_indent(depth + 1); printf(":=\n");
            xq_print_ast(node->value.let_clause.bind_expr, depth + 2);
            break;

        case XQ_AST_WHERE_CLAUSE:
            printf("WhereClause\n");
            xq_print_ast(node->value.where_clause.condition, depth + 1);
            break;

        case XQ_AST_ORDER_BY_CLAUSE:
            printf("OrderBy%s\n", node->value.order_by.stable ? " (stable)" : "");
            if (node->value.order_by.specs)
                print_list("spec", node->value.order_by.specs, depth + 1);
            break;

        case XQ_AST_ORDER_SPEC:
            printf("OrderSpec: %s\n",
                   node->value.order_spec.order == XQ_SORT_ASCENDING ? "ascending" : "descending");
            xq_print_ast(node->value.order_spec.expr, depth + 1);
            break;

        case XQ_AST_SOME_EXPR:
            printf("SomeExpr\n");
            if (node->value.quantified.bindings)
                print_list("binding", node->value.quantified.bindings, depth + 1);
            do_indent(depth + 1); printf("satisfies:\n");
            xq_print_ast(node->value.quantified.satisfies, depth + 2);
            break;

        case XQ_AST_EVERY_EXPR:
            printf("EveryExpr\n");
            if (node->value.quantified.bindings)
                print_list("binding", node->value.quantified.bindings, depth + 1);
            do_indent(depth + 1); printf("satisfies:\n");
            xq_print_ast(node->value.quantified.satisfies, depth + 2);
            break;

        case XQ_AST_QUANT_BINDING:
            printf("QuantBinding: $%.*s\n",
                   node->value.quant_binding.var_name_len,
                   node->value.quant_binding.var_name);
            do_indent(depth + 1); printf("in:\n");
            xq_print_ast(node->value.quant_binding.in_expr, depth + 2);
            break;

        case XQ_AST_IF_EXPR:
            printf("IfExpr\n");
            do_indent(depth + 1); printf("condition:\n");
            xq_print_ast(node->value.if_expr.condition, depth + 2);
            do_indent(depth + 1); printf("then:\n");
            xq_print_ast(node->value.if_expr.then_expr, depth + 2);
            do_indent(depth + 1); printf("else:\n");
            xq_print_ast(node->value.if_expr.else_expr, depth + 2);
            break;

        case XQ_AST_COMP_ELEM:
            printf("CompElem: %.*s\n", node->value.element.tag_name_len,
                   node->value.element.tag_name);
            if (node->value.element.content) {
                do_indent(depth + 1); printf("content:\n");
                xq_print_ast(node->value.element.content, depth + 2);
            }
            break;

        case XQ_AST_COMP_ATTR:
            printf("CompAttr: %.*s\n", node->value.comp_attr.attr_name_len,
                   node->value.comp_attr.attr_name);
            if (node->value.comp_attr.attr_value) {
                do_indent(depth + 1); printf("value:\n");
                xq_print_ast(node->value.comp_attr.attr_value, depth + 2);
            }
            break;

        case XQ_AST_COMP_DOC:
            printf("CompDoc\n");
            xq_print_ast(node->value.enclosed.content, depth + 1);
            break;

        case XQ_AST_COMP_TEXT:
            printf("CompText\n");
            xq_print_ast(node->value.enclosed.content, depth + 1);
            break;

        case XQ_AST_COMP_COMMENT:
            printf("CompComment\n");
            xq_print_ast(node->value.enclosed.content, depth + 1);
            break;

        case XQ_AST_COMP_PI:
            printf("CompPI: %.*s\n", node->value.comp_pi.pi_target_len,
                   node->value.comp_pi.pi_target);
            if (node->value.comp_pi.pi_content)
                xq_print_ast(node->value.comp_pi.pi_content, depth + 1);
            break;

        case XQ_AST_ENCLOSED_EXPR:
            printf("EnclosedExpr\n");
            xq_print_ast(node->value.enclosed.content, depth + 1);
            break;

        case XQ_AST_INSTANCE_OF:
            printf("InstanceOf\n");
            xq_print_ast(node->value.type_expr.expr, depth + 1);
            xq_print_ast(node->value.type_expr.type, depth + 1);
            break;

        case XQ_AST_TREAT_AS:
            printf("TreatAs\n");
            xq_print_ast(node->value.type_expr.expr, depth + 1);
            xq_print_ast(node->value.type_expr.type, depth + 1);
            break;

        case XQ_AST_CASTABLE_AS:
            printf("CastableAs\n");
            xq_print_ast(node->value.type_expr.expr, depth + 1);
            xq_print_ast(node->value.type_expr.type, depth + 1);
            break;

        case XQ_AST_CAST_AS:
            printf("CastAs\n");
            xq_print_ast(node->value.type_expr.expr, depth + 1);
            xq_print_ast(node->value.type_expr.type, depth + 1);
            break;

        case XQ_AST_SEQUENCE_TYPE:
            printf("SequenceType: %.*s",
                   node->value.seq_type.type_name_len,
                   node->value.seq_type.type_name);
            if (node->value.seq_type.occurrence)
                printf("%c", node->value.seq_type.occurrence);
            printf("\n");
            break;

        case XQ_AST_EMPTY_SEQUENCE:
            printf("empty-sequence()\n");
            break;

        case XQ_AST_TYPESWITCH:
            printf("TypeSwitch\n");
            xq_print_ast(node->value.typeswitch.operand, depth + 1);
            if (node->value.typeswitch.cases)
                print_list("case", node->value.typeswitch.cases, depth + 1);
            if (node->value.typeswitch.default_expr) {
                do_indent(depth + 1); printf("default:\n");
                xq_print_ast(node->value.typeswitch.default_expr, depth + 2);
            }
            break;

        case XQ_AST_CASE_CLAUSE:
            printf("CaseClause");
            if (node->value.case_clause.var_name)
                printf(": $%.*s", node->value.case_clause.var_name_len,
                       node->value.case_clause.var_name);
            printf("\n");
            if (node->value.case_clause.case_type)
                xq_print_ast(node->value.case_clause.case_type, depth + 1);
            do_indent(depth + 1); printf("return:\n");
            xq_print_ast(node->value.case_clause.case_expr, depth + 2);
            break;

        case XQ_AST_SEQUENCE:
            printf("Sequence\n");
            if (node->value.sequence.items)
                print_list("item", node->value.sequence.items, depth + 1);
            break;

        case XQ_AST_PREDICATE:
        case XQ_AST_OPTION_DECL:
        case XQ_AST_IMPORT_SCHEMA:
        case XQ_AST_IMPORT_MODULE:
        case XQ_AST_DIRECT_ELEM:
        case XQ_AST_RETURN_CLAUSE:
        case XQ_AST_STRING_CONCAT:
            printf("(unhandled node type %d)\n", node->type);
            break;
    }
}
