# SELECT Clauses


Supporting clauses for SELECT: target list, ORDER BY, LIMIT/
OFFSET, GROUP BY, HAVING, and FOR UPDATE/SHARE locking.


```yaml
name: pg-select-clauses
version: 17.0.0
description: Target list, ORDER BY, LIMIT, GROUP BY, HAVING, FOR UPDATE
provides: [pg-select-clauses]
depends: [pg-type-decls, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
opt_sort_clause(A) ::= sort_clause(B). {
    A = B;
}

opt_sort_clause(A) ::= . {
    A = NIL;
}

/* ----- sort_clause ----- */

sort_clause(A) ::= ORDER BY sortby_list(D). {
    A = D;
}

/* ----- sortby_list ----- */

sortby_list(A) ::= sortby(B). {
    A = list_make1(B);
}

sortby_list(A) ::= sortby_list(B) COMMA sortby(D). {
    A = lappend(B, D);
}

/* ----- sortby ----- */

sortby(A) ::= a_expr(B) USING qual_all_Op(D) opt_nulls_order(E). {
    A = makeNode(SortBy);
    					A->node = B;
    					A->sortby_dir = SORTBY_USING;
    					A->sortby_nulls = E;
    					A->useOp = D;
    					A->location = LOC(D);
}

sortby(A) ::= a_expr(B) opt_asc_desc(C) opt_nulls_order(D). {
    A = makeNode(SortBy);
    					A->node = B;
    					A->sortby_dir = C;
    					A->sortby_nulls = D;
    					A->useOp = NIL;
    					A->location = -1;
}

/* ----- select_limit ----- */

select_limit(A) ::= limit_clause(B) offset_clause(C). {
    A = B;
    					(A)->limitOffset = C;
    					(A)->offsetLoc = LOC(C);
}

select_limit(A) ::= offset_clause(B) limit_clause(C). {
    A = C;
    					(A)->limitOffset = B;
    					(A)->offsetLoc = LOC(B);
}

select_limit(A) ::= limit_clause(B). {
    A = B;
}

select_limit(A) ::= offset_clause(B). {
    SelectLimit *n = palloc_object(SelectLimit);

    					n->limitOffset = B;
    					n->limitCount = NULL;
    					n->limitOption = LIMIT_OPTION_COUNT;
    					n->offsetLoc = LOC(B);
    					n->countLoc = -1;
    					n->optionLoc = -1;
    					A = n;
}

/* ----- opt_select_limit ----- */

opt_select_limit(A) ::= select_limit(B). {
    A = B;
}

opt_select_limit(A) ::= . {
    A = NULL;
}

/* ----- limit_clause ----- */

limit_clause(A) ::= LIMIT select_limit_value(C). {
    SelectLimit *n = palloc_object(SelectLimit);

    					n->limitOffset = NULL;
    					n->limitCount = C;
    					n->limitOption = LIMIT_OPTION_COUNT;
    					n->offsetLoc = -1;
    					n->countLoc = LOC(B);
    					n->optionLoc = -1;
    					A = n;
}

limit_clause(A) ::= LIMIT select_limit_value(C) COMMA select_offset_value(E). {
    ereport(ERROR,
    							(errcode(ERRCODE_SYNTAX_ERROR),
    							 errmsg("LIMIT #,# syntax is not supported"),
    							 errhint("Use separate LIMIT and OFFSET clauses."),
    							 parser_errposition(LOC(B))));
}

limit_clause(A) ::= FETCH first_or_next(C) select_fetch_first_value(D) row_or_rows(E) ONLY. {
    SelectLimit *n = palloc_object(SelectLimit);

    					n->limitOffset = NULL;
    					n->limitCount = D;
    					n->limitOption = LIMIT_OPTION_COUNT;
    					n->offsetLoc = -1;
    					n->countLoc = LOC(B);
    					n->optionLoc = -1;
    					A = n;
}

limit_clause(A) ::= FETCH first_or_next(C) select_fetch_first_value(D) row_or_rows(E) WITH TIES. {
    SelectLimit *n = palloc_object(SelectLimit);

    					n->limitOffset = NULL;
    					n->limitCount = D;
    					n->limitOption = LIMIT_OPTION_WITH_TIES;
    					n->offsetLoc = -1;
    					n->countLoc = LOC(B);
    					n->optionLoc = LOC(F);
    					A = n;
}

limit_clause(A) ::= FETCH first_or_next(C) row_or_rows(D) ONLY. {
    SelectLimit *n = palloc_object(SelectLimit);

    					n->limitOffset = NULL;
    					n->limitCount = makeIntConst(1, -1);
    					n->limitOption = LIMIT_OPTION_COUNT;
    					n->offsetLoc = -1;
    					n->countLoc = LOC(B);
    					n->optionLoc = -1;
    					A = n;
}

limit_clause(A) ::= FETCH first_or_next(C) row_or_rows(D) WITH TIES. {
    SelectLimit *n = palloc_object(SelectLimit);

    					n->limitOffset = NULL;
    					n->limitCount = makeIntConst(1, -1);
    					n->limitOption = LIMIT_OPTION_WITH_TIES;
    					n->offsetLoc = -1;
    					n->countLoc = LOC(B);
    					n->optionLoc = LOC(E);
    					A = n;
}

/* ----- offset_clause ----- */

offset_clause(A) ::= OFFSET select_offset_value(C). {
    A = C;
}

offset_clause(A) ::= OFFSET select_fetch_first_value(C) row_or_rows(D). {
    A = C;
}

/* ----- select_limit_value ----- */

select_limit_value(A) ::= a_expr(B). {
    A = B;
}

select_limit_value(A) ::= ALL. {
    A = makeNullAConst(LOC(B));
}

/* ----- select_offset_value ----- */

select_offset_value(A) ::= a_expr(B). {
    A = B;
}

/* ----- select_fetch_first_value ----- */

select_fetch_first_value(A) ::= c_expr(B). {
    A = B;
}

select_fetch_first_value(A) ::= PLUS i_or_F_const(C). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "+", NULL, C, LOC(B));
}

select_fetch_first_value(A) ::= MINUS i_or_F_const(C). {
    A = doNegate(C, LOC(B));
}

/* ----- i_or_F_const ----- */

i_or_F_const(A) ::= iconst(B). {
    A = makeIntConst(B,LOC(B));
}

i_or_F_const(A) ::= FCONST. {
    A = makeFloatConst(B,LOC(B));
}

/* ----- row_or_rows ----- */

row_or_rows(A) ::= ROW. {
    A = 0;
}

row_or_rows(A) ::= ROWS. {
    A = 0;
}

/* ----- first_or_next ----- */

first_or_next(A) ::= FIRST_P. {
    A = 0;
}

first_or_next(A) ::= NEXT. {
    A = 0;
}

/* ----- group_clause ----- */

group_clause(A) ::= GROUP_P BY set_quantifier(D) group_by_list(E). {
    GroupClause *n = palloc_object(GroupClause);

    					n->distinct = D == SET_QUANTIFIER_DISTINCT;
    					n->all = false;
    					n->list = E;
    					A = n;
}

group_clause(A) ::= GROUP_P BY ALL. {
    GroupClause *n = palloc_object(GroupClause);
    					n->distinct = false;
    					n->all = true;
    					n->list = NIL;
    					A = n;
}

group_clause(A) ::= . {
    GroupClause *n = palloc_object(GroupClause);

    					n->distinct = false;
    					n->all = false;
    					n->list = NIL;
    					A = n;
}

/* ----- group_by_list ----- */

group_by_list(A) ::= group_by_item(B). {
    A = list_make1(B);
}

group_by_list(A) ::= group_by_list(B) COMMA group_by_item(D). {
    A = lappend(B,D);
}

/* ----- group_by_item ----- */

group_by_item(A) ::= a_expr(B). {
    A = B;
}

group_by_item(A) ::= empty_grouping_set(B). {
    A = B;
}

group_by_item(A) ::= cube_clause(B). {
    A = B;
}

group_by_item(A) ::= rollup_clause(B). {
    A = B;
}

group_by_item(A) ::= grouping_sets_clause(B). {
    A = B;
}

/* ----- empty_grouping_set ----- */

empty_grouping_set(A) ::= LPAREN RPAREN. {
    A = (Node *) makeGroupingSet(GROUPING_SET_EMPTY, NIL, LOC(B));
}

/* ----- rollup_clause ----- */

rollup_clause(A) ::= ROLLUP LPAREN expr_list(D) RPAREN. {
    A = (Node *) makeGroupingSet(GROUPING_SET_ROLLUP, D, LOC(B));
}

/* ----- cube_clause ----- */

cube_clause(A) ::= CUBE LPAREN expr_list(D) RPAREN. {
    A = (Node *) makeGroupingSet(GROUPING_SET_CUBE, D, LOC(B));
}

/* ----- grouping_sets_clause ----- */

grouping_sets_clause(A) ::= GROUPING SETS LPAREN group_by_list(E) RPAREN. {
    A = (Node *) makeGroupingSet(GROUPING_SET_SETS, E, LOC(B));
}

/* ----- having_clause ----- */

having_clause(A) ::= HAVING a_expr(C). {
    A = C;
}

having_clause(A) ::= . {
    A = NULL;
}

/* ----- for_locking_clause ----- */

for_locking_clause(A) ::= for_locking_items(B). {
    A = B;
}

for_locking_clause(A) ::= FOR READ ONLY. {
    A = NIL;
}

/* ----- opt_for_locking_clause ----- */

opt_for_locking_clause(A) ::= for_locking_clause(B). {
    A = B;
}

opt_for_locking_clause(A) ::= . {
    A = NIL;
}

/* ----- for_locking_items ----- */

for_locking_items(A) ::= for_locking_item(B). {
    A = list_make1(B);
}

for_locking_items(A) ::= for_locking_items(B) for_locking_item(C). {
    A = lappend(B, C);
}

/* ----- for_locking_item ----- */

for_locking_item(A) ::= for_locking_strength(B) locked_rels_list(C) opt_nowait_or_skip(D). {
    LockingClause *n = makeNode(LockingClause);

    					n->lockedRels = C;
    					n->strength = B;
    					n->waitPolicy = D;
    					A = (Node *) n;
}

/* ----- for_locking_strength ----- */

for_locking_strength(A) ::= FOR UPDATE. {
    A = LCS_FORUPDATE;
}

for_locking_strength(A) ::= FOR NO KEY UPDATE. {
    A = LCS_FORNOKEYUPDATE;
}

for_locking_strength(A) ::= FOR SHARE. {
    A = LCS_FORSHARE;
}

for_locking_strength(A) ::= FOR KEY SHARE. {
    A = LCS_FORKEYSHARE;
}

/* ----- opt_for_locking_strength ----- */

opt_for_locking_strength(A) ::= for_locking_strength(B). {
    A = B;
}

opt_for_locking_strength(A) ::= . {
    A = LCS_NONE;
}

/* ----- locked_rels_list ----- */

locked_rels_list(A) ::= OF qualified_name_list(C). {
    A = C;
}

locked_rels_list(A) ::= . {
    A = NIL;
}

/* ----- values_clause ----- */

opt_target_list(A) ::= target_list(B). {
    A = B;
}

opt_target_list(A) ::= . {
    A = NIL;
}

/* ----- target_list ----- */

target_list(A) ::= target_el(B). {
    A = list_make1(B);
}

target_list(A) ::= target_list(B) COMMA target_el(D). {
    A = lappend(B, D);
}

/* ----- target_el ----- */

target_el(A) ::= a_expr(B) AS colLabel(D). {
    A = makeNode(ResTarget);
    					A->name = D;
    					A->indirection = NIL;
    					A->val = (Node *) B;
    					A->location = LOC(B);
}

target_el(A) ::= a_expr(B) bareColLabel(C). {
    A = makeNode(ResTarget);
    					A->name = C;
    					A->indirection = NIL;
    					A->val = (Node *) B;
    					A->location = LOC(B);
}

target_el(A) ::= a_expr(B). {
    A = makeNode(ResTarget);
    					A->name = NULL;
    					A->indirection = NIL;
    					A->val = (Node *) B;
    					A->location = LOC(B);
}

target_el(A) ::= STAR. {
    ColumnRef  *n = makeNode(ColumnRef);

    					n->fields = list_make1(makeNode(A_Star));
    					n->location = LOC(B);

    					A = makeNode(ResTarget);
    					A->name = NULL;
    					A->indirection = NIL;
    					A->val = (Node *) n;
    					A->location = LOC(B);
}

/* ----- qualified_name_list ----- */
```

