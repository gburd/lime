# PL/pgSQL Support


Grammar rules for PL/pgSQL integration: expression parsing
and assignment statement parsing modes injected by the
PL/pgSQL handler via MODE tokens.


```yaml
name: pg-plpgsql
version: 17.0.0
description: PL/pgSQL expression and assignment parsing modes
provides: [pg-plpgsql]
depends: [pg-type-decls, pg-expressions]
```

## Production Rules

```lime rules
pLpgSQL_Expr(A) ::= opt_distinct_clause(B) opt_target_list(C) from_clause(D) where_clause(E) group_clause(F) having_clause(G) window_clause(H) opt_sort_clause(I) opt_select_limit(J) opt_for_locking_clause(K). {
    selectStmt *n = makeNode(selectStmt);

    					n->distinctClause = B;
    					n->targetList = C;
    					n->fromClause = D;
    					n->whereClause = E;
    					n->groupClause = (F)->list;
    					n->groupDistinct = (F)->distinct;
    					n->groupByAll = (F)->all;
    					n->havingClause = G;
    					n->windowClause = H;
    					n->sortClause = I;
    					if (J)
    					{
    						n->limitOffset = J->limitOffset;
    						n->limitCount = J->limitCount;
    						if (!n->sortClause &&
    							J->limitOption == LIMIT_OPTION_WITH_TIES)
    							ereport(ERROR,
    									(errcode(ERRCODE_SYNTAX_ERROR),
    									 errmsg("WITH TIES cannot be specified without ORDER BY clause"),
    									 parser_errposition(J->optionLoc)));
    						n->limitOption = J->limitOption;
    					}
    					n->lockingClause = K;
    					A = (Node *) n;
}

/* ----- pLAssignStmt ----- */

pLAssignStmt(A) ::= plassign_target(B) opt_indirection(C) plassign_equals(D) pLpgSQL_Expr(E). {
    pLAssignStmt *n = makeNode(pLAssignStmt);

    					n->name = B;
    					n->indirection = check_indirection(C, yyscanner);

    					n->val = (selectStmt *) E;
    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- plassign_target ----- */

plassign_target(A) ::= colId(B). {
    A = B;
}

plassign_target(A) ::= PARAM. {
    A = psprintf("$%d", B);
}

/* ----- plassign_equals ----- */

plassign_equals(A) ::= COLON_EQUALS.

plassign_equals(A) ::= EQ.

/* ----- colId ----- */
```

