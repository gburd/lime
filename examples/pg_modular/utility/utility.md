# Utility Statements


Miscellaneous utility statements: COPY, EXPLAIN, VACUUM,
ANALYZE, PREPARE, EXECUTE, DEALLOCATE, FETCH, DECLARE
CURSOR, DISCARD, LOAD, LISTEN, UNLISTEN, NOTIFY, WAIT,
and CLOSE.


```yaml
name: pg-utility
version: 17.0.0
description: COPY, EXPLAIN, VACUUM, ANALYZE, PREPARE, EXECUTE, FETCH, LISTEN, etc.
provides: [pg-utility]
depends: [pg-type-decls, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
discardStmt(A) ::= DISCARD ALL. {
    discardStmt *n = makeNode(discardStmt);

    					n->target = DISCARD_ALL;
    					A = (Node *) n;
}

discardStmt(A) ::= DISCARD TEMP. {
    discardStmt *n = makeNode(discardStmt);

    					n->target = DISCARD_TEMP;
    					A = (Node *) n;
}

discardStmt(A) ::= DISCARD TEMPORARY. {
    discardStmt *n = makeNode(discardStmt);

    					n->target = DISCARD_TEMP;
    					A = (Node *) n;
}

discardStmt(A) ::= DISCARD PLANS. {
    discardStmt *n = makeNode(discardStmt);

    					n->target = DISCARD_PLANS;
    					A = (Node *) n;
}

discardStmt(A) ::= DISCARD SEQUENCES. {
    discardStmt *n = makeNode(discardStmt);

    					n->target = DISCARD_SEQUENCES;
    					A = (Node *) n;
}

/* ----- alterTableStmt ----- */

reloptions(A) ::= LPAREN reloption_list(C) RPAREN. {
    A = C;
}

/* ----- opt_reloptions ----- */

opt_reloptions(A) ::= WITH reloptions(C). {
    A = C;
}

opt_reloptions(A) ::= . {
    A = NIL;
}

/* ----- reloption_list ----- */

reloption_list(A) ::= reloption_elem(B). {
    A = list_make1(B);
}

reloption_list(A) ::= reloption_list(B) COMMA reloption_elem(D). {
    A = lappend(B, D);
}

/* ----- reloption_elem ----- */

reloption_elem(A) ::= colLabel(B) EQ def_arg(D). {
    A = makeDefElem(B, (Node *) D, LOC(B));
}

reloption_elem(A) ::= colLabel(B). {
    A = makeDefElem(B, NULL, LOC(B));
}

reloption_elem(A) ::= colLabel(B) DOT colLabel(D) EQ def_arg(F). {
    A = makeDefElemExtended(B, D, (Node *) F,
    											 DEFELEM_UNSPEC, LOC(B));
}

reloption_elem(A) ::= colLabel(B) DOT colLabel(D). {
    A = makeDefElemExtended(B, D, NULL, DEFELEM_UNSPEC, LOC(B));
}

/* ----- alter_identity_column_option_list ----- */

closePortalStmt(A) ::= CLOSE cursor_name(C). {
    closePortalStmt *n = makeNode(closePortalStmt);

    					n->portalname = C;
    					A = (Node *) n;
}

closePortalStmt(A) ::= CLOSE ALL. {
    closePortalStmt *n = makeNode(closePortalStmt);

    					n->portalname = NULL;
    					A = (Node *) n;
}

/* ----- copyStmt ----- */

copyStmt(A) ::= COPY opt_binary(C) qualified_name(D) opt_column_list(E) copy_from(F) opt_program(G) copy_file_name(H) copy_delimiter(I) opt_with(J) copy_options(K) where_clause(L). {
    copyStmt *n = makeNode(copyStmt);

    					n->relation = D;
    					n->query = NULL;
    					n->attlist = E;
    					n->is_from = F;
    					n->is_program = G;
    					n->filename = H;
    					n->whereClause = L;

    					if (n->is_program && n->filename == NULL)
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("STDIN/STDOUT not allowed with PROGRAM"),
    								 parser_errposition(LOC(I))));

    					if (!n->is_from && n->whereClause != NULL)
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("WHERE clause not allowed with COPY TO"),
    								 errhint("Try the COPY (SELECT ... WHERE ...) TO variant."),
    								 parser_errposition(LOC(L))));

    					n->options = NIL;

    					if (C)
    						n->options = lappend(n->options, C);
    					if (I)
    						n->options = lappend(n->options, I);
    					if (K)
    						n->options = list_concat(n->options, K);
    					A = (Node *) n;
}

copyStmt(A) ::= COPY LPAREN preparableStmt(D) RPAREN TO opt_program(G) copy_file_name(H) opt_with(I) copy_options(J). {
    copyStmt *n = makeNode(copyStmt);

    					n->relation = NULL;
    					n->query = D;
    					n->attlist = NIL;
    					n->is_from = false;
    					n->is_program = G;
    					n->filename = H;
    					n->options = J;

    					if (n->is_program && n->filename == NULL)
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("STDIN/STDOUT not allowed with PROGRAM"),
    								 parser_errposition(LOC(F))));

    					A = (Node *) n;
}

/* ----- copy_from ----- */

copy_from(A) ::= FROM. {
    A = true;
}

copy_from(A) ::= TO. {
    A = false;
}

/* ----- opt_program ----- */

opt_program(A) ::= PROGRAM. {
    A = true;
}

opt_program(A) ::= . {
    A = false;
}

/* ----- copy_file_name ----- */

copy_file_name(A) ::= sconst(B). {
    A = B;
}

copy_file_name(A) ::= STDIN. {
    A = NULL;
}

copy_file_name(A) ::= STDOUT. {
    A = NULL;
}

/* ----- copy_options ----- */

copy_options(A) ::= copy_opt_list(B). {
    A = B;
}

copy_options(A) ::= LPAREN copy_generic_opt_list(C) RPAREN. {
    A = C;
}

/* ----- copy_opt_list ----- */

copy_opt_list(A) ::= copy_opt_list(B) copy_opt_item(C). {
    A = lappend(B, C);
}

copy_opt_list(A) ::= . {
    A = NIL;
}

/* ----- copy_opt_item ----- */

copy_opt_item(A) ::= BINARY. {
    A = makeDefElem("format", (Node *) makeString("binary"), LOC(B));
}

copy_opt_item(A) ::= FREEZE. {
    A = makeDefElem("freeze", (Node *) makeBoolean(true), LOC(B));
}

copy_opt_item(A) ::= DELIMITER opt_as(C) sconst(D). {
    A = makeDefElem("delimiter", (Node *) makeString(D), LOC(B));
}

copy_opt_item(A) ::= NULL_P opt_as(C) sconst(D). {
    A = makeDefElem("null", (Node *) makeString(D), LOC(B));
}

copy_opt_item(A) ::= CSV. {
    A = makeDefElem("format", (Node *) makeString("csv"), LOC(B));
}

copy_opt_item(A) ::= JSON. {
    A = makeDefElem("format", (Node *) makeString("json"), LOC(B));
}

copy_opt_item(A) ::= HEADER_P. {
    A = makeDefElem("header", (Node *) makeBoolean(true), LOC(B));
}

copy_opt_item(A) ::= QUOTE opt_as(C) sconst(D). {
    A = makeDefElem("quote", (Node *) makeString(D), LOC(B));
}

copy_opt_item(A) ::= ESCAPE opt_as(C) sconst(D). {
    A = makeDefElem("escape", (Node *) makeString(D), LOC(B));
}

copy_opt_item(A) ::= FORCE QUOTE columnList(D). {
    A = makeDefElem("force_quote", (Node *) D, LOC(B));
}

copy_opt_item(A) ::= FORCE QUOTE STAR. {
    A = makeDefElem("force_quote", (Node *) makeNode(A_Star), LOC(B));
}

copy_opt_item(A) ::= FORCE NOT NULL_P columnList(E). {
    A = makeDefElem("force_not_null", (Node *) E, LOC(B));
}

copy_opt_item(A) ::= FORCE NOT NULL_P STAR. {
    A = makeDefElem("force_not_null", (Node *) makeNode(A_Star), LOC(B));
}

copy_opt_item(A) ::= FORCE NULL_P columnList(D). {
    A = makeDefElem("force_null", (Node *) D, LOC(B));
}

copy_opt_item(A) ::= FORCE NULL_P STAR. {
    A = makeDefElem("force_null", (Node *) makeNode(A_Star), LOC(B));
}

copy_opt_item(A) ::= ENCODING sconst(C). {
    A = makeDefElem("encoding", (Node *) makeString(C), LOC(B));
}

/* ----- opt_binary ----- */

opt_binary(A) ::= BINARY. {
    A = makeDefElem("format", (Node *) makeString("binary"), LOC(B));
}

opt_binary(A) ::= . {
    A = NULL;
}

/* ----- copy_delimiter ----- */

copy_delimiter(A) ::= opt_using(B) DELIMITERS sconst(D). {
    A = makeDefElem("delimiter", (Node *) makeString(D), LOC(C));
}

copy_delimiter(A) ::= . {
    A = NULL;
}

/* ----- opt_using ----- */

opt_using(A) ::= USING.

/* ----- copy_generic_opt_list ----- */

copy_generic_opt_list(A) ::= copy_generic_opt_elem(B). {
    A = list_make1(B);
}

copy_generic_opt_list(A) ::= copy_generic_opt_list(B) COMMA copy_generic_opt_elem(D). {
    A = lappend(B, D);
}

/* ----- copy_generic_opt_elem ----- */

copy_generic_opt_elem(A) ::= colLabel(B) copy_generic_opt_arg(C). {
    A = makeDefElem(B, C, LOC(B));
}

copy_generic_opt_elem(A) ::= FORMAT_LA copy_generic_opt_arg(C). {
    A = makeDefElem("format", C, LOC(B));
}

/* ----- copy_generic_opt_arg ----- */

copy_generic_opt_arg(A) ::= opt_boolean_or_string(B). {
    A = (Node *) makeString(B);
}

copy_generic_opt_arg(A) ::= numericOnly(B). {
    A = (Node *) B;
}

copy_generic_opt_arg(A) ::= STAR. {
    A = (Node *) makeNode(A_Star);
}

copy_generic_opt_arg(A) ::= DEFAULT. {
    A = (Node *) makeString("default");
}

copy_generic_opt_arg(A) ::= LPAREN copy_generic_opt_arg_list(C) RPAREN. {
    A = (Node *) C;
}

copy_generic_opt_arg(A) ::= . {
    A = NULL;
}

/* ----- copy_generic_opt_arg_list ----- */

copy_generic_opt_arg_list(A) ::= copy_generic_opt_arg_list_item(B). {
    A = list_make1(B);
}

copy_generic_opt_arg_list(A) ::= copy_generic_opt_arg_list(B) COMMA copy_generic_opt_arg_list_item(D). {
    A = lappend(B, D);
}

/* ----- copy_generic_opt_arg_list_item ----- */

copy_generic_opt_arg_list_item(A) ::= opt_boolean_or_string(B). {
    A = (Node *) makeString(B);
}

/* ----- createStmt ----- */

fetchStmt(A) ::= FETCH fetch_args(C). {
    fetchStmt *n = (fetchStmt *) C;

    					n->ismove = false;
    					A = (Node *) n;
}

fetchStmt(A) ::= MOVE fetch_args(C). {
    fetchStmt *n = (fetchStmt *) C;

    					n->ismove = true;
    					A = (Node *) n;
}

/* ----- fetch_args ----- */

fetch_args(A) ::= cursor_name(B). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = B;
    					n->direction = FETCH_FORWARD;
    					n->howMany = 1;
    					n->location = -1;
    					n->direction_keyword = FETCH_KEYWORD_NONE;
    					A = (Node *) n;
}

fetch_args(A) ::= from_in(B) cursor_name(C). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = C;
    					n->direction = FETCH_FORWARD;
    					n->howMany = 1;
    					n->location = -1;
    					n->direction_keyword = FETCH_KEYWORD_NONE;
    					A = (Node *) n;
}

fetch_args(A) ::= signedIconst(B) opt_from_in(C) cursor_name(D). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = D;
    					n->direction = FETCH_FORWARD;
    					n->howMany = B;
    					n->location = LOC(B);
    					n->direction_keyword = FETCH_KEYWORD_NONE;
    					A = (Node *) n;
}

fetch_args(A) ::= NEXT opt_from_in(C) cursor_name(D). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = D;
    					n->direction = FETCH_FORWARD;
    					n->howMany = 1;
    					n->location = -1;
    					n->direction_keyword = FETCH_KEYWORD_NEXT;
    					A = (Node *) n;
}

fetch_args(A) ::= PRIOR opt_from_in(C) cursor_name(D). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = D;
    					n->direction = FETCH_BACKWARD;
    					n->howMany = 1;
    					n->location = -1;
    					n->direction_keyword = FETCH_KEYWORD_PRIOR;
    					A = (Node *) n;
}

fetch_args(A) ::= FIRST_P opt_from_in(C) cursor_name(D). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = D;
    					n->direction = FETCH_ABSOLUTE;
    					n->howMany = 1;
    					n->location = -1;
    					n->direction_keyword = FETCH_KEYWORD_FIRST;
    					A = (Node *) n;
}

fetch_args(A) ::= LAST_P opt_from_in(C) cursor_name(D). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = D;
    					n->direction = FETCH_ABSOLUTE;
    					n->howMany = -1;
    					n->location = -1;
    					n->direction_keyword = FETCH_KEYWORD_LAST;
    					A = (Node *) n;
}

fetch_args(A) ::= ABSOLUTE_P signedIconst(C) opt_from_in(D) cursor_name(E). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = E;
    					n->direction = FETCH_ABSOLUTE;
    					n->howMany = C;
    					n->location = LOC(C);
    					n->direction_keyword = FETCH_KEYWORD_ABSOLUTE;
    					A = (Node *) n;
}

fetch_args(A) ::= RELATIVE_P signedIconst(C) opt_from_in(D) cursor_name(E). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = E;
    					n->direction = FETCH_RELATIVE;
    					n->howMany = C;
    					n->location = LOC(C);
    					n->direction_keyword = FETCH_KEYWORD_RELATIVE;
    					A = (Node *) n;
}

fetch_args(A) ::= ALL opt_from_in(C) cursor_name(D). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = D;
    					n->direction = FETCH_FORWARD;
    					n->howMany = FETCH_ALL;
    					n->location = -1;
    					n->direction_keyword = FETCH_KEYWORD_ALL;
    					A = (Node *) n;
}

fetch_args(A) ::= FORWARD opt_from_in(C) cursor_name(D). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = D;
    					n->direction = FETCH_FORWARD;
    					n->howMany = 1;
    					n->location = -1;
    					n->direction_keyword = FETCH_KEYWORD_FORWARD;
    					A = (Node *) n;
}

fetch_args(A) ::= FORWARD signedIconst(C) opt_from_in(D) cursor_name(E). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = E;
    					n->direction = FETCH_FORWARD;
    					n->howMany = C;
    					n->location = LOC(C);
    					n->direction_keyword = FETCH_KEYWORD_FORWARD;
    					A = (Node *) n;
}

fetch_args(A) ::= FORWARD ALL opt_from_in(D) cursor_name(E). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = E;
    					n->direction = FETCH_FORWARD;
    					n->howMany = FETCH_ALL;
    					n->location = -1;
    					n->direction_keyword = FETCH_KEYWORD_FORWARD_ALL;
    					A = (Node *) n;
}

fetch_args(A) ::= BACKWARD opt_from_in(C) cursor_name(D). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = D;
    					n->direction = FETCH_BACKWARD;
    					n->howMany = 1;
    					n->location = -1;
    					n->direction_keyword = FETCH_KEYWORD_BACKWARD;
    					A = (Node *) n;
}

fetch_args(A) ::= BACKWARD signedIconst(C) opt_from_in(D) cursor_name(E). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = E;
    					n->direction = FETCH_BACKWARD;
    					n->howMany = C;
    					n->location = LOC(C);
    					n->direction_keyword = FETCH_KEYWORD_BACKWARD;
    					A = (Node *) n;
}

fetch_args(A) ::= BACKWARD ALL opt_from_in(D) cursor_name(E). {
    fetchStmt *n = makeNode(fetchStmt);

    					n->portalname = E;
    					n->direction = FETCH_BACKWARD;
    					n->howMany = FETCH_ALL;
    					n->location = -1;
    					n->direction_keyword = FETCH_KEYWORD_BACKWARD_ALL;
    					A = (Node *) n;
}

/* ----- from_in ----- */

notifyStmt(A) ::= NOTIFY colId(C) notify_payload(D). {
    notifyStmt *n = makeNode(notifyStmt);

    					n->conditionname = C;
    					n->payload = D;
    					A = (Node *) n;
}

/* ----- notify_payload ----- */

notify_payload(A) ::= COMMA sconst(C). {
    A = C;
}

notify_payload(A) ::= . {
    A = NULL;
}

/* ----- listenStmt ----- */

listenStmt(A) ::= LISTEN colId(C). {
    listenStmt *n = makeNode(listenStmt);

    					n->conditionname = C;
    					A = (Node *) n;
}

/* ----- unlistenStmt ----- */

unlistenStmt(A) ::= UNLISTEN colId(C). {
    unlistenStmt *n = makeNode(unlistenStmt);

    					n->conditionname = C;
    					A = (Node *) n;
}

unlistenStmt(A) ::= UNLISTEN STAR. {
    unlistenStmt *n = makeNode(unlistenStmt);

    					n->conditionname = NULL;
    					A = (Node *) n;
}

/* ----- transactionStmt ----- */

loadStmt(A) ::= LOAD file_name(C). {
    loadStmt   *n = makeNode(loadStmt);

    					n->filename = C;
    					A = (Node *) n;
}

/* ----- createdbStmt ----- */

vacuumStmt(A) ::= VACUUM opt_full(C) opt_freeze(D) opt_verbose(E) opt_analyze(F) opt_vacuum_relation_list(G). {
    vacuumStmt *n = makeNode(vacuumStmt);

    					n->options = NIL;
    					if (C)
    						n->options = lappend(n->options,
    											 makeDefElem("full", NULL, LOC(C)));
    					if (D)
    						n->options = lappend(n->options,
    											 makeDefElem("freeze", NULL, LOC(D)));
    					if (E)
    						n->options = lappend(n->options,
    											 makeDefElem("verbose", NULL, LOC(E)));
    					if (F)
    						n->options = lappend(n->options,
    											 makeDefElem("analyze", NULL, LOC(F)));
    					n->rels = G;
    					n->is_vacuumcmd = true;
    					A = (Node *) n;
}

vacuumStmt(A) ::= VACUUM LPAREN utility_option_list(D) RPAREN opt_vacuum_relation_list(F). {
    vacuumStmt *n = makeNode(vacuumStmt);

    					n->options = D;
    					n->rels = F;
    					n->is_vacuumcmd = true;
    					A = (Node *) n;
}

/* ----- analyzeStmt ----- */

analyzeStmt(A) ::= analyze_keyword(B) opt_utility_option_list(C) opt_vacuum_relation_list(D). {
    vacuumStmt *n = makeNode(vacuumStmt);

    					n->options = C;
    					n->rels = D;
    					n->is_vacuumcmd = false;
    					A = (Node *) n;
}

analyzeStmt(A) ::= analyze_keyword(B) VERBOSE opt_vacuum_relation_list(D). {
    vacuumStmt *n = makeNode(vacuumStmt);

    					n->options = list_make1(makeDefElem("verbose", NULL, LOC(C)));
    					n->rels = D;
    					n->is_vacuumcmd = false;
    					A = (Node *) n;
}

/* ----- analyze_keyword ----- */

analyze_keyword(A) ::= ANALYZE.

analyze_keyword(A) ::= ANALYSE.

/* ----- opt_analyze ----- */

opt_analyze(A) ::= analyze_keyword(B). {
    A = true;
}

opt_analyze(A) ::= . {
    A = false;
}

/* ----- opt_verbose ----- */

opt_verbose(A) ::= VERBOSE. {
    A = true;
}

opt_verbose(A) ::= . {
    A = false;
}

/* ----- opt_full ----- */

opt_full(A) ::= FULL. {
    A = true;
}

opt_full(A) ::= . {
    A = false;
}

/* ----- opt_freeze ----- */

opt_freeze(A) ::= FREEZE. {
    A = true;
}

opt_freeze(A) ::= . {
    A = false;
}

/* ----- opt_name_list ----- */

opt_name_list(A) ::= LPAREN name_list(C) RPAREN. {
    A = C;
}

opt_name_list(A) ::= . {
    A = NIL;
}

/* ----- vacuum_relation ----- */

vacuum_relation(A) ::= relation_expr(B) opt_name_list(C). {
    A = (Node *) makeVacuumRelation(B, InvalidOid, C);
}

/* ----- vacuum_relation_list ----- */

vacuum_relation_list(A) ::= vacuum_relation(B). {
    A = list_make1(B);
}

vacuum_relation_list(A) ::= vacuum_relation_list(B) COMMA vacuum_relation(D). {
    A = lappend(B, D);
}

/* ----- opt_vacuum_relation_list ----- */

opt_vacuum_relation_list(A) ::= vacuum_relation_list(B). {
    A = B;
}

opt_vacuum_relation_list(A) ::= . {
    A = NIL;
}

/* ----- explainStmt ----- */

explainStmt(A) ::= EXPLAIN explainableStmt(C). {
    explainStmt *n = makeNode(explainStmt);

    					n->query = C;
    					n->options = NIL;
    					A = (Node *) n;
}

explainStmt(A) ::= EXPLAIN analyze_keyword(C) opt_verbose(D) explainableStmt(E). {
    explainStmt *n = makeNode(explainStmt);

    					n->query = E;
    					n->options = list_make1(makeDefElem("analyze", NULL, LOC(C)));
    					if (D)
    						n->options = lappend(n->options,
    											 makeDefElem("verbose", NULL, LOC(D)));
    					A = (Node *) n;
}

explainStmt(A) ::= EXPLAIN VERBOSE explainableStmt(D). {
    explainStmt *n = makeNode(explainStmt);

    					n->query = D;
    					n->options = list_make1(makeDefElem("verbose", NULL, LOC(C)));
    					A = (Node *) n;
}

explainStmt(A) ::= EXPLAIN LPAREN utility_option_list(D) RPAREN explainableStmt(F). {
    explainStmt *n = makeNode(explainStmt);

    					n->query = F;
    					n->options = D;
    					A = (Node *) n;
}

/* ----- explainableStmt ----- */

explainableStmt(A) ::= selectStmt(B).

explainableStmt(A) ::= insertStmt(B).

explainableStmt(A) ::= updateStmt(B).

explainableStmt(A) ::= deleteStmt(B).

explainableStmt(A) ::= mergeStmt(B).

explainableStmt(A) ::= declareCursorStmt(B).

explainableStmt(A) ::= createAsStmt(B).

explainableStmt(A) ::= createMatViewStmt(B).

explainableStmt(A) ::= refreshMatViewStmt(B).

explainableStmt(A) ::= executeStmt(B).

/* ----- prepareStmt ----- */

prepareStmt(A) ::= PREPARE name(C) prep_type_clause(D) AS preparableStmt(F). {
    prepareStmt *n = makeNode(prepareStmt);

    					n->name = C;
    					n->argtypes = D;
    					n->query = F;
    					A = (Node *) n;
}

/* ----- prep_type_clause ----- */

prep_type_clause(A) ::= LPAREN type_list(C) RPAREN. {
    A = C;
}

prep_type_clause(A) ::= . {
    A = NIL;
}

/* ----- preparableStmt ----- */

preparableStmt(A) ::= selectStmt(B).

preparableStmt(A) ::= insertStmt(B).

preparableStmt(A) ::= updateStmt(B).

preparableStmt(A) ::= deleteStmt(B).

preparableStmt(A) ::= mergeStmt(B).

/* ----- executeStmt ----- */

executeStmt(A) ::= EXECUTE name(C) execute_param_clause(D). {
    executeStmt *n = makeNode(executeStmt);

    					n->name = C;
    					n->params = D;
    					A = (Node *) n;
}

executeStmt(A) ::= CREATE optTemp(C) TABLE create_as_target(E) AS EXECUTE name(H) execute_param_clause(I) opt_with_data(J). {
    CreateTableAsStmt *ctas = makeNode(CreateTableAsStmt);
    					executeStmt *n = makeNode(executeStmt);

    					n->name = H;
    					n->params = I;
    					ctas->query = (Node *) n;
    					ctas->into = E;
    					ctas->objtype = OBJECT_TABLE;
    					ctas->is_select_into = false;
    					ctas->if_not_exists = false;

    					E->rel->relpersistence = C;
    					E->skipData = !(J);
    					A = (Node *) ctas;
}

executeStmt(A) ::= CREATE optTemp(C) TABLE IF_P NOT EXISTS create_as_target(H) AS EXECUTE name(K) execute_param_clause(L) opt_with_data(M). {
    CreateTableAsStmt *ctas = makeNode(CreateTableAsStmt);
    					executeStmt *n = makeNode(executeStmt);

    					n->name = K;
    					n->params = L;
    					ctas->query = (Node *) n;
    					ctas->into = H;
    					ctas->objtype = OBJECT_TABLE;
    					ctas->is_select_into = false;
    					ctas->if_not_exists = true;

    					H->rel->relpersistence = C;
    					H->skipData = !(M);
    					A = (Node *) ctas;
}

/* ----- execute_param_clause ----- */

execute_param_clause(A) ::= LPAREN expr_list(C) RPAREN. {
    A = C;
}

execute_param_clause(A) ::= . {
    A = NIL;
}

/* ----- deallocateStmt ----- */

deallocateStmt(A) ::= DEALLOCATE name(C). {
    deallocateStmt *n = makeNode(deallocateStmt);

    						n->name = C;
    						n->isall = false;
    						n->location = LOC(C);
    						A = (Node *) n;
}

deallocateStmt(A) ::= DEALLOCATE PREPARE name(D). {
    deallocateStmt *n = makeNode(deallocateStmt);

    						n->name = D;
    						n->isall = false;
    						n->location = LOC(D);
    						A = (Node *) n;
}

deallocateStmt(A) ::= DEALLOCATE ALL. {
    deallocateStmt *n = makeNode(deallocateStmt);

    						n->name = NULL;
    						n->isall = true;
    						n->location = -1;
    						A = (Node *) n;
}

deallocateStmt(A) ::= DEALLOCATE PREPARE ALL. {
    deallocateStmt *n = makeNode(deallocateStmt);

    						n->name = NULL;
    						n->isall = true;
    						n->location = -1;
    						A = (Node *) n;
}

/* ----- insertStmt ----- */

declareCursorStmt(A) ::= DECLARE cursor_name(C) cursor_options(D) CURSOR opt_hold(F) FOR selectStmt(H). {
    declareCursorStmt *n = makeNode(declareCursorStmt);

    					n->portalname = C;

    					n->options = D | F | CURSOR_OPT_FAST_PLAN;
    					n->query = H;
    					A = (Node *) n;
}

/* ----- cursor_name ----- */

cursor_name(A) ::= name(B). {
    A = B;
}

/* ----- cursor_options ----- */

cursor_options(A) ::= . {
    A = 0;
}

cursor_options(A) ::= cursor_options(B) NO SCROLL. {
    A = B | CURSOR_OPT_NO_SCROLL;
}

cursor_options(A) ::= cursor_options(B) SCROLL. {
    A = B | CURSOR_OPT_SCROLL;
}

cursor_options(A) ::= cursor_options(B) BINARY. {
    A = B | CURSOR_OPT_BINARY;
}

cursor_options(A) ::= cursor_options(B) ASENSITIVE. {
    A = B | CURSOR_OPT_ASENSITIVE;
}

cursor_options(A) ::= cursor_options(B) INSENSITIVE. {
    A = B | CURSOR_OPT_INSENSITIVE;
}

/* ----- opt_hold ----- */

opt_hold(A) ::= . {
    A = 0;
}

opt_hold(A) ::= WITH HOLD. {
    A = CURSOR_OPT_HOLD;
}

opt_hold(A) ::= WITHOUT HOLD. {
    A = 0;
}

/* ----- selectStmt ----- */

waitStmt(A) ::= WAIT FOR LSN_P sconst(E) opt_wait_with_clause(F). {
    waitStmt *n = makeNode(waitStmt);
    					n->lsn_literal = E;
    					n->options = F;
    					A = (Node *) n;
}

/* ----- opt_wait_with_clause ----- */

opt_wait_with_clause(A) ::= WITH LPAREN utility_option_list(D) RPAREN. {
    A = D;
}

opt_wait_with_clause(A) ::= . {
    A = NIL;
}

/* ----- within_group_clause ----- */
```

