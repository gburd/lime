# CREATE TABLE


CREATE TABLE grammar: column definitions, constraints
(CHECK, UNIQUE, PRIMARY KEY, FOREIGN KEY, EXCLUDE),
partitioning, inheritance, LIKE, and table options.


```yaml
name: pg-ddl-tables
version: 17.0.0
description: CREATE TABLE, column definitions, constraints, partitioning
provides: [pg-ddl-tables]
depends: [pg-type-decls, pg-expressions, pg-typenames, pg-base-helpers]
```

## Production Rules

```lime rules
partitions_list(A) ::= singlePartitionSpec(B). {
    A = list_make1(B);
}

partitions_list(A) ::= partitions_list(B) COMMA singlePartitionSpec(D). {
    A = lappend(B, D);
}

/* ----- singlePartitionSpec ----- */

singlePartitionSpec(A) ::= PARTITION qualified_name(C) partitionBoundSpec(D). {
    singlePartitionSpec *n = makeNode(singlePartitionSpec);

    					n->name = C;
    					n->bound = D;

    					A = n;
}

/* ----- partition_cmd ----- */

partitionBoundSpec(A) ::= FOR VALUES WITH LPAREN hash_partbound(F) RPAREN. {
    ListCell   *lc;
    					partitionBoundSpec *n = makeNode(partitionBoundSpec);

    					n->strategy = PARTITION_STRATEGY_HASH;
    					n->modulus = n->remainder = -1;

    					foreach (lc, F)
    					{
    						DefElem    *opt = lfirst_node(DefElem, lc);

    						if (strcmp(opt->defname, "modulus") == 0)
    						{
    							if (n->modulus != -1)
    								ereport(ERROR,
    										(errcode(ERRCODE_DUPLICATE_OBJECT),
    										 errmsg("modulus for hash partition provided more than once"),
    										 parser_errposition(opt->location)));
    							n->modulus = defGetInt32(opt);
    						}
    						else if (strcmp(opt->defname, "remainder") == 0)
    						{
    							if (n->remainder != -1)
    								ereport(ERROR,
    										(errcode(ERRCODE_DUPLICATE_OBJECT),
    										 errmsg("remainder for hash partition provided more than once"),
    										 parser_errposition(opt->location)));
    							n->remainder = defGetInt32(opt);
    						}
    						else
    							ereport(ERROR,
    									(errcode(ERRCODE_SYNTAX_ERROR),
    									 errmsg("unrecognized hash partition bound specification \"%s\"",
    											opt->defname),
    									 parser_errposition(opt->location)));
    					}

    					if (n->modulus == -1)
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("modulus for hash partition must be specified"),
    								 parser_errposition(LOC(D))));
    					if (n->remainder == -1)
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("remainder for hash partition must be specified"),
    								 parser_errposition(LOC(D))));

    					n->location = LOC(D);

    					A = n;
}

partitionBoundSpec(A) ::= FOR VALUES IN_P LPAREN expr_list(F) RPAREN. {
    partitionBoundSpec *n = makeNode(partitionBoundSpec);

    					n->strategy = PARTITION_STRATEGY_LIST;
    					n->is_default = false;
    					n->listdatums = F;
    					n->location = LOC(D);

    					A = n;
}

partitionBoundSpec(A) ::= FOR VALUES FROM LPAREN expr_list(F) RPAREN TO LPAREN expr_list(J) RPAREN. {
    partitionBoundSpec *n = makeNode(partitionBoundSpec);

    					n->strategy = PARTITION_STRATEGY_RANGE;
    					n->is_default = false;
    					n->lowerdatums = F;
    					n->upperdatums = J;
    					n->location = LOC(D);

    					A = n;
}

partitionBoundSpec(A) ::= DEFAULT. {
    partitionBoundSpec *n = makeNode(partitionBoundSpec);

    					n->is_default = true;
    					n->location = LOC(B);

    					A = n;
}

/* ----- hash_partbound_elem ----- */

hash_partbound_elem(A) ::= nonReservedWord(B) iconst(C). {
    A = makeDefElem(B, (Node *) makeInteger(C), LOC(B));
}

/* ----- hash_partbound ----- */

hash_partbound(A) ::= hash_partbound_elem(B). {
    A = list_make1(B);
}

hash_partbound(A) ::= hash_partbound(B) COMMA hash_partbound_elem(D). {
    A = lappend(B, D);
}

/* ----- alterCompositeTypeStmt ----- */

createStmt(A) ::= CREATE optTemp(C) TABLE qualified_name(E) LPAREN optTableElementList(G) RPAREN optInherit(I) optPartitionSpec(J) table_access_method_clause(K) optWith(L) onCommitOption(M) optTableSpace(N). {
    createStmt *n = makeNode(createStmt);

    					E->relpersistence = C;
    					n->relation = E;
    					n->tableElts = G;
    					n->inhRelations = I;
    					n->partspec = J;
    					n->ofTypename = NULL;
    					n->constraints = NIL;
    					n->accessMethod = K;
    					n->options = L;
    					n->oncommit = M;
    					n->tablespacename = N;
    					n->if_not_exists = false;
    					A = (Node *) n;
}

createStmt(A) ::= CREATE optTemp(C) TABLE IF_P NOT EXISTS qualified_name(H) LPAREN optTableElementList(J) RPAREN optInherit(L) optPartitionSpec(M) table_access_method_clause(N) optWith(O) onCommitOption(P) optTableSpace(Q). {
    createStmt *n = makeNode(createStmt);

    					H->relpersistence = C;
    					n->relation = H;
    					n->tableElts = J;
    					n->inhRelations = L;
    					n->partspec = M;
    					n->ofTypename = NULL;
    					n->constraints = NIL;
    					n->accessMethod = N;
    					n->options = O;
    					n->oncommit = P;
    					n->tablespacename = Q;
    					n->if_not_exists = true;
    					A = (Node *) n;
}

createStmt(A) ::= CREATE optTemp(C) TABLE qualified_name(E) OF any_name(G) optTypedTableElementList(H) optPartitionSpec(I) table_access_method_clause(J) optWith(K) onCommitOption(L) optTableSpace(M). {
    createStmt *n = makeNode(createStmt);

    					E->relpersistence = C;
    					n->relation = E;
    					n->tableElts = H;
    					n->inhRelations = NIL;
    					n->partspec = I;
    					n->ofTypename = makeTypeNameFromNameList(G);
    					n->ofTypename->location = LOC(G);
    					n->constraints = NIL;
    					n->accessMethod = J;
    					n->options = K;
    					n->oncommit = L;
    					n->tablespacename = M;
    					n->if_not_exists = false;
    					A = (Node *) n;
}

createStmt(A) ::= CREATE optTemp(C) TABLE IF_P NOT EXISTS qualified_name(H) OF any_name(J) optTypedTableElementList(K) optPartitionSpec(L) table_access_method_clause(M) optWith(N) onCommitOption(O) optTableSpace(P). {
    createStmt *n = makeNode(createStmt);

    					H->relpersistence = C;
    					n->relation = H;
    					n->tableElts = K;
    					n->inhRelations = NIL;
    					n->partspec = L;
    					n->ofTypename = makeTypeNameFromNameList(J);
    					n->ofTypename->location = LOC(J);
    					n->constraints = NIL;
    					n->accessMethod = M;
    					n->options = N;
    					n->oncommit = O;
    					n->tablespacename = P;
    					n->if_not_exists = true;
    					A = (Node *) n;
}

createStmt(A) ::= CREATE optTemp(C) TABLE qualified_name(E) PARTITION OF qualified_name(H) optTypedTableElementList(I) partitionBoundSpec(J) optPartitionSpec(K) table_access_method_clause(L) optWith(M) onCommitOption(N) optTableSpace(O). {
    createStmt *n = makeNode(createStmt);

    					E->relpersistence = C;
    					n->relation = E;
    					n->tableElts = I;
    					n->inhRelations = list_make1(H);
    					n->partbound = J;
    					n->partspec = K;
    					n->ofTypename = NULL;
    					n->constraints = NIL;
    					n->accessMethod = L;
    					n->options = M;
    					n->oncommit = N;
    					n->tablespacename = O;
    					n->if_not_exists = false;
    					A = (Node *) n;
}

createStmt(A) ::= CREATE optTemp(C) TABLE IF_P NOT EXISTS qualified_name(H) PARTITION OF qualified_name(K) optTypedTableElementList(L) partitionBoundSpec(M) optPartitionSpec(N) table_access_method_clause(O) optWith(P) onCommitOption(Q) optTableSpace(R). {
    createStmt *n = makeNode(createStmt);

    					H->relpersistence = C;
    					n->relation = H;
    					n->tableElts = L;
    					n->inhRelations = list_make1(K);
    					n->partbound = M;
    					n->partspec = N;
    					n->ofTypename = NULL;
    					n->constraints = NIL;
    					n->accessMethod = O;
    					n->options = P;
    					n->oncommit = Q;
    					n->tablespacename = R;
    					n->if_not_exists = true;
    					A = (Node *) n;
}

/* ----- optTemp ----- */

optTemp(A) ::= TEMPORARY. {
    A = RELPERSISTENCE_TEMP;
}

optTemp(A) ::= TEMP. {
    A = RELPERSISTENCE_TEMP;
}

optTemp(A) ::= LOCAL TEMPORARY. {
    A = RELPERSISTENCE_TEMP;
}

optTemp(A) ::= LOCAL TEMP. {
    A = RELPERSISTENCE_TEMP;
}

optTemp(A) ::= GLOBAL TEMPORARY. {
    ereport(WARNING,
    							(errmsg("GLOBAL is deprecated in temporary table creation"),
    							 parser_errposition(LOC(B))));
    					A = RELPERSISTENCE_TEMP;
}

optTemp(A) ::= GLOBAL TEMP. {
    ereport(WARNING,
    							(errmsg("GLOBAL is deprecated in temporary table creation"),
    							 parser_errposition(LOC(B))));
    					A = RELPERSISTENCE_TEMP;
}

optTemp(A) ::= UNLOGGED. {
    A = RELPERSISTENCE_UNLOGGED;
}

optTemp(A) ::= . {
    A = RELPERSISTENCE_PERMANENT;
}

/* ----- optTableElementList ----- */

optTableElementList(A) ::= tableElementList(B). {
    A = B;
}

optTableElementList(A) ::= . {
    A = NIL;
}

/* ----- optTypedTableElementList ----- */

optTypedTableElementList(A) ::= LPAREN typedTableElementList(C) RPAREN. {
    A = C;
}

optTypedTableElementList(A) ::= . {
    A = NIL;
}

/* ----- tableElementList ----- */

tableElementList(A) ::= tableElement(B). {
    A = list_make1(B);
}

tableElementList(A) ::= tableElementList(B) COMMA tableElement(D). {
    A = lappend(B, D);
}

/* ----- typedTableElementList ----- */

typedTableElementList(A) ::= typedTableElement(B). {
    A = list_make1(B);
}

typedTableElementList(A) ::= typedTableElementList(B) COMMA typedTableElement(D). {
    A = lappend(B, D);
}

/* ----- tableElement ----- */

tableElement(A) ::= columnDef(B). {
    A = B;
}

tableElement(A) ::= tableLikeClause(B). {
    A = B;
}

tableElement(A) ::= tableConstraint(B). {
    A = B;
}

/* ----- typedTableElement ----- */

typedTableElement(A) ::= columnOptions(B). {
    A = B;
}

typedTableElement(A) ::= tableConstraint(B). {
    A = B;
}

/* ----- columnDef ----- */

columnDef(A) ::= colId(B) typename(C) opt_column_storage(D) opt_column_compression(E) create_generic_options(F) colQualList(G). {
    ColumnDef *n = makeNode(ColumnDef);

    					n->colname = B;
    					n->typeName = C;
    					n->storage_name = D;
    					n->compression = E;
    					n->inhcount = 0;
    					n->is_local = true;
    					n->is_not_null = false;
    					n->is_from_type = false;
    					n->storage = 0;
    					n->raw_default = NULL;
    					n->cooked_default = NULL;
    					n->collOid = InvalidOid;
    					n->fdwoptions = F;
    					SplitColQualList(G, &n->constraints, &n->collClause,
    									 yyscanner);
    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- columnOptions ----- */

columnOptions(A) ::= colId(B) colQualList(C). {
    ColumnDef *n = makeNode(ColumnDef);

    					n->colname = B;
    					n->typeName = NULL;
    					n->inhcount = 0;
    					n->is_local = true;
    					n->is_not_null = false;
    					n->is_from_type = false;
    					n->storage = 0;
    					n->raw_default = NULL;
    					n->cooked_default = NULL;
    					n->collOid = InvalidOid;
    					SplitColQualList(C, &n->constraints, &n->collClause,
    									 yyscanner);
    					n->location = LOC(B);
    					A = (Node *) n;
}

columnOptions(A) ::= colId(B) WITH OPTIONS colQualList(E). {
    ColumnDef *n = makeNode(ColumnDef);

    					n->colname = B;
    					n->typeName = NULL;
    					n->inhcount = 0;
    					n->is_local = true;
    					n->is_not_null = false;
    					n->is_from_type = false;
    					n->storage = 0;
    					n->raw_default = NULL;
    					n->cooked_default = NULL;
    					n->collOid = InvalidOid;
    					SplitColQualList(E, &n->constraints, &n->collClause,
    									 yyscanner);
    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- column_compression ----- */

column_compression(A) ::= COMPRESSION colId(C). {
    A = C;
}

column_compression(A) ::= COMPRESSION DEFAULT. {
    A = pstrdup("default");
}

/* ----- opt_column_compression ----- */

opt_column_compression(A) ::= column_compression(B). {
    A = B;
}

opt_column_compression(A) ::= . {
    A = NULL;
}

/* ----- column_storage ----- */

column_storage(A) ::= STORAGE colId(C). {
    A = C;
}

column_storage(A) ::= STORAGE DEFAULT. {
    A = pstrdup("default");
}

/* ----- opt_column_storage ----- */

opt_column_storage(A) ::= column_storage(B). {
    A = B;
}

opt_column_storage(A) ::= . {
    A = NULL;
}

/* ----- colQualList ----- */

colQualList(A) ::= colQualList(B) colConstraint(C). {
    A = lappend(B, C);
}

colQualList(A) ::= . {
    A = NIL;
}

/* ----- colConstraint ----- */

colConstraint(A) ::= CONSTRAINT name(C) colConstraintElem(D). {
    Constraint *n = castNode(Constraint, D);

    					n->conname = C;
    					n->location = LOC(B);
    					A = (Node *) n;
}

colConstraint(A) ::= colConstraintElem(B). {
    A = B;
}

colConstraint(A) ::= constraintAttr(B). {
    A = B;
}

colConstraint(A) ::= COLLATE any_name(C). {
    CollateClause *n = makeNode(CollateClause);

    					n->arg = NULL;
    					n->collname = C;
    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- colConstraintElem ----- */

colConstraintElem(A) ::= NOT NULL_P opt_no_inherit(D). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_NOTNULL;
    					n->location = LOC(B);
    					n->is_no_inherit = D;
    					n->is_enforced = true;
    					n->skip_validation = false;
    					n->initially_valid = true;
    					A = (Node *) n;
}

colConstraintElem(A) ::= NULL_P. {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_NULL;
    					n->location = LOC(B);
    					A = (Node *) n;
}

colConstraintElem(A) ::= UNIQUE opt_unique_null_treatment(C) opt_definition(D) optConsTableSpace(E). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_UNIQUE;
    					n->location = LOC(B);
    					n->nulls_not_distinct = !C;
    					n->keys = NULL;
    					n->options = D;
    					n->indexname = NULL;
    					n->indexspace = E;
    					A = (Node *) n;
}

colConstraintElem(A) ::= PRIMARY KEY opt_definition(D) optConsTableSpace(E). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_PRIMARY;
    					n->location = LOC(B);
    					n->keys = NULL;
    					n->options = D;
    					n->indexname = NULL;
    					n->indexspace = E;
    					A = (Node *) n;
}

colConstraintElem(A) ::= CHECK LPAREN a_expr(D) RPAREN opt_no_inherit(F). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_CHECK;
    					n->location = LOC(B);
    					n->is_no_inherit = F;
    					n->raw_expr = D;
    					n->cooked_expr = NULL;
    					n->is_enforced = true;
    					n->skip_validation = false;
    					n->initially_valid = true;
    					A = (Node *) n;
}

colConstraintElem(A) ::= DEFAULT b_expr(C). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_DEFAULT;
    					n->location = LOC(B);
    					n->raw_expr = C;
    					n->cooked_expr = NULL;
    					A = (Node *) n;
}

colConstraintElem(A) ::= GENERATED generated_when(C) AS IDENTITY_P optParenthesizedSeqOptList(F). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_IDENTITY;
    					n->generated_when = C;
    					n->options = F;
    					n->location = LOC(B);
    					A = (Node *) n;
}

colConstraintElem(A) ::= GENERATED generated_when(C) AS LPAREN a_expr(F) RPAREN opt_virtual_or_stored(H). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_GENERATED;
    					n->generated_when = C;
    					n->raw_expr = F;
    					n->cooked_expr = NULL;
    					n->generated_kind = H;
    					n->location = LOC(B);







    					if (C != ATTRIBUTE_IDENTITY_ALWAYS)
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("for a generated column, GENERATED ALWAYS must be specified"),
    								 parser_errposition(LOC(C))));

    					A = (Node *) n;
}

colConstraintElem(A) ::= REFERENCES qualified_name(C) opt_column_list(D) key_match(E) key_actions(F). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_FOREIGN;
    					n->location = LOC(B);
    					n->pktable = C;
    					n->fk_attrs = NIL;
    					n->pk_attrs = D;
    					n->fk_matchtype = E;
    					n->fk_upd_action = (F)->updateAction->action;
    					n->fk_del_action = (F)->deleteAction->action;
    					n->fk_del_set_cols = (F)->deleteAction->cols;
    					n->is_enforced = true;
    					n->skip_validation = false;
    					n->initially_valid = true;
    					A = (Node *) n;
}

/* ----- opt_unique_null_treatment ----- */

opt_unique_null_treatment(A) ::= NULLS_P DISTINCT. {
    A = true;
}

opt_unique_null_treatment(A) ::= NULLS_P NOT DISTINCT. {
    A = false;
}

opt_unique_null_treatment(A) ::= . {
    A = true;
}

/* ----- generated_when ----- */

generated_when(A) ::= ALWAYS. {
    A = ATTRIBUTE_IDENTITY_ALWAYS;
}

generated_when(A) ::= BY DEFAULT. {
    A = ATTRIBUTE_IDENTITY_BY_DEFAULT;
}

/* ----- opt_virtual_or_stored ----- */

opt_virtual_or_stored(A) ::= STORED. {
    A = ATTRIBUTE_GENERATED_STORED;
}

opt_virtual_or_stored(A) ::= VIRTUAL. {
    A = ATTRIBUTE_GENERATED_VIRTUAL;
}

opt_virtual_or_stored(A) ::= . {
    A = ATTRIBUTE_GENERATED_VIRTUAL;
}

/* ----- constraintAttr ----- */

constraintAttr(A) ::= DEFERRABLE. {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_ATTR_DEFERRABLE;
    					n->location = LOC(B);
    					A = (Node *) n;
}

constraintAttr(A) ::= NOT DEFERRABLE. {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_ATTR_NOT_DEFERRABLE;
    					n->location = LOC(B);
    					A = (Node *) n;
}

constraintAttr(A) ::= INITIALLY DEFERRED. {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_ATTR_DEFERRED;
    					n->location = LOC(B);
    					A = (Node *) n;
}

constraintAttr(A) ::= INITIALLY IMMEDIATE. {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_ATTR_IMMEDIATE;
    					n->location = LOC(B);
    					A = (Node *) n;
}

constraintAttr(A) ::= ENFORCED. {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_ATTR_ENFORCED;
    					n->location = LOC(B);
    					A = (Node *) n;
}

constraintAttr(A) ::= NOT ENFORCED. {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_ATTR_NOT_ENFORCED;
    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- tableLikeClause ----- */

tableLikeClause(A) ::= LIKE qualified_name(C) tableLikeOptionList(D). {
    tableLikeClause *n = makeNode(tableLikeClause);

    					n->relation = C;
    					n->options = D;
    					n->relationOid = InvalidOid;
    					A = (Node *) n;
}

/* ----- tableLikeOptionList ----- */

tableLikeOptionList(A) ::= tableLikeOptionList(B) INCLUDING tableLikeOption(D). {
    A = B | D;
}

tableLikeOptionList(A) ::= tableLikeOptionList(B) EXCLUDING tableLikeOption(D). {
    A = B & ~D;
}

tableLikeOptionList(A) ::= . {
    A = 0;
}

/* ----- tableLikeOption ----- */

tableLikeOption(A) ::= COMMENTS. {
    A = CREATE_TABLE_LIKE_COMMENTS;
}

tableLikeOption(A) ::= COMPRESSION. {
    A = CREATE_TABLE_LIKE_COMPRESSION;
}

tableLikeOption(A) ::= CONSTRAINTS. {
    A = CREATE_TABLE_LIKE_CONSTRAINTS;
}

tableLikeOption(A) ::= DEFAULTS. {
    A = CREATE_TABLE_LIKE_DEFAULTS;
}

tableLikeOption(A) ::= IDENTITY_P. {
    A = CREATE_TABLE_LIKE_IDENTITY;
}

tableLikeOption(A) ::= GENERATED. {
    A = CREATE_TABLE_LIKE_GENERATED;
}

tableLikeOption(A) ::= INDEXES. {
    A = CREATE_TABLE_LIKE_INDEXES;
}

tableLikeOption(A) ::= STATISTICS. {
    A = CREATE_TABLE_LIKE_STATISTICS;
}

tableLikeOption(A) ::= STORAGE. {
    A = CREATE_TABLE_LIKE_STORAGE;
}

tableLikeOption(A) ::= ALL. {
    A = CREATE_TABLE_LIKE_ALL;
}

/* ----- tableConstraint ----- */

tableConstraint(A) ::= CONSTRAINT name(C) constraintElem(D). {
    Constraint *n = castNode(Constraint, D);

    					n->conname = C;
    					n->location = LOC(B);
    					A = (Node *) n;
}

tableConstraint(A) ::= constraintElem(B). {
    A = B;
}

/* ----- constraintElem ----- */

constraintElem(A) ::= CHECK LPAREN a_expr(D) RPAREN constraintAttributeSpec(F). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_CHECK;
    					n->location = LOC(B);
    					n->raw_expr = D;
    					n->cooked_expr = NULL;
    					processCASbits(F, LOC(F), "CHECK",
    								   NULL, NULL, &n->is_enforced, &n->skip_validation,
    								   &n->is_no_inherit, yyscanner);
    					n->initially_valid = !n->skip_validation;
    					A = (Node *) n;
}

constraintElem(A) ::= NOT NULL_P colId(D) constraintAttributeSpec(E). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_NOTNULL;
    					n->location = LOC(B);
    					n->keys = list_make1(makeString(D));
    					processCASbits(E, LOC(E), "NOT NULL",
    								   NULL, NULL, NULL, &n->skip_validation,
    								   &n->is_no_inherit, yyscanner);
    					n->initially_valid = !n->skip_validation;
    					A = (Node *) n;
}

constraintElem(A) ::= UNIQUE opt_unique_null_treatment(C) LPAREN columnList(E) opt_without_overlaps(F) RPAREN opt_c_include(H) opt_definition(I) optConsTableSpace(J) constraintAttributeSpec(K). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_UNIQUE;
    					n->location = LOC(B);
    					n->nulls_not_distinct = !C;
    					n->keys = E;
    					n->without_overlaps = F;
    					n->including = H;
    					n->options = I;
    					n->indexname = NULL;
    					n->indexspace = J;
    					processCASbits(K, LOC(K), "UNIQUE",
    								   &n->deferrable, &n->initdeferred, NULL,
    								   NULL, NULL, yyscanner);
    					A = (Node *) n;
}

constraintElem(A) ::= UNIQUE existingIndex(C) constraintAttributeSpec(D). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_UNIQUE;
    					n->location = LOC(B);
    					n->keys = NIL;
    					n->including = NIL;
    					n->options = NIL;
    					n->indexname = C;
    					n->indexspace = NULL;
    					processCASbits(D, LOC(D), "UNIQUE",
    								   &n->deferrable, &n->initdeferred, NULL,
    								   NULL, NULL, yyscanner);
    					A = (Node *) n;
}

constraintElem(A) ::= PRIMARY KEY LPAREN columnList(E) opt_without_overlaps(F) RPAREN opt_c_include(H) opt_definition(I) optConsTableSpace(J) constraintAttributeSpec(K). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_PRIMARY;
    					n->location = LOC(B);
    					n->keys = E;
    					n->without_overlaps = F;
    					n->including = H;
    					n->options = I;
    					n->indexname = NULL;
    					n->indexspace = J;
    					processCASbits(K, LOC(K), "PRIMARY KEY",
    								   &n->deferrable, &n->initdeferred, NULL,
    								   NULL, NULL, yyscanner);
    					A = (Node *) n;
}

constraintElem(A) ::= PRIMARY KEY existingIndex(D) constraintAttributeSpec(E). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_PRIMARY;
    					n->location = LOC(B);
    					n->keys = NIL;
    					n->including = NIL;
    					n->options = NIL;
    					n->indexname = D;
    					n->indexspace = NULL;
    					processCASbits(E, LOC(E), "PRIMARY KEY",
    								   &n->deferrable, &n->initdeferred, NULL,
    								   NULL, NULL, yyscanner);
    					A = (Node *) n;
}

constraintElem(A) ::= EXCLUDE access_method_clause(C) LPAREN exclusionConstraintList(E) RPAREN opt_c_include(G) opt_definition(H) optConsTableSpace(I) optWhereClause(J) constraintAttributeSpec(K). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_EXCLUSION;
    					n->location = LOC(B);
    					n->access_method = C;
    					n->exclusions = E;
    					n->including = G;
    					n->options = H;
    					n->indexname = NULL;
    					n->indexspace = I;
    					n->where_clause = J;
    					processCASbits(K, LOC(K), "EXCLUDE",
    								   &n->deferrable, &n->initdeferred, NULL,
    								   NULL, NULL, yyscanner);
    					A = (Node *) n;
}

constraintElem(A) ::= FOREIGN KEY LPAREN columnList(E) optionalPeriodName(F) RPAREN REFERENCES qualified_name(I) opt_column_and_period_list(J) key_match(K) key_actions(L) constraintAttributeSpec(M). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_FOREIGN;
    					n->location = LOC(B);
    					n->pktable = I;
    					n->fk_attrs = E;
    					if (F)
    					{
    						n->fk_attrs = lappend(n->fk_attrs, F);
    						n->fk_with_period = true;
    					}
    					n->pk_attrs = linitial(J);
    					if (lsecond(J))
    					{
    						n->pk_attrs = lappend(n->pk_attrs, lsecond(J));
    						n->pk_with_period = true;
    					}
    					n->fk_matchtype = K;
    					n->fk_upd_action = (L)->updateAction->action;
    					n->fk_del_action = (L)->deleteAction->action;
    					n->fk_del_set_cols = (L)->deleteAction->cols;
    					processCASbits(M, LOC(M), "FOREIGN KEY",
    								   &n->deferrable, &n->initdeferred,
    								   &n->is_enforced, &n->skip_validation, NULL,
    								   yyscanner);
    					n->initially_valid = !n->skip_validation;
    					A = (Node *) n;
}

/* ----- domainConstraint ----- */

domainConstraint(A) ::= CONSTRAINT name(C) domainConstraintElem(D). {
    Constraint *n = castNode(Constraint, D);

    					n->conname = C;
    					n->location = LOC(B);
    					A = (Node *) n;
}

domainConstraint(A) ::= domainConstraintElem(B). {
    A = B;
}

/* ----- domainConstraintElem ----- */

domainConstraintElem(A) ::= CHECK LPAREN a_expr(D) RPAREN constraintAttributeSpec(F). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_CHECK;
    					n->location = LOC(B);
    					n->raw_expr = D;
    					n->cooked_expr = NULL;
    					processCASbits(F, LOC(F), "CHECK",
    								   NULL, NULL, NULL, &n->skip_validation,
    								   &n->is_no_inherit, yyscanner);
    					n->is_enforced = true;
    					n->initially_valid = !n->skip_validation;
    					A = (Node *) n;
}

domainConstraintElem(A) ::= NOT NULL_P constraintAttributeSpec(D). {
    Constraint *n = makeNode(Constraint);

    					n->contype = CONSTR_NOTNULL;
    					n->location = LOC(B);
    					n->keys = list_make1(makeString("value"));

    					processCASbits(D, LOC(D), "NOT NULL",
    								   NULL, NULL, NULL,
    								   NULL, NULL, yyscanner);
    					n->initially_valid = true;
    					A = (Node *) n;
}

/* ----- opt_no_inherit ----- */

opt_no_inherit(A) ::= NO INHERIT. {
    A = true;
}

opt_no_inherit(A) ::= . {
    A = false;
}

/* ----- opt_without_overlaps ----- */

opt_without_overlaps(A) ::= WITHOUT OVERLAPS. {
    A = true;
}

opt_without_overlaps(A) ::= . {
    A = false;
}

/* ----- opt_column_list ----- */

opt_column_list(A) ::= LPAREN columnList(C) RPAREN. {
    A = C;
}

opt_column_list(A) ::= . {
    A = NIL;
}

/* ----- columnList ----- */

columnList(A) ::= columnElem(B). {
    A = list_make1(B);
}

columnList(A) ::= columnList(B) COMMA columnElem(D). {
    A = lappend(B, D);
}

/* ----- optionalPeriodName ----- */

optionalPeriodName(A) ::= COMMA PERIOD columnElem(D). {
    A = D;
}

optionalPeriodName(A) ::= . {
    A = NULL;
}

/* ----- opt_column_and_period_list ----- */

opt_column_and_period_list(A) ::= LPAREN columnList(C) optionalPeriodName(D) RPAREN. {
    A = list_make2(C, D);
}

opt_column_and_period_list(A) ::= . {
    A = list_make2(NIL, NULL);
}

/* ----- columnElem ----- */

columnElem(A) ::= colId(B). {
    A = (Node *) makeString(B);
}

/* ----- opt_c_include ----- */

opt_c_include(A) ::= INCLUDE LPAREN columnList(D) RPAREN. {
    A = D;
}

opt_c_include(A) ::= . {
    A = NIL;
}

/* ----- key_match ----- */

key_match(A) ::= MATCH FULL. {
    A = FKCONSTR_MATCH_FULL;
}

key_match(A) ::= MATCH PARTIAL. {
    ereport(ERROR,
    						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    						 errmsg("MATCH PARTIAL not yet implemented"),
    						 parser_errposition(LOC(B))));
    				A = FKCONSTR_MATCH_PARTIAL;
}

key_match(A) ::= MATCH SIMPLE. {
    A = FKCONSTR_MATCH_SIMPLE;
}

key_match(A) ::= . {
    A = FKCONSTR_MATCH_SIMPLE;
}

/* ----- exclusionConstraintList ----- */

exclusionConstraintList(A) ::= exclusionConstraintElem(B). {
    A = list_make1(B);
}

exclusionConstraintList(A) ::= exclusionConstraintList(B) COMMA exclusionConstraintElem(D). {
    A = lappend(B, D);
}

/* ----- exclusionConstraintElem ----- */

exclusionConstraintElem(A) ::= index_elem(B) WITH any_operator(D). {
    A = list_make2(B, D);
}

exclusionConstraintElem(A) ::= index_elem(B) WITH OPERATOR LPAREN any_operator(F) RPAREN. {
    A = list_make2(B, F);
}

/* ----- optWhereClause ----- */

optWhereClause(A) ::= WHERE LPAREN a_expr(D) RPAREN. {
    A = D;
}

optWhereClause(A) ::= . {
    A = NULL;
}

/* ----- key_actions ----- */

key_actions(A) ::= key_update(B). {
    KeyActions *n = palloc_object(KeyActions);

    					n->updateAction = B;
    					n->deleteAction = palloc_object(KeyAction);
    					n->deleteAction->action = FKCONSTR_ACTION_NOACTION;
    					n->deleteAction->cols = NIL;
    					A = n;
}

key_actions(A) ::= key_delete(B). {
    KeyActions *n = palloc_object(KeyActions);

    					n->updateAction = palloc_object(KeyAction);
    					n->updateAction->action = FKCONSTR_ACTION_NOACTION;
    					n->updateAction->cols = NIL;
    					n->deleteAction = B;
    					A = n;
}

key_actions(A) ::= key_update(B) key_delete(C). {
    KeyActions *n = palloc_object(KeyActions);

    					n->updateAction = B;
    					n->deleteAction = C;
    					A = n;
}

key_actions(A) ::= key_delete(B) key_update(C). {
    KeyActions *n = palloc_object(KeyActions);

    					n->updateAction = C;
    					n->deleteAction = B;
    					A = n;
}

key_actions(A) ::= . {
    KeyActions *n = palloc_object(KeyActions);

    					n->updateAction = palloc_object(KeyAction);
    					n->updateAction->action = FKCONSTR_ACTION_NOACTION;
    					n->updateAction->cols = NIL;
    					n->deleteAction = palloc_object(KeyAction);
    					n->deleteAction->action = FKCONSTR_ACTION_NOACTION;
    					n->deleteAction->cols = NIL;
    					A = n;
}

/* ----- key_update ----- */

key_update(A) ::= ON UPDATE key_action(D). {
    if ((D)->cols)
    						ereport(ERROR,
    								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								 errmsg("a column list with %s is only supported for ON DELETE actions",
    										(D)->action == FKCONSTR_ACTION_SETNULL ? "SET NULL" : "SET DEFAULT"),
    								 parser_errposition(LOC(B))));
    					A = D;
}

/* ----- key_delete ----- */

key_delete(A) ::= ON DELETE_P key_action(D). {
    A = D;
}

/* ----- key_action ----- */

key_action(A) ::= NO ACTION. {
    KeyAction *n = palloc_object(KeyAction);

    					n->action = FKCONSTR_ACTION_NOACTION;
    					n->cols = NIL;
    					A = n;
}

key_action(A) ::= RESTRICT. {
    KeyAction *n = palloc_object(KeyAction);

    					n->action = FKCONSTR_ACTION_RESTRICT;
    					n->cols = NIL;
    					A = n;
}

key_action(A) ::= CASCADE. {
    KeyAction *n = palloc_object(KeyAction);

    					n->action = FKCONSTR_ACTION_CASCADE;
    					n->cols = NIL;
    					A = n;
}

key_action(A) ::= SET NULL_P opt_column_list(D). {
    KeyAction *n = palloc_object(KeyAction);

    					n->action = FKCONSTR_ACTION_SETNULL;
    					n->cols = D;
    					A = n;
}

key_action(A) ::= SET DEFAULT opt_column_list(D). {
    KeyAction *n = palloc_object(KeyAction);

    					n->action = FKCONSTR_ACTION_SETDEFAULT;
    					n->cols = D;
    					A = n;
}

/* ----- optInherit ----- */

optInherit(A) ::= INHERITS LPAREN qualified_name_list(D) RPAREN. {
    A = D;
}

optInherit(A) ::= . {
    A = NIL;
}

/* ----- optPartitionSpec ----- */

optPartitionSpec(A) ::= partitionSpec(B). {
    A = B;
}

optPartitionSpec(A) ::= . {
    A = NULL;
}

/* ----- partitionSpec ----- */

partitionSpec(A) ::= PARTITION BY colId(D) LPAREN part_params(F) RPAREN. {
    partitionSpec *n = makeNode(partitionSpec);

    					n->strategy = parsePartitionStrategy(D, LOC(D), yyscanner);
    					n->partParams = F;
    					n->location = LOC(B);

    					A = n;
}

/* ----- part_params ----- */

part_params(A) ::= part_elem(B). {
    A = list_make1(B);
}

part_params(A) ::= part_params(B) COMMA part_elem(D). {
    A = lappend(B, D);
}

/* ----- part_elem ----- */

part_elem(A) ::= colId(B) opt_collate(C) opt_qualified_name(D). {
    PartitionElem *n = makeNode(PartitionElem);

    					n->name = B;
    					n->expr = NULL;
    					n->collation = C;
    					n->opclass = D;
    					n->location = LOC(B);
    					A = n;
}

part_elem(A) ::= func_expr_windowless(B) opt_collate(C) opt_qualified_name(D). {
    PartitionElem *n = makeNode(PartitionElem);

    					n->name = NULL;
    					n->expr = B;
    					n->collation = C;
    					n->opclass = D;
    					n->location = LOC(B);
    					A = n;
}

part_elem(A) ::= LPAREN a_expr(C) RPAREN opt_collate(E) opt_qualified_name(F). {
    PartitionElem *n = makeNode(PartitionElem);

    					n->name = NULL;
    					n->expr = C;
    					n->collation = E;
    					n->opclass = F;
    					n->location = LOC(B);
    					A = n;
}

/* ----- table_access_method_clause ----- */

table_access_method_clause(A) ::= USING name(C). {
    A = C;
}

table_access_method_clause(A) ::= . {
    A = NULL;
}

/* ----- optWith ----- */

optWith(A) ::= WITH reloptions(C). {
    A = C;
}

optWith(A) ::= WITHOUT OIDS. {
    A = NIL;
}

optWith(A) ::= . {
    A = NIL;
}

/* ----- onCommitOption ----- */

onCommitOption(A) ::= ON COMMIT DROP. {
    A = ONCOMMIT_DROP;
}

onCommitOption(A) ::= ON COMMIT DELETE_P ROWS. {
    A = ONCOMMIT_DELETE_ROWS;
}

onCommitOption(A) ::= ON COMMIT PRESERVE ROWS. {
    A = ONCOMMIT_PRESERVE_ROWS;
}

onCommitOption(A) ::= . {
    A = ONCOMMIT_NOOP;
}

/* ----- optTableSpace ----- */

optTableSpace(A) ::= TABLESPACE name(C). {
    A = C;
}

optTableSpace(A) ::= . {
    A = NULL;
}

/* ----- optConsTableSpace ----- */

optConsTableSpace(A) ::= USING INDEX TABLESPACE name(E). {
    A = E;
}

optConsTableSpace(A) ::= . {
    A = NULL;
}

/* ----- existingIndex ----- */

existingIndex(A) ::= USING INDEX name(D). {
    A = D;
}

/* ----- createStatsStmt ----- */

opt_with_data(A) ::= WITH DATA_P. {
    A = true;
}

opt_with_data(A) ::= WITH NO DATA_P. {
    A = false;
}

opt_with_data(A) ::= . {
    A = true;
}

/* ----- createMatViewStmt ----- */

optNoLog(A) ::= UNLOGGED. {
    A = RELPERSISTENCE_UNLOGGED;
}

optNoLog(A) ::= . {
    A = RELPERSISTENCE_PERMANENT;
}

/* ----- refreshMatViewStmt ----- */

createForeignTableStmt(A) ::= CREATE FOREIGN TABLE qualified_name(E) LPAREN optTableElementList(G) RPAREN optInherit(I) SERVER name(K) create_generic_options(L). {
    createForeignTableStmt *n = makeNode(createForeignTableStmt);

    					E->relpersistence = RELPERSISTENCE_PERMANENT;
    					n->base.relation = E;
    					n->base.tableElts = G;
    					n->base.inhRelations = I;
    					n->base.ofTypename = NULL;
    					n->base.constraints = NIL;
    					n->base.options = NIL;
    					n->base.oncommit = ONCOMMIT_NOOP;
    					n->base.tablespacename = NULL;
    					n->base.if_not_exists = false;

    					n->servername = K;
    					n->options = L;
    					A = (Node *) n;
}

createForeignTableStmt(A) ::= CREATE FOREIGN TABLE IF_P NOT EXISTS qualified_name(H) LPAREN optTableElementList(J) RPAREN optInherit(L) SERVER name(N) create_generic_options(O). {
    createForeignTableStmt *n = makeNode(createForeignTableStmt);

    					H->relpersistence = RELPERSISTENCE_PERMANENT;
    					n->base.relation = H;
    					n->base.tableElts = J;
    					n->base.inhRelations = L;
    					n->base.ofTypename = NULL;
    					n->base.constraints = NIL;
    					n->base.options = NIL;
    					n->base.oncommit = ONCOMMIT_NOOP;
    					n->base.tablespacename = NULL;
    					n->base.if_not_exists = true;

    					n->servername = N;
    					n->options = O;
    					A = (Node *) n;
}

createForeignTableStmt(A) ::= CREATE FOREIGN TABLE qualified_name(E) PARTITION OF qualified_name(H) optTypedTableElementList(I) partitionBoundSpec(J) SERVER name(L) create_generic_options(M). {
    createForeignTableStmt *n = makeNode(createForeignTableStmt);

    					E->relpersistence = RELPERSISTENCE_PERMANENT;
    					n->base.relation = E;
    					n->base.inhRelations = list_make1(H);
    					n->base.tableElts = I;
    					n->base.partbound = J;
    					n->base.ofTypename = NULL;
    					n->base.constraints = NIL;
    					n->base.options = NIL;
    					n->base.oncommit = ONCOMMIT_NOOP;
    					n->base.tablespacename = NULL;
    					n->base.if_not_exists = false;

    					n->servername = L;
    					n->options = M;
    					A = (Node *) n;
}

createForeignTableStmt(A) ::= CREATE FOREIGN TABLE IF_P NOT EXISTS qualified_name(H) PARTITION OF qualified_name(K) optTypedTableElementList(L) partitionBoundSpec(M) SERVER name(O) create_generic_options(P). {
    createForeignTableStmt *n = makeNode(createForeignTableStmt);

    					H->relpersistence = RELPERSISTENCE_PERMANENT;
    					n->base.relation = H;
    					n->base.inhRelations = list_make1(K);
    					n->base.tableElts = L;
    					n->base.partbound = M;
    					n->base.ofTypename = NULL;
    					n->base.constraints = NIL;
    					n->base.options = NIL;
    					n->base.oncommit = ONCOMMIT_NOOP;
    					n->base.tablespacename = NULL;
    					n->base.if_not_exists = true;

    					n->servername = O;
    					n->options = P;
    					A = (Node *) n;
}

/* ----- importForeignSchemaStmt ----- */
```

