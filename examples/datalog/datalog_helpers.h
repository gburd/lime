/*
 * datalog_helpers.h
 *    Declarations for Datalog grammar AST construction helpers.
 *
 * These functions are used by the Lime-generated parser's reduction
 * actions to build the Datalog AST.
 */

#ifndef DATALOG_HELPERS_H
#define DATALOG_HELPERS_H

#include "datalog_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Program construction --- */
DlProgram    *dl_make_program(DatalogParseState *pstate, DlClauseList *clauses);

/* --- Clause construction --- */
DlClause     *dl_make_fact(DatalogParseState *pstate, DlLiteral *head);
DlClause     *dl_make_rule(DatalogParseState *pstate, DlLiteral *head,
                            DlLiteralList *body);
DlClause     *dl_make_query(DatalogParseState *pstate, DlLiteralList *body);

/* --- Clause list --- */
DlClauseList *dl_clause_list_create(DlClause *clause);
DlClauseList *dl_clause_list_append(DlClauseList *list, DlClause *clause);

/* --- Literal construction --- */
DlLiteral    *dl_make_literal(DatalogParseState *pstate, DatalogString name,
                               DlTermList *args);
DlLiteral    *dl_make_negated(DatalogParseState *pstate, DlLiteral *inner);
DlLiteral    *dl_make_comparison(DatalogParseState *pstate, DlCompOp op,
                                  DlTerm *left, DlTerm *right);
DlLiteral    *dl_make_aggregate(DatalogParseState *pstate, DlAggFunc func,
                                 DlTerm *target, DlLiteral *body);

/* --- Literal list --- */
DlLiteralList *dl_literal_list_create(DlLiteral *lit);
DlLiteralList *dl_literal_list_append(DlLiteralList *list, DlLiteral *lit);

/* --- Term construction --- */
DlTerm       *dl_make_variable(DatalogParseState *pstate, DatalogString name);
DlTerm       *dl_make_anon_variable(DatalogParseState *pstate);
DlTerm       *dl_make_atom(DatalogParseState *pstate, DatalogString name);
DlTerm       *dl_make_integer(DatalogParseState *pstate, DatalogString str);
DlTerm       *dl_make_float(DatalogParseState *pstate, DatalogString str);
DlTerm       *dl_make_string(DatalogParseState *pstate, DatalogString str);
DlTerm       *dl_make_boolean(DatalogParseState *pstate, int val);
DlTerm       *dl_make_nil(DatalogParseState *pstate);
DlTerm       *dl_make_keyword(DatalogParseState *pstate, DatalogString name);
DlTerm       *dl_make_vector(DatalogParseState *pstate, DlTermList *elems);
DlTerm       *dl_make_map(DatalogParseState *pstate, DlTermList *pairs);
DlTerm       *dl_make_set(DatalogParseState *pstate, DlTermList *elems);

/* --- Term list --- */
DlTermList   *dl_term_list_create(DlTerm *term);
DlTermList   *dl_term_list_append(DlTermList *list, DlTerm *term);
DlTermList   *dl_term_list_concat(DlTermList *a, DlTermList *b);

/* --- AST printing (for debugging / test output) --- */
void          dl_print_program(const DlProgram *prog, FILE *out);
void          dl_print_clause(const DlClause *clause, FILE *out);
void          dl_print_literal(const DlLiteral *lit, FILE *out);
void          dl_print_term(const DlTerm *term, FILE *out);

/* --- Memory cleanup --- */
void          dl_free_program(DlProgram *prog);

#ifdef __cplusplus
}
#endif

#endif /* DATALOG_HELPERS_H */
