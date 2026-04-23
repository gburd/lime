/*
 * pg_gram_helpers.c
 *    Helper functions for PostgreSQL grammar semantic actions.
 *
 * These functions were originally static functions in the epilogue
 * (after the second %%) of PostgreSQL's gram.y.  They are extracted
 * into a separate compilation unit so the Lime-generated parser can
 * call them from its reduction actions.
 *
 * The functions are essentially unchanged from the PostgreSQL originals;
 * the only modifications are:
 *   - Removed the "static" qualifier so they can be called externally.
 *   - Added the pg_gram_helpers.h header with declarations.
 *   - The parser_yyerror / parser_errposition macros must be available
 *     via the included headers.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 */
#include "pg_gram_helpers.h"

#include <ctype.h>
#include <limits.h>

#include "catalog/index.h"
#include "catalog/namespace.h"
#include "catalog/pg_am.h"
#include "catalog/pg_trigger.h"
#include "commands/defrem.h"
#include "commands/trigger.h"
#include "utils/datetime.h"
#include "utils/xml.h"

/*
 * Convenience macros -- these must match what the grammar %include block
 * sets up.  In PostgreSQL's gram.y they are defined in the prologue.
 */
#define parser_yyerror(msg)  scanner_yyerror(msg, yyscanner)
#define parser_errposition(pos)  scanner_errposition(pos, yyscanner)


/* ----------------------------------------------------------------
 *                    AST Node Construction
 * ----------------------------------------------------------------
 */

RawStmt *
makeRawStmt(Node *stmt, int stmt_location)
{
	RawStmt    *rs = makeNode(RawStmt);

	rs->stmt = stmt;
	rs->stmt_location = stmt_location;
	rs->stmt_len = 0;			/* might get changed later */
	return rs;
}

void
updateRawStmtEnd(RawStmt *rs, int end_location)
{
	/*
	 * If we already set the length, don't change it.  This is for situations
	 * like "select foo ;; select bar" where the same statement will be last
	 * in the string for more than one semicolon.
	 */
	if (rs->stmt_len > 0)
		return;

	/* OK, update length of RawStmt */
	rs->stmt_len = end_location - rs->stmt_location;
}

Node *
makeColumnRef(char *colname, List *indirection,
			  int location, core_yyscan_t yyscanner)
{
	/*
	 * Generate a ColumnRef node, with an A_Indirection node added if there is
	 * any subscripting in the specified indirection list.  However, any field
	 * selection at the start of the indirection list must be transposed into
	 * the "fields" part of the ColumnRef node.
	 */
	ColumnRef  *c = makeNode(ColumnRef);
	int			nfields = 0;
	ListCell   *l;

	c->location = location;
	foreach(l, indirection)
	{
		if (IsA(lfirst(l), A_Indices))
		{
			A_Indirection *i = makeNode(A_Indirection);

			if (nfields == 0)
			{
				/* easy case - all indirection goes to A_Indirection */
				c->fields = list_make1(makeString(colname));
				i->indirection = check_indirection(indirection, yyscanner);
			}
			else
			{
				/* got to split the list in two */
				i->indirection = check_indirection(list_copy_tail(indirection,
																  nfields),
												   yyscanner);
				indirection = list_truncate(indirection, nfields);
				c->fields = lcons(makeString(colname), indirection);
			}
			i->arg = (Node *) c;
			return (Node *) i;
		}
		else if (IsA(lfirst(l), A_Star))
		{
			/* We only allow '*' at the end of a ColumnRef */
			if (lnext(indirection, l) != NULL)
				parser_yyerror("improper use of \"*\"");
		}
		nfields++;
	}
	/* No subscripting, so all indirection gets added to field list */
	c->fields = lcons(makeString(colname), indirection);
	return (Node *) c;
}

Node *
makeTypeCast(Node *arg, TypeName *typename, int location)
{
	TypeCast   *n = makeNode(TypeCast);

	n->arg = arg;
	n->typeName = typename;
	n->location = location;
	return (Node *) n;
}

