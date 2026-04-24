# UPDATE Statement


UPDATE statement grammar: SET clauses (single column and
multi-column), FROM, WHERE, and RETURNING.


```yaml
name: pg-update
version: 17.0.0
description: UPDATE statement (SET clauses, FROM, WHERE, RETURNING)
provides: [pg-update]
depends: [pg-type-decls, pg-expressions, pg-from-clause, pg-base-helpers]
```

## Production Rules

```lime rules
updateStmt(A) ::= opt_with_clause(B) UPDATE relation_expr_opt_alias(D) SET set_clause_list(F) from_clause(G) where_or_current_clause(H) returning_clause(I). {
    updateStmt *n = makeNode(updateStmt);

    					n->relation = D;
    					n->targetList = F;
    					n->fromClause = G;
    					n->whereClause = H;
    					n->returningClause = I;
    					n->withClause = B;
    					A = (Node *) n;
}

/* ----- set_clause_list ----- */

set_clause_list(A) ::= set_clause(B). {
    A = B;
}

set_clause_list(A) ::= set_clause_list(B) COMMA set_clause(D). {
    A = list_concat(B,D);
}

/* ----- set_clause ----- */

set_clause(A) ::= set_target(B) EQ a_expr(D). {
    B->val = (Node *) D;
    					A = list_make1(B);
}

set_clause(A) ::= LPAREN set_target_list(C) RPAREN EQ a_expr(F). {
    int			ncolumns = list_length(C);
    					int			i = 1;
    					ListCell   *col_cell;


    					foreach(col_cell, C)
    					{
    						ResTarget  *res_col = (ResTarget *) lfirst(col_cell);
    						MultiAssignRef *r = makeNode(MultiAssignRef);

    						r->source = (Node *) F;
    						r->colno = i;
    						r->ncolumns = ncolumns;
    						res_col->val = (Node *) r;
    						i++;
    					}

    					A = C;
}

/* ----- set_target ----- */

set_target(A) ::= colId(B) opt_indirection(C). {
    A = makeNode(ResTarget);
    					A->name = B;
    					A->indirection = check_indirection(C, yyscanner);
    					A->val = NULL;	
    					A->location = LOC(B);
}

/* ----- set_target_list ----- */

set_target_list(A) ::= set_target(B). {
    A = list_make1(B);
}

set_target_list(A) ::= set_target_list(B) COMMA set_target(D). {
    A = lappend(B,D);
}

/* ----- mergeStmt ----- */
```

