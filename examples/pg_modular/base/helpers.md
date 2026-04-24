# Base Helper Rules


Shared non-terminals used across multiple modules:
qualified names, column lists, WHERE clauses, and common
option patterns.


```yaml
name: pg-base-helpers
version: 17.0.0
description: Shared helper rules (names, qualifiers, common options)
provides: [pg-base-helpers]
depends: [pg-type-decls]
```

## Production Rules

```lime rules
opt_single_name(A) ::= colId(B). {
    A = B;
}

opt_single_name(A) ::= . {
    A = NULL;
}

/* ----- opt_qualified_name ----- */

opt_qualified_name(A) ::= any_name(B). {
    A = B;
}

opt_qualified_name(A) ::= . {
    A = NIL;
}

/* ----- opt_concurrently ----- */

opt_concurrently(A) ::= CONCURRENTLY. {
    A = true;
}

opt_concurrently(A) ::= . {
    A = false;
}

/* ----- opt_usingindex ----- */

opt_usingindex(A) ::= USING INDEX. {
    A = true;
}

opt_usingindex(A) ::= . {
    A = false;
}

/* ----- opt_drop_behavior ----- */

opt_utility_option_list(A) ::= LPAREN utility_option_list(C) RPAREN. {
    A = C;
}

opt_utility_option_list(A) ::= . {
    A = NULL;
}

/* ----- utility_option_list ----- */

utility_option_list(A) ::= utility_option_elem(B). {
    A = list_make1(B);
}

utility_option_list(A) ::= utility_option_list(B) COMMA utility_option_elem(D). {
    A = lappend(B, D);
}

/* ----- utility_option_elem ----- */

utility_option_elem(A) ::= utility_option_name(B) utility_option_arg(C). {
    A = makeDefElem(B, C, LOC(B));
}

/* ----- utility_option_name ----- */

utility_option_name(A) ::= nonReservedWord(B). {
    A = B;
}

utility_option_name(A) ::= analyze_keyword(B). {
    A = "analyze";
}

utility_option_name(A) ::= FORMAT_LA. {
    A = "format";
}

/* ----- utility_option_arg ----- */

utility_option_arg(A) ::= opt_boolean_or_string(B). {
    A = (Node *) makeString(B);
}

utility_option_arg(A) ::= numericOnly(B). {
    A = (Node *) B;
}

utility_option_arg(A) ::= . {
    A = NULL;
}

/* ----- callStmt ----- */

createStatsStmt(A) ::= CREATE STATISTICS opt_qualified_name(D) opt_name_list(E) ON stats_params(G) FROM from_list(I). {
    createStatsStmt *n = makeNode(createStatsStmt);

    					n->defnames = D;
    					n->stat_types = E;
    					n->exprs = G;
    					n->relations = I;
    					n->stxcomment = NULL;
    					n->if_not_exists = false;
    					A = (Node *) n;
}

createStatsStmt(A) ::= CREATE STATISTICS IF_P NOT EXISTS any_name(G) opt_name_list(H) ON stats_params(J) FROM from_list(L). {
    createStatsStmt *n = makeNode(createStatsStmt);

    					n->defnames = G;
    					n->stat_types = H;
    					n->exprs = J;
    					n->relations = L;
    					n->stxcomment = NULL;
    					n->if_not_exists = true;
    					A = (Node *) n;
}

/* ----- stats_params ----- */

stats_params(A) ::= stats_param(B). {
    A = list_make1(B);
}

stats_params(A) ::= stats_params(B) COMMA stats_param(D). {
    A = lappend(B, D);
}

/* ----- stats_param ----- */

stats_param(A) ::= colId(B). {
    A = makeNode(StatsElem);
    					A->name = B;
    					A->expr = NULL;
}

stats_param(A) ::= func_expr_windowless(B). {
    A = makeNode(StatsElem);
    					A->name = NULL;
    					A->expr = B;
}

stats_param(A) ::= LPAREN a_expr(C) RPAREN. {
    A = makeNode(StatsElem);
    					A->name = NULL;
    					A->expr = C;
}

/* ----- alterStatsStmt ----- */

createAsStmt(A) ::= CREATE optTemp(C) TABLE create_as_target(E) AS selectStmt(G) opt_with_data(H). {
    CreateTableAsStmt *ctas = makeNode(CreateTableAsStmt);

    					ctas->query = G;
    					ctas->into = E;
    					ctas->objtype = OBJECT_TABLE;
    					ctas->is_select_into = false;
    					ctas->if_not_exists = false;

    					E->rel->relpersistence = C;
    					E->skipData = !(H);
    					A = (Node *) ctas;
}

createAsStmt(A) ::= CREATE optTemp(C) TABLE IF_P NOT EXISTS create_as_target(H) AS selectStmt(J) opt_with_data(K). {
    CreateTableAsStmt *ctas = makeNode(CreateTableAsStmt);

    					ctas->query = J;
    					ctas->into = H;
    					ctas->objtype = OBJECT_TABLE;
    					ctas->is_select_into = false;
    					ctas->if_not_exists = true;

    					H->rel->relpersistence = C;
    					H->skipData = !(K);
    					A = (Node *) ctas;
}

/* ----- create_as_target ----- */

createMatViewStmt(A) ::= CREATE optNoLog(C) MATERIALIZED VIEW create_mv_target(F) AS selectStmt(H) opt_with_data(I). {
    CreateTableAsStmt *ctas = makeNode(CreateTableAsStmt);

    					ctas->query = H;
    					ctas->into = F;
    					ctas->objtype = OBJECT_MATVIEW;
    					ctas->is_select_into = false;
    					ctas->if_not_exists = false;

    					F->rel->relpersistence = C;
    					F->skipData = !(I);
    					A = (Node *) ctas;
}

createMatViewStmt(A) ::= CREATE optNoLog(C) MATERIALIZED VIEW IF_P NOT EXISTS create_mv_target(I) AS selectStmt(K) opt_with_data(L). {
    CreateTableAsStmt *ctas = makeNode(CreateTableAsStmt);

    					ctas->query = K;
    					ctas->into = I;
    					ctas->objtype = OBJECT_MATVIEW;
    					ctas->is_select_into = false;
    					ctas->if_not_exists = true;

    					I->rel->relpersistence = C;
    					I->skipData = !(L);
    					A = (Node *) ctas;
}

/* ----- create_mv_target ----- */

refreshMatViewStmt(A) ::= REFRESH MATERIALIZED VIEW opt_concurrently(E) qualified_name(F) opt_with_data(G). {
    refreshMatViewStmt *n = makeNode(refreshMatViewStmt);

    					n->concurrent = E;
    					n->relation = F;
    					n->skipData = !(G);
    					A = (Node *) n;
}

/* ----- createSeqStmt ----- */

optConstrFromTable(A) ::= FROM qualified_name(C). {
    A = C;
}

optConstrFromTable(A) ::= . {
    A = NULL;
}

/* ----- constraintAttributeSpec ----- */

constraintAttributeSpec(A) ::= . {
    A = 0;
}

constraintAttributeSpec(A) ::= constraintAttributeSpec(B) constraintAttributeElem(C). {
    int		newspec = B | C;


    					if ((newspec & (CAS_NOT_DEFERRABLE | CAS_INITIALLY_DEFERRED)) == (CAS_NOT_DEFERRABLE | CAS_INITIALLY_DEFERRED))
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("constraint declared INITIALLY DEFERRED must be DEFERRABLE"),
    								 parser_errposition(LOC(C))));

    					if ((newspec & (CAS_NOT_DEFERRABLE | CAS_DEFERRABLE)) == (CAS_NOT_DEFERRABLE | CAS_DEFERRABLE) ||
    						(newspec & (CAS_INITIALLY_IMMEDIATE | CAS_INITIALLY_DEFERRED)) == (CAS_INITIALLY_IMMEDIATE | CAS_INITIALLY_DEFERRED) ||
    						(newspec & (CAS_NOT_ENFORCED | CAS_ENFORCED)) == (CAS_NOT_ENFORCED | CAS_ENFORCED))
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("conflicting constraint properties"),
    								 parser_errposition(LOC(C))));
    					A = newspec;
}

/* ----- constraintAttributeElem ----- */

constraintAttributeElem(A) ::= NOT DEFERRABLE. {
    A = CAS_NOT_DEFERRABLE;
}

constraintAttributeElem(A) ::= DEFERRABLE. {
    A = CAS_DEFERRABLE;
}

constraintAttributeElem(A) ::= INITIALLY IMMEDIATE. {
    A = CAS_INITIALLY_IMMEDIATE;
}

constraintAttributeElem(A) ::= INITIALLY DEFERRED. {
    A = CAS_INITIALLY_DEFERRED;
}

constraintAttributeElem(A) ::= NOT VALID. {
    A = CAS_NOT_VALID;
}

constraintAttributeElem(A) ::= NO INHERIT. {
    A = CAS_NO_INHERIT;
}

constraintAttributeElem(A) ::= NOT ENFORCED. {
    A = CAS_NOT_ENFORCED;
}

constraintAttributeElem(A) ::= ENFORCED. {
    A = CAS_ENFORCED;
}

/* ----- createEventTrigStmt ----- */

opt_enum_val_list(A) ::= enum_val_list(B). {
    A = B;
}

opt_enum_val_list(A) ::= . {
    A = NIL;
}

/* ----- enum_val_list ----- */

enum_val_list(A) ::= sconst(B). {
    A = list_make1(makeString(B));
}

enum_val_list(A) ::= enum_val_list(B) COMMA sconst(D). {
    A = lappend(B, makeString(D));
}

/* ----- alterEnumStmt ----- */

parameter_name_list(A) ::= parameter_name(B). {
    A = list_make1(makeString(B));
}

parameter_name_list(A) ::= parameter_name_list(B) COMMA parameter_name(D). {
    A = lappend(B, makeString(D));
}

/* ----- parameter_name ----- */

parameter_name(A) ::= colId(B). {
    A = B;
}

parameter_name(A) ::= parameter_name(B) DOT colId(D). {
    A = psprintf("%s.%s", B, D);
}

/* ----- privilege_target ----- */

opt_no(A) ::= NO. {
    A = true;
}

opt_no(A) ::= . {
    A = false;
}

/* ----- alterObjectSchemaStmt ----- */

event(A) ::= SELECT. {
    A = CMD_SELECT;
}

event(A) ::= UPDATE. {
    A = CMD_UPDATE;
}

event(A) ::= DELETE_P. {
    A = CMD_DELETE;
}

event(A) ::= INSERT. {
    A = CMD_INSERT;
}

/* ----- opt_instead ----- */

opt_instead(A) ::= INSTEAD. {
    A = true;
}

opt_instead(A) ::= ALSO. {
    A = false;
}

opt_instead(A) ::= . {
    A = false;
}

/* ----- notifyStmt ----- */

any_with(A) ::= WITH.

any_with(A) ::= WITH_LA.

/* ----- createConversionStmt ----- */

where_clause(A) ::= WHERE a_expr(C). {
    A = C;
}

where_clause(A) ::= . {
    A = NULL;
}

/* ----- where_or_current_clause ----- */

where_or_current_clause(A) ::= WHERE a_expr(C). {
    A = C;
}

where_or_current_clause(A) ::= WHERE CURRENT_P OF cursor_name(E). {
    CurrentOfExpr *n = makeNode(CurrentOfExpr);


    					n->cursor_name = E;
    					n->cursor_param = 0;
    					A = (Node *) n;
}

where_or_current_clause(A) ::= . {
    A = NULL;
}

/* ----- optTableFuncElementList ----- */

optTableFuncElementList(A) ::= tableFuncElementList(B). {
    A = B;
}

optTableFuncElementList(A) ::= . {
    A = NIL;
}

/* ----- tableFuncElementList ----- */

tableFuncElementList(A) ::= tableFuncElement(B). {
    A = list_make1(B);
}

tableFuncElementList(A) ::= tableFuncElementList(B) COMMA tableFuncElement(D). {
    A = lappend(B, D);
}

/* ----- tableFuncElement ----- */

tableFuncElement(A) ::= colId(B) typename(C) opt_collate_clause(D). {
    ColumnDef *n = makeNode(ColumnDef);

    					n->colname = B;
    					n->typeName = C;
    					n->inhcount = 0;
    					n->is_local = true;
    					n->is_not_null = false;
    					n->is_from_type = false;
    					n->storage = 0;
    					n->raw_default = NULL;
    					n->cooked_default = NULL;
    					n->collClause = (CollateClause *) D;
    					n->collOid = InvalidOid;
    					n->constraints = NIL;
    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- xmltable ----- */

qualified_name_list(A) ::= qualified_name(B). {
    A = list_make1(B);
}

qualified_name_list(A) ::= qualified_name_list(B) COMMA qualified_name(D). {
    A = lappend(B, D);
}

/* ----- qualified_name ----- */

qualified_name(A) ::= colId(B). {
    A = makeRangeVar(NULL, B, LOC(B));
}

qualified_name(A) ::= colId(B) indirection(C). {
    A = makeRangeVarFromQualifiedName(B, C, LOC(B), yyscanner);
}

/* ----- name_list ----- */

name_list(A) ::= name(B). {
    A = list_make1(makeString(B));
}

name_list(A) ::= name_list(B) COMMA name(D). {
    A = lappend(B, makeString(D));
}

/* ----- name ----- */

name(A) ::= colId(B). {
    A = B;
}

/* ----- attr_name ----- */

attr_name(A) ::= colLabel(B). {
    A = B;
}

/* ----- file_name ----- */

file_name(A) ::= sconst(B). {
    A = B;
}

/* ----- func_name ----- */
```