Node *
makeStringConstCast(char *str, int location, TypeName *typename)
{
	Node	   *s = makeStringConst(str, location);

	return makeTypeCast(s, typename, -1);
}

Node *
makeIntConst(int val, int location)
{
	A_Const    *n = makeNode(A_Const);

	n->val.ival.type = T_Integer;
	n->val.ival.ival = val;
	n->location = location;

	return (Node *) n;
}

Node *
makeFloatConst(char *str, int location)
{
	A_Const    *n = makeNode(A_Const);

	n->val.fval.type = T_Float;
	n->val.fval.fval = str;
	n->location = location;

	return (Node *) n;
}

Node *
makeBoolAConst(bool state, int location)
{
	A_Const    *n = makeNode(A_Const);

	n->val.boolval.type = T_Boolean;
	n->val.boolval.boolval = state;
	n->location = location;

	return (Node *) n;
}

Node *
makeBitStringConst(char *str, int location)
{
	A_Const    *n = makeNode(A_Const);

	n->val.bsval.type = T_BitString;
	n->val.bsval.bsval = str;
	n->location = location;

	return (Node *) n;
}

Node *
makeNullAConst(int location)
{
	A_Const    *n = makeNode(A_Const);

	n->isnull = true;
	n->location = location;

	return (Node *) n;
}

Node *
makeAConst(Node *v, int location)
{
	Node	   *n;

	switch (v->type)
	{
		case T_Float:
			n = makeFloatConst(castNode(Float, v)->fval, location);
			break;

		case T_Integer:
			n = makeIntConst(castNode(Integer, v)->ival, location);
			break;

		default:
			/* currently not used */
			Assert(false);
			n = NULL;
	}

	return n;
}

RoleSpec *
makeRoleSpec(RoleSpecType type, int location)
{
	RoleSpec   *spec = makeNode(RoleSpec);

	spec->roletype = type;
	spec->location = location;

	return spec;
}


/* ----------------------------------------------------------------
 *                    Expression Construction
 * ----------------------------------------------------------------
 */

Node *
makeSetOp(SetOperation op, bool all, Node *larg, Node *rarg)
{
	SelectStmt *n = makeNode(SelectStmt);

	n->op = op;
	n->all = all;
	n->larg = (SelectStmt *) larg;
	n->rarg = (SelectStmt *) rarg;
	return (Node *) n;
}

/*
 * SystemFuncName()
 * Build a properly-qualified reference to a built-in function.
 */
List *
SystemFuncName(char *name)
{
	return list_make2(makeString("pg_catalog"), makeString(name));
}

/*
 * SystemTypeName()
 * Build a properly-qualified reference to a built-in type.
 *
 * typmod is defaulted, but may be changed afterwards by caller.
 * Likewise for the location.
 */
TypeName *
SystemTypeName(char *name)
{
	return makeTypeNameFromNameList(list_make2(makeString("pg_catalog"),
											   makeString(name)));
}

/*
 * doNegate()
 * Handle negation of a numeric constant.
 *
 * Formerly, we did this here because the optimizer couldn't cope with
 * indexquals that looked like "var = -4" --- it wants "var = const"
 * and a unary minus operator applied to a constant didn't qualify.
 * As of Postgres 7.0, that problem doesn't exist anymore because there
 * is a constant-subexpression simplifier in the optimizer.  However,
 * there's still a good reason for doing this here, which is that we can
 * postpone committing to a particular internal representation for simple
 * negative constants.  It's better to leave "-123.456" in string form
 * until we know what the desired type is.
 */
Node *
doNegate(Node *n, int location)
{
	if (IsA(n, A_Const))
	{
		A_Const    *con = (A_Const *) n;

		/* report the constant's location as that of the '-' sign */
		con->location = location;

		if (IsA(&con->val, Integer))
		{
			con->val.ival.ival = -con->val.ival.ival;
			return n;
		}
		if (IsA(&con->val, Float))
		{
			doNegateFloat(&con->val.fval);
			return n;
		}
	}

	return (Node *) makeSimpleA_Expr(AEXPR_OP, "-", NULL, n, location);
}

