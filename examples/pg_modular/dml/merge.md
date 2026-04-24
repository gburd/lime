# MERGE Statement


MERGE statement grammar: WHEN MATCHED (UPDATE, DELETE),
WHEN NOT MATCHED (INSERT), with optional conditions.


```yaml
name: pg-merge
version: 17.0.0
description: MERGE statement (WHEN MATCHED/NOT MATCHED)
provides: [pg-merge]
depends: [pg-type-decls, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
mergeStmt(A) ::= opt_with_clause(B) MERGE INTO relation_expr_opt_alias(E) USING table_ref(G) ON a_expr(I) merge_when_list(J) returning_clause(K). {
    mergeStmt  *m = makeNode(mergeStmt);

    					m->withClause = B;
    					m->relation = E;
    					m->sourceRelation = G;
    					m->joinCondition = I;
    					m->mergeWhenClauses = J;
    					m->returningClause = K;

    					A = (Node *) m;
}

/* ----- merge_when_list ----- */

merge_when_list(A) ::= merge_when_clause(B). {
    A = list_make1(B);
}

merge_when_list(A) ::= merge_when_list(B) merge_when_clause(C). {
    A = lappend(B,C);
}

/* ----- merge_when_clause ----- */

merge_when_clause(A) ::= merge_when_tgt_matched(B) opt_merge_when_condition(C) THEN merge_update(E). {
    E->matchKind = B;
    					E->condition = C;

    					A = (Node *) E;
}

merge_when_clause(A) ::= merge_when_tgt_matched(B) opt_merge_when_condition(C) THEN merge_delete(E). {
    E->matchKind = B;
    					E->condition = C;

    					A = (Node *) E;
}

merge_when_clause(A) ::= merge_when_tgt_not_matched(B) opt_merge_when_condition(C) THEN merge_insert(E). {
    E->matchKind = B;
    					E->condition = C;

    					A = (Node *) E;
}

merge_when_clause(A) ::= merge_when_tgt_matched(B) opt_merge_when_condition(C) THEN DO NOTHING. {
    MergeWhenClause *m = makeNode(MergeWhenClause);

    					m->matchKind = B;
    					m->commandType = CMD_NOTHING;
    					m->condition = C;

    					A = (Node *) m;
}

merge_when_clause(A) ::= merge_when_tgt_not_matched(B) opt_merge_when_condition(C) THEN DO NOTHING. {
    MergeWhenClause *m = makeNode(MergeWhenClause);

    					m->matchKind = B;
    					m->commandType = CMD_NOTHING;
    					m->condition = C;

    					A = (Node *) m;
}

/* ----- merge_when_tgt_matched ----- */

merge_when_tgt_matched(A) ::= WHEN MATCHED. {
    A = MERGE_WHEN_MATCHED;
}

merge_when_tgt_matched(A) ::= WHEN NOT MATCHED BY SOURCE. {
    A = MERGE_WHEN_NOT_MATCHED_BY_SOURCE;
}

/* ----- merge_when_tgt_not_matched ----- */

merge_when_tgt_not_matched(A) ::= WHEN NOT MATCHED. {
    A = MERGE_WHEN_NOT_MATCHED_BY_TARGET;
}

merge_when_tgt_not_matched(A) ::= WHEN NOT MATCHED BY TARGET. {
    A = MERGE_WHEN_NOT_MATCHED_BY_TARGET;
}

/* ----- opt_merge_when_condition ----- */

opt_merge_when_condition(A) ::= AND a_expr(C). {
    A = C;
}

opt_merge_when_condition(A) ::= . {
    A = NULL;
}

/* ----- merge_update ----- */

merge_update(A) ::= UPDATE SET set_clause_list(D). {
    MergeWhenClause *n = makeNode(MergeWhenClause);
    					n->commandType = CMD_UPDATE;
    					n->override = OVERRIDING_NOT_SET;
    					n->targetList = D;
    					n->values = NIL;

    					A = n;
}

/* ----- merge_delete ----- */

merge_delete(A) ::= DELETE_P. {
    MergeWhenClause *n = makeNode(MergeWhenClause);
    					n->commandType = CMD_DELETE;
    					n->override = OVERRIDING_NOT_SET;
    					n->targetList = NIL;
    					n->values = NIL;

    					A = n;
}

/* ----- merge_insert ----- */

merge_insert(A) ::= INSERT merge_values_clause(C). {
    MergeWhenClause *n = makeNode(MergeWhenClause);
    					n->commandType = CMD_INSERT;
    					n->override = OVERRIDING_NOT_SET;
    					n->targetList = NIL;
    					n->values = C;
    					A = n;
}

merge_insert(A) ::= INSERT OVERRIDING override_kind(D) VALUE_P merge_values_clause(F). {
    MergeWhenClause *n = makeNode(MergeWhenClause);
    					n->commandType = CMD_INSERT;
    					n->override = D;
    					n->targetList = NIL;
    					n->values = F;
    					A = n;
}

merge_insert(A) ::= INSERT LPAREN insert_column_list(D) RPAREN merge_values_clause(F). {
    MergeWhenClause *n = makeNode(MergeWhenClause);
    					n->commandType = CMD_INSERT;
    					n->override = OVERRIDING_NOT_SET;
    					n->targetList = D;
    					n->values = F;
    					A = n;
}

merge_insert(A) ::= INSERT LPAREN insert_column_list(D) RPAREN OVERRIDING override_kind(G) VALUE_P merge_values_clause(I). {
    MergeWhenClause *n = makeNode(MergeWhenClause);
    					n->commandType = CMD_INSERT;
    					n->override = G;
    					n->targetList = D;
    					n->values = I;
    					A = n;
}

merge_insert(A) ::= INSERT DEFAULT VALUES. {
    MergeWhenClause *n = makeNode(MergeWhenClause);
    					n->commandType = CMD_INSERT;
    					n->override = OVERRIDING_NOT_SET;
    					n->targetList = NIL;
    					n->values = NIL;
    					A = n;
}

/* ----- merge_values_clause ----- */

merge_values_clause(A) ::= VALUES LPAREN expr_list(D) RPAREN. {
    A = D;
}

/* ----- declareCursorStmt ----- */
```

