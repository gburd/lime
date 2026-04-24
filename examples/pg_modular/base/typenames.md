# Type Name Rules


Production rules for parsing SQL type names: numeric types,
character types, date/time types, bit strings, intervals,
arrays, and JSON types.


```yaml
name: pg-typenames
version: 17.0.0
description: Type name parsing (numeric, character, datetime, bit, etc.)
provides: [pg-typenames]
depends: [pg-type-decls, pg-keywords]
```

## Production Rules

```lime rules
typename(A) ::= simpleTypename(B) opt_array_bounds(C). {
    A = B;
    					A->arrayBounds = C;
}

typename(A) ::= SETOF simpleTypename(C) opt_array_bounds(D). {
    A = C;
    					A->arrayBounds = D;
    					A->setof = true;
}

typename(A) ::= simpleTypename(B) ARRAY LBRACKET iconst(E) RBRACKET. {
    A = B;
    					A->arrayBounds = list_make1(makeInteger(E));
}

typename(A) ::= SETOF simpleTypename(C) ARRAY LBRACKET iconst(F) RBRACKET. {
    A = C;
    					A->arrayBounds = list_make1(makeInteger(F));
    					A->setof = true;
}

typename(A) ::= simpleTypename(B) ARRAY. {
    A = B;
    					A->arrayBounds = list_make1(makeInteger(-1));
}

typename(A) ::= SETOF simpleTypename(C) ARRAY. {
    A = C;
    					A->arrayBounds = list_make1(makeInteger(-1));
    					A->setof = true;
}

/* ----- opt_array_bounds ----- */

opt_array_bounds(A) ::= opt_array_bounds(B) LBRACKET RBRACKET. {
    A = lappend(B, makeInteger(-1));
}

opt_array_bounds(A) ::= opt_array_bounds(B) LBRACKET iconst(D) RBRACKET. {
    A = lappend(B, makeInteger(D));
}

opt_array_bounds(A) ::= . {
    A = NIL;
}

/* ----- simpleTypename ----- */

simpleTypename(A) ::= genericType(B). {
    A = B;
}

simpleTypename(A) ::= numeric(B). {
    A = B;
}

simpleTypename(A) ::= bit(B). {
    A = B;
}

simpleTypename(A) ::= character_tn(B). {
    A = B;
}

simpleTypename(A) ::= constDatetime(B). {
    A = B;
}

simpleTypename(A) ::= constInterval(B) opt_interval(C). {
    A = B;
    					A->typmods = C;
}

simpleTypename(A) ::= constInterval(B) LPAREN iconst(D) RPAREN. {
    A = B;
    					A->typmods = list_make2(makeIntConst(INTERVAL_FULL_RANGE, -1),
    											 makeIntConst(D, LOC(D)));
}

simpleTypename(A) ::= jsonType(B). {
    A = B;
}

/* ----- constTypename ----- */

constTypename(A) ::= numeric(B). {
    A = B;
}

constTypename(A) ::= constBit(B). {
    A = B;
}

constTypename(A) ::= constCharacter(B). {
    A = B;
}

constTypename(A) ::= constDatetime(B). {
    A = B;
}

constTypename(A) ::= jsonType(B). {
    A = B;
}

/* ----- genericType ----- */

genericType(A) ::= type_function_name(B) opt_type_modifiers(C). {
    A = makeTypeName(B);
    					A->typmods = C;
    					A->location = LOC(B);
}

genericType(A) ::= type_function_name(B) attrs(C) opt_type_modifiers(D). {
    A = makeTypeNameFromNameList(lcons(makeString(B), C));
    					A->typmods = D;
    					A->location = LOC(B);
}

/* ----- opt_type_modifiers ----- */

opt_type_modifiers(A) ::= LPAREN expr_list(C) RPAREN. {
    A = C;
}

opt_type_modifiers(A) ::= . {
    A = NIL;
}

/* ----- numeric ----- */

numeric(A) ::= INT_P. {
    A = SystemTypeName("int4");
    					A->location = LOC(B);
}

numeric(A) ::= INTEGER. {
    A = SystemTypeName("int4");
    					A->location = LOC(B);
}

numeric(A) ::= SMALLINT. {
    A = SystemTypeName("int2");
    					A->location = LOC(B);
}

numeric(A) ::= BIGINT. {
    A = SystemTypeName("int8");
    					A->location = LOC(B);
}

numeric(A) ::= REAL. {
    A = SystemTypeName("float4");
    					A->location = LOC(B);
}

numeric(A) ::= FLOAT_P opt_float(C). {
    A = C;
    					A->location = LOC(B);
}

numeric(A) ::= DOUBLE_P PRECISION. {
    A = SystemTypeName("float8");
    					A->location = LOC(B);
}

numeric(A) ::= DECIMAL_P opt_type_modifiers(C). {
    A = SystemTypeName("numeric");
    					A->typmods = C;
    					A->location = LOC(B);
}

numeric(A) ::= DEC opt_type_modifiers(C). {
    A = SystemTypeName("numeric");
    					A->typmods = C;
    					A->location = LOC(B);
}

numeric(A) ::= NUMERIC opt_type_modifiers(C). {
    A = SystemTypeName("numeric");
    					A->typmods = C;
    					A->location = LOC(B);
}

numeric(A) ::= BOOLEAN_P. {
    A = SystemTypeName("bool");
    					A->location = LOC(B);
}

/* ----- opt_float ----- */

opt_float(A) ::= LPAREN iconst(C) RPAREN. {
    if (C < 1)
    						ereport(ERROR,
    								(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
    								 errmsg("precision for type float must be at least 1 bit"),
    								 parser_errposition(LOC(C))));
    					else if (C <= 24)
    						A = SystemTypeName("float4");
    					else if (C <= 53)
    						A = SystemTypeName("float8");
    					else
    						ereport(ERROR,
    								(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
    								 errmsg("precision for type float must be less than 54 bits"),
    								 parser_errposition(LOC(C))));
}

opt_float(A) ::= . {
    A = SystemTypeName("float8");
}

/* ----- bit ----- */

bit(A) ::= bitWithLength(B). {
    A = B;
}

bit(A) ::= bitWithoutLength(B). {
    A = B;
}

/* ----- constBit ----- */

constBit(A) ::= bitWithLength(B). {
    A = B;
}

constBit(A) ::= bitWithoutLength(B). {
    A = B;
    					A->typmods = NIL;
}

/* ----- bitWithLength ----- */

bitWithLength(A) ::= BIT opt_varying(C) LPAREN expr_list(E) RPAREN. {
    char *typname;

    					typname = C ? "varbit" : "bit";
    					A = SystemTypeName(typname);
    					A->typmods = E;
    					A->location = LOC(B);
}

/* ----- bitWithoutLength ----- */

bitWithoutLength(A) ::= BIT opt_varying(C). {
    if (C)
    					{
    						A = SystemTypeName("varbit");
    					}
    					else
    					{
    						A = SystemTypeName("bit");
    						A->typmods = list_make1(makeIntConst(1, -1));
    					}
    					A->location = LOC(B);
}

/* ----- character ----- */

character_tn(A) ::= characterWithLength(B). {
    A = B;
}

character_tn(A) ::= characterWithoutLength(B). {
    A = B;
}

/* ----- constCharacter ----- */

constCharacter(A) ::= characterWithLength(B). {
    A = B;
}

constCharacter(A) ::= characterWithoutLength(B). {
    A = B;
    					A->typmods = NIL;
}

/* ----- characterWithLength ----- */

characterWithLength(A) ::= character(B) LPAREN iconst(D) RPAREN. {
    A = SystemTypeName(B);
    					A->typmods = list_make1(makeIntConst(D, LOC(D)));
    					A->location = LOC(B);
}

/* ----- characterWithoutLength ----- */

characterWithoutLength(A) ::= character(B). {
    A = SystemTypeName(B);

    					if (strcmp(B, "bpchar") == 0)
    						A->typmods = list_make1(makeIntConst(1, -1));
    					A->location = LOC(B);
}

/* ----- character ----- */

character(A) ::= CHARACTER opt_varying(C). {
    A = C ? "varchar": "bpchar";
}

character(A) ::= CHAR_P opt_varying(C). {
    A = C ? "varchar": "bpchar";
}

character(A) ::= VARCHAR. {
    A = "varchar";
}

character(A) ::= NATIONAL CHARACTER opt_varying(D). {
    A = D ? "varchar": "bpchar";
}

character(A) ::= NATIONAL CHAR_P opt_varying(D). {
    A = D ? "varchar": "bpchar";
}

character(A) ::= NCHAR opt_varying(C). {
    A = C ? "varchar": "bpchar";
}

/* ----- opt_varying ----- */

opt_varying(A) ::= VARYING. {
    A = true;
}

opt_varying(A) ::= . {
    A = false;
}

/* ----- constDatetime ----- */

constDatetime(A) ::= TIMESTAMP LPAREN iconst(D) RPAREN opt_timezone(F). {
    if (F)
    						A = SystemTypeName("timestamptz");
    					else
    						A = SystemTypeName("timestamp");
    					A->typmods = list_make1(makeIntConst(D, LOC(D)));
    					A->location = LOC(B);
}

constDatetime(A) ::= TIMESTAMP opt_timezone(C). {
    if (C)
    						A = SystemTypeName("timestamptz");
    					else
    						A = SystemTypeName("timestamp");
    					A->location = LOC(B);
}

constDatetime(A) ::= TIME LPAREN iconst(D) RPAREN opt_timezone(F). {
    if (F)
    						A = SystemTypeName("timetz");
    					else
    						A = SystemTypeName("time");
    					A->typmods = list_make1(makeIntConst(D, LOC(D)));
    					A->location = LOC(B);
}

constDatetime(A) ::= TIME opt_timezone(C). {
    if (C)
    						A = SystemTypeName("timetz");
    					else
    						A = SystemTypeName("time");
    					A->location = LOC(B);
}

/* ----- constInterval ----- */

constInterval(A) ::= INTERVAL. {
    A = SystemTypeName("interval");
    					A->location = LOC(B);
}

/* ----- opt_timezone ----- */

opt_timezone(A) ::= WITH_LA TIME ZONE. {
    A = true;
}

opt_timezone(A) ::= WITHOUT_LA TIME ZONE. {
    A = false;
}

opt_timezone(A) ::= . {
    A = false;
}

/* ----- opt_interval ----- */

opt_interval(A) ::= YEAR_P. {
    A = list_make1(makeIntConst(INTERVAL_MASK(YEAR), LOC(B)));
}

opt_interval(A) ::= MONTH_P. {
    A = list_make1(makeIntConst(INTERVAL_MASK(MONTH), LOC(B)));
}

opt_interval(A) ::= DAY_P. {
    A = list_make1(makeIntConst(INTERVAL_MASK(DAY), LOC(B)));
}

opt_interval(A) ::= HOUR_P. {
    A = list_make1(makeIntConst(INTERVAL_MASK(HOUR), LOC(B)));
}

opt_interval(A) ::= MINUTE_P. {
    A = list_make1(makeIntConst(INTERVAL_MASK(MINUTE), LOC(B)));
}

opt_interval(A) ::= interval_second(B). {
    A = B;
}

opt_interval(A) ::= YEAR_P TO MONTH_P. {
    A = list_make1(makeIntConst(INTERVAL_MASK(YEAR) |
    												 INTERVAL_MASK(MONTH), LOC(B)));
}

opt_interval(A) ::= DAY_P TO HOUR_P. {
    A = list_make1(makeIntConst(INTERVAL_MASK(DAY) |
    												 INTERVAL_MASK(HOUR), LOC(B)));
}

opt_interval(A) ::= DAY_P TO MINUTE_P. {
    A = list_make1(makeIntConst(INTERVAL_MASK(DAY) |
    												 INTERVAL_MASK(HOUR) |
    												 INTERVAL_MASK(MINUTE), LOC(B)));
}

opt_interval(A) ::= DAY_P TO interval_second(D). {
    A = D;
    					linitial(A) = makeIntConst(INTERVAL_MASK(DAY) |
    												INTERVAL_MASK(HOUR) |
    												INTERVAL_MASK(MINUTE) |
    												INTERVAL_MASK(SECOND), LOC(B));
}

opt_interval(A) ::= HOUR_P TO MINUTE_P. {
    A = list_make1(makeIntConst(INTERVAL_MASK(HOUR) |
    												 INTERVAL_MASK(MINUTE), LOC(B)));
}

opt_interval(A) ::= HOUR_P TO interval_second(D). {
    A = D;
    					linitial(A) = makeIntConst(INTERVAL_MASK(HOUR) |
    												INTERVAL_MASK(MINUTE) |
    												INTERVAL_MASK(SECOND), LOC(B));
}

opt_interval(A) ::= MINUTE_P TO interval_second(D). {
    A = D;
    					linitial(A) = makeIntConst(INTERVAL_MASK(MINUTE) |
    												INTERVAL_MASK(SECOND), LOC(B));
}

opt_interval(A) ::= . {
    A = NIL;
}

/* ----- interval_second ----- */

interval_second(A) ::= SECOND_P. {
    A = list_make1(makeIntConst(INTERVAL_MASK(SECOND), LOC(B)));
}

interval_second(A) ::= SECOND_P LPAREN iconst(D) RPAREN. {
    A = list_make2(makeIntConst(INTERVAL_MASK(SECOND), LOC(B)),
    									makeIntConst(D, LOC(D)));
}

/* ----- jsonType ----- */

jsonType(A) ::= JSON. {
    A = SystemTypeName("json");
    					A->location = LOC(B);
}

/* ----- a_expr ----- */
```