void
doNegateFloat(Float *v)
{
	char	   *oldval = v->fval;

	if (*oldval == '+')
		oldval++;
	if (*oldval == '-')
		v->fval = oldval + 1;	/* just strip the '-' */
	else
		v->fval = psprintf("-%s", oldval);
}

Node *
makeAndExpr(Node *lexpr, Node *rexpr, int location)
{
	/* Flatten "a AND b AND c ..." to a single BoolExpr on sight */
	if (IsA(lexpr, BoolExpr))
	{
		BoolExpr   *blexpr = (BoolExpr *) lexpr;

		if (blexpr->boolop == AND_EXPR)
		{
			blexpr->args = lappend(blexpr->args, rexpr);
			return (Node *) blexpr;
		}
	}
	return (Node *) makeBoolExpr(AND_EXPR, list_make2(lexpr, rexpr), location);
}

Node *
makeOrExpr(Node *lexpr, Node *rexpr, int location)
{
	/* Flatten "a OR b OR c ..." to a single BoolExpr on sight */
	if (IsA(lexpr, BoolExpr))
	{
		BoolExpr   *blexpr = (BoolExpr *) lexpr;

		if (blexpr->boolop == OR_EXPR)
		{
			blexpr->args = lappend(blexpr->args, rexpr);
			return (Node *) blexpr;
		}
	}
	return (Node *) makeBoolExpr(OR_EXPR, list_make2(lexpr, rexpr), location);
}

Node *
makeNotExpr(Node *expr, int location)
{
	return (Node *) makeBoolExpr(NOT_EXPR, list_make1(expr), location);
}

Node *
makeAArrayExpr(List *elements, int location, int location_end)
{
	A_ArrayExpr *n = makeNode(A_ArrayExpr);

	n->elements = elements;
	n->location = location;
	n->list_start = location;
	n->list_end = location_end;
	return (Node *) n;
}

Node *
makeSQLValueFunction(SQLValueFunctionOp op, int32 typmod, int location)
{
	SQLValueFunction *svf = makeNode(SQLValueFunction);

	svf->op = op;
	/* svf->type will be filled during parse analysis */
	svf->typmod = typmod;
	svf->location = location;
	return (Node *) svf;
}

Node *
makeXmlExpr(XmlExprOp op, char *name, List *named_args, List *args,
			int location)
{
	XmlExpr    *x = makeNode(XmlExpr);

	x->op = op;
	x->name = name;

	/*
	 * named_args is a list of ResTarget; it'll be split apart into separate
	 * expression and name lists in transformXmlExpr().
	 */
	x->named_args = named_args;
	x->arg_names = NIL;
	x->args = args;
	/* xmloption, if relevant, must be filled in by caller */
	/* type and typmod will be filled in during parse analysis */
	x->type = InvalidOid;		/* marks the node as not analyzed */
	x->location = location;
	return (Node *) x;
}


/* ----------------------------------------------------------------
 *                  Validation and Extraction
 * ----------------------------------------------------------------
 */

/*
 * check_qualified_name --- check the result of qualified_name production
 *
 * It's easiest to let the grammar production for qualified_name allow
 * subscripts and '*', which we then must reject here.
 */
void
check_qualified_name(List *names, core_yyscan_t yyscanner)
{
	ListCell   *i;

	foreach(i, names)
	{
		if (!IsA(lfirst(i), String))
			parser_yyerror("syntax error");
	}
}

/*
 * check_func_name --- check the result of func_name production
 *
 * It's easiest to let the grammar production for func_name allow subscripts
 * and '*', which we then must reject here.
 */
List *
check_func_name(List *names, core_yyscan_t yyscanner)
{
	ListCell   *i;

	foreach(i, names)
	{
		if (!IsA(lfirst(i), String))
			parser_yyerror("syntax error");
	}
	return names;
}

/*
 * check_indirection --- check the result of indirection production
 *
 * We only allow '*' at the end of the list, but it's hard to enforce that
 * in the grammar, so do it here.
 */
