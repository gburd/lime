/*
 * datalog_helpers.c
 *    AST construction helper functions for the Datalog/EDN Lime grammar.
 *
 * These functions are called from the Lime-generated parser's reduction
 * actions to build the DlProgram AST tree.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "datalog_helpers.h"

/* ======================================================================
 * Internal allocation helpers
 * ====================================================================== */

static void *dl_alloc(size_t size)
{
    void *p = calloc(1, size);
    if (!p) {
        fprintf(stderr, "datalog: out of memory\n");
        exit(1);
    }
    return p;
}

/* ======================================================================
 * Term list helpers
 * ====================================================================== */

static DlTermCell *make_term_cell(DlTerm *data)
{
    DlTermCell *cell = (DlTermCell *)dl_alloc(sizeof(DlTermCell));
    cell->data = data;
    cell->next = NULL;
    return cell;
}

DlTermList *dl_term_list_create(DlTerm *term)
{
    DlTermList *list = (DlTermList *)dl_alloc(sizeof(DlTermList));
    DlTermCell *cell = make_term_cell(term);
    list->length = 1;
    list->head = cell;
    list->tail = cell;
    return list;
}

DlTermList *dl_term_list_append(DlTermList *list, DlTerm *term)
{
    if (!list)
        return dl_term_list_create(term);
    DlTermCell *cell = make_term_cell(term);
    list->tail->next = cell;
    list->tail = cell;
    list->length++;
    return list;
}

DlTermList *dl_term_list_concat(DlTermList *a, DlTermList *b)
{
    if (!a) return b;
    if (!b) return a;
    a->tail->next = b->head;
    a->tail = b->tail;
    a->length += b->length;
    /* Free the b list header (cells are now owned by a) */
    free(b);
    return a;
}

/* ======================================================================
 * Literal list helpers
 * ====================================================================== */

static DlLiteralCell *make_literal_cell(DlLiteral *data)
{
    DlLiteralCell *cell = (DlLiteralCell *)dl_alloc(sizeof(DlLiteralCell));
    cell->data = data;
    cell->next = NULL;
    return cell;
}

DlLiteralList *dl_literal_list_create(DlLiteral *lit)
{
    DlLiteralList *list = (DlLiteralList *)dl_alloc(sizeof(DlLiteralList));
    DlLiteralCell *cell = make_literal_cell(lit);
    list->length = 1;
    list->head = cell;
    list->tail = cell;
    return list;
}

DlLiteralList *dl_literal_list_append(DlLiteralList *list, DlLiteral *lit)
{
    if (!list)
        return dl_literal_list_create(lit);
    DlLiteralCell *cell = make_literal_cell(lit);
    list->tail->next = cell;
    list->tail = cell;
    list->length++;
    return list;
}

/* ======================================================================
 * Clause list helpers
 * ====================================================================== */

static DlClauseCell *make_clause_cell(DlClause *data)
{
    DlClauseCell *cell = (DlClauseCell *)dl_alloc(sizeof(DlClauseCell));
    cell->data = data;
    cell->next = NULL;
    return cell;
}

DlClauseList *dl_clause_list_create(DlClause *clause)
{
    DlClauseList *list = (DlClauseList *)dl_alloc(sizeof(DlClauseList));
    DlClauseCell *cell = make_clause_cell(clause);
    list->length = 1;
    list->head = cell;
    list->tail = cell;
    return list;
}

DlClauseList *dl_clause_list_append(DlClauseList *list, DlClause *clause)
{
    if (!list)
        return dl_clause_list_create(clause);
    DlClauseCell *cell = make_clause_cell(clause);
    list->tail->next = cell;
    list->tail = cell;
    list->length++;
    return list;
}

/* ======================================================================
 * Term construction
 * ====================================================================== */

DlTerm *dl_make_variable(DatalogParseState *pstate, DatalogString name)
{
    (void)pstate;
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_VARIABLE;
    t->u.name = name;
    return t;
}

DlTerm *dl_make_anon_variable(DatalogParseState *pstate)
{
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_ANON_VAR;
    /* Generate a unique internal name for tracking */
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "_anon_%d", pstate->anon_count++);
    char *s = (char *)malloc((size_t)(len + 1));
    memcpy(s, buf, (size_t)(len + 1));
    t->u.name.val = s;
    t->u.name.len = len;
    return t;
}

DlTerm *dl_make_atom(DatalogParseState *pstate, DatalogString name)
{
    (void)pstate;
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_ATOM;
    t->u.name = name;
    return t;
}

