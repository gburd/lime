# DELETE Statement


DELETE statement grammar: DELETE FROM with USING, WHERE
CURRENT OF, and RETURNING.


```yaml
name: pg-delete
version: 17.0.0
description: DELETE statement (USING, WHERE, RETURNING)
provides: [pg-delete]
depends: [pg-type-decls, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
deleteStmt(A) ::= opt_with_clause(B) DELETE_P FROM relation_expr_opt_alias(E) using_clause(F) where_or_current_clause(G) returning_clause(H). {
    deleteStmt *n = makeNode(deleteStmt);

    					n->relation = E;
    					n->usingClause = F;
    					n->whereClause = G;
    					n->returningClause = H;
    					n->withClause = B;
    					A = (Node *) n;
}

/* ----- using_clause ----- */

using_clause(A) ::= USING from_list(C). {
    A = C;
}

using_clause(A) ::= . {
    A = NIL;
}

/* ----- lockStmt ----- */
```