List *
check_indirection(List *indirection, core_yyscan_t yyscanner)
{
	ListCell   *l;

	foreach(l, indirection)
	{
		if (IsA(lfirst(l), A_Star))
		{
			if (lnext(indirection, l) != NULL)
				parser_yyerror("improper use of \"*\"");
		}
	}
	return indirection;
}

/*
 * extractArgTypes()
 * Given a list of FunctionParameter nodes, extract a list of just the
 * argument types (TypeNames) for input parameters only.  This is what
 * is needed to look up an existing function, which is what is wanted by
 * the productions that use this call.
 */
List *
extractArgTypes(List *parameters)
{
	List	   *result = NIL;
	ListCell   *i;

	foreach(i, parameters)
	{
		FunctionParameter *p = (FunctionParameter *) lfirst(i);

		if (p->mode != FUNC_PARAM_OUT && p->mode != FUNC_PARAM_TABLE)
			result = lappend(result, p->argType);
	}
	return result;
}

/*
 * extractAggrArgTypes()
 * As above, but work from the output of the aggr_args production.
 */
List *
extractAggrArgTypes(List *aggrargs)
{
	Assert(list_length(aggrargs) == 2);
	return extractArgTypes((List *) linitial(aggrargs));
}

/*
 * makeOrderedSetArgs()
 * Build the result of the aggr_args production (which see the comments for).
 * This handles only the case where both given lists are nonempty, so that
 * we have to deal with multiple VARIADIC arguments.
 */
List *
makeOrderedSetArgs(List *directargs, List *orderedargs,
				   core_yyscan_t yyscanner)
{
	FunctionParameter *lastd = (FunctionParameter *) llast(directargs);
	Integer    *ndirectargs;

	/* No restriction unless last direct arg is VARIADIC */
	if (lastd->mode == FUNC_PARAM_VARIADIC)
	{
		FunctionParameter *firsto = (FunctionParameter *) linitial(orderedargs);

		/*
		 * We ignore the names, though the aggr_arg production allows them; it
		 * doesn't allow default values, so those need not be checked.
		 */
		if (list_length(orderedargs) != 1 ||
			firsto->mode != FUNC_PARAM_VARIADIC ||
			!equal(lastd->argType, firsto->argType))
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("an ordered-set aggregate with a VARIADIC direct argument must have one VARIADIC aggregated argument of the same data type"),
					 parser_errposition(firsto->location)));

		/* OK, drop the duplicate VARIADIC argument from the internal form */
		orderedargs = NIL;
	}

	/* don't merge into the next line, as list_concat changes directargs */
	ndirectargs = makeInteger(list_length(directargs));

	return list_make2(list_concat(directargs, orderedargs),
					  ndirectargs);
}

/*
 * insertSelectOptions()
 * Insert ORDER BY, etc into an already-constructed SelectStmt.
 *
 * This routine is just to avoid duplicating code in SelectStmt productions.
 */
