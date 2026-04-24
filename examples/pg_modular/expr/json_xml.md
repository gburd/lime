# JSON and XML


JSON and XML expression support: XMLTABLE, JSON_TABLE,
JSON constructors, JSON predicates, XML namespaces,
and XMLEXISTS.


```yaml
name: pg-json-xml
version: 17.0.0
description: JSON and XML table/query/constructor expressions
provides: [pg-json-xml]
depends: [pg-type-decls, pg-expressions, pg-typenames]
```

## Production Rules

```lime rules
xmltable(A) ::= XMLTABLE LPAREN c_expr(D) xmlexists_argument(E) COLUMNS xmltable_column_list(G) RPAREN. {
    RangeTableFunc *n = makeNode(RangeTableFunc);

    					n->rowexpr = D;
    					n->docexpr = E;
    					n->columns = G;
    					n->namespaces = NIL;
    					n->location = LOC(B);
    					A = (Node *) n;
}

xmltable(A) ::= XMLTABLE LPAREN XMLNAMESPACES LPAREN xml_namespace_list(F) RPAREN COMMA c_expr(I) xmlexists_argument(J) COLUMNS xmltable_column_list(L) RPAREN. {
    RangeTableFunc *n = makeNode(RangeTableFunc);

    					n->rowexpr = I;
    					n->docexpr = J;
    					n->columns = L;
    					n->namespaces = F;
    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- xmltable_column_list ----- */

xmltable_column_list(A) ::= xmltable_column_el(B). {
    A = list_make1(B);
}

xmltable_column_list(A) ::= xmltable_column_list(B) COMMA xmltable_column_el(D). {
    A = lappend(B, D);
}

/* ----- xmltable_column_el ----- */

xmltable_column_el(A) ::= colId(B) typename(C). {
    RangeTableFuncCol *fc = makeNode(RangeTableFuncCol);

    					fc->colname = B;
    					fc->for_ordinality = false;
    					fc->typeName = C;
    					fc->is_not_null = false;
    					fc->colexpr = NULL;
    					fc->coldefexpr = NULL;
    					fc->location = LOC(B);

    					A = (Node *) fc;
}

xmltable_column_el(A) ::= colId(B) typename(C) xmltable_column_option_list(D). {
    RangeTableFuncCol *fc = makeNode(RangeTableFuncCol);
    					ListCell   *option;
    					bool		nullability_seen = false;

    					fc->colname = B;
    					fc->typeName = C;
    					fc->for_ordinality = false;
    					fc->is_not_null = false;
    					fc->colexpr = NULL;
    					fc->coldefexpr = NULL;
    					fc->location = LOC(B);

    					foreach(option, D)
    					{
    						DefElem   *defel = (DefElem *) lfirst(option);

    						if (strcmp(defel->defname, "default") == 0)
    						{
    							if (fc->coldefexpr != NULL)
    								ereport(ERROR,
    										(errcode(ERRCODE_SYNTAX_ERROR),
    										 errmsg("only one DEFAULT value is allowed"),
    										 parser_errposition(defel->location)));
    							fc->coldefexpr = defel->arg;
    						}
    						else if (strcmp(defel->defname, "path") == 0)
    						{
    							if (fc->colexpr != NULL)
    								ereport(ERROR,
    										(errcode(ERRCODE_SYNTAX_ERROR),
    										 errmsg("only one PATH value per column is allowed"),
    										 parser_errposition(defel->location)));
    							fc->colexpr = defel->arg;
    						}
    						else if (strcmp(defel->defname, "__pg__is_not_null") == 0)
    						{
    							if (nullability_seen)
    								ereport(ERROR,
    										(errcode(ERRCODE_SYNTAX_ERROR),
    										 errmsg("conflicting or redundant NULL / NOT NULL declarations for column \"%s\"", fc->colname),
    										 parser_errposition(defel->location)));
    							fc->is_not_null = boolVal(defel->arg);
    							nullability_seen = true;
    						}
    						else
    						{
    							ereport(ERROR,
    									(errcode(ERRCODE_SYNTAX_ERROR),
    									 errmsg("unrecognized column option \"%s\"",
    											defel->defname),
    									 parser_errposition(defel->location)));
    						}
    					}
    					A = (Node *) fc;
}

xmltable_column_el(A) ::= colId(B) FOR ORDINALITY. {
    RangeTableFuncCol *fc = makeNode(RangeTableFuncCol);

    					fc->colname = B;
    					fc->for_ordinality = true;

    					fc->location = LOC(B);

    					A = (Node *) fc;
}

/* ----- xmltable_column_option_list ----- */

xmltable_column_option_list(A) ::= xmltable_column_option_el(B). {
    A = list_make1(B);
}

xmltable_column_option_list(A) ::= xmltable_column_option_list(B) xmltable_column_option_el(C). {
    A = lappend(B, C);
}

/* ----- xmltable_column_option_el ----- */

xmltable_column_option_el(A) ::= IDENT b_expr(C). {
    if (strcmp(B, "__pg__is_not_null") == 0)
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("option name \"%s\" cannot be used in XMLTABLE", B),
    								 parser_errposition(LOC(B))));
    					A = makeDefElem(B, C, LOC(B));
}

xmltable_column_option_el(A) ::= DEFAULT b_expr(C). {
    A = makeDefElem("default", C, LOC(B));
}

xmltable_column_option_el(A) ::= NOT NULL_P. {
    A = makeDefElem("__pg__is_not_null", (Node *) makeBoolean(true), LOC(B));
}

xmltable_column_option_el(A) ::= NULL_P. {
    A = makeDefElem("__pg__is_not_null", (Node *) makeBoolean(false), LOC(B));
}

xmltable_column_option_el(A) ::= PATH b_expr(C). {
    A = makeDefElem("path", C, LOC(B));
}

/* ----- xml_namespace_list ----- */

xml_namespace_list(A) ::= xml_namespace_el(B). {
    A = list_make1(B);
}

xml_namespace_list(A) ::= xml_namespace_list(B) COMMA xml_namespace_el(D). {
    A = lappend(B, D);
}

/* ----- xml_namespace_el ----- */

xml_namespace_el(A) ::= b_expr(B) AS colLabel(D). {
    A = makeNode(ResTarget);
    					A->name = D;
    					A->indirection = NIL;
    					A->val = B;
    					A->location = LOC(B);
}

xml_namespace_el(A) ::= DEFAULT b_expr(C). {
    A = makeNode(ResTarget);
    					A->name = NULL;
    					A->indirection = NIL;
    					A->val = C;
    					A->location = LOC(B);
}

/* ----- json_table ----- */

json_table(A) ::= JSON_TABLE LPAREN json_value_expr(D) COMMA a_expr(F) json_table_path_name_opt(G) json_passing_clause_opt(H) COLUMNS LPAREN json_table_column_definition_list(K) RPAREN json_on_error_clause_opt(M) RPAREN. {
    JsonTable *n = makeNode(JsonTable);
    					char	  *pathstring;

    					n->context_item = (JsonValueExpr *) D;
    					if (!IsA(F, A_Const) ||
    						castNode(A_Const, F)->val.node.type != T_String)
    						ereport(ERROR,
    								errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								errmsg("only string constants are supported in JSON_TABLE path specification"),
    								parser_errposition(LOC(F)));
    					pathstring = castNode(A_Const, F)->val.sval.sval;
    					n->pathspec = makeJsonTablePathSpec(pathstring, G, LOC(F), LOC(G));
    					n->passing = H;
    					n->columns = K;
    					n->on_error = (JsonBehavior *) M;
    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- json_table_path_name_opt ----- */

json_table_path_name_opt(A) ::= AS name(C). {
    A = C;
}

json_table_path_name_opt(A) ::= . {
    A = NULL;
}

/* ----- json_table_column_definition_list ----- */

json_table_column_definition_list(A) ::= json_table_column_definition(B). {
    A = list_make1(B);
}

json_table_column_definition_list(A) ::= json_table_column_definition_list(B) COMMA json_table_column_definition(D). {
    A = lappend(B, D);
}

/* ----- json_table_column_definition ----- */

json_table_column_definition(A) ::= colId(B) FOR ORDINALITY. {
    JsonTableColumn *n = makeNode(JsonTableColumn);

    					n->coltype = JTC_FOR_ORDINALITY;
    					n->name = B;
    					n->location = LOC(B);
    					A = (Node *) n;
}

json_table_column_definition(A) ::= colId(B) typename(C) json_table_column_path_clause_opt(D) json_wrapper_behavior(E) json_quotes_clause_opt(F) json_behavior_clause_opt(G). {
    JsonTableColumn *n = makeNode(JsonTableColumn);

    					n->coltype = JTC_REGULAR;
    					n->name = B;
    					n->typeName = C;
    					n->format = makeJsonFormat(JS_FORMAT_DEFAULT, JS_ENC_DEFAULT, -1);
    					n->pathspec = (JsonTablePathSpec *) D;
    					n->wrapper = E;
    					n->quotes = F;
    					n->on_empty = (JsonBehavior *) linitial(G);
    					n->on_error = (JsonBehavior *) lsecond(G);
    					n->location = LOC(B);
    					A = (Node *) n;
}

json_table_column_definition(A) ::= colId(B) typename(C) json_format_clause(D) json_table_column_path_clause_opt(E) json_wrapper_behavior(F) json_quotes_clause_opt(G) json_behavior_clause_opt(H). {
    JsonTableColumn *n = makeNode(JsonTableColumn);

    					n->coltype = JTC_FORMATTED;
    					n->name = B;
    					n->typeName = C;
    					n->format = (JsonFormat *) D;
    					n->pathspec = (JsonTablePathSpec *) E;
    					n->wrapper = F;
    					n->quotes = G;
    					n->on_empty = (JsonBehavior *) linitial(H);
    					n->on_error = (JsonBehavior *) lsecond(H);
    					n->location = LOC(B);
    					A = (Node *) n;
}

json_table_column_definition(A) ::= colId(B) typename(C) EXISTS json_table_column_path_clause_opt(E) json_on_error_clause_opt(F). {
    JsonTableColumn *n = makeNode(JsonTableColumn);

    					n->coltype = JTC_EXISTS;
    					n->name = B;
    					n->typeName = C;
    					n->format = makeJsonFormat(JS_FORMAT_DEFAULT, JS_ENC_DEFAULT, -1);
    					n->wrapper = JSW_NONE;
    					n->quotes = JS_QUOTES_UNSPEC;
    					n->pathspec = (JsonTablePathSpec *) E;
    					n->on_empty = NULL;
    					n->on_error = (JsonBehavior *) F;
    					n->location = LOC(B);
    					A = (Node *) n;
}

json_table_column_definition(A) ::= NESTED path_opt(C) sconst(D) COLUMNS LPAREN json_table_column_definition_list(G) RPAREN. {
    JsonTableColumn *n = makeNode(JsonTableColumn);

    					n->coltype = JTC_NESTED;
    					n->pathspec = (JsonTablePathSpec *)
    						makeJsonTablePathSpec(D, NULL, LOC(D), -1);
    					n->columns = G;
    					n->location = LOC(B);
    					A = (Node *) n;
}

json_table_column_definition(A) ::= NESTED path_opt(C) sconst(D) AS name(F) COLUMNS LPAREN json_table_column_definition_list(I) RPAREN. {
    JsonTableColumn *n = makeNode(JsonTableColumn);

    					n->coltype = JTC_NESTED;
    					n->pathspec = (JsonTablePathSpec *)
    						makeJsonTablePathSpec(D, F, LOC(D), LOC(F));
    					n->columns = I;
    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- path_opt ----- */

path_opt(A) ::= PATH.

/* ----- json_table_column_path_clause_opt ----- */

json_table_column_path_clause_opt(A) ::= PATH sconst(C). {
    A = (Node *) makeJsonTablePathSpec(C, NULL, LOC(C), -1);
}

json_table_column_path_clause_opt(A) ::= . {
    A = NULL;
}

/* ----- typename ----- */

xml_root_version(A) ::= VERSION_P a_expr(C). {
    A = C;
}

xml_root_version(A) ::= VERSION_P NO VALUE_P. {
    A = makeNullAConst(-1);
}

/* ----- opt_xml_root_standalone ----- */

opt_xml_root_standalone(A) ::= COMMA STANDALONE_P YES_P. {
    A = makeIntConst(XML_STANDALONE_YES, -1);
}

opt_xml_root_standalone(A) ::= COMMA STANDALONE_P NO. {
    A = makeIntConst(XML_STANDALONE_NO, -1);
}

opt_xml_root_standalone(A) ::= COMMA STANDALONE_P NO VALUE_P. {
    A = makeIntConst(XML_STANDALONE_NO_VALUE, -1);
}

opt_xml_root_standalone(A) ::= . {
    A = makeIntConst(XML_STANDALONE_OMITTED, -1);
}

/* ----- xml_attributes ----- */

xml_attributes(A) ::= XMLATTRIBUTES LPAREN labeled_expr_list(D) RPAREN. {
    A = D;
}

/* ----- labeled_expr_list ----- */

labeled_expr_list(A) ::= labeled_expr(B). {
    A = list_make1(B);
}

labeled_expr_list(A) ::= labeled_expr_list(B) COMMA labeled_expr(D). {
    A = lappend(B, D);
}

/* ----- labeled_expr ----- */

labeled_expr(A) ::= a_expr(B) AS colLabel(D). {
    A = makeNode(ResTarget);
    					A->name = D;
    					A->indirection = NIL;
    					A->val = (Node *) B;
    					A->location = LOC(B);
}

labeled_expr(A) ::= a_expr(B). {
    A = makeNode(ResTarget);
    					A->name = NULL;
    					A->indirection = NIL;
    					A->val = (Node *) B;
    					A->location = LOC(B);
}

/* ----- document_or_content ----- */

document_or_content(A) ::= DOCUMENT_P. {
    A = XMLOPTION_DOCUMENT;
}

document_or_content(A) ::= CONTENT_P. {
    A = XMLOPTION_CONTENT;
}

/* ----- xml_indent_option ----- */

xml_indent_option(A) ::= INDENT. {
    A = true;
}

xml_indent_option(A) ::= NO INDENT. {
    A = false;
}

xml_indent_option(A) ::= . {
    A = false;
}

/* ----- xml_whitespace_option ----- */

xml_whitespace_option(A) ::= PRESERVE WHITESPACE_P. {
    A = true;
}

xml_whitespace_option(A) ::= STRIP_P WHITESPACE_P. {
    A = false;
}

xml_whitespace_option(A) ::= . {
    A = false;
}

/* ----- xmlexists_argument ----- */

xmlexists_argument(A) ::= PASSING c_expr(C). {
    A = C;
}

xmlexists_argument(A) ::= PASSING c_expr(C) xml_passing_mech(D). {
    A = C;
}

xmlexists_argument(A) ::= PASSING xml_passing_mech(C) c_expr(D). {
    A = D;
}

xmlexists_argument(A) ::= PASSING xml_passing_mech(C) c_expr(D) xml_passing_mech(E). {
    A = D;
}

/* ----- xml_passing_mech ----- */

xml_passing_mech(A) ::= BY REF_P.

xml_passing_mech(A) ::= BY VALUE_P.

/* ----- waitStmt ----- */

json_passing_clause_opt(A) ::= PASSING json_arguments(C). {
    A = C;
}

json_passing_clause_opt(A) ::= . {
    A = NIL;
}

/* ----- json_arguments ----- */

json_arguments(A) ::= json_argument(B). {
    A = list_make1(B);
}

json_arguments(A) ::= json_arguments(B) COMMA json_argument(D). {
    A = lappend(B, D);
}

/* ----- json_argument ----- */

json_argument(A) ::= json_value_expr(B) AS colLabel(D). {
    JsonArgument *n = makeNode(JsonArgument);

    				n->val = (JsonValueExpr *) B;
    				n->name = D;
    				A = (Node *) n;
}

/* ----- json_wrapper_behavior ----- */

json_wrapper_behavior(A) ::= WITHOUT WRAPPER. {
    A = JSW_NONE;
}

json_wrapper_behavior(A) ::= WITHOUT ARRAY WRAPPER. {
    A = JSW_NONE;
}

json_wrapper_behavior(A) ::= WITH WRAPPER. {
    A = JSW_UNCONDITIONAL;
}

json_wrapper_behavior(A) ::= WITH ARRAY WRAPPER. {
    A = JSW_UNCONDITIONAL;
}

json_wrapper_behavior(A) ::= WITH CONDITIONAL ARRAY WRAPPER. {
    A = JSW_CONDITIONAL;
}

json_wrapper_behavior(A) ::= WITH UNCONDITIONAL ARRAY WRAPPER. {
    A = JSW_UNCONDITIONAL;
}

json_wrapper_behavior(A) ::= WITH CONDITIONAL WRAPPER. {
    A = JSW_CONDITIONAL;
}

json_wrapper_behavior(A) ::= WITH UNCONDITIONAL WRAPPER. {
    A = JSW_UNCONDITIONAL;
}

json_wrapper_behavior(A) ::= . {
    A = JSW_UNSPEC;
}

/* ----- json_behavior ----- */

json_behavior(A) ::= DEFAULT a_expr(C). {
    A = (Node *) makeJsonBehavior(JSON_BEHAVIOR_DEFAULT, C, LOC(B));
}

json_behavior(A) ::= json_behavior_type(B). {
    A = (Node *) makeJsonBehavior(B, NULL, LOC(B));
}

/* ----- json_behavior_type ----- */

json_behavior_type(A) ::= ERROR_P. {
    A = JSON_BEHAVIOR_ERROR;
}

json_behavior_type(A) ::= NULL_P. {
    A = JSON_BEHAVIOR_NULL;
}

json_behavior_type(A) ::= TRUE_P. {
    A = JSON_BEHAVIOR_TRUE;
}

json_behavior_type(A) ::= FALSE_P. {
    A = JSON_BEHAVIOR_FALSE;
}

json_behavior_type(A) ::= UNKNOWN. {
    A = JSON_BEHAVIOR_UNKNOWN;
}

json_behavior_type(A) ::= EMPTY_P ARRAY. {
    A = JSON_BEHAVIOR_EMPTY_ARRAY;
}

json_behavior_type(A) ::= EMPTY_P OBJECT_P. {
    A = JSON_BEHAVIOR_EMPTY_OBJECT;
}

json_behavior_type(A) ::= EMPTY_P. {
    A = JSON_BEHAVIOR_EMPTY_ARRAY;
}

/* ----- json_behavior_clause_opt ----- */

json_behavior_clause_opt(A) ::= json_behavior(B) ON EMPTY_P. {
    A = list_make2(B, NULL);
}

json_behavior_clause_opt(A) ::= json_behavior(B) ON ERROR_P. {
    A = list_make2(NULL, B);
}

json_behavior_clause_opt(A) ::= json_behavior(B) ON EMPTY_P json_behavior(E) ON ERROR_P. {
    A = list_make2(B, E);
}

json_behavior_clause_opt(A) ::= . {
    A = list_make2(NULL, NULL);
}

/* ----- json_on_error_clause_opt ----- */

json_on_error_clause_opt(A) ::= json_behavior(B) ON ERROR_P. {
    A = B;
}

json_on_error_clause_opt(A) ::= . {
    A = NULL;
}

/* ----- json_value_expr ----- */

json_value_expr(A) ::= a_expr(B) json_format_clause_opt(C). {
    A = (Node *) makeJsonValueExpr((Expr *) B, NULL,
    												castNode(JsonFormat, C));
}

/* ----- json_format_clause ----- */

json_format_clause(A) ::= FORMAT_LA JSON ENCODING name(E). {
    int		encoding;

    					if (!pg_strcasecmp(E, "utf8"))
    						encoding = JS_ENC_UTF8;
    					else if (!pg_strcasecmp(E, "utf16"))
    						encoding = JS_ENC_UTF16;
    					else if (!pg_strcasecmp(E, "utf32"))
    						encoding = JS_ENC_UTF32;
    					else
    						ereport(ERROR,
    								(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
    								 errmsg("unrecognized JSON encoding: %s", E),
    								 parser_errposition(LOC(E))));

    					A = (Node *) makeJsonFormat(JS_FORMAT_JSON, encoding, LOC(B));
}

json_format_clause(A) ::= FORMAT_LA JSON. {
    A = (Node *) makeJsonFormat(JS_FORMAT_JSON, JS_ENC_DEFAULT, LOC(B));
}

/* ----- json_format_clause_opt ----- */

json_format_clause_opt(A) ::= json_format_clause(B). {
    A = B;
}

json_format_clause_opt(A) ::= . {
    A = (Node *) makeJsonFormat(JS_FORMAT_DEFAULT, JS_ENC_DEFAULT, -1);
}

/* ----- json_quotes_clause_opt ----- */

json_quotes_clause_opt(A) ::= KEEP QUOTES ON SCALAR STRING_P. {
    A = JS_QUOTES_KEEP;
}

json_quotes_clause_opt(A) ::= KEEP QUOTES. {
    A = JS_QUOTES_KEEP;
}

json_quotes_clause_opt(A) ::= OMIT QUOTES ON SCALAR STRING_P. {
    A = JS_QUOTES_OMIT;
}

json_quotes_clause_opt(A) ::= OMIT QUOTES. {
    A = JS_QUOTES_OMIT;
}

json_quotes_clause_opt(A) ::= . {
    A = JS_QUOTES_UNSPEC;
}

/* ----- json_returning_clause_opt ----- */

json_returning_clause_opt(A) ::= RETURNING typename(C) json_format_clause_opt(D). {
    JsonOutput *n = makeNode(JsonOutput);

    					n->typeName = C;
    					n->returning = makeNode(JsonReturning);
    					n->returning->format = (JsonFormat *) D;
    					A = (Node *) n;
}

json_returning_clause_opt(A) ::= . {
    A = NULL;
}

/* ----- json_predicate_type_constraint ----- */

json_predicate_type_constraint(A) ::= JSON. [UNBOUNDED] {
    A = JS_TYPE_ANY;
}

json_predicate_type_constraint(A) ::= JSON VALUE_P. {
    A = JS_TYPE_ANY;
}

json_predicate_type_constraint(A) ::= JSON ARRAY. {
    A = JS_TYPE_ARRAY;
}

json_predicate_type_constraint(A) ::= JSON OBJECT_P. {
    A = JS_TYPE_OBJECT;
}

json_predicate_type_constraint(A) ::= JSON SCALAR. {
    A = JS_TYPE_SCALAR;
}

/* ----- json_key_uniqueness_constraint_opt ----- */

json_key_uniqueness_constraint_opt(A) ::= WITH UNIQUE KEYS. {
    A = true;
}

json_key_uniqueness_constraint_opt(A) ::= WITH UNIQUE. [UNBOUNDED] {
    A = true;
}

json_key_uniqueness_constraint_opt(A) ::= WITHOUT UNIQUE KEYS. {
    A = false;
}

json_key_uniqueness_constraint_opt(A) ::= WITHOUT UNIQUE. [UNBOUNDED] {
    A = false;
}

json_key_uniqueness_constraint_opt(A) ::= . [UNBOUNDED] {
    A = false;
}

/* ----- json_name_and_value_list ----- */

json_name_and_value_list(A) ::= json_name_and_value(B). {
    A = list_make1(B);
}

json_name_and_value_list(A) ::= json_name_and_value_list(B) COMMA json_name_and_value(D). {
    A = lappend(B, D);
}

/* ----- json_name_and_value ----- */

json_name_and_value(A) ::= c_expr(B) VALUE_P json_value_expr(D). {
    A = makeJsonKeyValue(B, D);
}

json_name_and_value(A) ::= a_expr(B) COLON json_value_expr(D). {
    A = makeJsonKeyValue(B, D);
}

/* ----- json_object_constructor_null_clause_opt ----- */

json_object_constructor_null_clause_opt(A) ::= NULL_P ON NULL_P. {
    A = false;
}

json_object_constructor_null_clause_opt(A) ::= ABSENT ON NULL_P. {
    A = true;
}

json_object_constructor_null_clause_opt(A) ::= . {
    A = false;
}

/* ----- json_array_constructor_null_clause_opt ----- */

json_array_constructor_null_clause_opt(A) ::= NULL_P ON NULL_P. {
    A = false;
}

json_array_constructor_null_clause_opt(A) ::= ABSENT ON NULL_P. {
    A = true;
}

json_array_constructor_null_clause_opt(A) ::= . {
    A = true;
}

/* ----- json_value_expr_list ----- */

json_value_expr_list(A) ::= json_value_expr(B). {
    A = list_make1(B);
}

json_value_expr_list(A) ::= json_value_expr_list(B) COMMA json_value_expr(D). {
    A = lappend(B, D);
}

/* ----- json_aggregate_func ----- */

json_aggregate_func(A) ::= JSON_OBJECTAGG LPAREN json_name_and_value(D) json_object_constructor_null_clause_opt(E) json_key_uniqueness_constraint_opt(F) json_returning_clause_opt(G) RPAREN. {
    JsonObjectAgg *n = makeNode(JsonObjectAgg);

    					n->arg = (JsonKeyValue *) D;
    					n->absent_on_null = E;
    					n->unique = F;
    					n->constructor = makeNode(JsonAggConstructor);
    					n->constructor->output = (JsonOutput *) G;
    					n->constructor->agg_order = NULL;
    					n->constructor->location = LOC(B);
    					A = (Node *) n;
}

json_aggregate_func(A) ::= JSON_ARRAYAGG LPAREN json_value_expr(D) json_array_aggregate_order_by_clause_opt(E) json_array_constructor_null_clause_opt(F) json_returning_clause_opt(G) RPAREN. {
    JsonArrayAgg *n = makeNode(JsonArrayAgg);

    					n->arg = (JsonValueExpr *) D;
    					n->absent_on_null = F;
    					n->constructor = makeNode(JsonAggConstructor);
    					n->constructor->agg_order = E;
    					n->constructor->output = (JsonOutput *) G;
    					n->constructor->location = LOC(B);
    					A = (Node *) n;
}

/* ----- json_array_aggregate_order_by_clause_opt ----- */

json_array_aggregate_order_by_clause_opt(A) ::= ORDER BY sortby_list(D). {
    A = D;
}

json_array_aggregate_order_by_clause_opt(A) ::= . {
    A = NIL;
}

/* ----- graph_pattern ----- */
```

