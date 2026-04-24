# Common Table Expressions


WITH clause support: common table expressions, RECURSIVE,
MATERIALIZED/NOT MATERIALIZED, SEARCH, and CYCLE clauses.


```yaml
name: pg-cte
version: 17.0.0
description: Common Table Expressions (WITH clause, recursive CTEs)
provides: [pg-cte]
depends: [pg-type-decls, pg-expressions]
```

## Production Rules

```lime rules
with_clause(A) ::= WITH cte_list(C). {
    A = makeNode(WithClause);
    				A->ctes = C;
    				A->recursive = false;
    				A->location = LOC(B);
}

with_clause(A) ::= WITH_LA cte_list(C). {
    A = makeNode(WithClause);
    				A->ctes = C;
    				A->recursive = false;
    				A->location = LOC(B);
}

with_clause(A) ::= WITH RECURSIVE cte_list(D). {
    A = makeNode(WithClause);
    				A->ctes = D;
    				A->recursive = true;
    				A->location = LOC(B);
}

/* ----- cte_list ----- */

cte_list(A) ::= common_table_expr(B). {
    A = list_make1(B);
}

cte_list(A) ::= cte_list(B) COMMA common_table_expr(D). {
    A = lappend(B, D);
}

/* ----- common_table_expr ----- */

common_table_expr(A) ::= name(B) opt_name_list(C) AS opt_materialized(E) LPAREN preparableStmt(G) RPAREN opt_search_clause(I) opt_cycle_clause(J). {
    CommonTableExpr *n = makeNode(CommonTableExpr);

    				n->ctename = B;
    				n->aliascolnames = C;
    				n->ctematerialized = E;
    				n->ctequery = G;
    				n->search_clause = castNode(CTESearchClause, I);
    				n->cycle_clause = castNode(CTECycleClause, J);
    				n->location = LOC(B);
    				A = (Node *) n;
}

/* ----- opt_materialized ----- */

opt_materialized(A) ::= MATERIALIZED. {
    A = CTEMaterializeAlways;
}

opt_materialized(A) ::= NOT MATERIALIZED. {
    A = CTEMaterializeNever;
}

opt_materialized(A) ::= . {
    A = CTEMaterializeDefault;
}

/* ----- opt_search_clause ----- */

opt_search_clause(A) ::= SEARCH DEPTH FIRST_P BY columnList(F) SET colId(H). {
    CTESearchClause *n = makeNode(CTESearchClause);

    				n->search_col_list = F;
    				n->search_breadth_first = false;
    				n->search_seq_column = H;
    				n->location = LOC(B);
    				A = (Node *) n;
}

opt_search_clause(A) ::= SEARCH BREADTH FIRST_P BY columnList(F) SET colId(H). {
    CTESearchClause *n = makeNode(CTESearchClause);

    				n->search_col_list = F;
    				n->search_breadth_first = true;
    				n->search_seq_column = H;
    				n->location = LOC(B);
    				A = (Node *) n;
}

opt_search_clause(A) ::= . {
    A = NULL;
}

/* ----- opt_cycle_clause ----- */

opt_cycle_clause(A) ::= CYCLE columnList(C) SET colId(E) TO aexprConst(G) DEFAULT aexprConst(I) USING colId(K). {
    CTECycleClause *n = makeNode(CTECycleClause);

    				n->cycle_col_list = C;
    				n->cycle_mark_column = E;
    				n->cycle_mark_value = G;
    				n->cycle_mark_default = I;
    				n->cycle_path_column = K;
    				n->location = LOC(B);
    				A = (Node *) n;
}

opt_cycle_clause(A) ::= CYCLE columnList(C) SET colId(E) USING colId(G). {
    CTECycleClause *n = makeNode(CTECycleClause);

    				n->cycle_col_list = C;
    				n->cycle_mark_column = E;
    				n->cycle_mark_value = makeBoolAConst(true, -1);
    				n->cycle_mark_default = makeBoolAConst(false, -1);
    				n->cycle_path_column = G;
    				n->location = LOC(B);
    				A = (Node *) n;
}

opt_cycle_clause(A) ::= . {
    A = NULL;
}

/* ----- opt_with_clause ----- */

opt_with_clause(A) ::= with_clause(B). {
    A = B;
}

opt_with_clause(A) ::= . {
    A = NULL;
}

/* ----- into_clause ----- */
```