void
insertSelectOptions(SelectStmt *stmt,
					List *sortClause, List *lockingClause,
					SelectLimit *limitClause,
					WithClause *withClause,
					core_yyscan_t yyscanner)
{
	Assert(IsA(stmt, SelectStmt));

	/*
	 * Tests here are to reject constructs like
	 *	(SELECT foo ORDER BY bar) ORDER BY baz
	 */
	if (sortClause)
	{
		if (stmt->sortClause)
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("multiple ORDER BY clauses not allowed"),
					 parser_errposition(exprLocation((Node *) sortClause))));
		stmt->sortClause = sortClause;
	}
	/* We can handle multiple locking clauses, though */
	stmt->lockingClause = list_concat(stmt->lockingClause, lockingClause);
	if (limitClause && limitClause->limitOffset)
	{
		if (stmt->limitOffset)
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("multiple OFFSET clauses not allowed"),
					 parser_errposition(limitClause->offsetLoc)));
		stmt->limitOffset = limitClause->limitOffset;
	}
	if (limitClause && limitClause->limitCount)
	{
		if (stmt->limitCount)
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("multiple LIMIT clauses not allowed"),
					 parser_errposition(limitClause->countLoc)));
		stmt->limitCount = limitClause->limitCount;
	}
	if (limitClause)
	{
		/* If there was a conflict, we must have detected it above */
		Assert(!stmt->limitOption);
		if (!stmt->sortClause && limitClause->limitOption == LIMIT_OPTION_WITH_TIES)
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("WITH TIES cannot be specified without ORDER BY clause"),
					 parser_errposition(limitClause->optionLoc)));
		if (limitClause->limitOption == LIMIT_OPTION_WITH_TIES && stmt->lockingClause)
		{
			ListCell   *lc;

			foreach(lc, stmt->lockingClause)
			{
				LockingClause *lock = lfirst_node(LockingClause, lc);

				if (lock->waitPolicy == LockWaitSkip)
					ereport(ERROR,
							(errcode(ERRCODE_SYNTAX_ERROR),
							 errmsg("%s and %s options cannot be used together",
									"SKIP LOCKED", "WITH TIES"),
							 parser_errposition(limitClause->optionLoc)));
			}
		}
		stmt->limitOption = limitClause->limitOption;
	}
	if (withClause)
	{
		if (stmt->withClause)
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("multiple WITH clauses not allowed"),
					 parser_errposition(exprLocation((Node *) withClause))));
		stmt->withClause = withClause;
	}
}

/*
 * Merge the input and output parameters of a table function.
 */
List *
mergeTableFuncParameters(List *func_args, List *columns,
						 core_yyscan_t yyscanner)
{
	ListCell   *lc;

	/* Explicit OUT and INOUT parameters shouldn't be used in this syntax */
	foreach(lc, func_args)
	{
		FunctionParameter *p = (FunctionParameter *) lfirst(lc);

		if (p->mode != FUNC_PARAM_DEFAULT &&
			p->mode != FUNC_PARAM_IN &&
			p->mode != FUNC_PARAM_VARIADIC)
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("OUT and INOUT arguments aren't allowed in TABLE functions"),
					 parser_errposition(p->location)));
	}

	return list_concat(func_args, columns);
}

/*
 * Determine return type of a TABLE function.  A single result column
 * returns setof that column's type; otherwise return setof record.
 */
TypeName *
TableFuncTypeName(List *columns)
{
	TypeName   *result;

	if (list_length(columns) == 1)
	{
		FunctionParameter *p = (FunctionParameter *) linitial(columns);

		result = copyObject(p->argType);
	}
	else
		result = SystemTypeName("record");

	result->setof = true;

	return result;
}

/*
 * Convert a list of (dotted) names to a RangeVar (like
 * makeRangeVarFromNameList, but with position support).  The
 * "AnyName" refers to the any_name production in the grammar.
 */
RangeVar *
makeRangeVarFromAnyName(List *names, int position, core_yyscan_t yyscanner)
{
	RangeVar   *r = makeNode(RangeVar);

	switch (list_length(names))
	{
		case 1:
			r->catalogname = NULL;
			r->schemaname = NULL;
			r->relname = strVal(linitial(names));
			break;
		case 2:
			r->catalogname = NULL;
			r->schemaname = strVal(linitial(names));
			r->relname = strVal(lsecond(names));
			break;
		case 3:
			r->catalogname = strVal(linitial(names));
			r->schemaname = strVal(lsecond(names));
			r->relname = strVal(lthird(names));
			break;
		default:
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("improper qualified name (too many dotted names): %s",
							NameListToString(names)),
					 parser_errposition(position)));
			break;
	}

	r->relpersistence = RELPERSISTENCE_PERMANENT;
	r->location = position;

	return r;
}

/*
 * Convert a relation_name with name and namelist to a RangeVar using
 * makeRangeVar.
 */
