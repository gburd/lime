# INSERT Statement


INSERT statement grammar: INSERT INTO with VALUES or SELECT,
ON CONFLICT (upsert), OVERRIDING, and RETURNING clause.


```yaml
name: pg-insert
version: 17.0.0
description: INSERT statement (with ON CONFLICT, RETURNING)
provides: [pg-insert]
depends: [pg-type-decls, pg-select, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
insertStmt(A) ::= opt_with_clause(B) INSERT INTO insert_target(E) insert_rest(F) opt_on_conflict(G) returning_clause(H). {
    F->relation = E;
    					F->onConflictClause = G;
    					F->returningClause = H;
    					F->withClause = B;
    					A = (Node *) F;
}

/* ----- insert_target ----- */

insert_target(A) ::= qualified_name(B). {
    A = B;
}

insert_target(A) ::= qualified_name(B) AS colId(D). {
    B->alias = makeAlias(D, NIL);
    					A = B;
}

/* ----- insert_rest ----- */

insert_rest(A) ::= selectStmt(B). {
    A = makeNode(insertStmt);
    					A->cols = NIL;
    					A->selectStmt = B;
}

insert_rest(A) ::= OVERRIDING override_kind(C) VALUE_P selectStmt(E). {
    A = makeNode(insertStmt);
    					A->cols = NIL;
    					A->override = C;
    					A->selectStmt = E;
}

insert_rest(A) ::= LPAREN insert_column_list(C) RPAREN selectStmt(E). {
    A = makeNode(insertStmt);
    					A->cols = C;
    					A->selectStmt = E;
}

insert_rest(A) ::= LPAREN insert_column_list(C) RPAREN OVERRIDING override_kind(F) VALUE_P selectStmt(H). {
    A = makeNode(insertStmt);
    					A->cols = C;
    					A->override = F;
    					A->selectStmt = H;
}

insert_rest(A) ::= DEFAULT VALUES. {
    A = makeNode(insertStmt);
    					A->cols = NIL;
    					A->selectStmt = NULL;
}

/* ----- override_kind ----- */

override_kind(A) ::= USER. {
    A = OVERRIDING_USER_VALUE;
}

override_kind(A) ::= SYSTEM_P. {
    A = OVERRIDING_SYSTEM_VALUE;
}

/* ----- insert_column_list ----- */

insert_column_list(A) ::= insert_column_item(B). {
    A = list_make1(B);
}

insert_column_list(A) ::= insert_column_list(B) COMMA insert_column_item(D). {
    A = lappend(B, D);
}

/* ----- insert_column_item ----- */

insert_column_item(A) ::= colId(B) opt_indirection(C). {
    A = makeNode(ResTarget);
    					A->name = B;
    					A->indirection = check_indirection(C, yyscanner);
    					A->val = NULL;
    					A->location = LOC(B);
}

/* ----- opt_on_conflict ----- */

opt_on_conflict(A) ::= ON CONFLICT opt_conf_expr(D) DO SELECT opt_for_locking_strength(G) where_clause(H). {
    A = makeNode(OnConflictClause);
    					A->action = ONCONFLICT_SELECT;
    					A->infer = D;
    					A->targetList = NIL;
    					A->lockStrength = G;
    					A->whereClause = H;
    					A->location = LOC(B);
}

opt_on_conflict(A) ::= ON CONFLICT opt_conf_expr(D) DO UPDATE SET set_clause_list(H) where_clause(I). {
    A = makeNode(OnConflictClause);
    					A->action = ONCONFLICT_UPDATE;
    					A->infer = D;
    					A->targetList = H;
    					A->lockStrength = LCS_NONE;
    					A->whereClause = I;
    					A->location = LOC(B);
}

opt_on_conflict(A) ::= ON CONFLICT opt_conf_expr(D) DO NOTHING. {
    A = makeNode(OnConflictClause);
    					A->action = ONCONFLICT_NOTHING;
    					A->infer = D;
    					A->targetList = NIL;
    					A->lockStrength = LCS_NONE;
    					A->whereClause = NULL;
    					A->location = LOC(B);
}

opt_on_conflict(A) ::= . {
    A = NULL;
}

/* ----- opt_conf_expr ----- */

opt_conf_expr(A) ::= LPAREN index_params(C) RPAREN where_clause(E). {
    A = makeNode(InferClause);
    					A->indexElems = C;
    					A->whereClause = E;
    					A->conname = NULL;
    					A->location = LOC(B);
}

opt_conf_expr(A) ::= ON CONSTRAINT name(D). {
    A = makeNode(InferClause);
    					A->indexElems = NIL;
    					A->whereClause = NULL;
    					A->conname = D;
    					A->location = LOC(B);
}

opt_conf_expr(A) ::= . {
    A = NULL;
}

/* ----- returning_clause ----- */

returning_clause(A) ::= RETURNING returning_with_clause(C) target_list(D). {
    ReturningClause *n = makeNode(ReturningClause);

    					n->options = C;
    					n->exprs = D;
    					A = n;
}

returning_clause(A) ::= . {
    A = NULL;
}

/* ----- returning_with_clause ----- */

returning_with_clause(A) ::= WITH LPAREN returning_options(D) RPAREN. {
    A = D;
}

returning_with_clause(A) ::= . {
    A = NIL;
}

/* ----- returning_options ----- */

returning_options(A) ::= returning_option(B). {
    A = list_make1(B);
}

returning_options(A) ::= returning_options(B) COMMA returning_option(D). {
    A = lappend(B, D);
}

/* ----- returning_option ----- */

returning_option(A) ::= returning_option_kind(B) AS colId(D). {
    ReturningOption *n = makeNode(ReturningOption);

    					n->option = B;
    					n->value = D;
    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- returning_option_kind ----- */

returning_option_kind(A) ::= OLD. {
    A = RETURNING_OPTION_OLD;
}

returning_option_kind(A) ::= NEW. {
    A = RETURNING_OPTION_NEW;
}

/* ----- deleteStmt ----- */
```

