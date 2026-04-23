/*
 * pg_gram_helpers.h
 *    Helper function declarations for PostgreSQL grammar semantic actions.
 *
 * These functions were originally static functions in the epilogue of
 * PostgreSQL's gram.y.  They are extracted here so that the Lime-generated
 * parser can call them from its reduction actions.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 */
#ifndef PG_GRAM_HELPERS_H
#define PG_GRAM_HELPERS_H

#include "postgres.h"
#include "nodes/parsenodes.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "parser/parser.h"
#include "parser/gramparse.h"

/* ----------------------------------------------------------------
 * Private struct types used by grammar productions
 *
 * These are defined in gram.y's prologue and are not part of the
 * public PostgreSQL API.  They are needed by some grammar actions
 * and helper functions.
 * ----------------------------------------------------------------
 */

/* Result of privilege_target production */
typedef struct PrivTarget
{
	GrantTargetType targtype;
	ObjectType	objtype;
	List	   *objs;
} PrivTarget;

/* Result of import_qualification production */
typedef struct ImportQual
{
	ImportForeignSchemaType type;
	List	   *table_names;
} ImportQual;

/* Result of select_limit & limit_clause productions */
typedef struct SelectLimit
{
	Node	   *limitOffset;
	Node	   *limitCount;
	LimitOption limitOption;
	ParseLoc	offsetLoc;
	ParseLoc	countLoc;
	ParseLoc	optionLoc;
} SelectLimit;

/* Result of group_clause production */
typedef struct GroupClause
{
	bool		distinct;
	bool		all;
	List	   *list;
} GroupClause;

/* Result of key_action production */
typedef struct KeyAction
{
	char		action;
	List	   *cols;
} KeyAction;

/* Result of key_actions production */
typedef struct KeyActions
{
	KeyAction  *updateAction;
	KeyAction  *deleteAction;
} KeyActions;

/* ----------------------------------------------------------------
 * Constraint attribute bitmask flags
 * ----------------------------------------------------------------
 */
#define CAS_NOT_DEFERRABLE			0x01
#define CAS_DEFERRABLE				0x02
#define CAS_INITIALLY_IMMEDIATE		0x04
#define CAS_INITIALLY_DEFERRED		0x08
#define CAS_NOT_VALID				0x10
#define CAS_NO_INHERIT				0x20
#define CAS_NOT_ENFORCED			0x40
#define CAS_ENFORCED				0x80

/* ----------------------------------------------------------------
 * Helper function declarations
 *
 * Functions that take a core_yyscan_t parameter need access to the
 * scanner for error reporting via parser_yyerror / parser_errposition.
 * ----------------------------------------------------------------
 */

/* AST node construction */
extern RawStmt *makeRawStmt(Node *stmt, int stmt_location);
extern void updateRawStmtEnd(RawStmt *rs, int end_location);
extern Node *makeColumnRef(char *colname, List *indirection,
						   int location, core_yyscan_t yyscanner);
extern Node *makeTypeCast(Node *arg, TypeName *typename, int location);
extern Node *makeStringConstCast(char *str, int location, TypeName *typename);
extern Node *makeIntConst(int val, int location);
extern Node *makeFloatConst(char *str, int location);
extern Node *makeBoolAConst(bool state, int location);
extern Node *makeBitStringConst(char *str, int location);
extern Node *makeNullAConst(int location);
extern Node *makeAConst(Node *v, int location);
extern RoleSpec *makeRoleSpec(RoleSpecType type, int location);

/* Expression construction */
extern Node *makeSetOp(SetOperation op, bool all, Node *larg, Node *rarg);
extern Node *doNegate(Node *n, int location);
extern void doNegateFloat(Float *v);
extern Node *makeAndExpr(Node *lexpr, Node *rexpr, int location);
extern Node *makeOrExpr(Node *lexpr, Node *rexpr, int location);
extern Node *makeNotExpr(Node *expr, int location);
extern Node *makeAArrayExpr(List *elements, int location, int location_end);
extern Node *makeSQLValueFunction(SQLValueFunctionOp op, int32 typmod,
								  int location);
extern Node *makeXmlExpr(XmlExprOp op, char *name, List *named_args,
						 List *args, int location);
extern Node *makeRecursiveViewSelect(char *relname, List *aliases, Node *query);

/* Validation and extraction */
extern void check_qualified_name(List *names, core_yyscan_t yyscanner);
extern List *check_func_name(List *names, core_yyscan_t yyscanner);
extern List *check_indirection(List *indirection, core_yyscan_t yyscanner);
extern List *extractArgTypes(List *parameters);
extern List *extractAggrArgTypes(List *aggrargs);
extern List *makeOrderedSetArgs(List *directargs, List *orderedargs,
								core_yyscan_t yyscanner);
extern void insertSelectOptions(SelectStmt *stmt,
								List *sortClause, List *lockingClause,
								SelectLimit *limitClause,
								WithClause *withClause,
								core_yyscan_t yyscanner);
extern List *mergeTableFuncParameters(List *func_args, List *columns,
									  core_yyscan_t yyscanner);
extern TypeName *TableFuncTypeName(List *columns);
extern RangeVar *makeRangeVarFromAnyName(List *names, int position,
										 core_yyscan_t yyscanner);
extern RangeVar *makeRangeVarFromQualifiedName(char *name, List *namelist,
											   int location,
											   core_yyscan_t yyscanner);
extern void SplitColQualList(List *qualList,
							 List **constraintList,
							 CollateClause **collClause,
							 core_yyscan_t yyscanner);
extern void processCASbits(int cas_bits, int location, const char *constrType,
						   bool *deferrable, bool *initdeferred,
						   bool *is_enforced,
						   bool *not_valid, bool *no_inherit,
						   core_yyscan_t yyscanner);
extern PartitionStrategy parsePartitionStrategy(char *strategy, int location,
												core_yyscan_t yyscanner);

/* Publication helpers */
extern void preprocess_pub_all_objtype_list(List *all_objects_list,
											List **pubobjects,
											bool *all_tables,
											bool *all_sequences,
											core_yyscan_t yyscanner);
extern void preprocess_pubobj_list(List *pubobjspec_list,
								   core_yyscan_t yyscanner);

/* Built-in name constructors (these are not static in PG's gram.y) */
extern List *SystemFuncName(char *name);
extern TypeName *SystemTypeName(char *name);

/* Parser initialization */
extern void parser_init(base_yy_extra_type *yyext);

#endif /* PG_GRAM_HELPERS_H */