RangeVar *
makeRangeVarFromQualifiedName(char *name, List *namelist, int location,
							  core_yyscan_t yyscanner)
{
	RangeVar   *r;

	check_qualified_name(namelist, yyscanner);
	r = makeRangeVar(NULL, NULL, location);

	switch (list_length(namelist))
	{
		case 1:
			r->catalogname = NULL;
			r->schemaname = name;
			r->relname = strVal(linitial(namelist));
			break;
		case 2:
			r->catalogname = name;
			r->schemaname = strVal(linitial(namelist));
			r->relname = strVal(lsecond(namelist));
			break;
		default:
			ereport(ERROR,
					errcode(ERRCODE_SYNTAX_ERROR),
					errmsg("improper qualified name (too many dotted names): %s",
						   NameListToString(lcons(makeString(name), namelist))),
					parser_errposition(location));
			break;
	}

	return r;
}

/*
 * Separate Constraint nodes from COLLATE clauses in a ColQualList
 */
void
SplitColQualList(List *qualList,
				 List **constraintList, CollateClause **collClause,
				 core_yyscan_t yyscanner)
{
	ListCell   *cell;

	*collClause = NULL;
	foreach(cell, qualList)
	{
		Node	   *n = (Node *) lfirst(cell);

		if (IsA(n, Constraint))
		{
			/* keep it in list */
			continue;
		}
		if (IsA(n, CollateClause))
		{
			CollateClause *c = (CollateClause *) n;

			if (*collClause)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("multiple COLLATE clauses not allowed"),
						 parser_errposition(c->location)));
			*collClause = c;
		}
		else
			elog(ERROR, "unexpected node type %d", (int) n->type);
		/* remove non-Constraint nodes from qualList */
		qualList = foreach_delete_current(qualList, cell);
	}
	*constraintList = qualList;
}

/*
 * Process result of ConstraintAttributeSpec, and set appropriate bool flags
 * in the output command node.  Pass NULL for any flags the particular
 * command doesn't support.
 */
void
processCASbits(int cas_bits, int location, const char *constrType,
			   bool *deferrable, bool *initdeferred, bool *is_enforced,
			   bool *not_valid, bool *no_inherit, core_yyscan_t yyscanner)
{
	/* defaults */
	if (deferrable)
		*deferrable = false;
	if (initdeferred)
		*initdeferred = false;
	if (not_valid)
		*not_valid = false;
	if (is_enforced)
		*is_enforced = true;

	if (cas_bits & (CAS_DEFERRABLE | CAS_INITIALLY_DEFERRED))
	{
		if (deferrable)
			*deferrable = true;
		else
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			/* translator: %s is CHECK, UNIQUE, or similar */
					 errmsg("%s constraints cannot be marked DEFERRABLE",
							constrType),
					 parser_errposition(location)));
	}

	if (cas_bits & CAS_INITIALLY_DEFERRED)
	{
		if (initdeferred)
			*initdeferred = true;
		else
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			/* translator: %s is CHECK, UNIQUE, or similar */
					 errmsg("%s constraints cannot be marked DEFERRABLE",
							constrType),
					 parser_errposition(location)));
	}

	if (cas_bits & CAS_NOT_VALID)
	{
		if (not_valid)
			*not_valid = true;
		else
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			/* translator: %s is CHECK, UNIQUE, or similar */
					 errmsg("%s constraints cannot be marked NOT VALID",
							constrType),
					 parser_errposition(location)));
	}

	if (cas_bits & CAS_NO_INHERIT)
	{
		if (no_inherit)
			*no_inherit = true;
		else
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			/* translator: %s is CHECK, UNIQUE, or similar */
					 errmsg("%s constraints cannot be marked NO INHERIT",
							constrType),
					 parser_errposition(location)));
	}

	if (cas_bits & CAS_NOT_ENFORCED)
	{
		if (is_enforced)
			*is_enforced = false;
		else
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 /* translator: %s is CHECK, UNIQUE, or similar */
					 errmsg("%s constraints cannot be marked NOT ENFORCED",
							constrType),
					 parser_errposition(location)));

		/*
		 * NB: The validated status is irrelevant when the constraint is set to
		 * NOT ENFORCED, but for consistency, it should be set accordingly.
		 * This ensures that if the constraint is later changed to ENFORCED, it
		 * will automatically be in the correct NOT VALIDATED state.
		 */
		if (not_valid)
			*not_valid = true;
	}

	if (cas_bits & CAS_ENFORCED)
	{
		if (is_enforced)
			*is_enforced = true;
		else
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 /* translator: %s is CHECK, UNIQUE, or similar */
					 errmsg("%s constraints cannot be marked ENFORCED",
							constrType),
					 parser_errposition(location)));
	}
}