DlTerm *dl_make_integer(DatalogParseState *pstate, DatalogString str)
{
    (void)pstate;
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_INTEGER;
    t->u.num_str = str;
    return t;
}

DlTerm *dl_make_float(DatalogParseState *pstate, DatalogString str)
{
    (void)pstate;
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_FLOAT;
    t->u.num_str = str;
    return t;
}

DlTerm *dl_make_string(DatalogParseState *pstate, DatalogString str)
{
    (void)pstate;
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_STRING;
    t->u.str_val = str;
    return t;
}

DlTerm *dl_make_boolean(DatalogParseState *pstate, int val)
{
    (void)pstate;
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_BOOLEAN;
    t->u.bool_val = (val != 0);
    return t;
}

DlTerm *dl_make_nil(DatalogParseState *pstate)
{
    (void)pstate;
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_NIL;
    return t;
}

DlTerm *dl_make_keyword(DatalogParseState *pstate, DatalogString name)
{
    (void)pstate;
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_KEYWORD;
    t->u.name = name;
    return t;
}

DlTerm *dl_make_vector(DatalogParseState *pstate, DlTermList *elems)
{
    (void)pstate;
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_VECTOR;
    t->u.elements = elems;
    return t;
}

DlTerm *dl_make_map(DatalogParseState *pstate, DlTermList *pairs)
{
    (void)pstate;
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_MAP;
    t->u.elements = pairs;
    return t;
}

DlTerm *dl_make_set(DatalogParseState *pstate, DlTermList *elems)
{
    (void)pstate;
    DlTerm *t = (DlTerm *)dl_alloc(sizeof(DlTerm));
    t->kind = DL_TERM_SET;
    t->u.elements = elems;
    return t;
}

/* ======================================================================
 * Literal construction
 * ====================================================================== */

DlLiteral *dl_make_literal(DatalogParseState *pstate, DatalogString name,
                            DlTermList *args)
{
    (void)pstate;
    DlLiteral *lit = (DlLiteral *)dl_alloc(sizeof(DlLiteral));
    lit->kind = DL_LIT_POSITIVE;
    lit->predicate = name;
    lit->args = args;
    return lit;
}

DlLiteral *dl_make_negated(DatalogParseState *pstate, DlLiteral *inner)
{
    (void)pstate;
    DlLiteral *lit = (DlLiteral *)dl_alloc(sizeof(DlLiteral));
    lit->kind = DL_LIT_NEGATED;
    lit->u.inner = inner;
    return lit;
}

DlLiteral *dl_make_comparison(DatalogParseState *pstate, DlCompOp op,
                               DlTerm *left, DlTerm *right)
{
    (void)pstate;
    DlLiteral *lit = (DlLiteral *)dl_alloc(sizeof(DlLiteral));
    lit->kind = DL_LIT_COMPARISON;
    lit->u.comp.op = op;
    lit->u.comp.left = left;
    lit->u.comp.right = right;
    return lit;
}

DlLiteral *dl_make_aggregate(DatalogParseState *pstate, DlAggFunc func,
                              DlTerm *target, DlLiteral *body)
{
    (void)pstate;
    DlLiteral *lit = (DlLiteral *)dl_alloc(sizeof(DlLiteral));
    lit->kind = DL_LIT_AGGREGATE;
    lit->u.agg.func = func;
    lit->u.agg.target = target;
    lit->u.agg.body = body;
    return lit;
}

/* ======================================================================
 * Clause construction
 * ====================================================================== */

DlClause *dl_make_fact(DatalogParseState *pstate, DlLiteral *head)
{
    (void)pstate;
    DlClause *c = (DlClause *)dl_alloc(sizeof(DlClause));
    c->kind = DL_CLAUSE_FACT;
    c->head = head;
    c->body = NULL;
    return c;
}

DlClause *dl_make_rule(DatalogParseState *pstate, DlLiteral *head,
                         DlLiteralList *body)
{
    (void)pstate;
    DlClause *c = (DlClause *)dl_alloc(sizeof(DlClause));
    c->kind = DL_CLAUSE_RULE;
    c->head = head;
    c->body = body;
    return c;
}

DlClause *dl_make_query(DatalogParseState *pstate, DlLiteralList *body)
{
    (void)pstate;
    DlClause *c = (DlClause *)dl_alloc(sizeof(DlClause));
    c->kind = DL_CLAUSE_QUERY;
    c->head = NULL;
    c->body = body;
    return c;
}

/* ======================================================================
 * Program construction
 * ====================================================================== */

