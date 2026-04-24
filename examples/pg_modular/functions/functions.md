# Functions, Procedures, and Operators


CREATE/ALTER/DROP FUNCTION, PROCEDURE, AGGREGATE, OPERATOR,
OPERATOR CLASS, OPERATOR FAMILY, CAST, ACCESS METHOD,
TRANSFORM, LANGUAGE, DO, and CALL. Includes function
argument lists, return types, and routine body support.


```yaml
name: pg-functions
version: 17.0.0
description: CREATE/ALTER/DROP FUNCTION, PROCEDURE, AGGREGATE, OPERATOR, CAST, etc.
provides: [pg-functions]
depends: [pg-type-decls, pg-expressions, pg-typenames, pg-base-helpers]
```

## Production Rules

```lime rules
callStmt(A) ::= CALL func_application(C). {
    callStmt   *n = makeNode(callStmt);

    					n->funccall = castNode(FuncCall, C);
    					A = (Node *) n;
}

/* ----- createRoleStmt ----- */

createPLangStmt(A) ::= CREATE opt_or_replace(C) opt_trusted(D) opt_procedural(E) LANGUAGE name(G). {
    createExtensionStmt *n = makeNode(createExtensionStmt);

    				n->if_not_exists = C;
    				n->extname = G;
    				n->options = NIL;
    				A = (Node *) n;
}

createPLangStmt(A) ::= CREATE opt_or_replace(C) opt_trusted(D) opt_procedural(E) LANGUAGE name(G) HANDLER handler_name(I) opt_inline_handler(J) opt_validator(K). {
    createPLangStmt *n = makeNode(createPLangStmt);

    				n->replace = C;
    				n->plname = G;
    				n->plhandler = I;
    				n->plinline = J;
    				n->plvalidator = K;
    				n->pltrusted = D;
    				A = (Node *) n;
}

/* ----- opt_trusted ----- */

opt_trusted(A) ::= TRUSTED. {
    A = true;
}

opt_trusted(A) ::= . {
    A = false;
}

/* ----- handler_name ----- */

handler_name(A) ::= name(B). {
    A = list_make1(makeString(B));
}

handler_name(A) ::= name(B) attrs(C). {
    A = lcons(makeString(B), C);
}

/* ----- opt_inline_handler ----- */

opt_inline_handler(A) ::= INLINE_P handler_name(C). {
    A = C;
}

opt_inline_handler(A) ::= . {
    A = NIL;
}

/* ----- validator_clause ----- */

validator_clause(A) ::= VALIDATOR handler_name(C). {
    A = C;
}

validator_clause(A) ::= NO VALIDATOR. {
    A = NIL;
}

/* ----- opt_validator ----- */

opt_validator(A) ::= validator_clause(B). {
    A = B;
}

opt_validator(A) ::= . {
    A = NIL;
}

/* ----- opt_procedural ----- */

opt_procedural(A) ::= PROCEDURAL.

/* ----- createTableSpaceStmt ----- */

createAmStmt(A) ::= CREATE ACCESS METHOD name(E) TYPE_P am_type(G) HANDLER handler_name(I). {
    createAmStmt *n = makeNode(createAmStmt);

    					n->amname = E;
    					n->handler_name = I;
    					n->amtype = G;
    					A = (Node *) n;
}

/* ----- am_type ----- */

am_type(A) ::= INDEX. {
    A = AMTYPE_INDEX;
}

am_type(A) ::= TABLE. {
    A = AMTYPE_TABLE;
}

/* ----- createTrigStmt ----- */

fUNCTION_or_PROCEDURE(A) ::= FUNCTION.

fUNCTION_or_PROCEDURE(A) ::= PROCEDURE.

/* ----- triggerFuncArgs ----- */

defineStmt(A) ::= CREATE opt_or_replace(C) AGGREGATE func_name(E) aggr_args(F) definition(G). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_AGGREGATE;
    					n->oldstyle = false;
    					n->replace = C;
    					n->defnames = E;
    					n->args = F;
    					n->definition = G;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE opt_or_replace(C) AGGREGATE func_name(E) old_aggr_definition(F). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_AGGREGATE;
    					n->oldstyle = true;
    					n->replace = C;
    					n->defnames = E;
    					n->args = NIL;
    					n->definition = F;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE OPERATOR any_operator(D) definition(E). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_OPERATOR;
    					n->oldstyle = false;
    					n->defnames = D;
    					n->args = NIL;
    					n->definition = E;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE TYPE_P any_name(D) definition(E). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_TYPE;
    					n->oldstyle = false;
    					n->defnames = D;
    					n->args = NIL;
    					n->definition = E;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE TYPE_P any_name(D). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_TYPE;
    					n->oldstyle = false;
    					n->defnames = D;
    					n->args = NIL;
    					n->definition = NIL;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE TYPE_P any_name(D) AS LPAREN optTableFuncElementList(G) RPAREN. {
    CompositeTypeStmt *n = makeNode(CompositeTypeStmt);


    					n->typevar = makeRangeVarFromAnyName(D, LOC(D), yyscanner);
    					n->coldeflist = G;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE TYPE_P any_name(D) AS ENUM_P LPAREN opt_enum_val_list(H) RPAREN. {
    CreateEnumStmt *n = makeNode(CreateEnumStmt);

    					n->typeName = D;
    					n->vals = H;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE TYPE_P any_name(D) AS RANGE definition(G). {
    CreateRangeStmt *n = makeNode(CreateRangeStmt);

    					n->typeName = D;
    					n->params = G;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE TEXT_P SEARCH PARSER any_name(F) definition(G). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_TSPARSER;
    					n->args = NIL;
    					n->defnames = F;
    					n->definition = G;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE TEXT_P SEARCH DICTIONARY any_name(F) definition(G). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_TSDICTIONARY;
    					n->args = NIL;
    					n->defnames = F;
    					n->definition = G;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE TEXT_P SEARCH TEMPLATE any_name(F) definition(G). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_TSTEMPLATE;
    					n->args = NIL;
    					n->defnames = F;
    					n->definition = G;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE TEXT_P SEARCH CONFIGURATION any_name(F) definition(G). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_TSCONFIGURATION;
    					n->args = NIL;
    					n->defnames = F;
    					n->definition = G;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE COLLATION any_name(D) definition(E). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_COLLATION;
    					n->args = NIL;
    					n->defnames = D;
    					n->definition = E;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE COLLATION IF_P NOT EXISTS any_name(G) definition(H). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_COLLATION;
    					n->args = NIL;
    					n->defnames = G;
    					n->definition = H;
    					n->if_not_exists = true;
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE COLLATION any_name(D) FROM any_name(F). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_COLLATION;
    					n->args = NIL;
    					n->defnames = D;
    					n->definition = list_make1(makeDefElem("from", (Node *) F, LOC(F)));
    					A = (Node *) n;
}

defineStmt(A) ::= CREATE COLLATION IF_P NOT EXISTS any_name(G) FROM any_name(I). {
    defineStmt *n = makeNode(defineStmt);

    					n->kind = OBJECT_COLLATION;
    					n->args = NIL;
    					n->defnames = G;
    					n->definition = list_make1(makeDefElem("from", (Node *) I, LOC(I)));
    					n->if_not_exists = true;
    					A = (Node *) n;
}

/* ----- definition ----- */

definition(A) ::= LPAREN def_list(C) RPAREN. {
    A = C;
}

/* ----- def_list ----- */

def_list(A) ::= def_elem(B). {
    A = list_make1(B);
}

def_list(A) ::= def_list(B) COMMA def_elem(D). {
    A = lappend(B, D);
}

/* ----- def_elem ----- */

def_elem(A) ::= colLabel(B) EQ def_arg(D). {
    A = makeDefElem(B, (Node *) D, LOC(B));
}

def_elem(A) ::= colLabel(B). {
    A = makeDefElem(B, NULL, LOC(B));
}

/* ----- def_arg ----- */

def_arg(A) ::= func_type(B). {
    A = (Node *) B;
}

def_arg(A) ::= reserved_keyword(B). {
    A = (Node *) makeString(pstrdup(B));
}

def_arg(A) ::= qual_all_Op(B). {
    A = (Node *) B;
}

def_arg(A) ::= numericOnly(B). {
    A = (Node *) B;
}

def_arg(A) ::= sconst(B). {
    A = (Node *) makeString(B);
}

def_arg(A) ::= NONE. {
    A = (Node *) makeString(pstrdup(B));
}

/* ----- old_aggr_definition ----- */

old_aggr_definition(A) ::= LPAREN old_aggr_list(C) RPAREN. {
    A = C;
}

/* ----- old_aggr_list ----- */

old_aggr_list(A) ::= old_aggr_elem(B). {
    A = list_make1(B);
}

old_aggr_list(A) ::= old_aggr_list(B) COMMA old_aggr_elem(D). {
    A = lappend(B, D);
}

/* ----- old_aggr_elem ----- */

old_aggr_elem(A) ::= IDENT EQ def_arg(D). {
    A = makeDefElem(B, (Node *) D, LOC(B));
}

/* ----- opt_enum_val_list ----- */

createOpClassStmt(A) ::= CREATE OPERATOR CLASS any_name(E) opt_default(F) FOR TYPE_P typename(I) USING name(K) opt_opfamily(L) AS opclass_item_list(N). {
    createOpClassStmt *n = makeNode(createOpClassStmt);

    					n->opclassname = E;
    					n->isDefault = F;
    					n->datatype = I;
    					n->amname = K;
    					n->opfamilyname = L;
    					n->items = N;
    					A = (Node *) n;
}

/* ----- opclass_item_list ----- */

opclass_item_list(A) ::= opclass_item(B). {
    A = list_make1(B);
}

opclass_item_list(A) ::= opclass_item_list(B) COMMA opclass_item(D). {
    A = lappend(B, D);
}

/* ----- opclass_item ----- */

opclass_item(A) ::= OPERATOR iconst(C) any_operator(D) opclass_purpose(E). {
    CreateOpClassItem *n = makeNode(CreateOpClassItem);
    					ObjectWithArgs *owa = makeNode(ObjectWithArgs);

    					owa->objname = D;
    					owa->objargs = NIL;
    					n->itemtype = OPCLASS_ITEM_OPERATOR;
    					n->name = owa;
    					n->number = C;
    					n->order_family = E;
    					A = (Node *) n;
}

opclass_item(A) ::= OPERATOR iconst(C) operator_with_argtypes(D) opclass_purpose(E). {
    CreateOpClassItem *n = makeNode(CreateOpClassItem);

    					n->itemtype = OPCLASS_ITEM_OPERATOR;
    					n->name = D;
    					n->number = C;
    					n->order_family = E;
    					A = (Node *) n;
}

opclass_item(A) ::= FUNCTION iconst(C) function_with_argtypes(D). {
    CreateOpClassItem *n = makeNode(CreateOpClassItem);

    					n->itemtype = OPCLASS_ITEM_FUNCTION;
    					n->name = D;
    					n->number = C;
    					A = (Node *) n;
}

opclass_item(A) ::= FUNCTION iconst(C) LPAREN type_list(E) RPAREN function_with_argtypes(G). {
    CreateOpClassItem *n = makeNode(CreateOpClassItem);

    					n->itemtype = OPCLASS_ITEM_FUNCTION;
    					n->name = G;
    					n->number = C;
    					n->class_args = E;
    					A = (Node *) n;
}

opclass_item(A) ::= STORAGE typename(C). {
    CreateOpClassItem *n = makeNode(CreateOpClassItem);

    					n->itemtype = OPCLASS_ITEM_STORAGETYPE;
    					n->storedtype = C;
    					A = (Node *) n;
}

/* ----- opt_default ----- */

opt_default(A) ::= DEFAULT. {
    A = true;
}

opt_default(A) ::= . {
    A = false;
}

/* ----- opt_opfamily ----- */

opt_opfamily(A) ::= FAMILY any_name(C). {
    A = C;
}

opt_opfamily(A) ::= . {
    A = NIL;
}

/* ----- opclass_purpose ----- */

opclass_purpose(A) ::= FOR SEARCH. {
    A = NIL;
}

opclass_purpose(A) ::= FOR ORDER BY any_name(E). {
    A = E;
}

opclass_purpose(A) ::= . {
    A = NIL;
}

/* ----- createOpFamilyStmt ----- */

createOpFamilyStmt(A) ::= CREATE OPERATOR FAMILY any_name(E) USING name(G). {
    createOpFamilyStmt *n = makeNode(createOpFamilyStmt);

    					n->opfamilyname = E;
    					n->amname = G;
    					A = (Node *) n;
}

/* ----- alterOpFamilyStmt ----- */

alterOpFamilyStmt(A) ::= ALTER OPERATOR FAMILY any_name(E) USING name(G) ADD_P opclass_item_list(I). {
    alterOpFamilyStmt *n = makeNode(alterOpFamilyStmt);

    					n->opfamilyname = E;
    					n->amname = G;
    					n->isDrop = false;
    					n->items = I;
    					A = (Node *) n;
}

alterOpFamilyStmt(A) ::= ALTER OPERATOR FAMILY any_name(E) USING name(G) DROP opclass_drop_list(I). {
    alterOpFamilyStmt *n = makeNode(alterOpFamilyStmt);

    					n->opfamilyname = E;
    					n->amname = G;
    					n->isDrop = true;
    					n->items = I;
    					A = (Node *) n;
}

/* ----- opclass_drop_list ----- */

opclass_drop_list(A) ::= opclass_drop(B). {
    A = list_make1(B);
}

opclass_drop_list(A) ::= opclass_drop_list(B) COMMA opclass_drop(D). {
    A = lappend(B, D);
}

/* ----- opclass_drop ----- */

opclass_drop(A) ::= OPERATOR iconst(C) LPAREN type_list(E) RPAREN. {
    CreateOpClassItem *n = makeNode(CreateOpClassItem);

    					n->itemtype = OPCLASS_ITEM_OPERATOR;
    					n->number = C;
    					n->class_args = E;
    					A = (Node *) n;
}

opclass_drop(A) ::= FUNCTION iconst(C) LPAREN type_list(E) RPAREN. {
    CreateOpClassItem *n = makeNode(CreateOpClassItem);

    					n->itemtype = OPCLASS_ITEM_FUNCTION;
    					n->number = C;
    					n->class_args = E;
    					A = (Node *) n;
}

/* ----- dropOpClassStmt ----- */

createFunctionStmt(A) ::= CREATE opt_or_replace(C) FUNCTION func_name(E) func_args_with_defaults(F) RETURNS func_return(H) opt_createfunc_opt_list(I) opt_routine_body(J). {
    createFunctionStmt *n = makeNode(createFunctionStmt);

    					n->is_procedure = false;
    					n->replace = C;
    					n->funcname = E;
    					n->parameters = F;
    					n->returnType = H;
    					n->options = I;
    					n->sql_body = J;
    					A = (Node *) n;
}

createFunctionStmt(A) ::= CREATE opt_or_replace(C) FUNCTION func_name(E) func_args_with_defaults(F) RETURNS TABLE LPAREN table_func_column_list(J) RPAREN opt_createfunc_opt_list(L) opt_routine_body(M). {
    createFunctionStmt *n = makeNode(createFunctionStmt);

    					n->is_procedure = false;
    					n->replace = C;
    					n->funcname = E;
    					n->parameters = mergeTableFuncParameters(F, J, yyscanner);
    					n->returnType = TableFuncTypeName(J);
    					n->returnType->location = LOC(H);
    					n->options = L;
    					n->sql_body = M;
    					A = (Node *) n;
}

createFunctionStmt(A) ::= CREATE opt_or_replace(C) FUNCTION func_name(E) func_args_with_defaults(F) opt_createfunc_opt_list(G) opt_routine_body(H). {
    createFunctionStmt *n = makeNode(createFunctionStmt);

    					n->is_procedure = false;
    					n->replace = C;
    					n->funcname = E;
    					n->parameters = F;
    					n->returnType = NULL;
    					n->options = G;
    					n->sql_body = H;
    					A = (Node *) n;
}

createFunctionStmt(A) ::= CREATE opt_or_replace(C) PROCEDURE func_name(E) func_args_with_defaults(F) opt_createfunc_opt_list(G) opt_routine_body(H). {
    createFunctionStmt *n = makeNode(createFunctionStmt);

    					n->is_procedure = true;
    					n->replace = C;
    					n->funcname = E;
    					n->parameters = F;
    					n->returnType = NULL;
    					n->options = G;
    					n->sql_body = H;
    					A = (Node *) n;
}

/* ----- opt_or_replace ----- */

opt_or_replace(A) ::= OR REPLACE. {
    A = true;
}

opt_or_replace(A) ::= . {
    A = false;
}

/* ----- func_args ----- */

func_args(A) ::= LPAREN func_args_list(C) RPAREN. {
    A = C;
}

func_args(A) ::= LPAREN RPAREN. {
    A = NIL;
}

/* ----- func_args_list ----- */

func_args_list(A) ::= func_arg(B). {
    A = list_make1(B);
}

func_args_list(A) ::= func_args_list(B) COMMA func_arg(D). {
    A = lappend(B, D);
}

/* ----- function_with_argtypes_list ----- */

function_with_argtypes_list(A) ::= function_with_argtypes(B). {
    A = list_make1(B);
}

function_with_argtypes_list(A) ::= function_with_argtypes_list(B) COMMA function_with_argtypes(D). {
    A = lappend(B, D);
}

/* ----- function_with_argtypes ----- */

function_with_argtypes(A) ::= func_name(B) func_args(C). {
    ObjectWithArgs *n = makeNode(ObjectWithArgs);

    					n->objname = B;
    					n->objargs = extractArgTypes(C);
    					n->objfuncargs = C;
    					A = n;
}

function_with_argtypes(A) ::= type_func_name_keyword(B). {
    ObjectWithArgs *n = makeNode(ObjectWithArgs);

    					n->objname = list_make1(makeString(pstrdup(B)));
    					n->args_unspecified = true;
    					A = n;
}

function_with_argtypes(A) ::= colId(B). {
    ObjectWithArgs *n = makeNode(ObjectWithArgs);

    					n->objname = list_make1(makeString(B));
    					n->args_unspecified = true;
    					A = n;
}

function_with_argtypes(A) ::= colId(B) indirection(C). {
    ObjectWithArgs *n = makeNode(ObjectWithArgs);

    					n->objname = check_func_name(lcons(makeString(B), C),
    												  yyscanner);
    					n->args_unspecified = true;
    					A = n;
}

/* ----- func_args_with_defaults ----- */

func_args_with_defaults(A) ::= LPAREN func_args_with_defaults_list(C) RPAREN. {
    A = C;
}

func_args_with_defaults(A) ::= LPAREN RPAREN. {
    A = NIL;
}

/* ----- func_args_with_defaults_list ----- */

func_args_with_defaults_list(A) ::= func_arg_with_default(B). {
    A = list_make1(B);
}

func_args_with_defaults_list(A) ::= func_args_with_defaults_list(B) COMMA func_arg_with_default(D). {
    A = lappend(B, D);
}

/* ----- func_arg ----- */

func_arg(A) ::= arg_class(B) param_name(C) func_type(D). {
    FunctionParameter *n = makeNode(FunctionParameter);

    					n->name = C;
    					n->argType = D;
    					n->mode = B;
    					n->defexpr = NULL;
    					n->location = LOC(B);
    					A = n;
}

func_arg(A) ::= param_name(B) arg_class(C) func_type(D). {
    FunctionParameter *n = makeNode(FunctionParameter);

    					n->name = B;
    					n->argType = D;
    					n->mode = C;
    					n->defexpr = NULL;
    					n->location = LOC(B);
    					A = n;
}

func_arg(A) ::= param_name(B) func_type(C). {
    FunctionParameter *n = makeNode(FunctionParameter);

    					n->name = B;
    					n->argType = C;
    					n->mode = FUNC_PARAM_DEFAULT;
    					n->defexpr = NULL;
    					n->location = LOC(B);
    					A = n;
}

func_arg(A) ::= arg_class(B) func_type(C). {
    FunctionParameter *n = makeNode(FunctionParameter);

    					n->name = NULL;
    					n->argType = C;
    					n->mode = B;
    					n->defexpr = NULL;
    					n->location = LOC(B);
    					A = n;
}

func_arg(A) ::= func_type(B). {
    FunctionParameter *n = makeNode(FunctionParameter);

    					n->name = NULL;
    					n->argType = B;
    					n->mode = FUNC_PARAM_DEFAULT;
    					n->defexpr = NULL;
    					n->location = LOC(B);
    					A = n;
}

/* ----- arg_class ----- */

arg_class(A) ::= IN_P. {
    A = FUNC_PARAM_IN;
}

arg_class(A) ::= OUT_P. {
    A = FUNC_PARAM_OUT;
}

arg_class(A) ::= INOUT. {
    A = FUNC_PARAM_INOUT;
}

arg_class(A) ::= IN_P OUT_P. {
    A = FUNC_PARAM_INOUT;
}

arg_class(A) ::= VARIADIC. {
    A = FUNC_PARAM_VARIADIC;
}

/* ----- param_name ----- */

param_name(A) ::= type_function_name(B).

/* ----- func_return ----- */

func_return(A) ::= func_type(B). {
    A = B;
}

/* ----- func_type ----- */

func_type(A) ::= typename(B). {
    A = B;
}

func_type(A) ::= type_function_name(B) attrs(C) PERCENT TYPE_P. {
    A = makeTypeNameFromNameList(lcons(makeString(B), C));
    					A->pct_type = true;
    					A->location = LOC(B);
}

func_type(A) ::= SETOF type_function_name(C) attrs(D) PERCENT TYPE_P. {
    A = makeTypeNameFromNameList(lcons(makeString(C), D));
    					A->pct_type = true;
    					A->setof = true;
    					A->location = LOC(C);
}

/* ----- func_arg_with_default ----- */

func_arg_with_default(A) ::= func_arg(B). {
    A = B;
}

func_arg_with_default(A) ::= func_arg(B) DEFAULT a_expr(D). {
    A = B;
    					A->defexpr = D;
}

func_arg_with_default(A) ::= func_arg(B) EQ a_expr(D). {
    A = B;
    					A->defexpr = D;
}

/* ----- aggr_arg ----- */

aggr_arg(A) ::= func_arg(B). {
    if (!(B->mode == FUNC_PARAM_DEFAULT ||
    						  B->mode == FUNC_PARAM_IN ||
    						  B->mode == FUNC_PARAM_VARIADIC))
    						ereport(ERROR,
    								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								 errmsg("aggregates cannot have output arguments"),
    								 parser_errposition(LOC(B))));
    					A = B;
}

/* ----- aggr_args ----- */

aggr_args(A) ::= LPAREN STAR RPAREN. {
    A = list_make2(NIL, makeInteger(-1));
}

aggr_args(A) ::= LPAREN aggr_args_list(C) RPAREN. {
    A = list_make2(C, makeInteger(-1));
}

aggr_args(A) ::= LPAREN ORDER BY aggr_args_list(E) RPAREN. {
    A = list_make2(E, makeInteger(0));
}

aggr_args(A) ::= LPAREN aggr_args_list(C) ORDER BY aggr_args_list(F) RPAREN. {
    A = makeOrderedSetArgs(C, F, yyscanner);
}

/* ----- aggr_args_list ----- */

aggr_args_list(A) ::= aggr_arg(B). {
    A = list_make1(B);
}

aggr_args_list(A) ::= aggr_args_list(B) COMMA aggr_arg(D). {
    A = lappend(B, D);
}

/* ----- aggregate_with_argtypes ----- */

aggregate_with_argtypes(A) ::= func_name(B) aggr_args(C). {
    ObjectWithArgs *n = makeNode(ObjectWithArgs);

    					n->objname = B;
    					n->objargs = extractAggrArgTypes(C);
    					n->objfuncargs = (List *) linitial(C);
    					A = n;
}

/* ----- aggregate_with_argtypes_list ----- */

aggregate_with_argtypes_list(A) ::= aggregate_with_argtypes(B). {
    A = list_make1(B);
}

aggregate_with_argtypes_list(A) ::= aggregate_with_argtypes_list(B) COMMA aggregate_with_argtypes(D). {
    A = lappend(B, D);
}

/* ----- opt_createfunc_opt_list ----- */

opt_createfunc_opt_list(A) ::= createfunc_opt_list(B).

opt_createfunc_opt_list(A) ::= . {
    A = NIL;
}

/* ----- createfunc_opt_list ----- */

createfunc_opt_list(A) ::= createfunc_opt_item(B). {
    A = list_make1(B);
}

createfunc_opt_list(A) ::= createfunc_opt_list(B) createfunc_opt_item(C). {
    A = lappend(B, C);
}

/* ----- common_func_opt_item ----- */

common_func_opt_item(A) ::= CALLED ON NULL_P INPUT_P. {
    A = makeDefElem("strict", (Node *) makeBoolean(false), LOC(B));
}

common_func_opt_item(A) ::= RETURNS NULL_P ON NULL_P INPUT_P. {
    A = makeDefElem("strict", (Node *) makeBoolean(true), LOC(B));
}

common_func_opt_item(A) ::= STRICT_P. {
    A = makeDefElem("strict", (Node *) makeBoolean(true), LOC(B));
}

common_func_opt_item(A) ::= IMMUTABLE. {
    A = makeDefElem("volatility", (Node *) makeString("immutable"), LOC(B));
}

common_func_opt_item(A) ::= STABLE. {
    A = makeDefElem("volatility", (Node *) makeString("stable"), LOC(B));
}

common_func_opt_item(A) ::= VOLATILE. {
    A = makeDefElem("volatility", (Node *) makeString("volatile"), LOC(B));
}

common_func_opt_item(A) ::= EXTERNAL SECURITY DEFINER. {
    A = makeDefElem("security", (Node *) makeBoolean(true), LOC(B));
}

common_func_opt_item(A) ::= EXTERNAL SECURITY INVOKER. {
    A = makeDefElem("security", (Node *) makeBoolean(false), LOC(B));
}

common_func_opt_item(A) ::= SECURITY DEFINER. {
    A = makeDefElem("security", (Node *) makeBoolean(true), LOC(B));
}

common_func_opt_item(A) ::= SECURITY INVOKER. {
    A = makeDefElem("security", (Node *) makeBoolean(false), LOC(B));
}

common_func_opt_item(A) ::= LEAKPROOF. {
    A = makeDefElem("leakproof", (Node *) makeBoolean(true), LOC(B));
}

common_func_opt_item(A) ::= NOT LEAKPROOF. {
    A = makeDefElem("leakproof", (Node *) makeBoolean(false), LOC(B));
}

common_func_opt_item(A) ::= COST numericOnly(C). {
    A = makeDefElem("cost", (Node *) C, LOC(B));
}

common_func_opt_item(A) ::= ROWS numericOnly(C). {
    A = makeDefElem("rows", (Node *) C, LOC(B));
}

common_func_opt_item(A) ::= SUPPORT any_name(C). {
    A = makeDefElem("support", (Node *) C, LOC(B));
}

common_func_opt_item(A) ::= functionSetResetClause(B). {
    A = makeDefElem("set", (Node *) B, LOC(B));
}

common_func_opt_item(A) ::= PARALLEL colId(C). {
    A = makeDefElem("parallel", (Node *) makeString(C), LOC(B));
}

/* ----- createfunc_opt_item ----- */

createfunc_opt_item(A) ::= AS func_as(C). {
    A = makeDefElem("as", (Node *) C, LOC(B));
}

createfunc_opt_item(A) ::= LANGUAGE nonReservedWord_or_Sconst(C). {
    A = makeDefElem("language", (Node *) makeString(C), LOC(B));
}

createfunc_opt_item(A) ::= TRANSFORM transform_type_list(C). {
    A = makeDefElem("transform", (Node *) C, LOC(B));
}

createfunc_opt_item(A) ::= WINDOW. {
    A = makeDefElem("window", (Node *) makeBoolean(true), LOC(B));
}

createfunc_opt_item(A) ::= common_func_opt_item(B). {
    A = B;
}

/* ----- func_as ----- */

func_as(A) ::= sconst(B). {
    A = list_make1(makeString(B));
}

func_as(A) ::= sconst(B) COMMA sconst(D). {
    A = list_make2(makeString(B), makeString(D));
}

/* ----- returnStmt ----- */

returnStmt(A) ::= RETURN a_expr(C). {
    returnStmt *r = makeNode(returnStmt);

    					r->returnval = (Node *) C;
    					A = (Node *) r;
}

/* ----- opt_routine_body ----- */

opt_routine_body(A) ::= returnStmt(B). {
    A = B;
}

opt_routine_body(A) ::= BEGIN_P ATOMIC routine_body_stmt_list(D) END_P. {
    A = (Node *) list_make1(D);
}

opt_routine_body(A) ::= . {
    A = NULL;
}

/* ----- routine_body_stmt_list ----- */

routine_body_stmt_list(A) ::= routine_body_stmt_list(B) routine_body_stmt(C) SEMICOLON. {
    if (C != NULL)
    						A = lappend(B, C);
    					else
    						A = B;
}

routine_body_stmt_list(A) ::= . {
    A = NIL;
}

/* ----- routine_body_stmt ----- */

routine_body_stmt(A) ::= stmt(B).

routine_body_stmt(A) ::= returnStmt(B).

/* ----- transform_type_list ----- */

transform_type_list(A) ::= FOR TYPE_P typename(D). {
    A = list_make1(D);
}

transform_type_list(A) ::= transform_type_list(B) COMMA FOR TYPE_P typename(F). {
    A = lappend(B, F);
}

/* ----- opt_definition ----- */

opt_definition(A) ::= WITH definition(C). {
    A = C;
}

opt_definition(A) ::= . {
    A = NIL;
}

/* ----- table_func_column ----- */

table_func_column(A) ::= param_name(B) func_type(C). {
    FunctionParameter *n = makeNode(FunctionParameter);

    					n->name = B;
    					n->argType = C;
    					n->mode = FUNC_PARAM_TABLE;
    					n->defexpr = NULL;
    					n->location = LOC(B);
    					A = n;
}

/* ----- table_func_column_list ----- */

table_func_column_list(A) ::= table_func_column(B). {
    A = list_make1(B);
}

table_func_column_list(A) ::= table_func_column_list(B) COMMA table_func_column(D). {
    A = lappend(B, D);
}

/* ----- alterFunctionStmt ----- */

alterFunctionStmt(A) ::= ALTER FUNCTION function_with_argtypes(D) alterfunc_opt_list(E) opt_restrict(F). {
    alterFunctionStmt *n = makeNode(alterFunctionStmt);

    					n->objtype = OBJECT_FUNCTION;
    					n->func = D;
    					n->actions = E;
    					A = (Node *) n;
}

alterFunctionStmt(A) ::= ALTER PROCEDURE function_with_argtypes(D) alterfunc_opt_list(E) opt_restrict(F). {
    alterFunctionStmt *n = makeNode(alterFunctionStmt);

    					n->objtype = OBJECT_PROCEDURE;
    					n->func = D;
    					n->actions = E;
    					A = (Node *) n;
}

alterFunctionStmt(A) ::= ALTER ROUTINE function_with_argtypes(D) alterfunc_opt_list(E) opt_restrict(F). {
    alterFunctionStmt *n = makeNode(alterFunctionStmt);

    					n->objtype = OBJECT_ROUTINE;
    					n->func = D;
    					n->actions = E;
    					A = (Node *) n;
}

/* ----- alterfunc_opt_list ----- */

alterfunc_opt_list(A) ::= common_func_opt_item(B). {
    A = list_make1(B);
}

alterfunc_opt_list(A) ::= alterfunc_opt_list(B) common_func_opt_item(C). {
    A = lappend(B, C);
}

/* ----- opt_restrict ----- */

opt_restrict(A) ::= RESTRICT.

/* ----- removeFuncStmt ----- */

removeFuncStmt(A) ::= DROP FUNCTION function_with_argtypes_list(D) opt_drop_behavior(E). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_FUNCTION;
    					n->objects = D;
    					n->behavior = E;
    					n->missing_ok = false;
    					n->concurrent = false;
    					A = (Node *) n;
}

removeFuncStmt(A) ::= DROP FUNCTION IF_P EXISTS function_with_argtypes_list(F) opt_drop_behavior(G). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_FUNCTION;
    					n->objects = F;
    					n->behavior = G;
    					n->missing_ok = true;
    					n->concurrent = false;
    					A = (Node *) n;
}

removeFuncStmt(A) ::= DROP PROCEDURE function_with_argtypes_list(D) opt_drop_behavior(E). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_PROCEDURE;
    					n->objects = D;
    					n->behavior = E;
    					n->missing_ok = false;
    					n->concurrent = false;
    					A = (Node *) n;
}

removeFuncStmt(A) ::= DROP PROCEDURE IF_P EXISTS function_with_argtypes_list(F) opt_drop_behavior(G). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_PROCEDURE;
    					n->objects = F;
    					n->behavior = G;
    					n->missing_ok = true;
    					n->concurrent = false;
    					A = (Node *) n;
}

removeFuncStmt(A) ::= DROP ROUTINE function_with_argtypes_list(D) opt_drop_behavior(E). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_ROUTINE;
    					n->objects = D;
    					n->behavior = E;
    					n->missing_ok = false;
    					n->concurrent = false;
    					A = (Node *) n;
}

removeFuncStmt(A) ::= DROP ROUTINE IF_P EXISTS function_with_argtypes_list(F) opt_drop_behavior(G). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_ROUTINE;
    					n->objects = F;
    					n->behavior = G;
    					n->missing_ok = true;
    					n->concurrent = false;
    					A = (Node *) n;
}

/* ----- removeAggrStmt ----- */

removeAggrStmt(A) ::= DROP AGGREGATE aggregate_with_argtypes_list(D) opt_drop_behavior(E). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_AGGREGATE;
    					n->objects = D;
    					n->behavior = E;
    					n->missing_ok = false;
    					n->concurrent = false;
    					A = (Node *) n;
}

removeAggrStmt(A) ::= DROP AGGREGATE IF_P EXISTS aggregate_with_argtypes_list(F) opt_drop_behavior(G). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_AGGREGATE;
    					n->objects = F;
    					n->behavior = G;
    					n->missing_ok = true;
    					n->concurrent = false;
    					A = (Node *) n;
}

/* ----- removeOperStmt ----- */

removeOperStmt(A) ::= DROP OPERATOR operator_with_argtypes_list(D) opt_drop_behavior(E). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_OPERATOR;
    					n->objects = D;
    					n->behavior = E;
    					n->missing_ok = false;
    					n->concurrent = false;
    					A = (Node *) n;
}

removeOperStmt(A) ::= DROP OPERATOR IF_P EXISTS operator_with_argtypes_list(F) opt_drop_behavior(G). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_OPERATOR;
    					n->objects = F;
    					n->behavior = G;
    					n->missing_ok = true;
    					n->concurrent = false;
    					A = (Node *) n;
}

/* ----- oper_argtypes ----- */

oper_argtypes(A) ::= LPAREN typename(C) RPAREN. {
    ereport(ERROR,
    						   (errcode(ERRCODE_SYNTAX_ERROR),
    							errmsg("missing argument"),
    							errhint("Use NONE to denote the missing argument of a unary operator."),
    							parser_errposition(LOC(D))));
}

oper_argtypes(A) ::= LPAREN typename(C) COMMA typename(E) RPAREN. {
    A = list_make2(C, E);
}

oper_argtypes(A) ::= LPAREN NONE COMMA typename(E) RPAREN. {
    A = list_make2(NULL, E);
}

oper_argtypes(A) ::= LPAREN typename(C) COMMA NONE RPAREN. {
    A = list_make2(C, NULL);
}

/* ----- any_operator ----- */

any_operator(A) ::= all_Op(B). {
    A = list_make1(makeString(B));
}

any_operator(A) ::= colId(B) DOT any_operator(D). {
    A = lcons(makeString(B), D);
}

/* ----- operator_with_argtypes_list ----- */

operator_with_argtypes_list(A) ::= operator_with_argtypes(B). {
    A = list_make1(B);
}

operator_with_argtypes_list(A) ::= operator_with_argtypes_list(B) COMMA operator_with_argtypes(D). {
    A = lappend(B, D);
}

/* ----- operator_with_argtypes ----- */

operator_with_argtypes(A) ::= any_operator(B) oper_argtypes(C). {
    ObjectWithArgs *n = makeNode(ObjectWithArgs);

    					n->objname = B;
    					n->objargs = C;
    					A = n;
}

/* ----- doStmt ----- */

doStmt(A) ::= DO dostmt_opt_list(C). {
    doStmt *n = makeNode(doStmt);

    					n->args = C;
    					A = (Node *) n;
}

/* ----- dostmt_opt_list ----- */

dostmt_opt_list(A) ::= dostmt_opt_item(B). {
    A = list_make1(B);
}

dostmt_opt_list(A) ::= dostmt_opt_list(B) dostmt_opt_item(C). {
    A = lappend(B, C);
}

/* ----- dostmt_opt_item ----- */

dostmt_opt_item(A) ::= sconst(B). {
    A = makeDefElem("as", (Node *) makeString(B), LOC(B));
}

dostmt_opt_item(A) ::= LANGUAGE nonReservedWord_or_Sconst(C). {
    A = makeDefElem("language", (Node *) makeString(C), LOC(B));
}

/* ----- createCastStmt ----- */

createCastStmt(A) ::= CREATE CAST LPAREN typename(E) AS typename(G) RPAREN WITH FUNCTION function_with_argtypes(K) cast_context(L). {
    createCastStmt *n = makeNode(createCastStmt);

    					n->sourcetype = E;
    					n->targettype = G;
    					n->func = K;
    					n->context = (CoercionContext) L;
    					n->inout = false;
    					A = (Node *) n;
}

createCastStmt(A) ::= CREATE CAST LPAREN typename(E) AS typename(G) RPAREN WITHOUT FUNCTION cast_context(K). {
    createCastStmt *n = makeNode(createCastStmt);

    					n->sourcetype = E;
    					n->targettype = G;
    					n->func = NULL;
    					n->context = (CoercionContext) K;
    					n->inout = false;
    					A = (Node *) n;
}

createCastStmt(A) ::= CREATE CAST LPAREN typename(E) AS typename(G) RPAREN WITH INOUT cast_context(K). {
    createCastStmt *n = makeNode(createCastStmt);

    					n->sourcetype = E;
    					n->targettype = G;
    					n->func = NULL;
    					n->context = (CoercionContext) K;
    					n->inout = true;
    					A = (Node *) n;
}

/* ----- cast_context ----- */

cast_context(A) ::= AS IMPLICIT_P. {
    A = COERCION_IMPLICIT;
}

cast_context(A) ::= AS ASSIGNMENT. {
    A = COERCION_ASSIGNMENT;
}

cast_context(A) ::= . {
    A = COERCION_EXPLICIT;
}

/* ----- dropCastStmt ----- */

createTransformStmt(A) ::= CREATE opt_or_replace(C) TRANSFORM FOR typename(F) LANGUAGE name(H) LPAREN transform_element_list(J) RPAREN. {
    createTransformStmt *n = makeNode(createTransformStmt);

    					n->replace = C;
    					n->type_name = F;
    					n->lang = H;
    					n->fromsql = linitial(J);
    					n->tosql = lsecond(J);
    					A = (Node *) n;
}

/* ----- transform_element_list ----- */

transform_element_list(A) ::= FROM SQL_P WITH FUNCTION function_with_argtypes(F) COMMA TO SQL_P WITH FUNCTION function_with_argtypes(L). {
    A = list_make2(F, L);
}

transform_element_list(A) ::= TO SQL_P WITH FUNCTION function_with_argtypes(F) COMMA FROM SQL_P WITH FUNCTION function_with_argtypes(L). {
    A = list_make2(L, F);
}

transform_element_list(A) ::= FROM SQL_P WITH FUNCTION function_with_argtypes(F). {
    A = list_make2(F, NULL);
}

transform_element_list(A) ::= TO SQL_P WITH FUNCTION function_with_argtypes(F). {
    A = list_make2(NULL, F);
}

/* ----- dropTransformStmt ----- */
```