/*
 * Parse a user-supplied partition strategy string into parse node
 * PartitionStrategy representation, or die trying.
 */
PartitionStrategy
parsePartitionStrategy(char *strategy, int location, core_yyscan_t yyscanner)
{
	if (pg_strcasecmp(strategy, "list") == 0)
		return PARTITION_STRATEGY_LIST;
	else if (pg_strcasecmp(strategy, "range") == 0)
		return PARTITION_STRATEGY_RANGE;
	else if (pg_strcasecmp(strategy, "hash") == 0)
		return PARTITION_STRATEGY_HASH;

	ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("unrecognized partitioning strategy \"%s\"", strategy),
			 parser_errposition(location)));
	return PARTITION_STRATEGY_LIST; /* keep compiler quiet */
}


/* ----------------------------------------------------------------
 *                    Publication Helpers
 * ----------------------------------------------------------------
 */

/*
 * Process all_objects_list to set all_tables and/or all_sequences.
 * Also, checks if the pub_object_type has been specified more than once.
 */
void
preprocess_pub_all_objtype_list(List *all_objects_list, List **pubobjects,
								bool *all_tables, bool *all_sequences,
								core_yyscan_t yyscanner)
{
	if (!all_objects_list)
		return;

	*all_tables = false;
	*all_sequences = false;

	foreach_ptr(PublicationAllObjSpec, obj, all_objects_list)
	{
		if (obj->pubobjtype == PUBLICATION_ALL_TABLES)
		{
			if (*all_tables)
				ereport(ERROR,
						errcode(ERRCODE_SYNTAX_ERROR),
						errmsg("invalid publication object list"),
						errdetail("ALL TABLES can be specified only once."),
						parser_errposition(obj->location));

			*all_tables = true;
			*pubobjects = list_concat(*pubobjects, obj->except_tables);
		}
		else if (obj->pubobjtype == PUBLICATION_ALL_SEQUENCES)
		{
			if (*all_sequences)
				ereport(ERROR,
					errcode(ERRCODE_SYNTAX_ERROR),
						errmsg("invalid publication object list"),
						errdetail("ALL SEQUENCES can be specified only once."),
						parser_errposition(obj->location));

			*all_sequences = true;
		}
	}
}

/*
 * Process pubobjspec_list to check for errors in any of the objects and
 * convert PUBLICATIONOBJ_CONTINUATION into appropriate PublicationObjSpecType.
 */
