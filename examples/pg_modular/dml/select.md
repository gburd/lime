# SELECT Statement


SELECT statement grammar: simple SELECT, UNION/INTERSECT/
EXCEPT, parenthesized subqueries, VALUES, and SELECT INTO.


```yaml
name: pg-select
version: 17.0.0
description: SELECT statement (simple, compound, VALUES)
provides: [pg-select]
depends: [pg-type-decls, pg-from-clause, pg-select-clauses, pg-window, pg-cte, pg-expressions]
```

## Production Rules

```lime rules
create_as_target(A) ::= qualified_name(B) opt_column_list(C) table_access_method_clause(D) optWith(E) onCommitOption(F) optTableSpace(G). {
    A = makeNode(IntoClause);
    					A->rel = B;
    					A->colNames = C;
    					A->accessMethod = D;
    					A->options = E;
    					A->onCommit = F;
    					A->tableSpaceName = G;
    					A->viewQuery = NULL;
    					A->skipData = false;
}

/* ----- opt_with_data ----- */

create_mv_target(A) ::= qualified_name(B) opt_column_list(C) table_access_method_clause(D) opt_reloptions(E) optTableSpace(F). {
    A = makeNode(IntoClause);
    					A->rel = B;
    					A->colNames = C;
    					A->accessMethod = D;
    					A->options = E;
    					A->onCommit = ONCOMMIT_NOOP;
    					A->tableSpaceName = F;
    					A->viewQuery = NULL;		
    					A->skipData = false;
}

/* ----- optNoLog ----- */

selectStmt(A) ::= select_no_parens(B). [UMINUS]

selectStmt(A) ::= select_with_parens(B). [UMINUS]

/* ----- select_with_parens ----- */

select_with_parens(A) ::= LPAREN select_no_parens(C) RPAREN. {
    A = C;
}

select_with_parens(A) ::= LPAREN select_with_parens(C) RPAREN. {
    A = C;
}

/* ----- select_no_parens ----- */

select_no_parens(A) ::= simple_select(B). {
    A = B;
}

select_no_parens(A) ::= select_clause(B) sort_clause(C). {
    insertSelectOptions((selectStmt *) B, C, NIL,
    										NULL, NULL,
    										yyscanner);
    					A = B;
}

select_no_parens(A) ::= select_clause(B) opt_sort_clause(C) for_locking_clause(D) opt_select_limit(E). {
    insertSelectOptions((selectStmt *) B, C, D,
    										E,
    										NULL,
    										yyscanner);
    					A = B;
}

select_no_parens(A) ::= select_clause(B) opt_sort_clause(C) select_limit(D) opt_for_locking_clause(E). {
    insertSelectOptions((selectStmt *) B, C, E,
    										D,
    										NULL,
    										yyscanner);
    					A = B;
}

select_no_parens(A) ::= with_clause(B) select_clause(C). {
    insertSelectOptions((selectStmt *) C, NULL, NIL,
    										NULL,
    										B,
    										yyscanner);
    					A = C;
}

select_no_parens(A) ::= with_clause(B) select_clause(C) sort_clause(D). {
    insertSelectOptions((selectStmt *) C, D, NIL,
    										NULL,
    										B,
    										yyscanner);
    					A = C;
}

select_no_parens(A) ::= with_clause(B) select_clause(C) opt_sort_clause(D) for_locking_clause(E) opt_select_limit(F). {
    insertSelectOptions((selectStmt *) C, D, E,
    										F,
    										B,
    										yyscanner);
    					A = C;
}

select_no_parens(A) ::= with_clause(B) select_clause(C) opt_sort_clause(D) select_limit(E) opt_for_locking_clause(F). {
    insertSelectOptions((selectStmt *) C, D, F,
    										E,
    										B,
    										yyscanner);
    					A = C;
}

/* ----- select_clause ----- */

select_clause(A) ::= simple_select(B). {
    A = B;
}

select_clause(A) ::= select_with_parens(B). {
    A = B;
}

/* ----- simple_select ----- */

simple_select(A) ::= SELECT opt_all_clause(C) opt_target_list(D) into_clause(E) from_clause(F) where_clause(G) group_clause(H) having_clause(I) window_clause(J). {
    selectStmt *n = makeNode(selectStmt);

    					n->targetList = D;
    					n->intoClause = E;
    					n->fromClause = F;
    					n->whereClause = G;
    					n->groupClause = (H)->list;
    					n->groupDistinct = (H)->distinct;
    					n->groupByAll = (H)->all;
    					n->havingClause = I;
    					n->windowClause = J;
    					A = (Node *) n;
}

simple_select(A) ::= SELECT distinct_clause(C) target_list(D) into_clause(E) from_clause(F) where_clause(G) group_clause(H) having_clause(I) window_clause(J). {
    selectStmt *n = makeNode(selectStmt);

    					n->distinctClause = C;
    					n->targetList = D;
    					n->intoClause = E;
    					n->fromClause = F;
    					n->whereClause = G;
    					n->groupClause = (H)->list;
    					n->groupDistinct = (H)->distinct;
    					n->groupByAll = (H)->all;
    					n->havingClause = I;
    					n->windowClause = J;
    					A = (Node *) n;
}

simple_select(A) ::= values_clause(B). {
    A = B;
}

simple_select(A) ::= TABLE relation_expr(C). {
    ColumnRef  *cr = makeNode(ColumnRef);
    					ResTarget  *rt = makeNode(ResTarget);
    					selectStmt *n = makeNode(selectStmt);

    					cr->fields = list_make1(makeNode(A_Star));
    					cr->location = -1;

    					rt->name = NULL;
    					rt->indirection = NIL;
    					rt->val = (Node *) cr;
    					rt->location = -1;

    					n->targetList = list_make1(rt);
    					n->fromClause = list_make1(C);
    					A = (Node *) n;
}

simple_select(A) ::= select_clause(B) UNION set_quantifier(D) select_clause(E). {
    A = makeSetOp(SETOP_UNION, D == SET_QUANTIFIER_ALL, B, E);
}

simple_select(A) ::= select_clause(B) INTERSECT set_quantifier(D) select_clause(E). {
    A = makeSetOp(SETOP_INTERSECT, D == SET_QUANTIFIER_ALL, B, E);
}

simple_select(A) ::= select_clause(B) EXCEPT set_quantifier(D) select_clause(E). {
    A = makeSetOp(SETOP_EXCEPT, D == SET_QUANTIFIER_ALL, B, E);
}

/* ----- with_clause ----- */

into_clause(A) ::= INTO optTempTableName(C). {
    A = makeNode(IntoClause);
    					A->rel = C;
    					A->colNames = NIL;
    					A->options = NIL;
    					A->onCommit = ONCOMMIT_NOOP;
    					A->tableSpaceName = NULL;
    					A->viewQuery = NULL;
    					A->skipData = false;
}

into_clause(A) ::= . {
    A = NULL;
}

/* ----- optTempTableName ----- */

optTempTableName(A) ::= TEMPORARY opt_table(C) qualified_name(D). {
    A = D;
    					A->relpersistence = RELPERSISTENCE_TEMP;
}

optTempTableName(A) ::= TEMP opt_table(C) qualified_name(D). {
    A = D;
    					A->relpersistence = RELPERSISTENCE_TEMP;
}

optTempTableName(A) ::= LOCAL TEMPORARY opt_table(D) qualified_name(E). {
    A = E;
    					A->relpersistence = RELPERSISTENCE_TEMP;
}

optTempTableName(A) ::= LOCAL TEMP opt_table(D) qualified_name(E). {
    A = E;
    					A->relpersistence = RELPERSISTENCE_TEMP;
}

optTempTableName(A) ::= GLOBAL TEMPORARY opt_table(D) qualified_name(E). {
    ereport(WARNING,
    							(errmsg("GLOBAL is deprecated in temporary table creation"),
    							 parser_errposition(LOC(B))));
    					A = E;
    					A->relpersistence = RELPERSISTENCE_TEMP;
}

optTempTableName(A) ::= GLOBAL TEMP opt_table(D) qualified_name(E). {
    ereport(WARNING,
    							(errmsg("GLOBAL is deprecated in temporary table creation"),
    							 parser_errposition(LOC(B))));
    					A = E;
    					A->relpersistence = RELPERSISTENCE_TEMP;
}

optTempTableName(A) ::= UNLOGGED opt_table(C) qualified_name(D). {
    A = D;
    					A->relpersistence = RELPERSISTENCE_UNLOGGED;
}

optTempTableName(A) ::= TABLE qualified_name(C). {
    A = C;
    					A->relpersistence = RELPERSISTENCE_PERMANENT;
}

optTempTableName(A) ::= qualified_name(B). {
    A = B;
    					A->relpersistence = RELPERSISTENCE_PERMANENT;
}

/* ----- opt_table ----- */

opt_table(A) ::= TABLE.

/* ----- set_quantifier ----- */

set_quantifier(A) ::= ALL. {
    A = SET_QUANTIFIER_ALL;
}

set_quantifier(A) ::= DISTINCT. {
    A = SET_QUANTIFIER_DISTINCT;
}

set_quantifier(A) ::= . {
    A = SET_QUANTIFIER_DEFAULT;
}

/* ----- distinct_clause ----- */

distinct_clause(A) ::= DISTINCT. {
    A = list_make1(NIL);
}

distinct_clause(A) ::= DISTINCT ON LPAREN expr_list(E) RPAREN. {
    A = E;
}

/* ----- opt_all_clause ----- */

opt_all_clause(A) ::= ALL.

/* ----- opt_distinct_clause ----- */

opt_distinct_clause(A) ::= distinct_clause(B). {
    A = B;
}

opt_distinct_clause(A) ::= opt_all_clause(B). {
    A = NIL;
}

/* ----- opt_sort_clause ----- */

values_clause(A) ::= VALUES LPAREN expr_list(D) RPAREN. {
    selectStmt *n = makeNode(selectStmt);

    					n->valuesLists = list_make1(D);
    					A = (Node *) n;
}

values_clause(A) ::= values_clause(B) COMMA LPAREN expr_list(E) RPAREN. {
    selectStmt *n = (selectStmt *) B;

    					n->valuesLists = lappend(n->valuesLists, E);
    					A = (Node *) n;
}

/* ----- from_clause ----- */
```

