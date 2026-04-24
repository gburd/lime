# Index Operations


CREATE INDEX (including UNIQUE, CONCURRENTLY, INCLUDE)
and REINDEX operations.


```yaml
name: pg-ddl-index
version: 17.0.0
description: CREATE INDEX, REINDEX
provides: [pg-ddl-index]
depends: [pg-type-decls, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
indexStmt(A) ::= CREATE opt_unique(C) INDEX opt_concurrently(E) opt_single_name(F) ON relation_expr(H) access_method_clause(I) LPAREN index_params(K) RPAREN opt_include(M) opt_unique_null_treatment(N) opt_reloptions(O) optTableSpace(P) where_clause(Q). {
    indexStmt *n = makeNode(indexStmt);

    					n->unique = C;
    					n->concurrent = E;
    					n->idxname = F;
    					n->relation = H;
    					n->accessMethod = I;
    					n->indexParams = K;
    					n->indexIncludingParams = M;
    					n->nulls_not_distinct = !N;
    					n->options = O;
    					n->tableSpace = P;
    					n->whereClause = Q;
    					n->excludeOpNames = NIL;
    					n->idxcomment = NULL;
    					n->indexOid = InvalidOid;
    					n->oldNumber = InvalidRelFileNumber;
    					n->oldCreateSubid = InvalidSubTransactionId;
    					n->oldFirstRelfilelocatorSubid = InvalidSubTransactionId;
    					n->primary = false;
    					n->isconstraint = false;
    					n->deferrable = false;
    					n->initdeferred = false;
    					n->transformed = false;
    					n->if_not_exists = false;
    					n->reset_default_tblspc = false;
    					A = (Node *) n;
}

indexStmt(A) ::= CREATE opt_unique(C) INDEX opt_concurrently(E) IF_P NOT EXISTS name(I) ON relation_expr(K) access_method_clause(L) LPAREN index_params(N) RPAREN opt_include(P) opt_unique_null_treatment(Q) opt_reloptions(R) optTableSpace(S) where_clause(T). {
    indexStmt *n = makeNode(indexStmt);

    					n->unique = C;
    					n->concurrent = E;
    					n->idxname = I;
    					n->relation = K;
    					n->accessMethod = L;
    					n->indexParams = N;
    					n->indexIncludingParams = P;
    					n->nulls_not_distinct = !Q;
    					n->options = R;
    					n->tableSpace = S;
    					n->whereClause = T;
    					n->excludeOpNames = NIL;
    					n->idxcomment = NULL;
    					n->indexOid = InvalidOid;
    					n->oldNumber = InvalidRelFileNumber;
    					n->oldCreateSubid = InvalidSubTransactionId;
    					n->oldFirstRelfilelocatorSubid = InvalidSubTransactionId;
    					n->primary = false;
    					n->isconstraint = false;
    					n->deferrable = false;
    					n->initdeferred = false;
    					n->transformed = false;
    					n->if_not_exists = true;
    					n->reset_default_tblspc = false;
    					A = (Node *) n;
}

/* ----- opt_unique ----- */

opt_unique(A) ::= UNIQUE. {
    A = true;
}

opt_unique(A) ::= . {
    A = false;
}

/* ----- access_method_clause ----- */

access_method_clause(A) ::= USING name(C). {
    A = C;
}

access_method_clause(A) ::= . {
    A = DEFAULT_INDEX_TYPE;
}

/* ----- index_params ----- */

index_params(A) ::= index_elem(B). {
    A = list_make1(B);
}

index_params(A) ::= index_params(B) COMMA index_elem(D). {
    A = lappend(B, D);
}

/* ----- index_elem_options ----- */

index_elem_options(A) ::= opt_collate(B) opt_qualified_name(C) opt_asc_desc(D) opt_nulls_order(E). {
    A = makeNode(IndexElem);
    			A->name = NULL;
    			A->expr = NULL;
    			A->indexcolname = NULL;
    			A->collation = B;
    			A->opclass = C;
    			A->opclassopts = NIL;
    			A->ordering = D;
    			A->nulls_ordering = E;
}

index_elem_options(A) ::= opt_collate(B) any_name(C) reloptions(D) opt_asc_desc(E) opt_nulls_order(F). {
    A = makeNode(IndexElem);
    			A->name = NULL;
    			A->expr = NULL;
    			A->indexcolname = NULL;
    			A->collation = B;
    			A->opclass = C;
    			A->opclassopts = D;
    			A->ordering = E;
    			A->nulls_ordering = F;
}

/* ----- index_elem ----- */

index_elem(A) ::= colId(B) index_elem_options(C). {
    A = C;
    					A->name = B;
    					A->location = LOC(B);
}

index_elem(A) ::= func_expr_windowless(B) index_elem_options(C). {
    A = C;
    					A->expr = B;
    					A->location = LOC(B);
}

index_elem(A) ::= LPAREN a_expr(C) RPAREN index_elem_options(E). {
    A = E;
    					A->expr = C;
    					A->location = LOC(B);
}

/* ----- opt_include ----- */

opt_include(A) ::= INCLUDE LPAREN index_including_params(D) RPAREN. {
    A = D;
}

opt_include(A) ::= . {
    A = NIL;
}

/* ----- index_including_params ----- */

index_including_params(A) ::= index_elem(B). {
    A = list_make1(B);
}

index_including_params(A) ::= index_including_params(B) COMMA index_elem(D). {
    A = lappend(B, D);
}

/* ----- opt_collate ----- */

opt_collate(A) ::= COLLATE any_name(C). {
    A = C;
}

opt_collate(A) ::= . {
    A = NIL;
}

/* ----- opt_asc_desc ----- */

opt_asc_desc(A) ::= ASC. {
    A = SORTBY_ASC;
}

opt_asc_desc(A) ::= DESC. {
    A = SORTBY_DESC;
}

opt_asc_desc(A) ::= . {
    A = SORTBY_DEFAULT;
}

/* ----- opt_nulls_order ----- */

opt_nulls_order(A) ::= NULLS_LA FIRST_P. {
    A = SORTBY_NULLS_FIRST;
}

opt_nulls_order(A) ::= NULLS_LA LAST_P. {
    A = SORTBY_NULLS_LAST;
}

opt_nulls_order(A) ::= . {
    A = SORTBY_NULLS_DEFAULT;
}

/* ----- createFunctionStmt ----- */

reindexStmt(A) ::= REINDEX opt_utility_option_list(C) reindex_target_relation(D) opt_concurrently(E) qualified_name(F). {
    reindexStmt *n = makeNode(reindexStmt);

    					n->kind = D;
    					n->relation = F;
    					n->name = NULL;
    					n->params = C;
    					if (E)
    						n->params = lappend(n->params,
    											makeDefElem("concurrently", NULL, LOC(E)));
    					A = (Node *) n;
}

reindexStmt(A) ::= REINDEX opt_utility_option_list(C) SCHEMA opt_concurrently(E) name(F). {
    reindexStmt *n = makeNode(reindexStmt);

    					n->kind = REINDEX_OBJECT_SCHEMA;
    					n->relation = NULL;
    					n->name = F;
    					n->params = C;
    					if (E)
    						n->params = lappend(n->params,
    											makeDefElem("concurrently", NULL, LOC(E)));
    					A = (Node *) n;
}

reindexStmt(A) ::= REINDEX opt_utility_option_list(C) reindex_target_all(D) opt_concurrently(E) opt_single_name(F). {
    reindexStmt *n = makeNode(reindexStmt);

    					n->kind = D;
    					n->relation = NULL;
    					n->name = F;
    					n->params = C;
    					if (E)
    						n->params = lappend(n->params,
    											makeDefElem("concurrently", NULL, LOC(E)));
    					A = (Node *) n;
}

/* ----- reindex_target_relation ----- */

reindex_target_relation(A) ::= INDEX. {
    A = REINDEX_OBJECT_INDEX;
}

reindex_target_relation(A) ::= TABLE. {
    A = REINDEX_OBJECT_TABLE;
}

/* ----- reindex_target_all ----- */

reindex_target_all(A) ::= SYSTEM_P. {
    A = REINDEX_OBJECT_SYSTEM;
}

reindex_target_all(A) ::= DATABASE. {
    A = REINDEX_OBJECT_DATABASE;
}

/* ----- alterTblSpcStmt ----- */
```