void
preprocess_pubobj_list(List *pubobjspec_list, core_yyscan_t yyscanner)
{
	ListCell   *cell;
	PublicationObjSpec *pubobj;
	PublicationObjSpecType prevobjtype = PUBLICATIONOBJ_CONTINUATION;

	if (!pubobjspec_list)
		return;

	pubobj = (PublicationObjSpec *) linitial(pubobjspec_list);
	if (pubobj->pubobjtype == PUBLICATIONOBJ_CONTINUATION)
		ereport(ERROR,
				errcode(ERRCODE_SYNTAX_ERROR),
				errmsg("invalid publication object list"),
				errdetail("One of TABLE or TABLES IN SCHEMA must be specified before a standalone table or schema name."),
				parser_errposition(pubobj->location));

	foreach(cell, pubobjspec_list)
	{
		pubobj = (PublicationObjSpec *) lfirst(cell);

		if (pubobj->pubobjtype == PUBLICATIONOBJ_CONTINUATION)
			pubobj->pubobjtype = prevobjtype;

		if (pubobj->pubobjtype == PUBLICATIONOBJ_TABLE)
		{
			/* relation name or pubtable must be set for this type of object */
			if (!pubobj->name && !pubobj->pubtable)
				ereport(ERROR,
						errcode(ERRCODE_SYNTAX_ERROR),
						errmsg("invalid table name"),
						parser_errposition(pubobj->location));

			if (pubobj->name)
			{
				/* convert it to PublicationTable */
				PublicationTable *pubtable = makeNode(PublicationTable);

				pubtable->relation =
					makeRangeVar(NULL, pubobj->name, pubobj->location);
				pubobj->pubtable = pubtable;
				pubobj->name = NULL;
			}
		}
		else if (pubobj->pubobjtype == PUBLICATIONOBJ_TABLES_IN_SCHEMA ||
				 pubobj->pubobjtype == PUBLICATIONOBJ_TABLES_IN_CUR_SCHEMA)
		{
			/* WHERE clause is not allowed on a schema object */
			if (pubobj->pubtable && pubobj->pubtable->whereClause)
				ereport(ERROR,
						errcode(ERRCODE_SYNTAX_ERROR),
						errmsg("WHERE clause not allowed for schema"),
						parser_errposition(pubobj->location));

			/* Column list is not allowed on a schema object */
			if (pubobj->pubtable && pubobj->pubtable->columns)
				ereport(ERROR,
						errcode(ERRCODE_SYNTAX_ERROR),
						errmsg("column specification not allowed for schema"),
						parser_errposition(pubobj->location));

			/*
			 * We can distinguish between the different type of schema objects
			 * based on whether name and pubtable is set.
			 */
			if (pubobj->name)
				pubobj->pubobjtype = PUBLICATIONOBJ_TABLES_IN_SCHEMA;
			else if (!pubobj->name && !pubobj->pubtable)
				pubobj->pubobjtype = PUBLICATIONOBJ_TABLES_IN_CUR_SCHEMA;
			else
				ereport(ERROR,
						errcode(ERRCODE_SYNTAX_ERROR),
						errmsg("invalid schema name"),
						parser_errposition(pubobj->location));
		}

		prevobjtype = pubobj->pubobjtype;
	}
}


/* ----------------------------------------------------------------
 *                    Recursive View Helper
 * ----------------------------------------------------------------
 */

/*
 * Recursive view transformation
 *
 * Convert
 *     CREATE RECURSIVE VIEW relname (aliases) AS query
 * to
 *     CREATE VIEW relname (aliases) AS
 *         WITH RECURSIVE relname (aliases) AS (query)
 *         SELECT aliases FROM relname
 *
 * Actually, just the WITH ... part, which is then inserted into the original
 * view definition as the query.
 */
Node *
makeRecursiveViewSelect(char *relname, List *aliases, Node *query)
{
	SelectStmt *s = makeNode(SelectStmt);
	WithClause *w = makeNode(WithClause);
	CommonTableExpr *cte = makeNode(CommonTableExpr);
	List	   *tl = NIL;
	ListCell   *lc;

	/* create common table expression */
	cte->ctename = relname;
	cte->aliascolnames = aliases;
	cte->ctematerialized = CTEMaterializeDefault;
	cte->ctequery = query;
	cte->location = -1;

	/* create WITH clause and attach CTE */
	w->recursive = true;
	w->ctes = list_make1(cte);
	w->location = -1;

	/*
	 * create target list for the new SELECT from the alias list of the
	 * recursive view specification
	 */
	foreach(lc, aliases)
	{
		ResTarget  *rt = makeNode(ResTarget);

		rt->name = NULL;
		rt->indirection = NIL;
		rt->val = makeColumnRef(strVal(lfirst(lc)), NIL, -1, 0);
		rt->location = -1;

		tl = lappend(tl, rt);
	}

	/*
	 * create new SELECT combining WITH clause, target list, and fake FROM
	 * clause
	 */
	s->withClause = w;
	s->targetList = tl;
	s->fromClause = list_make1(makeRangeVar(NULL, relname, -1));

	return (Node *) s;
}


/* ----------------------------------------------------------------
 *                    Parser Initialization
 * ----------------------------------------------------------------
 */

/*
 * parser_init()
 * Initialize to parse one query string
 */
void
parser_init(base_yy_extra_type *yyext)
{
	yyext->parsetree = NIL;		/* in case grammar forgets to set it */
}
