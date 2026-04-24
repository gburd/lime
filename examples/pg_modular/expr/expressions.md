# Expressions


The core expression grammar: a_expr (general expressions),
b_expr (restricted for operator precedence), c_expr (primary
expressions), function calls, CASE, arrays, operators, and
literal constants.


```yaml
name: pg-expr
version: 17.0.0
description: Expression grammar (a_expr, b_expr, c_expr, operators, functions)
provides: [pg-expressions]
depends: [pg-type-decls, pg-typenames, pg-base-helpers, pg-keywords]
```

## Production Rules

```lime rules
a_expr(A) ::= c_expr(B). {
    A = B;
}

a_expr(A) ::= a_expr(B) TYPECAST typename(D). {
    A = makeTypeCast(B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) COLLATE any_name(D). {
    CollateClause *n = makeNode(CollateClause);

    					n->arg = B;
    					n->collname = D;
    					n->location = LOC(C);
    					A = (Node *) n;
}

a_expr(A) ::= a_expr(B) AT TIME ZONE a_expr(F). [AT] {
    A = (Node *) makeFuncCall(SystemFuncName("timezone"),
    											   list_make2(F, B),
    											   COERCE_SQL_SYNTAX,
    											   LOC(C));
}

a_expr(A) ::= a_expr(B) AT LOCAL. [AT] {
    A = (Node *) makeFuncCall(SystemFuncName("timezone"),
    											   list_make1(B),
    											   COERCE_SQL_SYNTAX,
    											   -1);
}

a_expr(A) ::= PLUS a_expr(C). [UMINUS] {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "+", NULL, C, LOC(B));
}

a_expr(A) ::= MINUS a_expr(C). [UMINUS] {
    A = doNegate(C, LOC(B));
}

a_expr(A) ::= a_expr(B) PLUS a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "+", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) MINUS a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "-", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) STAR a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "*", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) SLASH a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "/", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) PERCENT a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "%", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) CARET a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "^", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) LT a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "<", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) GT a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, ">", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) EQ a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "=", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) LESS_EQUALS a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "<=", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) GREATER_EQUALS a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, ">=", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) NOT_EQUALS a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "<>", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) RIGHT_ARROW a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "->", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) PIPE a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "|", B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) qual_Op(C) a_expr(D). [OP] {
    A = (Node *) makeA_Expr(AEXPR_OP, C, B, D, LOC(C));
}

a_expr(A) ::= qual_Op(B) a_expr(C). [OP] {
    A = (Node *) makeA_Expr(AEXPR_OP, B, NULL, C, LOC(B));
}

a_expr(A) ::= a_expr(B) AND a_expr(D). {
    A = makeAndExpr(B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) OR a_expr(D). {
    A = makeOrExpr(B, D, LOC(C));
}

a_expr(A) ::= NOT a_expr(C). {
    A = makeNotExpr(C, LOC(B));
}

a_expr(A) ::= NOT_LA a_expr(C). [NOT] {
    A = makeNotExpr(C, LOC(B));
}

a_expr(A) ::= a_expr(B) LIKE a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_LIKE, "~~",
    												   B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) LIKE a_expr(D) ESCAPE a_expr(F). [LIKE] {
    FuncCall   *n = makeFuncCall(SystemFuncName("like_escape"),
    												 list_make2(D, F),
    												 COERCE_EXPLICIT_CALL,
    												 LOC(C));
    					A = (Node *) makeSimpleA_Expr(AEXPR_LIKE, "~~",
    												   B, (Node *) n, LOC(C));
}

a_expr(A) ::= a_expr(B) NOT_LA LIKE a_expr(E). [NOT_LA] {
    A = (Node *) makeSimpleA_Expr(AEXPR_LIKE, "!~~",
    												   B, E, LOC(C));
}

a_expr(A) ::= a_expr(B) NOT_LA LIKE a_expr(E) ESCAPE a_expr(G). [NOT_LA] {
    FuncCall   *n = makeFuncCall(SystemFuncName("like_escape"),
    												 list_make2(E, G),
    												 COERCE_EXPLICIT_CALL,
    												 LOC(C));
    					A = (Node *) makeSimpleA_Expr(AEXPR_LIKE, "!~~",
    												   B, (Node *) n, LOC(C));
}

a_expr(A) ::= a_expr(B) ILIKE a_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_ILIKE, "~~*",
    												   B, D, LOC(C));
}

a_expr(A) ::= a_expr(B) ILIKE a_expr(D) ESCAPE a_expr(F). [ILIKE] {
    FuncCall   *n = makeFuncCall(SystemFuncName("like_escape"),
    												 list_make2(D, F),
    												 COERCE_EXPLICIT_CALL,
    												 LOC(C));
    					A = (Node *) makeSimpleA_Expr(AEXPR_ILIKE, "~~*",
    												   B, (Node *) n, LOC(C));
}

a_expr(A) ::= a_expr(B) NOT_LA ILIKE a_expr(E). [NOT_LA] {
    A = (Node *) makeSimpleA_Expr(AEXPR_ILIKE, "!~~*",
    												   B, E, LOC(C));
}

a_expr(A) ::= a_expr(B) NOT_LA ILIKE a_expr(E) ESCAPE a_expr(G). [NOT_LA] {
    FuncCall   *n = makeFuncCall(SystemFuncName("like_escape"),
    												 list_make2(E, G),
    												 COERCE_EXPLICIT_CALL,
    												 LOC(C));
    					A = (Node *) makeSimpleA_Expr(AEXPR_ILIKE, "!~~*",
    												   B, (Node *) n, LOC(C));
}

a_expr(A) ::= a_expr(B) SIMILAR TO a_expr(E). [SIMILAR] {
    FuncCall   *n = makeFuncCall(SystemFuncName("similar_to_escape"),
    												 list_make1(E),
    												 COERCE_EXPLICIT_CALL,
    												 LOC(C));
    					A = (Node *) makeSimpleA_Expr(AEXPR_SIMILAR, "~",
    												   B, (Node *) n, LOC(C));
}

a_expr(A) ::= a_expr(B) SIMILAR TO a_expr(E) ESCAPE a_expr(G). [SIMILAR] {
    FuncCall   *n = makeFuncCall(SystemFuncName("similar_to_escape"),
    												 list_make2(E, G),
    												 COERCE_EXPLICIT_CALL,
    												 LOC(C));
    					A = (Node *) makeSimpleA_Expr(AEXPR_SIMILAR, "~",
    												   B, (Node *) n, LOC(C));
}

a_expr(A) ::= a_expr(B) NOT_LA SIMILAR TO a_expr(F). [NOT_LA] {
    FuncCall   *n = makeFuncCall(SystemFuncName("similar_to_escape"),
    												 list_make1(F),
    												 COERCE_EXPLICIT_CALL,
    												 LOC(C));
    					A = (Node *) makeSimpleA_Expr(AEXPR_SIMILAR, "!~",
    												   B, (Node *) n, LOC(C));
}

a_expr(A) ::= a_expr(B) NOT_LA SIMILAR TO a_expr(F) ESCAPE a_expr(H). [NOT_LA] {
    FuncCall   *n = makeFuncCall(SystemFuncName("similar_to_escape"),
    												 list_make2(F, H),
    												 COERCE_EXPLICIT_CALL,
    												 LOC(C));
    					A = (Node *) makeSimpleA_Expr(AEXPR_SIMILAR, "!~",
    												   B, (Node *) n, LOC(C));
}

a_expr(A) ::= a_expr(B) IS NULL_P. [IS] {
    NullTest   *n = makeNode(NullTest);

    					n->arg = (Expr *) B;
    					n->nulltesttype = IS_NULL;
    					n->location = LOC(C);
    					A = (Node *) n;
}

a_expr(A) ::= a_expr(B) ISNULL. {
    NullTest   *n = makeNode(NullTest);

    					n->arg = (Expr *) B;
    					n->nulltesttype = IS_NULL;
    					n->location = LOC(C);
    					A = (Node *) n;
}

a_expr(A) ::= a_expr(B) IS NOT NULL_P. [IS] {
    NullTest   *n = makeNode(NullTest);

    					n->arg = (Expr *) B;
    					n->nulltesttype = IS_NOT_NULL;
    					n->location = LOC(C);
    					A = (Node *) n;
}

a_expr(A) ::= a_expr(B) NOTNULL. {
    NullTest   *n = makeNode(NullTest);

    					n->arg = (Expr *) B;
    					n->nulltesttype = IS_NOT_NULL;
    					n->location = LOC(C);
    					A = (Node *) n;
}

a_expr(A) ::= row(B) OVERLAPS row(D). {
    if (list_length(B) != 2)
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("wrong number of parameters on left side of OVERLAPS expression"),
    								 parser_errposition(LOC(B))));
    					if (list_length(D) != 2)
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("wrong number of parameters on right side of OVERLAPS expression"),
    								 parser_errposition(LOC(D))));
    					A = (Node *) makeFuncCall(SystemFuncName("overlaps"),
    											   list_concat(B, D),
    											   COERCE_SQL_SYNTAX,
    											   LOC(C));
}

a_expr(A) ::= a_expr(B) IS TRUE_P. [IS] {
    BooleanTest *b = makeNode(BooleanTest);

    					b->arg = (Expr *) B;
    					b->booltesttype = IS_TRUE;
    					b->location = LOC(C);
    					A = (Node *) b;
}

a_expr(A) ::= a_expr(B) IS NOT TRUE_P. [IS] {
    BooleanTest *b = makeNode(BooleanTest);

    					b->arg = (Expr *) B;
    					b->booltesttype = IS_NOT_TRUE;
    					b->location = LOC(C);
    					A = (Node *) b;
}

a_expr(A) ::= a_expr(B) IS FALSE_P. [IS] {
    BooleanTest *b = makeNode(BooleanTest);

    					b->arg = (Expr *) B;
    					b->booltesttype = IS_FALSE;
    					b->location = LOC(C);
    					A = (Node *) b;
}

a_expr(A) ::= a_expr(B) IS NOT FALSE_P. [IS] {
    BooleanTest *b = makeNode(BooleanTest);

    					b->arg = (Expr *) B;
    					b->booltesttype = IS_NOT_FALSE;
    					b->location = LOC(C);
    					A = (Node *) b;
}

a_expr(A) ::= a_expr(B) IS UNKNOWN. [IS] {
    BooleanTest *b = makeNode(BooleanTest);

    					b->arg = (Expr *) B;
    					b->booltesttype = IS_UNKNOWN;
    					b->location = LOC(C);
    					A = (Node *) b;
}

a_expr(A) ::= a_expr(B) IS NOT UNKNOWN. [IS] {
    BooleanTest *b = makeNode(BooleanTest);

    					b->arg = (Expr *) B;
    					b->booltesttype = IS_NOT_UNKNOWN;
    					b->location = LOC(C);
    					A = (Node *) b;
}

a_expr(A) ::= a_expr(B) IS DISTINCT FROM a_expr(F). [IS] {
    A = (Node *) makeSimpleA_Expr(AEXPR_DISTINCT, "=", B, F, LOC(C));
}

a_expr(A) ::= a_expr(B) IS NOT DISTINCT FROM a_expr(G). [IS] {
    A = (Node *) makeSimpleA_Expr(AEXPR_NOT_DISTINCT, "=", B, G, LOC(C));
}

a_expr(A) ::= a_expr(B) BETWEEN opt_asymmetric(D) b_expr(E) AND a_expr(G). [BETWEEN] {
    A = (Node *) makeSimpleA_Expr(AEXPR_BETWEEN,
    												   "BETWEEN",
    												   B,
    												   (Node *) list_make2(E, G),
    												   LOC(C));
}

a_expr(A) ::= a_expr(B) NOT_LA BETWEEN opt_asymmetric(E) b_expr(F) AND a_expr(H). [NOT_LA] {
    A = (Node *) makeSimpleA_Expr(AEXPR_NOT_BETWEEN,
    												   "NOT BETWEEN",
    												   B,
    												   (Node *) list_make2(F, H),
    												   LOC(C));
}

a_expr(A) ::= a_expr(B) BETWEEN SYMMETRIC b_expr(E) AND a_expr(G). [BETWEEN] {
    A = (Node *) makeSimpleA_Expr(AEXPR_BETWEEN_SYM,
    												   "BETWEEN SYMMETRIC",
    												   B,
    												   (Node *) list_make2(E, G),
    												   LOC(C));
}

a_expr(A) ::= a_expr(B) NOT_LA BETWEEN SYMMETRIC b_expr(F) AND a_expr(H). [NOT_LA] {
    A = (Node *) makeSimpleA_Expr(AEXPR_NOT_BETWEEN_SYM,
    												   "NOT BETWEEN SYMMETRIC",
    												   B,
    												   (Node *) list_make2(F, H),
    												   LOC(C));
}

a_expr(A) ::= a_expr(B) IN_P select_with_parens(D). {
    SubLink	   *n = makeNode(SubLink);

    					n->subselect = D;
    					n->subLinkType = ANY_SUBLINK;
    					n->subLinkId = 0;
    					n->testexpr = B;
    					n->operName = NIL;		
    					n->location = LOC(C);
    					A = (Node *) n;
}

a_expr(A) ::= a_expr(B) IN_P LPAREN expr_list(E) RPAREN. {
    A_Expr *n = makeSimpleA_Expr(AEXPR_IN, "=", B, (Node *) E, LOC(C));

    					n->rexpr_list_start = LOC(D);
    					n->rexpr_list_end = LOC(F);
    					A = (Node *) n;
}

a_expr(A) ::= a_expr(B) NOT_LA IN_P select_with_parens(E). [NOT_LA] {
    SubLink	   *n = makeNode(SubLink);

    					n->subselect = E;
    					n->subLinkType = ANY_SUBLINK;
    					n->subLinkId = 0;
    					n->testexpr = B;
    					n->operName = NIL;		
    					n->location = LOC(C);

    					A = makeNotExpr((Node *) n, LOC(C));
}

a_expr(A) ::= a_expr(B) NOT_LA IN_P LPAREN expr_list(F) RPAREN. {
    A_Expr *n = makeSimpleA_Expr(AEXPR_IN, "<>", B, (Node *) F, LOC(C));

    					n->rexpr_list_start = LOC(E);
    					n->rexpr_list_end = LOC(G);
    					A = (Node *) n;
}

a_expr(A) ::= a_expr(B) subquery_Op(C) sub_type(D) select_with_parens(E). [OP] {
    SubLink	   *n = makeNode(SubLink);

    					n->subLinkType = D;
    					n->subLinkId = 0;
    					n->testexpr = B;
    					n->operName = C;
    					n->subselect = E;
    					n->location = LOC(C);
    					A = (Node *) n;
}

a_expr(A) ::= a_expr(B) subquery_Op(C) sub_type(D) LPAREN a_expr(F) RPAREN. [OP] {
    if (D == ANY_SUBLINK)
    						A = (Node *) makeA_Expr(AEXPR_OP_ANY, C, B, F, LOC(C));
    					else
    						A = (Node *) makeA_Expr(AEXPR_OP_ALL, C, B, F, LOC(C));
}

a_expr(A) ::= UNIQUE opt_unique_null_treatment(C) select_with_parens(D). {
    ereport(ERROR,
    							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    							 errmsg("UNIQUE predicate is not yet implemented"),
    							 parser_errposition(LOC(B))));
}

a_expr(A) ::= a_expr(B) IS DOCUMENT_P. [IS] {
    A = makeXmlExpr(IS_DOCUMENT, NULL, NIL,
    									 list_make1(B), LOC(C));
}

a_expr(A) ::= a_expr(B) IS NOT DOCUMENT_P. [IS] {
    A = makeNotExpr(makeXmlExpr(IS_DOCUMENT, NULL, NIL,
    												 list_make1(B), LOC(C)),
    									 LOC(C));
}

a_expr(A) ::= a_expr(B) IS NORMALIZED. [IS] {
    A = (Node *) makeFuncCall(SystemFuncName("is_normalized"),
    											   list_make1(B),
    											   COERCE_SQL_SYNTAX,
    											   LOC(C));
}

a_expr(A) ::= a_expr(B) IS unicode_normal_form(D) NORMALIZED. [IS] {
    A = (Node *) makeFuncCall(SystemFuncName("is_normalized"),
    											   list_make2(B, makeStringConst(D, LOC(D))),
    											   COERCE_SQL_SYNTAX,
    											   LOC(C));
}

a_expr(A) ::= a_expr(B) IS NOT NORMALIZED. [IS] {
    A = makeNotExpr((Node *) makeFuncCall(SystemFuncName("is_normalized"),
    														   list_make1(B),
    														   COERCE_SQL_SYNTAX,
    														   LOC(C)),
    									 LOC(C));
}

a_expr(A) ::= a_expr(B) IS NOT unicode_normal_form(E) NORMALIZED. [IS] {
    A = makeNotExpr((Node *) makeFuncCall(SystemFuncName("is_normalized"),
    														   list_make2(B, makeStringConst(E, LOC(E))),
    														   COERCE_SQL_SYNTAX,
    														   LOC(C)),
    									 LOC(C));
}

a_expr(A) ::= a_expr(B) IS json_predicate_type_constraint(D) json_key_uniqueness_constraint_opt(E). [IS] {
    JsonFormat *format = makeJsonFormat(JS_FORMAT_DEFAULT, JS_ENC_DEFAULT, -1);

    					A = makeJsonIsPredicate(B, format, D, E, InvalidOid, LOC(B));
}

a_expr(A) ::= a_expr(B) IS NOT json_predicate_type_constraint(E) json_key_uniqueness_constraint_opt(F). [IS] {
    JsonFormat *format = makeJsonFormat(JS_FORMAT_DEFAULT, JS_ENC_DEFAULT, -1);

    					A = makeNotExpr(makeJsonIsPredicate(B, format, E, F, InvalidOid, LOC(B)), LOC(B));
}

a_expr(A) ::= DEFAULT. {
    SetToDefault *n = makeNode(SetToDefault);


    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- b_expr ----- */

b_expr(A) ::= c_expr(B). {
    A = B;
}

b_expr(A) ::= b_expr(B) TYPECAST typename(D). {
    A = makeTypeCast(B, D, LOC(C));
}

b_expr(A) ::= PLUS b_expr(C). [UMINUS] {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "+", NULL, C, LOC(B));
}

b_expr(A) ::= MINUS b_expr(C). [UMINUS] {
    A = doNegate(C, LOC(B));
}

b_expr(A) ::= b_expr(B) PLUS b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "+", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) MINUS b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "-", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) STAR b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "*", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) SLASH b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "/", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) PERCENT b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "%", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) CARET b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "^", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) LT b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "<", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) GT b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, ">", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) EQ b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "=", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) LESS_EQUALS b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "<=", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) GREATER_EQUALS b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, ">=", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) NOT_EQUALS b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "<>", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) RIGHT_ARROW b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "->", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) PIPE b_expr(D). {
    A = (Node *) makeSimpleA_Expr(AEXPR_OP, "|", B, D, LOC(C));
}

b_expr(A) ::= b_expr(B) qual_Op(C) b_expr(D). [OP] {
    A = (Node *) makeA_Expr(AEXPR_OP, C, B, D, LOC(C));
}

b_expr(A) ::= qual_Op(B) b_expr(C). [OP] {
    A = (Node *) makeA_Expr(AEXPR_OP, B, NULL, C, LOC(B));
}

b_expr(A) ::= b_expr(B) IS DISTINCT FROM b_expr(F). [IS] {
    A = (Node *) makeSimpleA_Expr(AEXPR_DISTINCT, "=", B, F, LOC(C));
}

b_expr(A) ::= b_expr(B) IS NOT DISTINCT FROM b_expr(G). [IS] {
    A = (Node *) makeSimpleA_Expr(AEXPR_NOT_DISTINCT, "=", B, G, LOC(C));
}

b_expr(A) ::= b_expr(B) IS DOCUMENT_P. [IS] {
    A = makeXmlExpr(IS_DOCUMENT, NULL, NIL,
    									 list_make1(B), LOC(C));
}

b_expr(A) ::= b_expr(B) IS NOT DOCUMENT_P. [IS] {
    A = makeNotExpr(makeXmlExpr(IS_DOCUMENT, NULL, NIL,
    												 list_make1(B), LOC(C)),
    									 LOC(C));
}

/* ----- c_expr ----- */

c_expr(A) ::= columnref(B). {
    A = B;
}

c_expr(A) ::= aexprConst(B). {
    A = B;
}

c_expr(A) ::= PARAM opt_indirection(C). {
    ParamRef   *p = makeNode(ParamRef);

    					p->number = B;
    					p->location = LOC(B);
    					if (C)
    					{
    						A_Indirection *n = makeNode(A_Indirection);

    						n->arg = (Node *) p;
    						n->indirection = check_indirection(C, yyscanner);
    						A = (Node *) n;
    					}
    					else
    						A = (Node *) p;
}

c_expr(A) ::= LPAREN a_expr(C) RPAREN opt_indirection(E). {
    if (E)
    					{
    						A_Indirection *n = makeNode(A_Indirection);

    						n->arg = C;
    						n->indirection = check_indirection(E, yyscanner);
    						A = (Node *) n;
    					}
    					else
    						A = C;
}

c_expr(A) ::= case_expr(B). {
    A = B;
}

c_expr(A) ::= func_expr(B). {
    A = B;
}

c_expr(A) ::= select_with_parens(B). [UMINUS] {
    SubLink	   *n = makeNode(SubLink);

    					n->subLinkType = EXPR_SUBLINK;
    					n->subLinkId = 0;
    					n->testexpr = NULL;
    					n->operName = NIL;
    					n->subselect = B;
    					n->location = LOC(B);
    					A = (Node *) n;
}

c_expr(A) ::= select_with_parens(B) indirection(C). {
    SubLink	   *n = makeNode(SubLink);
    					A_Indirection *a = makeNode(A_Indirection);

    					n->subLinkType = EXPR_SUBLINK;
    					n->subLinkId = 0;
    					n->testexpr = NULL;
    					n->operName = NIL;
    					n->subselect = B;
    					n->location = LOC(B);
    					a->arg = (Node *) n;
    					a->indirection = check_indirection(C, yyscanner);
    					A = (Node *) a;
}

c_expr(A) ::= EXISTS select_with_parens(C). {
    SubLink	   *n = makeNode(SubLink);

    					n->subLinkType = EXISTS_SUBLINK;
    					n->subLinkId = 0;
    					n->testexpr = NULL;
    					n->operName = NIL;
    					n->subselect = C;
    					n->location = LOC(B);
    					A = (Node *) n;
}

c_expr(A) ::= ARRAY select_with_parens(C). {
    SubLink	   *n = makeNode(SubLink);

    					n->subLinkType = ARRAY_SUBLINK;
    					n->subLinkId = 0;
    					n->testexpr = NULL;
    					n->operName = NIL;
    					n->subselect = C;
    					n->location = LOC(B);
    					A = (Node *) n;
}

c_expr(A) ::= ARRAY array_expr(C). {
    A_ArrayExpr *n = castNode(A_ArrayExpr, C);


    					n->location = LOC(B);
    					A = (Node *) n;
}

c_expr(A) ::= explicit_row(B). {
    RowExpr	   *r = makeNode(RowExpr);

    					r->args = B;
    					r->row_typeid = InvalidOid;	
    					r->colnames = NIL;	
    					r->row_format = COERCE_EXPLICIT_CALL; 
    					r->location = LOC(B);
    					A = (Node *) r;
}

c_expr(A) ::= implicit_row(B). {
    RowExpr	   *r = makeNode(RowExpr);

    					r->args = B;
    					r->row_typeid = InvalidOid;	
    					r->colnames = NIL;	
    					r->row_format = COERCE_IMPLICIT_CAST; 
    					r->location = LOC(B);
    					A = (Node *) r;
}

c_expr(A) ::= GROUPING LPAREN expr_list(D) RPAREN. {
    GroupingFunc *g = makeNode(GroupingFunc);

    				  g->args = D;
    				  g->location = LOC(B);
    				  A = (Node *) g;
}

/* ----- func_application ----- */

func_application(A) ::= func_name(B) LPAREN RPAREN. {
    A = (Node *) makeFuncCall(B, NIL,
    											   COERCE_EXPLICIT_CALL,
    											   LOC(B));
}

func_application(A) ::= func_name(B) LPAREN func_arg_list(D) opt_sort_clause(E) RPAREN. {
    FuncCall   *n = makeFuncCall(B, D,
    												 COERCE_EXPLICIT_CALL,
    												 LOC(B));

    					n->agg_order = E;
    					A = (Node *) n;
}

func_application(A) ::= func_name(B) LPAREN VARIADIC func_arg_expr(E) opt_sort_clause(F) RPAREN. {
    FuncCall   *n = makeFuncCall(B, list_make1(E),
    												 COERCE_EXPLICIT_CALL,
    												 LOC(B));

    					n->func_variadic = true;
    					n->agg_order = F;
    					A = (Node *) n;
}

func_application(A) ::= func_name(B) LPAREN func_arg_list(D) COMMA VARIADIC func_arg_expr(G) opt_sort_clause(H) RPAREN. {
    FuncCall   *n = makeFuncCall(B, lappend(D, G),
    												 COERCE_EXPLICIT_CALL,
    												 LOC(B));

    					n->func_variadic = true;
    					n->agg_order = H;
    					A = (Node *) n;
}

func_application(A) ::= func_name(B) LPAREN ALL func_arg_list(E) opt_sort_clause(F) RPAREN. {
    FuncCall   *n = makeFuncCall(B, E,
    												 COERCE_EXPLICIT_CALL,
    												 LOC(B));

    					n->agg_order = F;




    					A = (Node *) n;
}

func_application(A) ::= func_name(B) LPAREN DISTINCT func_arg_list(E) opt_sort_clause(F) RPAREN. {
    FuncCall   *n = makeFuncCall(B, E,
    												 COERCE_EXPLICIT_CALL,
    												 LOC(B));

    					n->agg_order = F;
    					n->agg_distinct = true;
    					A = (Node *) n;
}

func_application(A) ::= func_name(B) LPAREN STAR RPAREN. {
    FuncCall   *n = makeFuncCall(B, NIL,
    												 COERCE_EXPLICIT_CALL,
    												 LOC(B));

    					n->agg_star = true;
    					A = (Node *) n;
}

/* ----- func_expr ----- */

func_expr(A) ::= func_application(B) within_group_clause(C) filter_clause(D) null_treatment(E) over_clause(F). {
    FuncCall   *n = (FuncCall *) B;









    					if (C != NIL)
    					{
    						if (n->agg_order != NIL)
    							ereport(ERROR,
    									(errcode(ERRCODE_SYNTAX_ERROR),
    									 errmsg("cannot use multiple ORDER BY clauses with WITHIN GROUP"),
    									 parser_errposition(LOC(C))));
    						if (n->agg_distinct)
    							ereport(ERROR,
    									(errcode(ERRCODE_SYNTAX_ERROR),
    									 errmsg("cannot use DISTINCT with WITHIN GROUP"),
    									 parser_errposition(LOC(C))));
    						if (n->func_variadic)
    							ereport(ERROR,
    									(errcode(ERRCODE_SYNTAX_ERROR),
    									 errmsg("cannot use VARIADIC with WITHIN GROUP"),
    									 parser_errposition(LOC(C))));
    						n->agg_order = C;
    						n->agg_within_group = true;
    					}
    					n->agg_filter = D;
    					n->ignore_nulls = E;
    					n->over = F;
    					A = (Node *) n;
}

func_expr(A) ::= json_aggregate_func(B) filter_clause(C) over_clause(D). {
    JsonAggConstructor *n = IsA(B, JsonObjectAgg) ?
    						((JsonObjectAgg *) B)->constructor :
    						((JsonArrayAgg *) B)->constructor;

    					n->agg_filter = C;
    					n->over = D;
    					A = (Node *) B;
}

func_expr(A) ::= func_expr_common_subexpr(B). {
    A = B;
}

/* ----- func_expr_windowless ----- */

func_expr_windowless(A) ::= func_application(B). {
    A = B;
}

func_expr_windowless(A) ::= func_expr_common_subexpr(B). {
    A = B;
}

func_expr_windowless(A) ::= json_aggregate_func(B). {
    A = B;
}

/* ----- func_expr_common_subexpr ----- */

func_expr_common_subexpr(A) ::= COLLATION FOR LPAREN a_expr(E) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("pg_collation_for"),
    											   list_make1(E),
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= CURRENT_DATE. {
    A = makeSQLValueFunction(SVFOP_CURRENT_DATE, -1, LOC(B));
}

func_expr_common_subexpr(A) ::= CURRENT_TIME. {
    A = makeSQLValueFunction(SVFOP_CURRENT_TIME, -1, LOC(B));
}

func_expr_common_subexpr(A) ::= CURRENT_TIME LPAREN iconst(D) RPAREN. {
    A = makeSQLValueFunction(SVFOP_CURRENT_TIME_N, D, LOC(B));
}

func_expr_common_subexpr(A) ::= CURRENT_TIMESTAMP. {
    A = makeSQLValueFunction(SVFOP_CURRENT_TIMESTAMP, -1, LOC(B));
}

func_expr_common_subexpr(A) ::= CURRENT_TIMESTAMP LPAREN iconst(D) RPAREN. {
    A = makeSQLValueFunction(SVFOP_CURRENT_TIMESTAMP_N, D, LOC(B));
}

func_expr_common_subexpr(A) ::= LOCALTIME. {
    A = makeSQLValueFunction(SVFOP_LOCALTIME, -1, LOC(B));
}

func_expr_common_subexpr(A) ::= LOCALTIME LPAREN iconst(D) RPAREN. {
    A = makeSQLValueFunction(SVFOP_LOCALTIME_N, D, LOC(B));
}

func_expr_common_subexpr(A) ::= LOCALTIMESTAMP. {
    A = makeSQLValueFunction(SVFOP_LOCALTIMESTAMP, -1, LOC(B));
}

func_expr_common_subexpr(A) ::= LOCALTIMESTAMP LPAREN iconst(D) RPAREN. {
    A = makeSQLValueFunction(SVFOP_LOCALTIMESTAMP_N, D, LOC(B));
}

func_expr_common_subexpr(A) ::= CURRENT_ROLE. {
    A = makeSQLValueFunction(SVFOP_CURRENT_ROLE, -1, LOC(B));
}

func_expr_common_subexpr(A) ::= CURRENT_USER. {
    A = makeSQLValueFunction(SVFOP_CURRENT_USER, -1, LOC(B));
}

func_expr_common_subexpr(A) ::= SESSION_USER. {
    A = makeSQLValueFunction(SVFOP_SESSION_USER, -1, LOC(B));
}

func_expr_common_subexpr(A) ::= SYSTEM_USER. {
    A = (Node *) makeFuncCall(SystemFuncName("system_user"),
    											   NIL,
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= USER. {
    A = makeSQLValueFunction(SVFOP_USER, -1, LOC(B));
}

func_expr_common_subexpr(A) ::= CURRENT_CATALOG. {
    A = makeSQLValueFunction(SVFOP_CURRENT_CATALOG, -1, LOC(B));
}

func_expr_common_subexpr(A) ::= CURRENT_SCHEMA. {
    A = makeSQLValueFunction(SVFOP_CURRENT_SCHEMA, -1, LOC(B));
}

func_expr_common_subexpr(A) ::= CAST LPAREN a_expr(D) AS typename(F) RPAREN. {
    A = makeTypeCast(D, F, LOC(B));
}

func_expr_common_subexpr(A) ::= EXTRACT LPAREN extract_list(D) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("extract"),
    											   D,
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= NORMALIZE LPAREN a_expr(D) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("normalize"),
    											   list_make1(D),
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= NORMALIZE LPAREN a_expr(D) COMMA unicode_normal_form(F) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("normalize"),
    											   list_make2(D, makeStringConst(F, LOC(F))),
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= OVERLAY LPAREN overlay_list(D) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("overlay"),
    											   D,
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= OVERLAY LPAREN func_arg_list_opt(D) RPAREN. {
    A = (Node *) makeFuncCall(list_make1(makeString("overlay")),
    											   D,
    											   COERCE_EXPLICIT_CALL,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= POSITION LPAREN position_list(D) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("position"),
    											   D,
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= SUBSTRING LPAREN substr_list(D) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("substring"),
    											   D,
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= SUBSTRING LPAREN func_arg_list_opt(D) RPAREN. {
    A = (Node *) makeFuncCall(list_make1(makeString("substring")),
    											   D,
    											   COERCE_EXPLICIT_CALL,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= TREAT LPAREN a_expr(D) AS typename(F) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName(strVal(llast(F->names))),
    											   list_make1(D),
    											   COERCE_EXPLICIT_CALL,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= TRIM LPAREN BOTH trim_list(E) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("btrim"),
    											   E,
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= TRIM LPAREN LEADING trim_list(E) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("ltrim"),
    											   E,
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= TRIM LPAREN TRAILING trim_list(E) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("rtrim"),
    											   E,
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= TRIM LPAREN trim_list(D) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("btrim"),
    											   D,
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= NULLIF LPAREN a_expr(D) COMMA a_expr(F) RPAREN. {
    A = (Node *) makeSimpleA_Expr(AEXPR_NULLIF, "=", D, F, LOC(B));
}

func_expr_common_subexpr(A) ::= COALESCE LPAREN expr_list(D) RPAREN. {
    CoalesceExpr *c = makeNode(CoalesceExpr);

    					c->args = D;
    					c->location = LOC(B);
    					A = (Node *) c;
}

func_expr_common_subexpr(A) ::= GREATEST LPAREN expr_list(D) RPAREN. {
    MinMaxExpr *v = makeNode(MinMaxExpr);

    					v->args = D;
    					v->op = IS_GREATEST;
    					v->location = LOC(B);
    					A = (Node *) v;
}

func_expr_common_subexpr(A) ::= LEAST LPAREN expr_list(D) RPAREN. {
    MinMaxExpr *v = makeNode(MinMaxExpr);

    					v->args = D;
    					v->op = IS_LEAST;
    					v->location = LOC(B);
    					A = (Node *) v;
}

func_expr_common_subexpr(A) ::= XMLCONCAT LPAREN expr_list(D) RPAREN. {
    A = makeXmlExpr(IS_XMLCONCAT, NULL, NIL, D, LOC(B));
}

func_expr_common_subexpr(A) ::= XMLELEMENT LPAREN NAME_P colLabel(E) RPAREN. {
    A = makeXmlExpr(IS_XMLELEMENT, E, NIL, NIL, LOC(B));
}

func_expr_common_subexpr(A) ::= XMLELEMENT LPAREN NAME_P colLabel(E) COMMA xml_attributes(G) RPAREN. {
    A = makeXmlExpr(IS_XMLELEMENT, E, G, NIL, LOC(B));
}

func_expr_common_subexpr(A) ::= XMLELEMENT LPAREN NAME_P colLabel(E) COMMA expr_list(G) RPAREN. {
    A = makeXmlExpr(IS_XMLELEMENT, E, NIL, G, LOC(B));
}

func_expr_common_subexpr(A) ::= XMLELEMENT LPAREN NAME_P colLabel(E) COMMA xml_attributes(G) COMMA expr_list(I) RPAREN. {
    A = makeXmlExpr(IS_XMLELEMENT, E, G, I, LOC(B));
}

func_expr_common_subexpr(A) ::= XMLEXISTS LPAREN c_expr(D) xmlexists_argument(E) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("xmlexists"),
    											   list_make2(D, E),
    											   COERCE_SQL_SYNTAX,
    											   LOC(B));
}

func_expr_common_subexpr(A) ::= XMLFOREST LPAREN labeled_expr_list(D) RPAREN. {
    A = makeXmlExpr(IS_XMLFOREST, NULL, D, NIL, LOC(B));
}

func_expr_common_subexpr(A) ::= XMLPARSE LPAREN document_or_content(D) a_expr(E) xml_whitespace_option(F) RPAREN. {
    XmlExpr *x = (XmlExpr *)
    						makeXmlExpr(IS_XMLPARSE, NULL, NIL,
    									list_make2(E, makeBoolAConst(F, -1)),
    									LOC(B));

    					x->xmloption = D;
    					A = (Node *) x;
}

func_expr_common_subexpr(A) ::= XMLPI LPAREN NAME_P colLabel(E) RPAREN. {
    A = makeXmlExpr(IS_XMLPI, E, NULL, NIL, LOC(B));
}

func_expr_common_subexpr(A) ::= XMLPI LPAREN NAME_P colLabel(E) COMMA a_expr(G) RPAREN. {
    A = makeXmlExpr(IS_XMLPI, E, NULL, list_make1(G), LOC(B));
}

func_expr_common_subexpr(A) ::= XMLROOT LPAREN a_expr(D) COMMA xml_root_version(F) opt_xml_root_standalone(G) RPAREN. {
    A = makeXmlExpr(IS_XMLROOT, NULL, NIL,
    									 list_make3(D, F, G), LOC(B));
}

func_expr_common_subexpr(A) ::= XMLSERIALIZE LPAREN document_or_content(D) a_expr(E) AS simpleTypename(G) xml_indent_option(H) RPAREN. {
    XmlSerialize *n = makeNode(XmlSerialize);

    					n->xmloption = D;
    					n->expr = E;
    					n->typeName = G;
    					n->indent = H;
    					n->location = LOC(B);
    					A = (Node *) n;
}

func_expr_common_subexpr(A) ::= JSON_OBJECT LPAREN func_arg_list(D) RPAREN. {
    A = (Node *) makeFuncCall(SystemFuncName("json_object"),
    											   D, COERCE_EXPLICIT_CALL, LOC(B));
}

func_expr_common_subexpr(A) ::= JSON_OBJECT LPAREN json_name_and_value_list(D) json_object_constructor_null_clause_opt(E) json_key_uniqueness_constraint_opt(F) json_returning_clause_opt(G) RPAREN. {
    JsonObjectConstructor *n = makeNode(JsonObjectConstructor);

    					n->exprs = D;
    					n->absent_on_null = E;
    					n->unique = F;
    					n->output = (JsonOutput *) G;
    					n->location = LOC(B);
    					A = (Node *) n;
}

func_expr_common_subexpr(A) ::= JSON_OBJECT LPAREN json_returning_clause_opt(D) RPAREN. {
    JsonObjectConstructor *n = makeNode(JsonObjectConstructor);

    					n->exprs = NULL;
    					n->absent_on_null = false;
    					n->unique = false;
    					n->output = (JsonOutput *) D;
    					n->location = LOC(B);
    					A = (Node *) n;
}

func_expr_common_subexpr(A) ::= JSON_ARRAY LPAREN json_value_expr_list(D) json_array_constructor_null_clause_opt(E) json_returning_clause_opt(F) RPAREN. {
    JsonArrayConstructor *n = makeNode(JsonArrayConstructor);

    					n->exprs = D;
    					n->absent_on_null = E;
    					n->output = (JsonOutput *) F;
    					n->location = LOC(B);
    					A = (Node *) n;
}

func_expr_common_subexpr(A) ::= JSON_ARRAY LPAREN select_no_parens(D) json_format_clause_opt(E) json_returning_clause_opt(F) RPAREN. {
    JsonArrayQueryConstructor *n = makeNode(JsonArrayQueryConstructor);

    					n->query = D;
    					n->format = (JsonFormat *) E;
    					n->absent_on_null = true;	
    					n->output = (JsonOutput *) F;
    					n->location = LOC(B);
    					A = (Node *) n;
}

func_expr_common_subexpr(A) ::= JSON_ARRAY LPAREN json_returning_clause_opt(D) RPAREN. {
    JsonArrayConstructor *n = makeNode(JsonArrayConstructor);

    					n->exprs = NIL;
    					n->absent_on_null = true;
    					n->output = (JsonOutput *) D;
    					n->location = LOC(B);
    					A = (Node *) n;
}

func_expr_common_subexpr(A) ::= JSON LPAREN json_value_expr(D) json_key_uniqueness_constraint_opt(E) RPAREN. {
    JsonParseExpr *n = makeNode(JsonParseExpr);

    					n->expr = (JsonValueExpr *) D;
    					n->unique_keys = E;
    					n->output = NULL;
    					n->location = LOC(B);
    					A = (Node *) n;
}

func_expr_common_subexpr(A) ::= JSON_SCALAR LPAREN a_expr(D) RPAREN. {
    JsonScalarExpr *n = makeNode(JsonScalarExpr);

    					n->expr = (Expr *) D;
    					n->output = NULL;
    					n->location = LOC(B);
    					A = (Node *) n;
}

func_expr_common_subexpr(A) ::= JSON_SERIALIZE LPAREN json_value_expr(D) json_returning_clause_opt(E) RPAREN. {
    JsonSerializeExpr *n = makeNode(JsonSerializeExpr);

    					n->expr = (JsonValueExpr *) D;
    					n->output = (JsonOutput *) E;
    					n->location = LOC(B);
    					A = (Node *) n;
}

func_expr_common_subexpr(A) ::= MERGE_ACTION LPAREN RPAREN. {
    MergeSupportFunc *m = makeNode(MergeSupportFunc);

    					m->msftype = TEXTOID;
    					m->location = LOC(B);
    					A = (Node *) m;
}

func_expr_common_subexpr(A) ::= JSON_QUERY LPAREN json_value_expr(D) COMMA a_expr(F) json_passing_clause_opt(G) json_returning_clause_opt(H) json_wrapper_behavior(I) json_quotes_clause_opt(J) json_behavior_clause_opt(K) RPAREN. {
    JsonFuncExpr *n = makeNode(JsonFuncExpr);

    					n->op = JSON_QUERY_OP;
    					n->context_item = (JsonValueExpr *) D;
    					n->pathspec = F;
    					n->passing = G;
    					n->output = (JsonOutput *) H;
    					n->wrapper = I;
    					n->quotes = J;
    					n->on_empty = (JsonBehavior *) linitial(K);
    					n->on_error = (JsonBehavior *) lsecond(K);
    					n->location = LOC(B);
    					A = (Node *) n;
}

func_expr_common_subexpr(A) ::= JSON_EXISTS LPAREN json_value_expr(D) COMMA a_expr(F) json_passing_clause_opt(G) json_on_error_clause_opt(H) RPAREN. {
    JsonFuncExpr *n = makeNode(JsonFuncExpr);

    					n->op = JSON_EXISTS_OP;
    					n->context_item = (JsonValueExpr *) D;
    					n->pathspec = F;
    					n->passing = G;
    					n->output = NULL;
    					n->on_error = (JsonBehavior *) H;
    					n->location = LOC(B);
    					A = (Node *) n;
}

func_expr_common_subexpr(A) ::= JSON_VALUE LPAREN json_value_expr(D) COMMA a_expr(F) json_passing_clause_opt(G) json_returning_clause_opt(H) json_behavior_clause_opt(I) RPAREN. {
    JsonFuncExpr *n = makeNode(JsonFuncExpr);

    					n->op = JSON_VALUE_OP;
    					n->context_item = (JsonValueExpr *) D;
    					n->pathspec = F;
    					n->passing = G;
    					n->output = (JsonOutput *) H;
    					n->on_empty = (JsonBehavior *) linitial(I);
    					n->on_error = (JsonBehavior *) lsecond(I);
    					n->location = LOC(B);
    					A = (Node *) n;
}

/* ----- xml_root_version ----- */

row(A) ::= ROW LPAREN expr_list(D) RPAREN. {
    A = D;
}

row(A) ::= ROW LPAREN RPAREN. {
    A = NIL;
}

row(A) ::= LPAREN expr_list(C) COMMA a_expr(E) RPAREN. {
    A = lappend(C, E);
}

/* ----- explicit_row ----- */

explicit_row(A) ::= ROW LPAREN expr_list(D) RPAREN. {
    A = D;
}

explicit_row(A) ::= ROW LPAREN RPAREN. {
    A = NIL;
}

/* ----- implicit_row ----- */

implicit_row(A) ::= LPAREN expr_list(C) COMMA a_expr(E) RPAREN. {
    A = lappend(C, E);
}

/* ----- sub_type ----- */

sub_type(A) ::= ANY. {
    A = ANY_SUBLINK;
}

sub_type(A) ::= SOME. {
    A = ANY_SUBLINK;
}

sub_type(A) ::= ALL. {
    A = ALL_SUBLINK;
}

/* ----- all_Op ----- */

all_Op(A) ::= OP(B). {
    A = B;
}

all_Op(A) ::= mathOp(B). {
    A = B;
}

/* ----- mathOp ----- */

mathOp(A) ::= PLUS. {
    A = "+";
}

mathOp(A) ::= MINUS. {
    A = "-";
}

mathOp(A) ::= STAR. {
    A = "*";
}

mathOp(A) ::= SLASH. {
    A = "/";
}

mathOp(A) ::= PERCENT. {
    A = "%";
}

mathOp(A) ::= CARET. {
    A = "^";
}

mathOp(A) ::= LT. {
    A = "<";
}

mathOp(A) ::= GT. {
    A = ">";
}

mathOp(A) ::= EQ. {
    A = "=";
}

mathOp(A) ::= LESS_EQUALS. {
    A = "<=";
}

mathOp(A) ::= GREATER_EQUALS. {
    A = ">=";
}

mathOp(A) ::= NOT_EQUALS. {
    A = "<>";
}

mathOp(A) ::= RIGHT_ARROW. {
    A = "->";
}

mathOp(A) ::= PIPE. {
    A = "|";
}

/* ----- qual_Op ----- */

qual_Op(A) ::= OP(B). {
    A = list_make1(makeString(B));
}

qual_Op(A) ::= OPERATOR LPAREN any_operator(D) RPAREN. {
    A = D;
}

/* ----- qual_all_Op ----- */

qual_all_Op(A) ::= all_Op(B). {
    A = list_make1(makeString(B));
}

qual_all_Op(A) ::= OPERATOR LPAREN any_operator(D) RPAREN. {
    A = D;
}

/* ----- subquery_Op ----- */

subquery_Op(A) ::= all_Op(B). {
    A = list_make1(makeString(B));
}

subquery_Op(A) ::= OPERATOR LPAREN any_operator(D) RPAREN. {
    A = D;
}

subquery_Op(A) ::= LIKE. {
    A = list_make1(makeString("~~"));
}

subquery_Op(A) ::= NOT_LA LIKE. {
    A = list_make1(makeString("!~~"));
}

subquery_Op(A) ::= ILIKE. {
    A = list_make1(makeString("~~*"));
}

subquery_Op(A) ::= NOT_LA ILIKE. {
    A = list_make1(makeString("!~~*"));
}

/* ----- expr_list ----- */

expr_list(A) ::= a_expr(B). {
    A = list_make1(B);
}

expr_list(A) ::= expr_list(B) COMMA a_expr(D). {
    A = lappend(B, D);
}

/* ----- func_arg_list ----- */

func_arg_list(A) ::= func_arg_expr(B). {
    A = list_make1(B);
}

func_arg_list(A) ::= func_arg_list(B) COMMA func_arg_expr(D). {
    A = lappend(B, D);
}

/* ----- func_arg_expr ----- */

func_arg_expr(A) ::= a_expr(B). {
    A = B;
}

func_arg_expr(A) ::= param_name(B) COLON_EQUALS a_expr(D). {
    NamedArgExpr *na = makeNode(NamedArgExpr);

    					na->name = B;
    					na->arg = (Expr *) D;
    					na->argnumber = -1;		
    					na->location = LOC(B);
    					A = (Node *) na;
}

func_arg_expr(A) ::= param_name(B) EQUALS_GREATER a_expr(D). {
    NamedArgExpr *na = makeNode(NamedArgExpr);

    					na->name = B;
    					na->arg = (Expr *) D;
    					na->argnumber = -1;		
    					na->location = LOC(B);
    					A = (Node *) na;
}

/* ----- func_arg_list_opt ----- */

func_arg_list_opt(A) ::= func_arg_list(B). {
    A = B;
}

func_arg_list_opt(A) ::= . {
    A = NIL;
}

/* ----- type_list ----- */

type_list(A) ::= typename(B). {
    A = list_make1(B);
}

type_list(A) ::= type_list(B) COMMA typename(D). {
    A = lappend(B, D);
}

/* ----- array_expr ----- */

array_expr(A) ::= LBRACKET expr_list(C) RBRACKET. {
    A = makeAArrayExpr(C, LOC(B), LOC(D));
}

array_expr(A) ::= LBRACKET array_expr_list(C) RBRACKET. {
    A = makeAArrayExpr(C, LOC(B), LOC(D));
}

array_expr(A) ::= LBRACKET RBRACKET. {
    A = makeAArrayExpr(NIL, LOC(B), LOC(C));
}

/* ----- array_expr_list ----- */

array_expr_list(A) ::= array_expr(B). {
    A = list_make1(B);
}

array_expr_list(A) ::= array_expr_list(B) COMMA array_expr(D). {
    A = lappend(B, D);
}

/* ----- extract_list ----- */

extract_list(A) ::= extract_arg(B) FROM a_expr(D). {
    A = list_make2(makeStringConst(B, LOC(B)), D);
}

/* ----- extract_arg ----- */

extract_arg(A) ::= IDENT. {
    A = B;
}

extract_arg(A) ::= YEAR_P. {
    A = "year";
}

extract_arg(A) ::= MONTH_P. {
    A = "month";
}

extract_arg(A) ::= DAY_P. {
    A = "day";
}

extract_arg(A) ::= HOUR_P. {
    A = "hour";
}

extract_arg(A) ::= MINUTE_P. {
    A = "minute";
}

extract_arg(A) ::= SECOND_P. {
    A = "second";
}

extract_arg(A) ::= sconst(B). {
    A = B;
}

/* ----- unicode_normal_form ----- */

unicode_normal_form(A) ::= NFC. {
    A = "NFC";
}

unicode_normal_form(A) ::= NFD. {
    A = "NFD";
}

unicode_normal_form(A) ::= NFKC. {
    A = "NFKC";
}

unicode_normal_form(A) ::= NFKD. {
    A = "NFKD";
}

/* ----- overlay_list ----- */

overlay_list(A) ::= a_expr(B) PLACING a_expr(D) FROM a_expr(F) FOR a_expr(H). {
    A = list_make4(B, D, F, H);
}

overlay_list(A) ::= a_expr(B) PLACING a_expr(D) FROM a_expr(F). {
    A = list_make3(B, D, F);
}

/* ----- position_list ----- */

position_list(A) ::= b_expr(B) IN_P b_expr(D). {
    A = list_make2(D, B);
}

/* ----- substr_list ----- */

substr_list(A) ::= a_expr(B) FROM a_expr(D) FOR a_expr(F). {
    A = list_make3(B, D, F);
}

substr_list(A) ::= a_expr(B) FOR a_expr(D) FROM a_expr(F). {
    A = list_make3(B, F, D);
}

substr_list(A) ::= a_expr(B) FROM a_expr(D). {
    A = list_make2(B, D);
}

substr_list(A) ::= a_expr(B) FOR a_expr(D). {
    A = list_make3(B, makeIntConst(1, -1),
    									makeTypeCast(D,
    												 SystemTypeName("int4"), -1));
}

substr_list(A) ::= a_expr(B) SIMILAR a_expr(D) ESCAPE a_expr(F). {
    A = list_make3(B, D, F);
}

/* ----- trim_list ----- */

trim_list(A) ::= a_expr(B) FROM expr_list(D). {
    A = lappend(D, B);
}

trim_list(A) ::= FROM expr_list(C). {
    A = C;
}

trim_list(A) ::= expr_list(B). {
    A = B;
}

/* ----- case_expr ----- */

case_expr(A) ::= CASE case_arg(C) when_clause_list(D) case_default(E) END_P. {
    CaseExpr   *c = makeNode(CaseExpr);

    					c->casetype = InvalidOid; 
    					c->arg = (Expr *) C;
    					c->args = D;
    					c->defresult = (Expr *) E;
    					c->location = LOC(B);
    					A = (Node *) c;
}

/* ----- when_clause_list ----- */

when_clause_list(A) ::= when_clause(B). {
    A = list_make1(B);
}

when_clause_list(A) ::= when_clause_list(B) when_clause(C). {
    A = lappend(B, C);
}

/* ----- when_clause ----- */

when_clause(A) ::= WHEN a_expr(C) THEN a_expr(E). {
    CaseWhen   *w = makeNode(CaseWhen);

    					w->expr = (Expr *) C;
    					w->result = (Expr *) E;
    					w->location = LOC(B);
    					A = (Node *) w;
}

/* ----- case_default ----- */

case_default(A) ::= ELSE a_expr(C). {
    A = C;
}

case_default(A) ::= . {
    A = NULL;
}

/* ----- case_arg ----- */

case_arg(A) ::= a_expr(B). {
    A = B;
}

case_arg(A) ::= . {
    A = NULL;
}

/* ----- columnref ----- */

columnref(A) ::= colId(B). {
    A = makeColumnRef(B, NIL, LOC(B), yyscanner);
}

columnref(A) ::= colId(B) indirection(C). {
    A = makeColumnRef(B, C, LOC(B), yyscanner);
}

/* ----- indirection_el ----- */

indirection_el(A) ::= DOT attr_name(C). {
    A = (Node *) makeString(C);
}

indirection_el(A) ::= DOT STAR. {
    A = (Node *) makeNode(A_Star);
}

indirection_el(A) ::= LBRACKET a_expr(C) RBRACKET. {
    A_Indices *ai = makeNode(A_Indices);

    					ai->is_slice = false;
    					ai->lidx = NULL;
    					ai->uidx = C;
    					A = (Node *) ai;
}

indirection_el(A) ::= LBRACKET opt_slice_bound(C) COLON opt_slice_bound(E) RBRACKET. {
    A_Indices *ai = makeNode(A_Indices);

    					ai->is_slice = true;
    					ai->lidx = C;
    					ai->uidx = E;
    					A = (Node *) ai;
}

/* ----- opt_slice_bound ----- */

opt_slice_bound(A) ::= a_expr(B). {
    A = B;
}

opt_slice_bound(A) ::= . {
    A = NULL;
}

/* ----- indirection ----- */

indirection(A) ::= indirection_el(B). {
    A = list_make1(B);
}

indirection(A) ::= indirection(B) indirection_el(C). {
    A = lappend(B, C);
}

/* ----- opt_indirection ----- */

opt_indirection(A) ::= . {
    A = NIL;
}

opt_indirection(A) ::= opt_indirection(B) indirection_el(C). {
    A = lappend(B, C);
}

/* ----- opt_asymmetric ----- */

opt_asymmetric(A) ::= ASYMMETRIC.

/* ----- json_passing_clause_opt ----- */

func_name(A) ::= type_function_name(B). {
    A = list_make1(makeString(B));
}

func_name(A) ::= colId(B) indirection(C). {
    A = check_func_name(lcons(makeString(B), C),
    											 yyscanner);
}

/* ----- aexprConst ----- */

aexprConst(A) ::= iconst(B). {
    A = makeIntConst(B, LOC(B));
}

aexprConst(A) ::= FCONST. {
    A = makeFloatConst(B, LOC(B));
}

aexprConst(A) ::= sconst(B). {
    A = makeStringConst(B, LOC(B));
}

aexprConst(A) ::= BCONST. {
    A = makeBitStringConst(B, LOC(B));
}

aexprConst(A) ::= XCONST. {
    A = makeBitStringConst(B, LOC(B));
}

aexprConst(A) ::= func_name(B) sconst(C). {
    TypeName   *t = makeTypeNameFromNameList(B);

    					t->location = LOC(B);
    					A = makeStringConstCast(C, LOC(C), t);
}

aexprConst(A) ::= func_name(B) LPAREN func_arg_list(D) opt_sort_clause(E) RPAREN sconst(G). {
    TypeName   *t = makeTypeNameFromNameList(B);
    					ListCell   *lc;







    					foreach(lc, D)
    					{
    						NamedArgExpr *arg = (NamedArgExpr *) lfirst(lc);

    						if (IsA(arg, NamedArgExpr))
    							ereport(ERROR,
    									(errcode(ERRCODE_SYNTAX_ERROR),
    									 errmsg("type modifier cannot have parameter name"),
    									 parser_errposition(arg->location)));
    					}
    					if (E != NIL)
    							ereport(ERROR,
    									(errcode(ERRCODE_SYNTAX_ERROR),
    									 errmsg("type modifier cannot have ORDER BY"),
    									 parser_errposition(LOC(E))));

    					t->typmods = D;
    					t->location = LOC(B);
    					A = makeStringConstCast(G, LOC(G), t);
}

aexprConst(A) ::= constTypename(B) sconst(C). {
    A = makeStringConstCast(C, LOC(C), B);
}

aexprConst(A) ::= constInterval(B) sconst(C) opt_interval(D). {
    TypeName   *t = B;

    					t->typmods = D;
    					A = makeStringConstCast(C, LOC(C), t);
}

aexprConst(A) ::= constInterval(B) LPAREN iconst(D) RPAREN sconst(F). {
    TypeName   *t = B;

    					t->typmods = list_make2(makeIntConst(INTERVAL_FULL_RANGE, -1),
    											makeIntConst(D, LOC(D)));
    					A = makeStringConstCast(F, LOC(F), t);
}

aexprConst(A) ::= TRUE_P. {
    A = makeBoolAConst(true, LOC(B));
}

aexprConst(A) ::= FALSE_P. {
    A = makeBoolAConst(false, LOC(B));
}

aexprConst(A) ::= NULL_P. {
    A = makeNullAConst(LOC(B));
}

/* ----- iconst ----- */

iconst(A) ::= ICONST. {
    A = B;
}

/* ----- sconst ----- */

sconst(A) ::= SCONST. {
    A = B;
}

/* ----- signedIconst ----- */

signedIconst(A) ::= iconst(B). {
    A = B;
}

signedIconst(A) ::= PLUS iconst(C). {
    A = + C;
}

signedIconst(A) ::= MINUS iconst(C). {
    A = - C;
}

/* ----- roleId ----- */
```