DlProgram *dl_make_program(DatalogParseState *pstate, DlClauseList *clauses)
{
    DlProgram *prog = (DlProgram *)dl_alloc(sizeof(DlProgram));
    prog->clauses = clauses;
    prog->nfacts = 0;
    prog->nrules = 0;
    prog->nqueries = 0;

    /* Count clause types */
    if (clauses) {
        DlClauseCell *cell;
        for (cell = clauses->head; cell; cell = cell->next) {
            switch (cell->data->kind) {
                case DL_CLAUSE_FACT:  prog->nfacts++;   break;
                case DL_CLAUSE_RULE:  prog->nrules++;   break;
                case DL_CLAUSE_QUERY: prog->nqueries++; break;
            }
        }
    }

    pstate->result = prog;
    return prog;
}

/* ======================================================================
 * AST printing (for debugging and test output)
 * ====================================================================== */

static const char *comp_op_str(DlCompOp op)
{
    switch (op) {
        case DL_COMP_EQ:  return "=";
        case DL_COMP_NEQ: return "\\=";
        case DL_COMP_LT:  return "<";
        case DL_COMP_GT:  return ">";
        case DL_COMP_LTE: return "<=";
        case DL_COMP_GTE: return ">=";
    }
    return "?";
}

static const char *agg_func_str(DlAggFunc func)
{
    switch (func) {
        case DL_AGG_COUNT: return "count";
        case DL_AGG_SUM:   return "sum";
        case DL_AGG_MIN:   return "min";
        case DL_AGG_MAX:   return "max";
        case DL_AGG_AVG:   return "avg";
    }
    return "?";
}

void dl_print_term(const DlTerm *term, FILE *out)
{
    if (!term) {
        fprintf(out, "<null>");
        return;
    }
    switch (term->kind) {
        case DL_TERM_VARIABLE:
            fprintf(out, "%.*s", term->u.name.len, term->u.name.val);
            break;
        case DL_TERM_ANON_VAR:
            fprintf(out, "_");
            break;
        case DL_TERM_ATOM:
            fprintf(out, "%.*s", term->u.name.len, term->u.name.val);
            break;
        case DL_TERM_INTEGER:
        case DL_TERM_FLOAT:
            fprintf(out, "%.*s", term->u.num_str.len, term->u.num_str.val);
            break;
        case DL_TERM_STRING:
            fprintf(out, "\"%.*s\"", term->u.str_val.len, term->u.str_val.val);
            break;
        case DL_TERM_BOOLEAN:
            fprintf(out, "%s", term->u.bool_val ? "true" : "false");
            break;
        case DL_TERM_NIL:
            fprintf(out, "nil");
            break;
        case DL_TERM_KEYWORD:
            fprintf(out, ":%.*s", term->u.name.len, term->u.name.val);
            break;
        case DL_TERM_VECTOR:
            fprintf(out, "[");
            if (term->u.elements) {
                DlTermCell *cell = term->u.elements->head;
                while (cell) {
                    dl_print_term(cell->data, out);
                    if (cell->next) fprintf(out, ", ");
                    cell = cell->next;
                }
            }
            fprintf(out, "]");
            break;
        case DL_TERM_MAP:
            fprintf(out, "{");
            if (term->u.elements) {
                DlTermCell *cell = term->u.elements->head;
                int i = 0;
                while (cell) {
                    dl_print_term(cell->data, out);
                    if (cell->next) {
                        fprintf(out, (i % 2 == 0) ? " " : ", ");
                    }
                    cell = cell->next;
                    i++;
                }
            }
            fprintf(out, "}");
            break;
        case DL_TERM_SET:
            fprintf(out, "#{");
            if (term->u.elements) {
                DlTermCell *cell = term->u.elements->head;
                while (cell) {
                    dl_print_term(cell->data, out);
                    if (cell->next) fprintf(out, ", ");
                    cell = cell->next;
                }
            }
            fprintf(out, "}");
            break;
    }
}

void dl_print_literal(const DlLiteral *lit, FILE *out)
{
    if (!lit) {
        fprintf(out, "<null>");
        return;
    }
    switch (lit->kind) {
        case DL_LIT_POSITIVE:
            fprintf(out, "%.*s", lit->predicate.len, lit->predicate.val);
            if (lit->args) {
                fprintf(out, "(");
                DlTermCell *cell = lit->args->head;
                while (cell) {
                    dl_print_term(cell->data, out);
                    if (cell->next) fprintf(out, ", ");
                    cell = cell->next;
                }
                fprintf(out, ")");
            }
            break;
        case DL_LIT_NEGATED:
            fprintf(out, "not ");
            dl_print_literal(lit->u.inner, out);
            break;
        case DL_LIT_COMPARISON:
            dl_print_term(lit->u.comp.left, out);
            fprintf(out, " %s ", comp_op_str(lit->u.comp.op));
            dl_print_term(lit->u.comp.right, out);
            break;
        case DL_LIT_AGGREGATE:
            fprintf(out, "%s(", agg_func_str(lit->u.agg.func));
            dl_print_term(lit->u.agg.target, out);
            fprintf(out, ", ");
            dl_print_literal(lit->u.agg.body, out);
            fprintf(out, ")");
            break;
    }
}

