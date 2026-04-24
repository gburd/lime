# Variable Statements


SET, SHOW, and RESET statements for runtime configuration
variables. Includes timezone and encoding handling.


```yaml
name: pg-variables
version: 17.0.0
description: SET, SHOW, and RESET variable statements
provides: [pg-variables]
depends: [pg-type-decls, pg-base-helpers]
```

## Production Rules

```lime rules
variableSetStmt(A) ::= SET set_rest(C). {
    variableSetStmt *n = C;

    					n->is_local = false;
    					A = (Node *) n;
}

variableSetStmt(A) ::= SET LOCAL set_rest(D). {
    variableSetStmt *n = D;

    					n->is_local = true;
    					A = (Node *) n;
}

variableSetStmt(A) ::= SET SESSION set_rest(D). {
    variableSetStmt *n = D;

    					n->is_local = false;
    					A = (Node *) n;
}

/* ----- set_rest ----- */

set_rest(A) ::= TRANSACTION transaction_mode_list(C). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_MULTI;
    					n->name = "TRANSACTION";
    					n->args = C;
    					n->jumble_args = true;
    					n->location = -1;
    					A = n;
}

set_rest(A) ::= SESSION CHARACTERISTICS AS TRANSACTION transaction_mode_list(F). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_MULTI;
    					n->name = "SESSION CHARACTERISTICS";
    					n->args = F;
    					n->jumble_args = true;
    					n->location = -1;
    					A = n;
}

set_rest(A) ::= set_rest_more(B).

/* ----- generic_set ----- */

generic_set(A) ::= var_name(B) TO var_list(D). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_VALUE;
    					n->name = B;
    					n->args = D;
    					n->location = LOC(D);
    					A = n;
}

generic_set(A) ::= var_name(B) EQ var_list(D). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_VALUE;
    					n->name = B;
    					n->args = D;
    					n->location = LOC(D);
    					A = n;
}

generic_set(A) ::= var_name(B) TO NULL_P. {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_VALUE;
    					n->name = B;
    					n->args = list_make1(makeNullAConst(LOC(D)));
    					n->location = LOC(D);
    					A = n;
}

generic_set(A) ::= var_name(B) EQ NULL_P. {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_VALUE;
    					n->name = B;
    					n->args = list_make1(makeNullAConst(LOC(D)));
    					n->location = LOC(D);
    					A = n;
}

generic_set(A) ::= var_name(B) TO DEFAULT. {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_DEFAULT;
    					n->name = B;
    					n->location = -1;
    					A = n;
}

generic_set(A) ::= var_name(B) EQ DEFAULT. {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_DEFAULT;
    					n->name = B;
    					n->location = -1;
    					A = n;
}

/* ----- set_rest_more ----- */

set_rest_more(A) ::= generic_set(B). {
    A = B;
}

set_rest_more(A) ::= var_name(B) FROM CURRENT_P. {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_CURRENT;
    					n->name = B;
    					n->location = -1;
    					A = n;
}

set_rest_more(A) ::= TIME ZONE zone_value(D). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_VALUE;
    					n->name = "timezone";
    					n->location = -1;
    					n->jumble_args = true;
    					if (D != NULL)
    						n->args = list_make1(D);
    					else
    						n->kind = VAR_SET_DEFAULT;
    					A = n;
}

set_rest_more(A) ::= CATALOG_P sconst(C). {
    ereport(ERROR,
    							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    							 errmsg("current database cannot be changed"),
    							 parser_errposition(LOC(C))));
    					A = NULL;
}

set_rest_more(A) ::= SCHEMA sconst(C). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_VALUE;
    					n->name = "search_path";
    					n->args = list_make1(makeStringConst(C, LOC(C)));
    					n->location = LOC(C);
    					A = n;
}

set_rest_more(A) ::= NAMES opt_encoding(C). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_VALUE;
    					n->name = "client_encoding";
    					n->location = LOC(C);
    					if (C != NULL)
    						n->args = list_make1(makeStringConst(C, LOC(C)));
    					else
    						n->kind = VAR_SET_DEFAULT;
    					A = n;
}

set_rest_more(A) ::= ROLE nonReservedWord_or_Sconst(C). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_VALUE;
    					n->name = "role";
    					n->args = list_make1(makeStringConst(C, LOC(C)));
    					n->location = LOC(C);
    					A = n;
}

set_rest_more(A) ::= SESSION AUTHORIZATION nonReservedWord_or_Sconst(D). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_VALUE;
    					n->name = "session_authorization";
    					n->args = list_make1(makeStringConst(D, LOC(D)));
    					n->location = LOC(D);
    					A = n;
}

set_rest_more(A) ::= SESSION AUTHORIZATION DEFAULT. {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_DEFAULT;
    					n->name = "session_authorization";
    					n->location = -1;
    					A = n;
}

set_rest_more(A) ::= XML_P OPTION document_or_content(D). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_VALUE;
    					n->name = "xmloption";
    					n->args = list_make1(makeStringConst(D == XMLOPTION_DOCUMENT ? "DOCUMENT" : "CONTENT", LOC(D)));
    					n->jumble_args = true;
    					n->location = -1;
    					A = n;
}

set_rest_more(A) ::= TRANSACTION SNAPSHOT sconst(D). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_SET_MULTI;
    					n->name = "TRANSACTION SNAPSHOT";
    					n->args = list_make1(makeStringConst(D, LOC(D)));
    					n->location = LOC(D);
    					A = n;
}

/* ----- var_name ----- */

var_name(A) ::= colId(B). {
    A = B;
}

var_name(A) ::= var_name(B) DOT colId(D). {
    A = psprintf("%s.%s", B, D);
}

/* ----- var_list ----- */

var_list(A) ::= var_value(B). {
    A = list_make1(B);
}

var_list(A) ::= var_list(B) COMMA var_value(D). {
    A = lappend(B, D);
}

/* ----- var_value ----- */

var_value(A) ::= opt_boolean_or_string(B). {
    A = makeStringConst(B, LOC(B));
}

var_value(A) ::= numericOnly(B). {
    A = makeAConst(B, LOC(B));
}

/* ----- iso_level ----- */

iso_level(A) ::= READ UNCOMMITTED. {
    A = "read uncommitted";
}

iso_level(A) ::= READ COMMITTED. {
    A = "read committed";
}

iso_level(A) ::= REPEATABLE READ. {
    A = "repeatable read";
}

iso_level(A) ::= SERIALIZABLE. {
    A = "serializable";
}

/* ----- opt_boolean_or_string ----- */

opt_boolean_or_string(A) ::= TRUE_P. {
    A = "true";
}

opt_boolean_or_string(A) ::= FALSE_P. {
    A = "false";
}

opt_boolean_or_string(A) ::= ON. {
    A = "on";
}

opt_boolean_or_string(A) ::= nonReservedWord_or_Sconst(B). {
    A = B;
}

/* ----- zone_value ----- */

zone_value(A) ::= sconst(B). {
    A = makeStringConst(B, LOC(B));
}

zone_value(A) ::= IDENT. {
    A = makeStringConst(B, LOC(B));
}

zone_value(A) ::= constInterval(B) sconst(C) opt_interval(D). {
    TypeName   *t = B;

    					if (D != NIL)
    					{
    						A_Const	   *n = (A_Const *) linitial(D);

    						if ((n->val.ival.ival & ~(INTERVAL_MASK(HOUR) | INTERVAL_MASK(MINUTE))) != 0)
    							ereport(ERROR,
    									(errcode(ERRCODE_SYNTAX_ERROR),
    									 errmsg("time zone interval must be HOUR or HOUR TO MINUTE"),
    									 parser_errposition(LOC(D))));
    					}
    					t->typmods = D;
    					A = makeStringConstCast(C, LOC(C), t);
}

zone_value(A) ::= constInterval(B) LPAREN iconst(D) RPAREN sconst(F). {
    TypeName   *t = B;

    					t->typmods = list_make2(makeIntConst(INTERVAL_FULL_RANGE, -1),
    											makeIntConst(D, LOC(D)));
    					A = makeStringConstCast(F, LOC(F), t);
}

zone_value(A) ::= numericOnly(B). {
    A = makeAConst(B, LOC(B));
}

zone_value(A) ::= DEFAULT. {
    A = NULL;
}

zone_value(A) ::= LOCAL. {
    A = NULL;
}

/* ----- opt_encoding ----- */

opt_encoding(A) ::= sconst(B). {
    A = B;
}

opt_encoding(A) ::= DEFAULT. {
    A = NULL;
}

opt_encoding(A) ::= . {
    A = NULL;
}

/* ----- nonReservedWord_or_Sconst ----- */

nonReservedWord_or_Sconst(A) ::= nonReservedWord(B). {
    A = B;
}

nonReservedWord_or_Sconst(A) ::= sconst(B). {
    A = B;
}

/* ----- variableResetStmt ----- */

variableResetStmt(A) ::= RESET reset_rest(C). {
    A = (Node *) C;
}

/* ----- reset_rest ----- */

reset_rest(A) ::= generic_reset(B). {
    A = B;
}

reset_rest(A) ::= TIME ZONE. {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_RESET;
    					n->name = "timezone";
    					n->location = -1;
    					A = n;
}

reset_rest(A) ::= TRANSACTION ISOLATION LEVEL. {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_RESET;
    					n->name = "transaction_isolation";
    					n->location = -1;
    					A = n;
}

reset_rest(A) ::= SESSION AUTHORIZATION. {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_RESET;
    					n->name = "session_authorization";
    					n->location = -1;
    					A = n;
}

/* ----- generic_reset ----- */

generic_reset(A) ::= var_name(B). {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_RESET;
    					n->name = B;
    					n->location = -1;
    					A = n;
}

generic_reset(A) ::= ALL. {
    variableSetStmt *n = makeNode(variableSetStmt);

    					n->kind = VAR_RESET_ALL;
    					n->location = -1;
    					A = n;
}

/* ----- setResetClause ----- */

setResetClause(A) ::= SET set_rest(C). {
    A = C;
}

setResetClause(A) ::= variableResetStmt(B). {
    A = (variableSetStmt *) B;
}

/* ----- functionSetResetClause ----- */

functionSetResetClause(A) ::= SET set_rest_more(C). {
    A = C;
}

functionSetResetClause(A) ::= variableResetStmt(B). {
    A = (variableSetStmt *) B;
}

/* ----- variableShowStmt ----- */

variableShowStmt(A) ::= SHOW var_name(C). {
    variableShowStmt *n = makeNode(variableShowStmt);

    					n->name = C;
    					A = (Node *) n;
}

variableShowStmt(A) ::= SHOW TIME ZONE. {
    variableShowStmt *n = makeNode(variableShowStmt);

    					n->name = "timezone";
    					A = (Node *) n;
}

variableShowStmt(A) ::= SHOW TRANSACTION ISOLATION LEVEL. {
    variableShowStmt *n = makeNode(variableShowStmt);

    					n->name = "transaction_isolation";
    					A = (Node *) n;
}

variableShowStmt(A) ::= SHOW SESSION AUTHORIZATION. {
    variableShowStmt *n = makeNode(variableShowStmt);

    					n->name = "session_authorization";
    					A = (Node *) n;
}

variableShowStmt(A) ::= SHOW ALL. {
    variableShowStmt *n = makeNode(variableShowStmt);

    					n->name = "all";
    					A = (Node *) n;
}

/* ----- constraintsSetStmt ----- */
```

