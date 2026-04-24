# Views, Rules, and Triggers


CREATE/ALTER VIEW, CREATE RULE, CREATE TRIGGER (row and
statement level), and CREATE EVENT TRIGGER.


```yaml
name: pg-views-triggers
version: 17.0.0
description: CREATE VIEW, RULE, TRIGGER, and EVENT TRIGGER
provides: [pg-views-triggers]
depends: [pg-type-decls, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
createTrigStmt(A) ::= CREATE opt_or_replace(C) TRIGGER name(E) triggerActionTime(F) triggerEvents(G) ON qualified_name(I) triggerReferencing(J) triggerForSpec(K) triggerWhen(L) EXECUTE fUNCTION_or_PROCEDURE(N) func_name(O) LPAREN triggerFuncArgs(Q) RPAREN. {
    createTrigStmt *n = makeNode(createTrigStmt);

    					n->replace = C;
    					n->isconstraint = false;
    					n->trigname = E;
    					n->relation = I;
    					n->funcname = O;
    					n->args = Q;
    					n->row = K;
    					n->timing = F;
    					n->events = intVal(linitial(G));
    					n->columns = (List *) lsecond(G);
    					n->whenClause = L;
    					n->transitionRels = J;
    					n->deferrable = false;
    					n->initdeferred = false;
    					n->constrrel = NULL;
    					A = (Node *) n;
}

createTrigStmt(A) ::= CREATE opt_or_replace(C) CONSTRAINT TRIGGER name(F) AFTER triggerEvents(H) ON qualified_name(J) optConstrFromTable(K) constraintAttributeSpec(L) FOR EACH ROW triggerWhen(P) EXECUTE fUNCTION_or_PROCEDURE(R) func_name(S) LPAREN triggerFuncArgs(U) RPAREN. {
    createTrigStmt *n = makeNode(createTrigStmt);
    					bool		dummy;

    					if ((L & CAS_NOT_VALID) != 0)
    						ereport(ERROR,
    								errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								errmsg("constraint triggers cannot be marked %s",
    									   "NOT VALID"),
    								parser_errposition(LOC(L)));
    					if ((L & CAS_NO_INHERIT) != 0)
    						ereport(ERROR,
    								errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								errmsg("constraint triggers cannot be marked %s",
    									   "NO INHERIT"),
    								parser_errposition(LOC(L)));
    					if ((L & CAS_NOT_ENFORCED) != 0)
    						ereport(ERROR,
    								errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								errmsg("constraint triggers cannot be marked %s",
    									   "NOT ENFORCED"),
    								parser_errposition(LOC(L)));

    					n->replace = C;
    					if (n->replace) 
    						ereport(ERROR,
    								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								 errmsg("CREATE OR REPLACE CONSTRAINT TRIGGER is not supported"),
    								 parser_errposition(LOC(B))));
    					n->isconstraint = true;
    					n->trigname = F;
    					n->relation = J;
    					n->funcname = S;
    					n->args = U;
    					n->row = true;
    					n->timing = TRIGGER_TYPE_AFTER;
    					n->events = intVal(linitial(H));
    					n->columns = (List *) lsecond(H);
    					n->whenClause = P;
    					n->transitionRels = NIL;
    					processCASbits(L, LOC(L), "TRIGGER",
    								   &n->deferrable, &n->initdeferred, &dummy,
    								   NULL, NULL, yyscanner);
    					n->constrrel = K;
    					A = (Node *) n;
}

/* ----- triggerActionTime ----- */

triggerActionTime(A) ::= BEFORE. {
    A = TRIGGER_TYPE_BEFORE;
}

triggerActionTime(A) ::= AFTER. {
    A = TRIGGER_TYPE_AFTER;
}

triggerActionTime(A) ::= INSTEAD OF. {
    A = TRIGGER_TYPE_INSTEAD;
}

/* ----- triggerEvents ----- */

triggerEvents(A) ::= triggerOneEvent(B). {
    A = B;
}

triggerEvents(A) ::= triggerEvents(B) OR triggerOneEvent(D). {
    int			events1 = intVal(linitial(B));
    					int			events2 = intVal(linitial(D));
    					List	   *columns1 = (List *) lsecond(B);
    					List	   *columns2 = (List *) lsecond(D);

    					if (events1 & events2)
    						parser_yyerror("duplicate trigger events specified");







    					A = list_make2(makeInteger(events1 | events2),
    									list_concat(columns1, columns2));
}

/* ----- triggerOneEvent ----- */

triggerOneEvent(A) ::= INSERT. {
    A = list_make2(makeInteger(TRIGGER_TYPE_INSERT), NIL);
}

triggerOneEvent(A) ::= DELETE_P. {
    A = list_make2(makeInteger(TRIGGER_TYPE_DELETE), NIL);
}

triggerOneEvent(A) ::= UPDATE. {
    A = list_make2(makeInteger(TRIGGER_TYPE_UPDATE), NIL);
}

triggerOneEvent(A) ::= UPDATE OF columnList(D). {
    A = list_make2(makeInteger(TRIGGER_TYPE_UPDATE), D);
}

triggerOneEvent(A) ::= TRUNCATE. {
    A = list_make2(makeInteger(TRIGGER_TYPE_TRUNCATE), NIL);
}

/* ----- triggerReferencing ----- */

triggerReferencing(A) ::= REFERENCING triggerTransitions(C). {
    A = C;
}

triggerReferencing(A) ::= . {
    A = NIL;
}

/* ----- triggerTransitions ----- */

triggerTransitions(A) ::= triggerTransition(B). {
    A = list_make1(B);
}

triggerTransitions(A) ::= triggerTransitions(B) triggerTransition(C). {
    A = lappend(B, C);
}

/* ----- triggerTransition ----- */

triggerTransition(A) ::= transitionOldOrNew(B) transitionRowOrTable(C) opt_as(D) transitionRelName(E). {
    triggerTransition *n = makeNode(triggerTransition);

    					n->name = E;
    					n->isNew = B;
    					n->isTable = C;
    					A = (Node *) n;
}

/* ----- transitionOldOrNew ----- */

transitionOldOrNew(A) ::= NEW. {
    A = true;
}

transitionOldOrNew(A) ::= OLD. {
    A = false;
}

/* ----- transitionRowOrTable ----- */

transitionRowOrTable(A) ::= TABLE. {
    A = true;
}

transitionRowOrTable(A) ::= ROW. {
    A = false;
}

/* ----- transitionRelName ----- */

transitionRelName(A) ::= colId(B). {
    A = B;
}

/* ----- triggerForSpec ----- */

triggerForSpec(A) ::= FOR triggerForOptEach(C) triggerForType(D). {
    A = D;
}

triggerForSpec(A) ::= . {
    A = false;
}

/* ----- triggerForOptEach ----- */

triggerForOptEach(A) ::= EACH.

/* ----- triggerForType ----- */

triggerForType(A) ::= ROW. {
    A = true;
}

triggerForType(A) ::= STATEMENT. {
    A = false;
}

/* ----- triggerWhen ----- */

triggerWhen(A) ::= WHEN LPAREN a_expr(D) RPAREN. {
    A = D;
}

triggerWhen(A) ::= . {
    A = NULL;
}

/* ----- fUNCTION_or_PROCEDURE ----- */

triggerFuncArgs(A) ::= triggerFuncArg(B). {
    A = list_make1(B);
}

triggerFuncArgs(A) ::= triggerFuncArgs(B) COMMA triggerFuncArg(D). {
    A = lappend(B, D);
}

triggerFuncArgs(A) ::= . {
    A = NIL;
}

/* ----- triggerFuncArg ----- */

triggerFuncArg(A) ::= iconst(B). {
    A = (Node *) makeString(psprintf("%d", B));
}

triggerFuncArg(A) ::= FCONST. {
    A = (Node *) makeString(B);
}

triggerFuncArg(A) ::= sconst(B). {
    A = (Node *) makeString(B);
}

triggerFuncArg(A) ::= colLabel(B). {
    A = (Node *) makeString(B);
}

/* ----- optConstrFromTable ----- */

createEventTrigStmt(A) ::= CREATE EVENT TRIGGER name(E) ON colLabel(G) EXECUTE fUNCTION_or_PROCEDURE(I) func_name(J) LPAREN RPAREN. {
    createEventTrigStmt *n = makeNode(createEventTrigStmt);

    					n->trigname = E;
    					n->eventname = G;
    					n->whenclause = NULL;
    					n->funcname = J;
    					A = (Node *) n;
}

createEventTrigStmt(A) ::= CREATE EVENT TRIGGER name(E) ON colLabel(G) WHEN event_trigger_when_list(I) EXECUTE fUNCTION_or_PROCEDURE(K) func_name(L) LPAREN RPAREN. {
    createEventTrigStmt *n = makeNode(createEventTrigStmt);

    					n->trigname = E;
    					n->eventname = G;
    					n->whenclause = I;
    					n->funcname = L;
    					A = (Node *) n;
}

/* ----- event_trigger_when_list ----- */

event_trigger_when_list(A) ::= event_trigger_when_item(B). {
    A = list_make1(B);
}

event_trigger_when_list(A) ::= event_trigger_when_list(B) AND event_trigger_when_item(D). {
    A = lappend(B, D);
}

/* ----- event_trigger_when_item ----- */

event_trigger_when_item(A) ::= colId(B) IN_P LPAREN event_trigger_value_list(E) RPAREN. {
    A = makeDefElem(B, (Node *) E, LOC(B));
}

/* ----- event_trigger_value_list ----- */

event_trigger_value_list(A) ::= SCONST. {
    A = list_make1(makeString(B));
}

event_trigger_value_list(A) ::= event_trigger_value_list(B) COMMA SCONST. {
    A = lappend(B, makeString(D));
}

/* ----- alterEventTrigStmt ----- */

alterEventTrigStmt(A) ::= ALTER EVENT TRIGGER name(E) enable_trigger(F). {
    alterEventTrigStmt *n = makeNode(alterEventTrigStmt);

    					n->trigname = E;
    					n->tgenabled = F;
    					A = (Node *) n;
}

/* ----- enable_trigger ----- */

enable_trigger(A) ::= ENABLE_P. {
    A = TRIGGER_FIRES_ON_ORIGIN;
}

enable_trigger(A) ::= ENABLE_P REPLICA. {
    A = TRIGGER_FIRES_ON_REPLICA;
}

enable_trigger(A) ::= ENABLE_P ALWAYS. {
    A = TRIGGER_FIRES_ALWAYS;
}

enable_trigger(A) ::= DISABLE_P. {
    A = TRIGGER_DISABLED;
}

/* ----- createAssertionStmt ----- */

createAssertionStmt(A) ::= CREATE ASSERTION any_name(D) CHECK LPAREN a_expr(G) RPAREN constraintAttributeSpec(I). {
    ereport(ERROR,
    							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    							 errmsg("CREATE ASSERTION is not yet implemented"),
    							 parser_errposition(LOC(B))));

    					A = NULL;
}

/* ----- defineStmt ----- */

ruleStmt(A) ::= CREATE opt_or_replace(C) RULE name(E) AS ON event(H) TO qualified_name(J) where_clause(K) DO opt_instead(M) ruleActionList(N). {
    ruleStmt   *n = makeNode(ruleStmt);

    					n->replace = C;
    					n->relation = J;
    					n->rulename = E;
    					n->whereClause = K;
    					n->event = H;
    					n->instead = M;
    					n->actions = N;
    					A = (Node *) n;
}

/* ----- ruleActionList ----- */

ruleActionList(A) ::= NOTHING. {
    A = NIL;
}

ruleActionList(A) ::= ruleActionStmt(B). {
    A = list_make1(B);
}

ruleActionList(A) ::= LPAREN ruleActionMulti(C) RPAREN. {
    A = C;
}

/* ----- ruleActionMulti ----- */

ruleActionMulti(A) ::= ruleActionMulti(B) SEMICOLON ruleActionStmtOrEmpty(D). {
    if (D != NULL)
    					A = lappend(B, D);
    				  else
    					A = B;
}

ruleActionMulti(A) ::= ruleActionStmtOrEmpty(B). {
    if (B != NULL)
    					A = list_make1(B);
    				  else
    					A = NIL;
}

/* ----- ruleActionStmt ----- */

ruleActionStmt(A) ::= selectStmt(B).

ruleActionStmt(A) ::= insertStmt(B).

ruleActionStmt(A) ::= updateStmt(B).

ruleActionStmt(A) ::= deleteStmt(B).

ruleActionStmt(A) ::= notifyStmt(B).

/* ----- ruleActionStmtOrEmpty ----- */

ruleActionStmtOrEmpty(A) ::= ruleActionStmt(B). {
    A = B;
}

ruleActionStmtOrEmpty(A) ::= . {
    A = NULL;
}

/* ----- event ----- */

viewStmt(A) ::= CREATE optTemp(C) VIEW qualified_name(E) opt_column_list(F) opt_reloptions(G) AS selectStmt(I) opt_check_option(J). {
    viewStmt   *n = makeNode(viewStmt);

    					n->view = E;
    					n->view->relpersistence = C;
    					n->aliases = F;
    					n->query = I;
    					n->replace = false;
    					n->options = G;
    					n->withCheckOption = J;
    					A = (Node *) n;
}

viewStmt(A) ::= CREATE OR REPLACE optTemp(E) VIEW qualified_name(G) opt_column_list(H) opt_reloptions(I) AS selectStmt(K) opt_check_option(L). {
    viewStmt   *n = makeNode(viewStmt);

    					n->view = G;
    					n->view->relpersistence = E;
    					n->aliases = H;
    					n->query = K;
    					n->replace = true;
    					n->options = I;
    					n->withCheckOption = L;
    					A = (Node *) n;
}

viewStmt(A) ::= CREATE optTemp(C) RECURSIVE VIEW qualified_name(F) LPAREN columnList(H) RPAREN opt_reloptions(J) AS selectStmt(L) opt_check_option(M). {
    viewStmt   *n = makeNode(viewStmt);

    					n->view = F;
    					n->view->relpersistence = C;
    					n->aliases = H;
    					n->query = makeRecursiveViewSelect(n->view->relname, n->aliases, L);
    					n->replace = false;
    					n->options = J;
    					n->withCheckOption = M;
    					if (n->withCheckOption != NO_CHECK_OPTION)
    						ereport(ERROR,
    								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								 errmsg("WITH CHECK OPTION not supported on recursive views"),
    								 parser_errposition(LOC(M))));
    					A = (Node *) n;
}

viewStmt(A) ::= CREATE OR REPLACE optTemp(E) RECURSIVE VIEW qualified_name(H) LPAREN columnList(J) RPAREN opt_reloptions(L) AS selectStmt(N) opt_check_option(O). {
    viewStmt   *n = makeNode(viewStmt);

    					n->view = H;
    					n->view->relpersistence = E;
    					n->aliases = J;
    					n->query = makeRecursiveViewSelect(n->view->relname, n->aliases, N);
    					n->replace = true;
    					n->options = L;
    					n->withCheckOption = O;
    					if (n->withCheckOption != NO_CHECK_OPTION)
    						ereport(ERROR,
    								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								 errmsg("WITH CHECK OPTION not supported on recursive views"),
    								 parser_errposition(LOC(O))));
    					A = (Node *) n;
}

/* ----- opt_check_option ----- */

opt_check_option(A) ::= WITH CHECK OPTION. {
    A = CASCADED_CHECK_OPTION;
}

opt_check_option(A) ::= WITH CASCADED CHECK OPTION. {
    A = CASCADED_CHECK_OPTION;
}

opt_check_option(A) ::= WITH LOCAL CHECK OPTION. {
    A = LOCAL_CHECK_OPTION;
}

opt_check_option(A) ::= . {
    A = NO_CHECK_OPTION;
}

/* ----- loadStmt ----- */
```