void dl_print_clause(const DlClause *clause, FILE *out)
{
    if (!clause) return;
    switch (clause->kind) {
        case DL_CLAUSE_FACT:
            dl_print_literal(clause->head, out);
            fprintf(out, ".\n");
            break;
        case DL_CLAUSE_RULE:
            dl_print_literal(clause->head, out);
            fprintf(out, " :- ");
            if (clause->body) {
                DlLiteralCell *cell = clause->body->head;
                while (cell) {
                    dl_print_literal(cell->data, out);
                    if (cell->next) fprintf(out, ", ");
                    cell = cell->next;
                }
            }
            fprintf(out, ".\n");
            break;
        case DL_CLAUSE_QUERY:
            fprintf(out, "?- ");
            if (clause->body) {
                DlLiteralCell *cell = clause->body->head;
                while (cell) {
                    dl_print_literal(cell->data, out);
                    if (cell->next) fprintf(out, ", ");
                    cell = cell->next;
                }
            }
            fprintf(out, ".\n");
            break;
    }
}

void dl_print_program(const DlProgram *prog, FILE *out)
{
    if (!prog || !prog->clauses) {
        fprintf(out, "(empty program)\n");
        return;
    }

    fprintf(out, "%% %d fact(s), %d rule(s), %d query/queries\n",
            prog->nfacts, prog->nrules, prog->nqueries);

    DlClauseCell *cell = prog->clauses->head;
    while (cell) {
        dl_print_clause(cell->data, out);
        cell = cell->next;
    }
}

/* ======================================================================
 * Memory cleanup
 * ====================================================================== */

static void dl_free_term_list(DlTermList *list);
static void dl_free_literal(DlLiteral *lit);

static void dl_free_term(DlTerm *term)
{
    if (!term) return;
    switch (term->kind) {
        case DL_TERM_ANON_VAR:
            /* The name was malloc'd in dl_make_anon_variable */
            free((void *)term->u.name.val);
            break;
        case DL_TERM_VECTOR:
        case DL_TERM_MAP:
        case DL_TERM_SET:
            dl_free_term_list(term->u.elements);
            break;
        default:
            break;
    }
    free(term);
}

static void dl_free_term_list(DlTermList *list)
{
    if (!list) return;
    DlTermCell *cell = list->head;
    while (cell) {
        DlTermCell *next = cell->next;
        dl_free_term(cell->data);
        free(cell);
        cell = next;
    }
    free(list);
}

static void dl_free_literal(DlLiteral *lit)
{
    if (!lit) return;
    switch (lit->kind) {
        case DL_LIT_POSITIVE:
            dl_free_term_list(lit->args);
            break;
        case DL_LIT_NEGATED:
            dl_free_literal(lit->u.inner);
            break;
        case DL_LIT_COMPARISON:
            dl_free_term(lit->u.comp.left);
            dl_free_term(lit->u.comp.right);
            break;
        case DL_LIT_AGGREGATE:
            dl_free_term(lit->u.agg.target);
            dl_free_literal(lit->u.agg.body);
            break;
    }
    free(lit);
}

static void dl_free_literal_list(DlLiteralList *list)
{
    if (!list) return;
    DlLiteralCell *cell = list->head;
    while (cell) {
        DlLiteralCell *next = cell->next;
        dl_free_literal(cell->data);
        free(cell);
        cell = next;
    }
    free(list);
}

static void dl_free_clause(DlClause *clause)
{
    if (!clause) return;
    dl_free_literal(clause->head);
    dl_free_literal_list(clause->body);
    free(clause);
}

void dl_free_program(DlProgram *prog)
{
    if (!prog) return;
    if (prog->clauses) {
        DlClauseCell *cell = prog->clauses->head;
        while (cell) {
            DlClauseCell *next = cell->next;
            dl_free_clause(cell->data);
            free(cell);
            cell = next;
        }
        free(prog->clauses);
    }
    free(prog);
}
