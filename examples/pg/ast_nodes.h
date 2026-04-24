/*
 * ast_nodes.h - PostgreSQL AST node definitions for Lime parser generator
 *
 * Extracted from PostgreSQL 18devel (src/backend/parser/gram.y and
 * src/include/nodes/ headers). These definitions provide the AST types
 * needed by the Lime-generated PostgreSQL SQL parser.
 *
 * This is a self-contained header that does not depend on the full
 * PostgreSQL source tree. Structures are kept compatible with PostgreSQL
 * for later AST comparison testing.
 */

#ifndef PG_AST_NODES_H
#define PG_AST_NODES_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ----------------------------------------------------------------
 * Basic types
 * ----------------------------------------------------------------
 */

typedef int32_t int32;
typedef int ParseLoc;   /* location in the source string */

/* ----------------------------------------------------------------
 * NodeTag enum - identifies every AST node type
 *
 * Extracted from all makeNode() calls in gram.y. Tags are grouped by
 * category: base values, statement nodes, expression nodes, utility
 * types, and support types.
 * ----------------------------------------------------------------
 */
typedef enum NodeTag
{
    T_Invalid = 0,

    /* ---- Value nodes ---- */
    T_Integer,
    T_Float,
    T_Boolean,
    T_String,
    T_BitString,
    T_Null,

    /* ---- List node ---- */
    T_List,
    T_IntList,
    T_OidList,

    /* ---- DML Statement nodes ---- */
    T_SelectStmt,
    T_InsertStmt,
    T_UpdateStmt,
    T_DeleteStmt,
    T_MergeStmt,
    T_RawStmt,
    T_PLAssignStmt,

    /* ---- DDL Statement nodes ---- */
    T_CreateStmt,
    T_CreateTableAsStmt,
    T_CreateSchemaStmt,
    T_CreateSeqStmt,
    T_CreateStatsStmt,
    T_CreateDomainStmt,
    T_CreateEnumStmt,
    T_CreateRangeStmt,
    T_CreateFunctionStmt,
    T_CreateTrigStmt,
    T_CreateEventTrigStmt,
    T_CreateRoleStmt,
    T_CreatePolicyStmt,
    T_CreatePropGraphStmt,
    T_CreateExtensionStmt,
    T_CreateFdwStmt,
    T_CreateForeignServerStmt,
    T_CreateForeignTableStmt,
    T_CreateUserMappingStmt,
    T_CreateCastStmt,
    T_CreateConversionStmt,
    T_CreateOpClassStmt,
    T_CreateOpFamilyStmt,
    T_CreatePLangStmt,
    T_CreatePublicationStmt,
    T_CreateSubscriptionStmt,
    T_CreateTableSpaceStmt,
    T_CreateTransformStmt,
    T_CreateAmStmt,
    T_CreatedbStmt,
    T_CompositeTypeStmt,

    /* ---- Alter Statement nodes ---- */
    T_AlterTableStmt,
    T_AlterTableCmd,
    T_ATAlterConstraint,
    T_AlterTableMoveAllStmt,
    T_AlterTableSpaceOptionsStmt,
    T_AlterDomainStmt,
    T_AlterEnumStmt,
    T_AlterFunctionStmt,
    T_AlterRoleStmt,
    T_AlterRoleSetStmt,
    T_AlterSeqStmt,
    T_AlterSystemStmt,
    T_AlterDatabaseStmt,
    T_AlterDatabaseRefreshCollStmt,
    T_AlterDatabaseSetStmt,
    T_AlterDefaultPrivilegesStmt,
    T_AlterCollationStmt,
    T_AlterOwnerStmt,
    T_AlterOperatorStmt,
    T_AlterTypeStmt,
    T_AlterObjectDependsStmt,
    T_AlterObjectSchemaStmt,
    T_AlterOpFamilyStmt,
    T_AlterPolicyStmt,
    T_AlterPropGraphStmt,
    T_AlterStatsStmt,
    T_AlterExtensionStmt,
    T_AlterExtensionContentsStmt,
    T_AlterFdwStmt,
    T_AlterForeignServerStmt,
    T_AlterUserMappingStmt,
    T_AlterTSConfigurationStmt,
    T_AlterTSDictionaryStmt,
    T_AlterPublicationStmt,
    T_AlterSubscriptionStmt,

    /* ---- Drop / discard nodes ---- */
    T_DropStmt,
    T_DropdbStmt,
    T_DropRoleStmt,
    T_DropOwnedStmt,
    T_DropTableSpaceStmt,
    T_DropSubscriptionStmt,
    T_DropUserMappingStmt,
    T_DiscardStmt,

    /* ---- Index / reindex ---- */
    T_IndexStmt,
    T_ReindexStmt,
    T_IndexElem,

    /* ---- Grant / revoke ---- */
    T_GrantStmt,
    T_GrantRoleStmt,
    T_AccessPriv,

    /* ---- Other DML / utility ---- */
    T_CopyStmt,
    T_ExplainStmt,
    T_VacuumStmt,
    T_VacuumRelation,
    T_LockStmt,
    T_FetchStmt,
    T_TruncateStmt,
    T_CommentStmt,
    T_SecLabelStmt,
    T_PrepareStmt,
    T_ExecuteStmt,
    T_DeallocateStmt,
    T_DeclareCursorStmt,
    T_ClosePortalStmt,
    T_CallStmt,
    T_DoStmt,
    T_ListenStmt,
    T_UnlistenStmt,
    T_NotifyStmt,
    T_LoadStmt,
    T_CheckPointStmt,
    T_WaitStmt,
    T_RepackStmt,

    /* ---- Transaction ---- */
    T_TransactionStmt,

    /* ---- Rule / view / trigger ---- */
    T_RuleStmt,
    T_ViewStmt,
    T_RefreshMatViewStmt,

    /* ---- Rename / reassign ---- */
    T_RenameStmt,
    T_ReassignOwnedStmt,

    /* ---- Variable / set ---- */
    T_VariableSetStmt,
    T_VariableShowStmt,

    /* ---- Import ---- */
    T_ImportForeignSchemaStmt,

    /* ---- Constraint ---- */
    T_Constraint,
    T_ConstraintsSetStmt,

    /* ---- Replication ---- */
    T_ReplicaIdentityStmt,

    /* ---- Return ---- */
    T_ReturnStmt,

    /* ---- Define ---- */
    T_DefineStmt,

    /* ---- Expression nodes ---- */
    T_A_Expr,
    T_A_Const,
    T_A_Star,
    T_A_Indices,
    T_A_Indirection,
    T_A_ArrayExpr,
    T_ColumnRef,
    T_ParamRef,
    T_FuncCall,
    T_NamedArgExpr,
    T_SubLink,
    T_BoolExpr,
    T_CaseExpr,
    T_CaseWhen,
    T_CoalesceExpr,
    T_MinMaxExpr,
    T_NullTest,
    T_BooleanTest,
    T_TypeCast,
    T_CollateClause,
    T_SetToDefault,
    T_CurrentOfExpr,
    T_SQLValueFunction,
    T_XmlExpr,
    T_XmlSerialize,
    T_GroupingFunc,
    T_RowExpr,
    T_MultiAssignRef,
    T_MergeSupportFunc,
    T_JsonFuncExpr,
    T_JsonObjectConstructor,
    T_JsonArrayConstructor,
    T_JsonArrayQueryConstructor,
    T_JsonObjectAgg,
    T_JsonArrayAgg,
    T_JsonAggConstructor,
    T_JsonArgument,
    T_JsonOutput,
    T_JsonReturning,
    T_JsonParseExpr,
    T_JsonScalarExpr,
    T_JsonSerializeExpr,
    T_JsonTable,
    T_JsonTableColumn,

    /* ---- FROM clause / range types ---- */
    T_RangeVar,
    T_RangeSubselect,
    T_RangeFunction,
    T_RangeTableFunc,
    T_RangeTableFuncCol,
    T_RangeTableSample,
    T_RangeGraphTable,
    T_JoinExpr,

    /* ---- Target / column / type ---- */
    T_ResTarget,
    T_ColumnDef,
    T_TypeName,
    T_Alias,
    T_IntoClause,

    /* ---- Sort / window / group ---- */
    T_SortBy,
    T_WindowDef,
    T_LockingClause,

    /* ---- CTE ---- */
    T_WithClause,
    T_CommonTableExpr,
    T_CTESearchClause,
    T_CTECycleClause,

    /* ---- Merge ---- */
    T_MergeWhenClause,

    /* ---- ON CONFLICT ---- */
    T_InferClause,
    T_OnConflictClause,

    /* ---- Partition ---- */
    T_PartitionSpec,
    T_PartitionElem,
    T_PartitionBoundSpec,
    T_PartitionCmd,
    T_SinglePartitionSpec,

    /* ---- Returning ---- */
    T_ReturningClause,
    T_ReturningOption,

    /* ---- Table LIKE ---- */
    T_TableLikeClause,

    /* ---- Function parameters ---- */
    T_FunctionParameter,
    T_ObjectWithArgs,

    /* ---- Operator class ---- */
    T_CreateOpClassItem,

    /* ---- Stats ---- */
    T_StatsElem,

    /* ---- Role ---- */
    T_RoleSpec,

    /* ---- Publication ---- */
    T_PublicationObjSpec,
    T_PublicationAllObjSpec,
    T_PublicationTable,

    /* ---- Trigger transition ---- */
    T_TriggerTransition,

    /* ---- Property graph ---- */
    T_GraphPattern,
    T_GraphElementPattern,
    T_PropGraphVertex,
    T_PropGraphEdge,
    T_PropGraphLabelAndProperties,
    T_PropGraphProperties,

    /* ---- Misc support nodes ---- */
    T_DefElem,

    T_NodeTag_Count     /* must be last */
} NodeTag;


/* ----------------------------------------------------------------
 * Base Node type
 * ----------------------------------------------------------------
 */
typedef struct Node
{
    NodeTag type;
} Node;


/* ----------------------------------------------------------------
 * makeNode() / newNode() macros
 * ----------------------------------------------------------------
 */
#define newNode(size, tag) \
    ({  Node *_result = (Node *) calloc(1, (size)); \
        assert(_result != NULL); \
        _result->type = (tag); \
        _result; \
    })

#define makeNode(_type_) \
    ((_type_ *) newNode(sizeof(_type_), T_##_type_))

#define nodeTag(nodeptr) (((const Node *)(nodeptr))->type)
#define IsA(nodeptr, _type_) (nodeTag(nodeptr) == T_##_type_)

#define castNode(_type_, nodeptr) \
    (assert(IsA(nodeptr, _type_)), (_type_ *)(nodeptr))

/* Null-safe node type check */
#define nodeTag_safe(nodeptr) \
    ((nodeptr) != NULL ? nodeTag(nodeptr) : T_Invalid)


/* ----------------------------------------------------------------
 * Value node types (used in constants, identifiers, etc.)
 * ----------------------------------------------------------------
 */
typedef struct Integer
{
    NodeTag type;       /* T_Integer */
    int     ival;
} Integer;

typedef struct Float
{
    NodeTag type;       /* T_Float */
    char   *fval;
} Float;

typedef struct Boolean
{
    NodeTag type;       /* T_Boolean */
    bool    boolval;
} Boolean;

typedef struct String
{
    NodeTag type;       /* T_String */
    char   *sval;
} String;

typedef struct BitString
{
    NodeTag type;       /* T_BitString */
    char   *bsval;
} BitString;

/* Helper constructors */
static inline Integer *makeInteger(int i)
{
    Integer *v = makeNode(Integer);
    v->ival = i;
    return v;
}

static inline Float *makeFloat(char *numericStr)
{
    Float *v = makeNode(Float);
    v->fval = numericStr;
    return v;
}

static inline Boolean *makeBoolean(bool val)
{
    Boolean *v = makeNode(Boolean);
    v->boolval = val;
    return v;
}

static inline String *makeString(char *str)
{
    String *v = makeNode(String);
    v->sval = str;
    return v;
}

/* Accessor macros matching PostgreSQL conventions */
#define intVal(v)       (((Integer *)(v))->ival)
#define floatVal(v)     (((Float *)(v))->fval)
#define boolVal(v)      (((Boolean *)(v))->boolval)
#define strVal(v)       (((String *)(v))->sval)


/* ----------------------------------------------------------------
 * List type (simplified PostgreSQL pg_list.h)
 * ----------------------------------------------------------------
 */
typedef struct ListCell
{
    union
    {
        void   *ptr_value;
        int     int_value;
    } data;
    struct ListCell *next;
} ListCell;

typedef struct List
{
    NodeTag     type;       /* T_List, T_IntList, or T_OidList */
    int         length;
    ListCell   *head;
    ListCell   *tail;
} List;

/* NIL represents an empty list */
#define NIL ((List *) NULL)

/* List access macros */
#define lfirst(lc)          ((lc)->data.ptr_value)
#define lfirst_int(lc)      ((lc)->data.int_value)
#define lfirst_node(type, lc) castNode(type, lfirst(lc))

#define linitial(l)         lfirst(list_head(l))
#define lsecond(l)          lfirst(list_second_cell(l))
#define lthird(l)           lfirst(list_third_cell(l))

#define list_head(l)        ((l) ? (l)->head : NULL)
#define list_second_cell(l) ((l)->head ? (l)->head->next : NULL)
#define list_third_cell(l)  (list_second_cell(l) ? list_second_cell(l)->next : NULL)
#define list_length(l)      ((l) ? (l)->length : 0)

#define foreach(cell, l) \
    for ((cell) = list_head(l); (cell) != NULL; (cell) = (cell)->next)

/* ---- List construction / manipulation declarations ---- */
List *lappend(List *list, void *datum);
List *lappend_int(List *list, int datum);
List *list_concat(List *list1, List *list2);
List *list_make1_impl(NodeTag t, void *datum1);
List *list_make2_impl(NodeTag t, void *datum1, void *datum2);
List *list_make3_impl(NodeTag t, void *datum1, void *datum2, void *datum3);
List *list_make4_impl(NodeTag t, void *datum1, void *datum2, void *datum3, void *datum4);

#define list_make1(x1)        list_make1_impl(T_List, (x1))
#define list_make2(x1,x2)     list_make2_impl(T_List, (x1), (x2))
#define list_make3(x1,x2,x3)  list_make3_impl(T_List, (x1), (x2), (x3))
#define list_make4(x1,x2,x3,x4) list_make4_impl(T_List, (x1), (x2), (x3), (x4))

#define list_make1_int(x1)    list_make1_impl(T_IntList, (void *)(intptr_t)(x1))

/* List utility declarations */
int list_member_int(const List *list, int datum);
List *list_delete_int(List *list, int datum);
void list_free(List *list);


/* ----------------------------------------------------------------
 * Enums used in AST nodes
 * ----------------------------------------------------------------
 */

/* A_Expr_Kind - types of A_Expr */
typedef enum A_Expr_Kind
{
    AEXPR_OP,               /* normal operator */
    AEXPR_OP_ANY,           /* scalar op ANY (array) */
    AEXPR_OP_ALL,           /* scalar op ALL (array) */
    AEXPR_DISTINCT,         /* IS DISTINCT FROM */
    AEXPR_NOT_DISTINCT,     /* IS NOT DISTINCT FROM */
    AEXPR_NULLIF,           /* NULLIF */
    AEXPR_IN,               /* [NOT] IN */
    AEXPR_LIKE,             /* [NOT] LIKE */
    AEXPR_ILIKE,            /* [NOT] ILIKE */
    AEXPR_SIMILAR,          /* [NOT] SIMILAR */
    AEXPR_BETWEEN,          /* name [NOT] BETWEEN */
    AEXPR_NOT_BETWEEN,
    AEXPR_BETWEEN_SYM,      /* name [NOT] BETWEEN SYMMETRIC */
    AEXPR_NOT_BETWEEN_SYM
} A_Expr_Kind;

/* BoolExprType */
typedef enum BoolExprType
{
    AND_EXPR,
    OR_EXPR,
    NOT_EXPR
} BoolExprType;

/* SubLinkType */
typedef enum SubLinkType
{
    EXISTS_SUBLINK,
    ALL_SUBLINK,
    ANY_SUBLINK,
    ROWCOMPARE_SUBLINK,
    EXPR_SUBLINK,
    MULTIEXPR_SUBLINK,
    ARRAY_SUBLINK,
    CTE_SUBLINK
} SubLinkType;

/* SetOperation */
typedef enum SetOperation
{
    SETOP_NONE = 0,
    SETOP_UNION,
    SETOP_INTERSECT,
    SETOP_EXCEPT
} SetOperation;

/* SetQuantifier */
typedef enum SetQuantifier
{
    SET_QUANTIFIER_DEFAULT,
    SET_QUANTIFIER_ALL,
    SET_QUANTIFIER_DISTINCT
} SetQuantifier;

/* JoinType */
typedef enum JoinType
{
    JOIN_INNER,
    JOIN_LEFT,
    JOIN_FULL,
    JOIN_RIGHT,
    JOIN_SEMI,
    JOIN_ANTI,
    JOIN_RIGHT_ANTI,
    JOIN_UNIQUE_OUTER,
    JOIN_UNIQUE_INNER
} JoinType;

/* OnCommitAction */
typedef enum OnCommitAction
{
    ONCOMMIT_NOOP,
    ONCOMMIT_PRESERVE_ROWS,
    ONCOMMIT_DELETE_ROWS,
    ONCOMMIT_DROP
} OnCommitAction;

/* SortByDir */
typedef enum SortByDir
{
    SORTBY_DEFAULT,
    SORTBY_ASC,
    SORTBY_DESC,
    SORTBY_USING
} SortByDir;

/* SortByNulls */
typedef enum SortByNulls
{
    SORTBY_NULLS_DEFAULT,
    SORTBY_NULLS_FIRST,
    SORTBY_NULLS_LAST
} SortByNulls;

/* ObjectType - shared by many DDL statements */
typedef enum ObjectType
{
    OBJECT_ACCESS_METHOD,
    OBJECT_AGGREGATE,
    OBJECT_ATTRIBUTE,
    OBJECT_CAST,
    OBJECT_COLLATION,
    OBJECT_COLUMN,
    OBJECT_CONVERSION,
    OBJECT_DATABASE,
    OBJECT_DOMAIN,
    OBJECT_DOMCONSTRAINT,
    OBJECT_EVENT_TRIGGER,
    OBJECT_EXTENSION,
    OBJECT_FDW,
    OBJECT_FOREIGN_SERVER,
    OBJECT_FOREIGN_TABLE,
    OBJECT_FUNCTION,
    OBJECT_INDEX,
    OBJECT_LANGUAGE,
    OBJECT_LARGEOBJECT,
    OBJECT_MATVIEW,
    OBJECT_OPCLASS,
    OBJECT_OPERATOR,
    OBJECT_OPFAMILY,
    OBJECT_PARAMETER_ACL,
    OBJECT_POLICY,
    OBJECT_PROCEDURE,
    OBJECT_PROPGRAPH,
    OBJECT_PUBLICATION,
    OBJECT_ROLE,
    OBJECT_ROUTINE,
    OBJECT_RULE,
    OBJECT_SCHEMA,
    OBJECT_SEQUENCE,
    OBJECT_STATISTIC_EXT,
    OBJECT_SUBSCRIPTION,
    OBJECT_TABCONSTRAINT,
    OBJECT_TABLE,
    OBJECT_TABLESPACE,
    OBJECT_TRANSFORM,
    OBJECT_TRIGGER,
    OBJECT_TSCONFIGURATION,
    OBJECT_TSDICTIONARY,
    OBJECT_TSPARSER,
    OBJECT_TSTEMPLATE,
    OBJECT_TYPE,
    OBJECT_VIEW
} ObjectType;

/* DropBehavior */
typedef enum DropBehavior
{
    DROP_RESTRICT,
    DROP_CASCADE
} DropBehavior;

/* GrantTargetType */
typedef enum GrantTargetType
{
    ACL_TARGET_OBJECT,
    ACL_TARGET_ALL_IN_SCHEMA,
    ACL_TARGET_DEFAULTS
} GrantTargetType;

/* ConstrType - constraint types */
typedef enum ConstrType
{
    CONSTR_NULL,
    CONSTR_NOTNULL,
    CONSTR_DEFAULT,
    CONSTR_IDENTITY,
    CONSTR_GENERATED,
    CONSTR_CHECK,
    CONSTR_PRIMARY,
    CONSTR_UNIQUE,
    CONSTR_EXCLUSION,
    CONSTR_FOREIGN,
    CONSTR_ATTR_DEFERRABLE,
    CONSTR_ATTR_NOT_DEFERRABLE,
    CONSTR_ATTR_DEFERRED,
    CONSTR_ATTR_IMMEDIATE,
    CONSTR_ATTR_ENFORCED,
    CONSTR_ATTR_NOT_ENFORCED
} ConstrType;

/* FkConstrMatch */
typedef enum FkConstrMatch
{
    FKCONSTR_MATCH_FULL,
    FKCONSTR_MATCH_PARTIAL,
    FKCONSTR_MATCH_SIMPLE
} FkConstrMatch;

/* FkConstrAction */
typedef enum FkConstrAction
{
    FKCONSTR_ACTION_NOACTION,
    FKCONSTR_ACTION_RESTRICT,
    FKCONSTR_ACTION_CASCADE,
    FKCONSTR_ACTION_SETNULL,
    FKCONSTR_ACTION_SETDEFAULT
} FkConstrAction;

/* LimitOption */
typedef enum LimitOption
{
    LIMIT_OPTION_COUNT,
    LIMIT_OPTION_WITH_TIES
} LimitOption;

/* NullTestType */
typedef enum NullTestType
{
    IS_NULL,
    IS_NOT_NULL
} NullTestType;

/* BoolTestType */
typedef enum BoolTestType
{
    IS_TRUE,
    IS_NOT_TRUE,
    IS_FALSE,
    IS_NOT_FALSE,
    IS_UNKNOWN,
    IS_NOT_UNKNOWN
} BoolTestType;

/* MinMaxOp */
typedef enum MinMaxOp
{
    IS_GREATEST,
    IS_LEAST
} MinMaxOp;

/* SQLValueFunctionOp */
typedef enum SQLValueFunctionOp
{
    SVFOP_CURRENT_DATE,
    SVFOP_CURRENT_TIME,
    SVFOP_CURRENT_TIME_N,
    SVFOP_CURRENT_TIMESTAMP,
    SVFOP_CURRENT_TIMESTAMP_N,
    SVFOP_LOCALTIME,
    SVFOP_LOCALTIME_N,
    SVFOP_LOCALTIMESTAMP,
    SVFOP_LOCALTIMESTAMP_N,
    SVFOP_CURRENT_ROLE,
    SVFOP_CURRENT_USER,
    SVFOP_USER,
    SVFOP_SESSION_USER,
    SVFOP_CURRENT_CATALOG,
    SVFOP_CURRENT_SCHEMA
} SQLValueFunctionOp;

/* XmlExprOp */
typedef enum XmlExprOp
{
    IS_XMLCONCAT,
    IS_XMLELEMENT,
    IS_XMLFOREST,
    IS_XMLPARSE,
    IS_XMLPI,
    IS_XMLROOT,
    IS_DOCUMENT
} XmlExprOp;

/* XmlOptionType */
typedef enum XmlOptionType
{
    XMLOPTION_DOCUMENT,
    XMLOPTION_CONTENT
} XmlOptionType;

/* XmlStandaloneType */
typedef enum XmlStandaloneType
{
    XML_STANDALONE_YES,
    XML_STANDALONE_NO,
    XML_STANDALONE_NO_VALUE,
    XML_STANDALONE_OMITTED
} XmlStandaloneType;

/* CoercionContext */
typedef enum CoercionContext
{
    COERCION_IMPLICIT,
    COERCION_ASSIGNMENT,
    COERCION_PLPGSQL,
    COERCION_EXPLICIT
} CoercionContext;

/* CoercionForm */
typedef enum CoercionForm
{
    COERCE_EXPLICIT_CALL,
    COERCE_EXPLICIT_CAST,
    COERCE_IMPLICIT_CAST,
    COERCE_SQL_SYNTAX
} CoercionForm;

/* OverridingKind */
typedef enum OverridingKind
{
    OVERRIDING_NOT_SET = 0,
    OVERRIDING_USER_VALUE,
    OVERRIDING_SYSTEM_VALUE
} OverridingKind;

/* RoleSpecType */
typedef enum RoleSpecType
{
    ROLESPEC_CSTRING,
    ROLESPEC_CURRENT_ROLE,
    ROLESPEC_CURRENT_USER,
    ROLESPEC_SESSION_USER,
    ROLESPEC_PUBLIC
} RoleSpecType;

/* VariableSetKind */
typedef enum VariableSetKind
{
    VAR_SET_VALUE,
    VAR_SET_DEFAULT,
    VAR_SET_CURRENT,
    VAR_SET_MULTI,
    VAR_RESET,
    VAR_RESET_ALL
} VariableSetKind;

/* FunctionParameterMode */
typedef enum FunctionParameterMode
{
    FUNC_PARAM_IN = 'i',
    FUNC_PARAM_OUT = 'o',
    FUNC_PARAM_INOUT = 'b',
    FUNC_PARAM_VARIADIC = 'v',
    FUNC_PARAM_TABLE = 't',
    FUNC_PARAM_DEFAULT = 'd'
} FunctionParameterMode;

/* PartitionStrategy */
typedef enum PartitionStrategy
{
    PARTITION_STRATEGY_LIST = 'l',
    PARTITION_STRATEGY_RANGE = 'r',
    PARTITION_STRATEGY_HASH = 'h'
} PartitionStrategy;

/* ReindexObjectType */
typedef enum ReindexObjectType
{
    REINDEX_OBJECT_INDEX,
    REINDEX_OBJECT_TABLE,
    REINDEX_OBJECT_SCHEMA,
    REINDEX_OBJECT_SYSTEM,
    REINDEX_OBJECT_DATABASE
} ReindexObjectType;

/* ImportForeignSchemaType */
typedef enum ImportForeignSchemaType
{
    FDW_IMPORT_SCHEMA_ALL,
    FDW_IMPORT_SCHEMA_LIMIT_TO,
    FDW_IMPORT_SCHEMA_EXCEPT
} ImportForeignSchemaType;

/* AlterTableType - subset used in gram.y */
typedef enum AlterTableType
{
    AT_AddColumn,
    AT_ColumnDefault,
    AT_DropNotNull,
    AT_SetNotNull,
    AT_SetExpression,
    AT_DropExpression,
    AT_SetStatistics,
    AT_SetOptions,
    AT_ResetOptions,
    AT_SetStorage,
    AT_SetCompression,
    AT_DropColumn,
    AT_AddConstraint,
    AT_AlterConstraint,
    AT_ValidateConstraint,
    AT_DropConstraint,
    AT_AlterColumnType,
    AT_AlterColumnGenericOptions,
    AT_ChangeOwner,
    AT_ClusterOn,
    AT_DropCluster,
    AT_SetLogged,
    AT_SetUnLogged,
    AT_SetTableSpace,
    AT_SetRelOptions,
    AT_ResetRelOptions,
    AT_SetAccessMethod,
    AT_AddInherit,
    AT_DropInherit,
    AT_AddOf,
    AT_DropOf,
    AT_ReplicaIdentity,
    AT_EnableTrig,
    AT_EnableAlwaysTrig,
    AT_EnableReplicaTrig,
    AT_DisableTrig,
    AT_EnableTrigAll,
    AT_DisableTrigAll,
    AT_EnableTrigUser,
    AT_DisableTrigUser,
    AT_EnableRule,
    AT_EnableAlwaysRule,
    AT_EnableReplicaRule,
    AT_DisableRule,
    AT_AddIdentity,
    AT_SetIdentity,
    AT_DropIdentity,
    AT_GenericOptions,
    AT_EnableRowSecurity,
    AT_DisableRowSecurity,
    AT_ForceRowSecurity,
    AT_NoForceRowSecurity,
    AT_AttachPartition,
    AT_DetachPartition,
    AT_DetachPartitionFinalize,
    AT_SplitPartition,
    AT_MergePartitions,
    AT_DropOids    /* unused but kept for compat */
} AlterTableType;

/* MergeMatchKind */
typedef enum MergeMatchKind
{
    MERGE_WHEN_MATCHED,
    MERGE_WHEN_NOT_MATCHED_BY_TARGET,
    MERGE_WHEN_NOT_MATCHED_BY_SOURCE
} MergeMatchKind;

/* CTEMaterialize */
typedef enum CTEMaterialize
{
    CTEMaterializeDefault,
    CTEMaterializeAlways,
    CTEMaterializeNever
} CTEMaterialize;

/* ReturningOptionKind */
typedef enum ReturningOptionKind
{
    RETURNING_OPTION_OLD,
    RETURNING_OPTION_NEW
} ReturningOptionKind;

/* JsonFuncExprOp */
typedef enum JsonFuncExprOp
{
    JSON_EXISTS_OP,
    JSON_QUERY_OP,
    JSON_VALUE_OP
} JsonFuncExprOp;

/* JsonBehaviorType */
typedef enum JsonBehaviorType
{
    JSON_BEHAVIOR_NULL,
    JSON_BEHAVIOR_ERROR,
    JSON_BEHAVIOR_DEFAULT,
    JSON_BEHAVIOR_TRUE,
    JSON_BEHAVIOR_FALSE,
    JSON_BEHAVIOR_UNKNOWN,
    JSON_BEHAVIOR_EMPTY_ARRAY,
    JSON_BEHAVIOR_EMPTY_OBJECT
} JsonBehaviorType;

/* Frame options (bitmask) */
#define FRAMEOPTION_NONDEFAULT          0x00001
#define FRAMEOPTION_RANGE               0x00002
#define FRAMEOPTION_ROWS                0x00004
#define FRAMEOPTION_GROUPS              0x00008
#define FRAMEOPTION_BETWEEN             0x00010
#define FRAMEOPTION_START_UNBOUNDED_PRECEDING  0x00020
#define FRAMEOPTION_END_UNBOUNDED_PRECEDING    0x00040
#define FRAMEOPTION_START_UNBOUNDED_FOLLOWING   0x00080
#define FRAMEOPTION_START_CURRENT_ROW   0x00100
#define FRAMEOPTION_END_CURRENT_ROW     0x00200
#define FRAMEOPTION_START_OFFSET_PRECEDING  0x00400
#define FRAMEOPTION_START_OFFSET_FOLLOWING  0x00800
#define FRAMEOPTION_END_OFFSET_PRECEDING    0x01000
#define FRAMEOPTION_END_OFFSET_FOLLOWING    0x02000
#define FRAMEOPTION_EXCLUDE_CURRENT_ROW     0x04000
#define FRAMEOPTION_EXCLUDE_GROUP           0x08000
#define FRAMEOPTION_EXCLUDE_TIES            0x10000
#define FRAMEOPTION_DEFAULTS \
    (FRAMEOPTION_RANGE | FRAMEOPTION_START_UNBOUNDED_PRECEDING | \
     FRAMEOPTION_END_CURRENT_ROW)

/* Table-LIKE option flags (bitmask) */
#define CREATE_TABLE_LIKE_COMMENTS      (1 << 0)
#define CREATE_TABLE_LIKE_COMPRESSION   (1 << 1)
#define CREATE_TABLE_LIKE_CONSTRAINTS   (1 << 2)
#define CREATE_TABLE_LIKE_DEFAULTS      (1 << 3)
#define CREATE_TABLE_LIKE_GENERATED     (1 << 4)
#define CREATE_TABLE_LIKE_IDENTITY      (1 << 5)
#define CREATE_TABLE_LIKE_INDEXES       (1 << 6)
#define CREATE_TABLE_LIKE_STATISTICS    (1 << 7)
#define CREATE_TABLE_LIKE_STORAGE       (1 << 8)
#define CREATE_TABLE_LIKE_ALL           0xFFFFFFFF


/* ----------------------------------------------------------------
 * Forward declarations for struct types
 * ----------------------------------------------------------------
 */
typedef struct TypeName TypeName;
typedef struct Alias Alias;
typedef struct RangeVar RangeVar;
typedef struct PublicationTable PublicationTable;


/* ================================================================
 * Expression node structs
 * ================================================================
 */

/* A_Expr - infix, prefix, postfix expressions */
typedef struct A_Expr
{
    NodeTag     type;           /* T_A_Expr */
    A_Expr_Kind kind;
    List       *name;           /* operator name (list of String or Value) */
    Node       *lexpr;          /* left argument (NULL for prefix) */
    Node       *rexpr;          /* right argument (NULL for postfix) */
    ParseLoc    rexpr_list_start;  /* for IN: location of '(' */
    ParseLoc    rexpr_list_end;    /* for IN: location of ')' */
    ParseLoc    location;
} A_Expr;

/* A_Const - constant value */
typedef struct A_Const
{
    NodeTag     type;           /* T_A_Const */
    union ValUnion
    {
        Node    node;       /* for type dispatch */
        Integer ival;
        Float   fval;
        Boolean boolval;
        String  sval;
        BitString bsval;
    } val;
    bool        isnull;     /* if true, val is meaningless */
    ParseLoc    location;
} A_Const;

/* A_Star - '*' in a ColumnRef or target list */
typedef struct A_Star
{
    NodeTag     type;           /* T_A_Star */
} A_Star;

/* A_Indices - array subscript or slice */
typedef struct A_Indices
{
    NodeTag     type;           /* T_A_Indices */
    bool        is_slice;
    Node       *lidx;          /* lower bound (NULL if not slice or [:expr]) */
    Node       *uidx;          /* upper bound or single subscript */
} A_Indices;

/* A_Indirection - node.field or node[subscript] */
typedef struct A_Indirection
{
    NodeTag     type;           /* T_A_Indirection */
    Node       *arg;
    List       *indirection;   /* list of A_Indices or String nodes */
} A_Indirection;

/* A_ArrayExpr - ARRAY[...] constructor */
typedef struct A_ArrayExpr
{
    NodeTag     type;           /* T_A_ArrayExpr */
    List       *elements;
    ParseLoc    location;
    ParseLoc    end_location;
} A_ArrayExpr;

/* ColumnRef - column reference (possibly qualified) */
typedef struct ColumnRef
{
    NodeTag     type;           /* T_ColumnRef */
    List       *fields;         /* list of String and/or A_Star */
    ParseLoc    location;
} ColumnRef;

/* ParamRef - $n parameter reference */
typedef struct ParamRef
{
    NodeTag     type;           /* T_ParamRef */
    int         number;
    ParseLoc    location;
} ParamRef;

/* FuncCall - function call */
typedef struct FuncCall
{
    NodeTag     type;           /* T_FuncCall */
    List       *funcname;       /* qualified function name */
    List       *args;           /* arguments */
    List       *agg_order;      /* ORDER BY within aggregate */
    Node       *agg_filter;     /* FILTER clause */
    struct WindowDef *over;     /* OVER clause if window function */
    bool        agg_within_group;
    bool        agg_star;       /* argument was '*' */
    bool        agg_distinct;   /* DISTINCT on arguments */
    bool        func_variadic;  /* last arg is VARIADIC */
    CoercionForm funcformat;
    ParseLoc    location;
} FuncCall;

/* NamedArgExpr - name => expr in function call */
typedef struct NamedArgExpr
{
    NodeTag     type;           /* T_NamedArgExpr */
    Node       *arg;
    char       *name;
    int         argnumber;      /* used by optimizer, -1 initially */
    ParseLoc    location;
} NamedArgExpr;

/* SubLink - subquery in expression */
typedef struct SubLink
{
    NodeTag         type;           /* T_SubLink */
    SubLinkType     subLinkType;
    int             subLinkId;
    Node           *testexpr;       /* outer-query expression */
    List           *operName;       /* operator name */
    Node           *subselect;      /* subquery (SelectStmt) */
    ParseLoc        location;
} SubLink;

/* BoolExpr - AND / OR / NOT */
typedef struct BoolExpr
{
    NodeTag         type;           /* T_BoolExpr */
    BoolExprType    boolop;
    List           *args;
    ParseLoc        location;
} BoolExpr;

/* CaseExpr / CaseWhen */
typedef struct CaseExpr
{
    NodeTag     type;           /* T_CaseExpr */
    Node       *arg;            /* implicit equality test value (NULL for CASE WHEN) */
    List       *args;           /* list of CaseWhen nodes */
    Node       *defresult;      /* ELSE result */
    ParseLoc    location;
} CaseExpr;

typedef struct CaseWhen
{
    NodeTag     type;           /* T_CaseWhen */
    Node       *expr;           /* condition */
    Node       *result;
    ParseLoc    location;
} CaseWhen;

/* CoalesceExpr */
typedef struct CoalesceExpr
{
    NodeTag     type;           /* T_CoalesceExpr */
    List       *args;
    ParseLoc    location;
} CoalesceExpr;

/* MinMaxExpr - GREATEST / LEAST */
typedef struct MinMaxExpr
{
    NodeTag     type;           /* T_MinMaxExpr */
    MinMaxOp    op;
    List       *args;
    ParseLoc    location;
} MinMaxExpr;

/* NullTest - IS [NOT] NULL */
typedef struct NullTest
{
    NodeTag         type;           /* T_NullTest */
    Node           *arg;
    NullTestType    nulltesttype;
    bool            argisrow;       /* T if arg is composite */
    ParseLoc        location;
} NullTest;

/* BooleanTest - IS [NOT] TRUE / FALSE / UNKNOWN */
typedef struct BooleanTest
{
    NodeTag         type;           /* T_BooleanTest */
    Node           *arg;
    BoolTestType    booltesttype;
    ParseLoc        location;
} BooleanTest;

/* TypeCast - CAST(expr AS type) */
typedef struct TypeCast
{
    NodeTag     type;           /* T_TypeCast */
    Node       *arg;
    TypeName   *typeName;
    ParseLoc    location;
} TypeCast;

/* CollateClause - COLLATE "name" */
typedef struct CollateClause
{
    NodeTag     type;           /* T_CollateClause */
    Node       *arg;
    List       *collname;
    ParseLoc    location;
} CollateClause;

/* SetToDefault - DEFAULT in INSERT/UPDATE */
typedef struct SetToDefault
{
    NodeTag     type;           /* T_SetToDefault */
    ParseLoc    location;
} SetToDefault;

/* CurrentOfExpr - WHERE CURRENT OF cursor */
typedef struct CurrentOfExpr
{
    NodeTag     type;           /* T_CurrentOfExpr */
    int         cvarno;
    char       *cursor_name;
    int         cursor_param;
} CurrentOfExpr;

/* SQLValueFunction - CURRENT_DATE, CURRENT_USER, etc. */
typedef struct SQLValueFunction
{
    NodeTag             type;       /* T_SQLValueFunction */
    SQLValueFunctionOp  op;
    int32               typmod;
    ParseLoc            location;
} SQLValueFunction;

/* GroupingFunc - GROUPING(...) */
typedef struct GroupingFunc
{
    NodeTag     type;           /* T_GroupingFunc */
    List       *args;           /* column references */
    ParseLoc    location;
} GroupingFunc;

/* RowExpr - ROW(a, b, c) or (a, b, c) */
typedef struct RowExpr
{
    NodeTag     type;           /* T_RowExpr */
    List       *args;
    CoercionForm row_format;
    ParseLoc    location;
} RowExpr;

/* MultiAssignRef - used in SET (a, b) = expr */
typedef struct MultiAssignRef
{
    NodeTag     type;           /* T_MultiAssignRef */
    Node       *source;
    int         colno;
    int         ncolumns;
} MultiAssignRef;

/* MergeSupportFunc */
typedef struct MergeSupportFunc
{
    NodeTag     type;           /* T_MergeSupportFunc */
    ParseLoc    location;
} MergeSupportFunc;

/* XmlExpr */
typedef struct XmlExpr
{
    NodeTag     type;           /* T_XmlExpr */
    XmlExprOp   op;
    char       *name;
    List       *named_args;
    List       *args;
    XmlOptionType xmloption;
    bool        indent;
    ParseLoc    location;
} XmlExpr;

/* XmlSerialize */
typedef struct XmlSerialize
{
    NodeTag         type;       /* T_XmlSerialize */
    XmlOptionType   xmloption;
    Node           *expr;
    TypeName       *typeName;
    bool            indent;
    ParseLoc        location;
} XmlSerialize;


/* ================================================================
 * JSON expression nodes
 * ================================================================
 */

typedef struct JsonReturning
{
    NodeTag     type;           /* T_JsonReturning */
    TypeName   *typeName;
} JsonReturning;

typedef struct JsonOutput
{
    NodeTag         type;       /* T_JsonOutput */
    TypeName       *typeName;
    JsonReturning  *returning;
} JsonOutput;

typedef struct JsonArgument
{
    NodeTag     type;           /* T_JsonArgument */
    Node       *val;
    char       *name;
} JsonArgument;

typedef struct JsonAggConstructor
{
    NodeTag         type;       /* T_JsonAggConstructor */
    JsonOutput     *output;
    Node           *agg_filter;
    List           *agg_order;
    struct WindowDef *over;
    ParseLoc        location;
} JsonAggConstructor;

typedef struct JsonObjectConstructor
{
    NodeTag         type;       /* T_JsonObjectConstructor */
    List           *exprs;
    JsonOutput     *output;
    bool            absent_on_null;
    bool            unique;
    ParseLoc        location;
} JsonObjectConstructor;

typedef struct JsonArrayConstructor
{
    NodeTag         type;       /* T_JsonArrayConstructor */
    List           *exprs;
    JsonOutput     *output;
    bool            absent_on_null;
    ParseLoc        location;
} JsonArrayConstructor;

typedef struct JsonArrayQueryConstructor
{
    NodeTag         type;       /* T_JsonArrayQueryConstructor */
    Node           *query;
    JsonOutput     *output;
    bool            absent_on_null;
    ParseLoc        location;
} JsonArrayQueryConstructor;

typedef struct JsonObjectAgg
{
    NodeTag             type;       /* T_JsonObjectAgg */
    JsonAggConstructor *constructor;
    Node               *arg;
    bool                absent_on_null;
    bool                unique;
} JsonObjectAgg;

typedef struct JsonArrayAgg
{
    NodeTag             type;       /* T_JsonArrayAgg */
    JsonAggConstructor *constructor;
    Node               *arg;
    bool                absent_on_null;
} JsonArrayAgg;

typedef struct JsonFuncExpr
{
    NodeTag         type;       /* T_JsonFuncExpr */
    JsonFuncExprOp  op;
    char           *column_name;
    Node           *context_item;
    Node           *pathspec;
    List           *passing;
    JsonOutput     *output;
    JsonBehaviorType on_empty;
    JsonBehaviorType on_error;
    Node           *on_empty_default;
    Node           *on_error_default;
    int             wrapper;
    bool            omit_quotes;
    ParseLoc        location;
} JsonFuncExpr;

typedef struct JsonParseExpr
{
    NodeTag         type;       /* T_JsonParseExpr */
    Node           *expr;
    JsonOutput     *output;
    bool            unique_keys;
    ParseLoc        location;
} JsonParseExpr;

typedef struct JsonScalarExpr
{
    NodeTag         type;       /* T_JsonScalarExpr */
    Node           *expr;
    JsonOutput     *output;
    ParseLoc        location;
} JsonScalarExpr;

typedef struct JsonSerializeExpr
{
    NodeTag         type;       /* T_JsonSerializeExpr */
    Node           *expr;
    JsonOutput     *output;
    ParseLoc        location;
} JsonSerializeExpr;

typedef struct JsonTable
{
    NodeTag         type;       /* T_JsonTable */
    Node           *context_item;
    Node           *pathspec;
    List           *passing;
    List           *columns;
    JsonBehaviorType on_error;
    Alias          *alias;
    bool            lateral;
    ParseLoc        location;
} JsonTable;

typedef struct JsonTableColumn
{
    NodeTag         type;       /* T_JsonTableColumn */
    int             coltype;
    char           *name;
    TypeName       *typeName;
    Node           *pathspec;
    List           *columns;
    JsonBehaviorType on_empty;
    JsonBehaviorType on_error;
    Node           *on_empty_default;
    Node           *on_error_default;
    int             wrapper;
    bool            omit_quotes;
    ParseLoc        location;
} JsonTableColumn;


/* ================================================================
 * Utility / support node structs
 * ================================================================
 */

/* Alias */
struct Alias
{
    NodeTag     type;           /* T_Alias */
    char       *aliasname;
    List       *colnames;       /* optional list of column aliases */
};

/* Helper */
static inline Alias *makeAlias(const char *aliasname, List *colnames)
{
    Alias *a = makeNode(Alias);
    a->aliasname = (char *) aliasname;
    a->colnames = colnames;
    return a;
}

/* TypeName */
struct TypeName
{
    NodeTag     type;           /* T_TypeName */
    List       *names;          /* qualified type name */
    int32       typemod;        /* -1 if unspecified */
    bool        setof;
    bool        pct_type;       /* %TYPE notation */
    List       *typmods;        /* type modifier expressions */
    List       *arrayBounds;    /* list of Integer for array dims */
    ParseLoc    location;
};

/* RangeVar - a qualified table/relation name */
struct RangeVar
{
    NodeTag     type;           /* T_RangeVar */
    char       *catalogname;
    char       *schemaname;
    char       *relname;
    bool        inh;            /* expand inheritance? */
    char        relpersistence; /* RELPERSISTENCE_PERMANENT etc. */
    Alias      *alias;
    ParseLoc    location;
};

/* Character constants for relpersistence */
#define RELPERSISTENCE_PERMANENT    'p'
#define RELPERSISTENCE_UNLOGGED     'u'
#define RELPERSISTENCE_TEMP         't'

/* IntoClause - SELECT INTO / CREATE TABLE AS target */
typedef struct IntoClause
{
    NodeTag         type;           /* T_IntoClause */
    RangeVar       *rel;
    List           *colNames;
    char           *accessMethod;
    List           *options;
    OnCommitAction  onCommit;
    char           *tableSpaceName;
    Node           *viewQuery;
    bool            skipData;
} IntoClause;

/* ResTarget - result target in SELECT/INSERT/UPDATE */
typedef struct ResTarget
{
    NodeTag     type;           /* T_ResTarget */
    char       *name;           /* column name (alias in SELECT, target in UPDATE) */
    List       *indirection;    /* subscripts, field refs */
    Node       *val;            /* expression value */
    ParseLoc    location;
} ResTarget;

/* ColumnDef */
typedef struct ColumnDef
{
    NodeTag     type;           /* T_ColumnDef */
    char       *colname;
    TypeName   *typeName;
    char       *compression;
    int         inhcount;
    bool        is_local;
    bool        is_not_null;
    bool        is_from_type;
    char        storage;
    char       *storage_name;
    Node       *raw_default;
    Node       *cooked_default;
    char        identity;
    RangeVar   *identitySequence;
    char        generated;
    CollateClause *collClause;
    List       *constraints;
    List       *fdwoptions;
    ParseLoc    location;
} ColumnDef;

/* SortBy */
typedef struct SortBy
{
    NodeTag     type;           /* T_SortBy */
    Node       *node;
    SortByDir   sortby_dir;
    SortByNulls sortby_nulls;
    List       *useOp;
    ParseLoc    location;
} SortBy;

/* WindowDef */
typedef struct WindowDef
{
    NodeTag     type;           /* T_WindowDef */
    char       *name;           /* window name (NULL in OVER clause) */
    char       *refname;        /* referenced window name */
    List       *partitionClause;
    List       *orderClause;
    int         frameOptions;
    Node       *startOffset;
    Node       *endOffset;
    ParseLoc    location;
} WindowDef;

/* LockingClause */
typedef struct LockingClause
{
    NodeTag     type;           /* T_LockingClause */
    List       *lockedRels;
    int         strength;       /* LockClauseStrength */
    int         waitPolicy;     /* LockWaitPolicy */
} LockingClause;

/* WithClause */
typedef struct WithClause
{
    NodeTag     type;           /* T_WithClause */
    List       *ctes;           /* list of CommonTableExpr */
    bool        recursive;
    ParseLoc    location;
} WithClause;

/* CommonTableExpr */
typedef struct CommonTableExpr
{
    NodeTag         type;       /* T_CommonTableExpr */
    char           *ctename;
    List           *aliascolnames;
    CTEMaterialize  ctematerialized;
    Node           *ctequery;
    struct CTESearchClause *search_clause;
    struct CTECycleClause  *cycle_clause;
    ParseLoc        location;
    /* set during analysis, not by parser */
    bool            cterecursive;
    int             cterefcount;
    List           *ctecolnames;
    List           *ctecoltypes;
    List           *ctecoltypmods;
    List           *ctecolcollations;
} CommonTableExpr;

/* CTESearchClause */
typedef struct CTESearchClause
{
    NodeTag     type;           /* T_CTESearchClause */
    List       *search_col_list;
    bool        search_breadth_first;
    char       *search_seq_column;
    ParseLoc    location;
} CTESearchClause;

/* CTECycleClause */
typedef struct CTECycleClause
{
    NodeTag     type;           /* T_CTECycleClause */
    List       *cycle_col_list;
    char       *cycle_mark_column;
    Node       *cycle_mark_value;
    Node       *cycle_mark_default;
    char       *cycle_path_column;
    ParseLoc    location;
} CTECycleClause;

/* InferClause - ON CONFLICT index inference */
typedef struct InferClause
{
    NodeTag     type;           /* T_InferClause */
    List       *indexElems;
    Node       *whereClause;
    char       *conname;
    ParseLoc    location;
} InferClause;

/* OnConflictClause - ON CONFLICT ... DO */
typedef struct OnConflictClause
{
    NodeTag         type;       /* T_OnConflictClause */
    int             action;     /* OC_Nothing or OC_Update */
    InferClause    *infer;
    List           *targetList;
    Node           *whereClause;
    ParseLoc        location;
} OnConflictClause;

/* ON CONFLICT actions */
#define OC_Nothing      1
#define OC_Update       2

/* MergeWhenClause */
typedef struct MergeWhenClause
{
    NodeTag         type;       /* T_MergeWhenClause */
    MergeMatchKind  matchKind;
    int             commandType;    /* CMD_UPDATE, CMD_DELETE, CMD_INSERT, CMD_NOTHING */
    bool            override;
    Node           *condition;
    List           *targetList;
    List           *values;
} MergeWhenClause;

/* Command type constants used in MergeWhenClause */
#define CMD_UNKNOWN     0
#define CMD_SELECT      1
#define CMD_UPDATE      2
#define CMD_INSERT      3
#define CMD_DELETE      4
#define CMD_MERGE       5
#define CMD_UTILITY     6
#define CMD_NOTHING     7

/* ReturningClause */
typedef struct ReturningClause
{
    NodeTag     type;           /* T_ReturningClause */
    List       *options;        /* list of ReturningOption */
    List       *exprs;          /* list of ResTarget */
} ReturningClause;

/* ReturningOption */
typedef struct ReturningOption
{
    NodeTag             type;       /* T_ReturningOption */
    ReturningOptionKind option;
    char               *value;
    ParseLoc            location;
} ReturningOption;

/* DefElem - generic name/value pair for options */
typedef struct DefElem
{
    NodeTag     type;           /* T_DefElem */
    char       *defnamespace;
    char       *defname;
    Node       *arg;
    int         defaction;      /* unspecified/set/add/drop */
    ParseLoc    location;
} DefElem;

/* RoleSpec */
typedef struct RoleSpec
{
    NodeTag         type;       /* T_RoleSpec */
    RoleSpecType    roletype;
    char           *rolename;
    ParseLoc        location;
} RoleSpec;


/* ================================================================
 * FROM clause / range node structs
 * ================================================================
 */

/* JoinExpr */
typedef struct JoinExpr
{
    NodeTag     type;           /* T_JoinExpr */
    JoinType    jointype;
    bool        isNatural;
    Node       *larg;
    Node       *rarg;
    List       *usingClause;
    Alias      *join_using_alias;
    Node       *quals;
    Alias      *alias;
    int         rtindex;    /* set by planner */
} JoinExpr;

/* RangeSubselect - subquery in FROM */
typedef struct RangeSubselect
{
    NodeTag     type;           /* T_RangeSubselect */
    bool        lateral;
    Node       *subquery;
    Alias      *alias;
} RangeSubselect;

/* RangeFunction - function in FROM */
typedef struct RangeFunction
{
    NodeTag     type;           /* T_RangeFunction */
    bool        lateral;
    bool        ordinality;
    bool        is_rowsfrom;
    List       *functions;      /* list of list (funcexpr + column defs) */
    Alias      *alias;
    List       *coldeflist;
} RangeFunction;

/* RangeTableFunc - XMLTABLE */
typedef struct RangeTableFunc
{
    NodeTag     type;           /* T_RangeTableFunc */
    bool        lateral;
    Node       *docexpr;
    Node       *rowexpr;
    List       *namespaces;
    List       *columns;        /* list of RangeTableFuncCol */
    Alias      *alias;
    ParseLoc    location;
} RangeTableFunc;

/* RangeTableFuncCol */
typedef struct RangeTableFuncCol
{
    NodeTag     type;           /* T_RangeTableFuncCol */
    char       *colname;
    TypeName   *typeName;
    bool        for_ordinality;
    bool        is_not_null;
    Node       *colexpr;
    Node       *coldefexpr;
    ParseLoc    location;
} RangeTableFuncCol;

/* RangeTableSample - TABLESAMPLE */
typedef struct RangeTableSample
{
    NodeTag     type;           /* T_RangeTableSample */
    Node       *relation;
    List       *method;
    List       *args;
    Node       *repeatable;
    ParseLoc    location;
} RangeTableSample;

/* RangeGraphTable - MATCH_RECOGNIZE / GRAPH_TABLE */
typedef struct RangeGraphTable
{
    NodeTag     type;           /* T_RangeGraphTable */
    RangeVar   *relation;
    List       *graphPattern;
    Node       *whereClause;
    List       *columns;
    Alias      *alias;
    bool        lateral;
    ParseLoc    location;
} RangeGraphTable;


/* ================================================================
 * Partition node structs
 * ================================================================
 */

typedef struct PartitionSpec
{
    NodeTag             type;       /* T_PartitionSpec */
    PartitionStrategy   strategy;
    List               *partParams; /* list of PartitionElem */
    ParseLoc            location;
} PartitionSpec;

typedef struct PartitionElem
{
    NodeTag     type;           /* T_PartitionElem */
    char       *name;
    Node       *expr;
    List       *collation;
    List       *opclass;
    ParseLoc    location;
} PartitionElem;

typedef struct PartitionBoundSpec
{
    NodeTag     type;           /* T_PartitionBoundSpec */
    char        strategy;
    bool        is_default;
    int         modulus;
    int         remainder;
    List       *listdatums;
    List       *lowerdatums;
    List       *upperdatums;
    ParseLoc    location;
} PartitionBoundSpec;

typedef struct PartitionCmd
{
    NodeTag             type;       /* T_PartitionCmd */
    RangeVar           *name;
    PartitionBoundSpec *bound;
    bool                concurrent;
} PartitionCmd;

typedef struct SinglePartitionSpec
{
    NodeTag             type;       /* T_SinglePartitionSpec */
    char               *name;
    PartitionBoundSpec *bound;
} SinglePartitionSpec;


/* ================================================================
 * Index node structs
 * ================================================================
 */

typedef struct IndexElem
{
    NodeTag     type;           /* T_IndexElem */
    char       *name;
    Node       *expr;
    char       *indexcolname;
    List       *collation;
    List       *opclass;
    List       *opclassopts;
    SortByDir   ordering;
    SortByNulls nulls_ordering;
} IndexElem;

typedef struct StatsElem
{
    NodeTag     type;           /* T_StatsElem */
    char       *name;
    Node       *expr;
} StatsElem;


/* ================================================================
 * Constraint struct
 * ================================================================
 */

typedef struct Constraint
{
    NodeTag     type;           /* T_Constraint */
    ConstrType  contype;
    char       *conname;
    bool        deferrable;
    bool        initdeferred;
    bool        is_enforced;
    bool        skip_validation;
    bool        initially_valid;
    ParseLoc    location;

    /* CHECK constraint fields */
    bool        is_no_inherit;
    Node       *raw_expr;
    char       *cooked_expr;
    char        generated_when;

    /* NOT NULL constraint fields */
    int         inhcount;
    List       *keys;           /* also for PRIMARY KEY / UNIQUE */

    /* UNIQUE/PRIMARY KEY fields */
    int         nulls_not_distinct;
    List       *including;
    List       *exclusions;
    List       *options;
    char       *indexname;
    char       *indexspace;
    bool        reset_default_tblspc;
    char       *access_method;
    Node       *where_clause;

    /* FOREIGN KEY fields */
    RangeVar   *pktable;
    List       *fk_attrs;
    List       *pk_attrs;
    char        fk_matchtype;
    char        fk_upd_action;
    char        fk_del_action;
    List       *fk_del_set_cols;
    List       *old_conpfeqop;
} Constraint;


/* ================================================================
 * Publication node structs
 * ================================================================
 */

typedef struct PublicationObjSpec
{
    NodeTag     type;           /* T_PublicationObjSpec */
    int         pubobjtype;
    char       *name;
    PublicationTable *pubtable;
    ParseLoc    location;
} PublicationObjSpec;

typedef struct PublicationAllObjSpec
{
    NodeTag     type;           /* T_PublicationAllObjSpec */
    int         puballobjtype;
} PublicationAllObjSpec;

typedef struct PublicationTable
{
    NodeTag     type;           /* T_PublicationTable */
    RangeVar   *relation;
    Node       *whereClause;
    List       *columns;
} PublicationTable;


/* ================================================================
 * Property graph node structs
 * ================================================================
 */

typedef struct GraphPattern
{
    NodeTag     type;           /* T_GraphPattern */
    List       *elements;       /* list of GraphElementPattern */
} GraphPattern;

typedef struct GraphElementPattern
{
    NodeTag     type;           /* T_GraphElementPattern */
    Node       *element;        /* PropGraphVertex or PropGraphEdge */
} GraphElementPattern;

typedef struct PropGraphVertex
{
    NodeTag     type;           /* T_PropGraphVertex */
    char       *variable;
    List       *labels;
    Node       *whereClause;
    ParseLoc    location;
} PropGraphVertex;

typedef struct PropGraphEdge
{
    NodeTag     type;           /* T_PropGraphEdge */
    char       *variable;
    List       *labels;
    Node       *whereClause;
    int         direction;      /* -1=left, 0=undirected, 1=right */
    ParseLoc    location;
} PropGraphEdge;

typedef struct PropGraphLabelAndProperties
{
    NodeTag     type;           /* T_PropGraphLabelAndProperties */
    char       *label;
    List       *properties;     /* list of PropGraphProperties */
} PropGraphLabelAndProperties;

typedef struct PropGraphProperties
{
    NodeTag     type;           /* T_PropGraphProperties */
    char       *name;
    Node       *expr;
} PropGraphProperties;


/* ================================================================
 * Trigger node structs
 * ================================================================
 */

typedef struct TriggerTransition
{
    NodeTag     type;           /* T_TriggerTransition */
    char       *name;
    bool        isNew;
    bool        isTable;
} TriggerTransition;


/* ================================================================
 * Function parameter node
 * ================================================================
 */

typedef struct FunctionParameter
{
    NodeTag     type;               /* T_FunctionParameter */
    char       *name;
    TypeName   *argType;
    FunctionParameterMode mode;
    Node       *defexpr;
} FunctionParameter;

typedef struct ObjectWithArgs
{
    NodeTag     type;           /* T_ObjectWithArgs */
    List       *objname;
    List       *objargs;
    List       *objfuncargs;
    bool        args_unspecified;
} ObjectWithArgs;

/* CreateOpClassItem */
typedef struct CreateOpClassItem
{
    NodeTag     type;           /* T_CreateOpClassItem */
    int         itemtype;
    ObjectWithArgs *name;
    int         number;
    List       *order_family;
    List       *class_args;
    TypeName   *storedtype;
} CreateOpClassItem;

/* AccessPriv */
typedef struct AccessPriv
{
    NodeTag     type;           /* T_AccessPriv */
    char       *priv_name;
    List       *cols;
} AccessPriv;

/* TableLikeClause */
typedef struct TableLikeClause
{
    NodeTag     type;           /* T_TableLikeClause */
    RangeVar   *relation;
    unsigned int options;       /* OR of CREATE_TABLE_LIKE_* flags */
    int         relationOid;    /* set during analysis */
} TableLikeClause;

/* VacuumRelation */
typedef struct VacuumRelation
{
    NodeTag     type;           /* T_VacuumRelation */
    RangeVar   *relation;
    int         oid;
    List       *va_cols;
} VacuumRelation;


/* ================================================================
 * DML Statement structs
 * ================================================================
 */

/* RawStmt - wraps a raw parse tree statement */
typedef struct RawStmt
{
    NodeTag     type;           /* T_RawStmt */
    Node       *stmt;
    ParseLoc    stmt_location;
    ParseLoc    stmt_len;
} RawStmt;

/* SelectStmt */
typedef struct SelectStmt
{
    NodeTag         type;           /* T_SelectStmt */

    /* These fields used in "leaf" SelectStmts */
    List           *distinctClause;
    IntoClause     *intoClause;
    List           *targetList;     /* list of ResTarget */
    List           *fromClause;     /* list of table references */
    Node           *whereClause;
    List           *groupClause;
    bool            groupDistinct;
    bool            groupByAll;
    Node           *havingClause;
    List           *windowClause;  /* list of WindowDef */

    /* These fields used in "leaf" and "set-op" SelectStmts */
    List           *valuesLists;    /* for VALUES(...) */
    List           *sortClause;     /* list of SortBy */
    Node           *limitOffset;
    Node           *limitCount;
    LimitOption     limitOption;
    List           *lockingClause;  /* list of LockingClause */
    WithClause     *withClause;

    /* These fields used only in set-op SelectStmts */
    SetOperation    op;
    bool            all;
    struct SelectStmt *larg;
    struct SelectStmt *rarg;
} SelectStmt;

/* InsertStmt */
typedef struct InsertStmt
{
    NodeTag             type;           /* T_InsertStmt */
    RangeVar           *relation;
    List               *cols;           /* list of ResTarget (target columns) */
    Node               *selectStmt;
    OnConflictClause   *onConflictClause;
    ReturningClause    *returningClause;
    WithClause         *withClause;
    OverridingKind      override;
} InsertStmt;

/* UpdateStmt */
typedef struct UpdateStmt
{
    NodeTag             type;           /* T_UpdateStmt */
    RangeVar           *relation;
    List               *targetList;     /* list of ResTarget */
    Node               *whereClause;
    List               *fromClause;
    ReturningClause    *returningClause;
    WithClause         *withClause;
} UpdateStmt;

/* DeleteStmt */
typedef struct DeleteStmt
{
    NodeTag             type;           /* T_DeleteStmt */
    RangeVar           *relation;
    List               *usingClause;
    Node               *whereClause;
    ReturningClause    *returningClause;
    WithClause         *withClause;
} DeleteStmt;

/* MergeStmt */
typedef struct MergeStmt
{
    NodeTag             type;           /* T_MergeStmt */
    RangeVar           *relation;       /* target table */
    Node               *sourceRelation; /* source table/subquery */
    Node               *joinCondition;
    List               *mergeWhenClauses;
    ReturningClause    *returningClause;
    WithClause         *withClause;
} MergeStmt;

/* PLAssignStmt - PL/pgSQL assignment */
typedef struct PLAssignStmt
{
    NodeTag     type;           /* T_PLAssignStmt */
    char       *name;
    List       *indirection;
    int         nnames;
    SelectStmt *val;
    ParseLoc    location;
} PLAssignStmt;


/* ================================================================
 * Selected DDL statement structs (commonly referenced)
 * ================================================================
 */

/* CreateStmt - CREATE TABLE */
typedef struct CreateStmt
{
    NodeTag         type;           /* T_CreateStmt */
    RangeVar       *relation;
    List           *tableElts;      /* list of ColumnDef, Constraint, TableLikeClause */
    List           *inhRelations;   /* list of RangeVar (inheritance parents) */
    PartitionBoundSpec *partbound;
    PartitionSpec  *partspec;
    TypeName       *ofTypename;
    List           *constraints;
    List           *options;
    OnCommitAction  oncommit;
    char           *tablespacename;
    char           *accessMethod;
    bool            if_not_exists;
} CreateStmt;

/* CreateTableAsStmt - CREATE TABLE AS / SELECT INTO */
typedef struct CreateTableAsStmt
{
    NodeTag         type;           /* T_CreateTableAsStmt */
    Node           *query;
    IntoClause     *into;
    ObjectType      objtype;        /* OBJECT_TABLE or OBJECT_MATVIEW */
    bool            is_select_into;
    bool            if_not_exists;
} CreateTableAsStmt;

/* IndexStmt - CREATE INDEX */
typedef struct IndexStmt
{
    NodeTag     type;           /* T_IndexStmt */
    char       *idxname;
    RangeVar   *relation;
    char       *accessMethod;
    char       *tableSpace;
    List       *indexParams;    /* list of IndexElem */
    List       *indexIncludingParams;
    List       *options;
    Node       *whereClause;
    List       *excludeOpNames;
    char       *idxcomment;
    int         indexOid;
    int         oldNumber;
    int         oldCreateSubid;
    int         oldFirstRelfilelocatorSubid;
    bool        unique;
    bool        nulls_not_distinct;
    bool        primary;
    bool        isconstraint;
    bool        deferrable;
    bool        initdeferred;
    bool        transformed;
    bool        concurrent;
    bool        if_not_exists;
    bool        reset_default_tblspc;
} IndexStmt;

/* ViewStmt - CREATE VIEW */
typedef struct ViewStmt
{
    NodeTag         type;           /* T_ViewStmt */
    RangeVar       *view;
    List           *aliases;
    Node           *query;
    bool            replace;
    List           *options;
    int             withCheckOption;
} ViewStmt;

/* AlterTableStmt */
typedef struct AlterTableStmt
{
    NodeTag         type;           /* T_AlterTableStmt */
    RangeVar       *relation;
    List           *cmds;           /* list of AlterTableCmd */
    ObjectType      objtype;
    bool            missing_ok;
} AlterTableStmt;

/* AlterTableCmd */
typedef struct AlterTableCmd
{
    NodeTag         type;           /* T_AlterTableCmd */
    AlterTableType  subtype;
    char           *name;
    int             num;
    RangeVar       *newowner;
    Node           *def;
    DropBehavior    behavior;
    bool            missing_ok;
    bool            recurse;
} AlterTableCmd;

/* VariableSetStmt - SET variable */
typedef struct VariableSetStmt
{
    NodeTag             type;       /* T_VariableSetStmt */
    VariableSetKind     kind;
    char               *name;
    List               *args;
    bool                is_local;
    ParseLoc            location;
} VariableSetStmt;

/* VariableShowStmt - SHOW variable */
typedef struct VariableShowStmt
{
    NodeTag     type;           /* T_VariableShowStmt */
    char       *name;
} VariableShowStmt;

/* TransactionStmt */
typedef struct TransactionStmt
{
    NodeTag     type;           /* T_TransactionStmt */
    int         kind;           /* TransactionStmtKind */
    List       *options;
    char       *savepoint_name;
    char       *gid;
    bool        chain;
    ParseLoc    location;
} TransactionStmt;

/* DropStmt */
typedef struct DropStmt
{
    NodeTag         type;           /* T_DropStmt */
    List           *objects;
    ObjectType      removeType;
    DropBehavior    behavior;
    bool            missing_ok;
    bool            concurrent;
} DropStmt;

/* RenameStmt */
typedef struct RenameStmt
{
    NodeTag         type;           /* T_RenameStmt */
    ObjectType      renameType;
    ObjectType      relationType;
    RangeVar       *relation;
    Node           *object;
    char           *subname;
    char           *newname;
    DropBehavior    behavior;
    bool            missing_ok;
} RenameStmt;

/* GrantStmt */
typedef struct GrantStmt
{
    NodeTag             type;       /* T_GrantStmt */
    bool                is_grant;
    GrantTargetType     targtype;
    ObjectType          objtype;
    List               *objects;
    List               *privileges; /* list of AccessPriv */
    List               *grantees;   /* list of RoleSpec */
    bool                grant_option;
    RoleSpec           *grantor;
    DropBehavior        behavior;
} GrantStmt;

/* ExplainStmt */
typedef struct ExplainStmt
{
    NodeTag     type;           /* T_ExplainStmt */
    Node       *query;
    List       *options;
} ExplainStmt;

/* CopyStmt */
typedef struct CopyStmt
{
    NodeTag     type;           /* T_CopyStmt */
    RangeVar   *relation;
    Node       *query;
    List       *attlist;
    bool        is_from;
    bool        is_program;
    char       *filename;
    List       *options;
    Node       *whereClause;
} CopyStmt;

/* LockStmt */
typedef struct LockStmt
{
    NodeTag     type;           /* T_LockStmt */
    List       *relations;
    int         mode;
    bool        nowait;
} LockStmt;

/* FetchStmt - FETCH / MOVE */
typedef struct FetchStmt
{
    NodeTag     type;           /* T_FetchStmt */
    int         direction;
    int         direction_keyword;
    long        howMany;
    char       *portalname;
    bool        ismove;
} FetchStmt;

/* PrepareStmt */
typedef struct PrepareStmt
{
    NodeTag     type;           /* T_PrepareStmt */
    char       *name;
    List       *argtypes;
    Node       *query;
} PrepareStmt;

/* ExecuteStmt */
typedef struct ExecuteStmt
{
    NodeTag     type;           /* T_ExecuteStmt */
    char       *name;
    List       *params;
} ExecuteStmt;

/* DeallocateStmt */
typedef struct DeallocateStmt
{
    NodeTag     type;           /* T_DeallocateStmt */
    char       *name;
    bool        isall;
    ParseLoc    location;
} DeallocateStmt;

/* DeclareCursorStmt */
typedef struct DeclareCursorStmt
{
    NodeTag     type;           /* T_DeclareCursorStmt */
    char       *portalname;
    int         options;
    Node       *query;
} DeclareCursorStmt;

/* TruncateStmt */
typedef struct TruncateStmt
{
    NodeTag         type;           /* T_TruncateStmt */
    List           *relations;
    bool            restart_seqs;
    DropBehavior    behavior;
} TruncateStmt;

/* ReturnStmt */
typedef struct ReturnStmt
{
    NodeTag     type;           /* T_ReturnStmt */
    Node       *returnval;
} ReturnStmt;


/* ================================================================
 * Helper function declarations (implemented in a .c companion)
 * ================================================================
 */

/* makeA_Expr / makeSimpleA_Expr - construct A_Expr nodes */
A_Expr *makeA_Expr(A_Expr_Kind kind, List *name,
                   Node *lexpr, Node *rexpr, ParseLoc location);
A_Expr *makeSimpleA_Expr(A_Expr_Kind kind, const char *name,
                         Node *lexpr, Node *rexpr, ParseLoc location);

/* makeBoolExpr - construct BoolExpr (AND/OR/NOT) */
BoolExpr *makeBoolExpr(BoolExprType boolop, List *args, ParseLoc location);

/* makeFuncCall - construct a FuncCall node */
FuncCall *makeFuncCall(List *name, List *args, CoercionForm funcformat,
                       ParseLoc location);

/* makeTypeCast helper */
Node *makeTypeCast_node(Node *arg, TypeName *typename_, ParseLoc location);

/* makeRangeVar - construct RangeVar from names */
RangeVar *makeRangeVar(char *schemaname, char *relname, ParseLoc location);

/* makeColumnRef helper */
Node *makeColumnRef_node(char *colname, List *indirection, ParseLoc location);

/* Value constructors used by the parser */
Node *makeIntConst(int val, ParseLoc location);
Node *makeFloatConst(char *str, ParseLoc location);
Node *makeBoolAConst(bool state, ParseLoc location);
Node *makeBitStringConst(char *str, ParseLoc location);
Node *makeNullAConst(ParseLoc location);
Node *makeAConst(Node *v, ParseLoc location);
Node *makeStringConst(char *str, ParseLoc location);
Node *makeStringConstCast(char *str, ParseLoc location, TypeName *typename_);

/* SetOp helper */
Node *makeSetOp(SetOperation op, bool all, Node *larg, Node *rarg);

/* makeRawStmt */
RawStmt *makeRawStmt(Node *stmt, ParseLoc stmt_location);

/* RoleSpec constructor */
RoleSpec *makeRoleSpec(RoleSpecType type, ParseLoc location);

/* SQL value function */
Node *makeSQLValueFunction(SQLValueFunctionOp op, int32 typmod,
                           ParseLoc location);

/* DefElem constructor */
DefElem *makeDefElem(char *name, Node *arg, ParseLoc location);
DefElem *makeDefElemExtended(char *nameSpace, char *name, Node *arg,
                             int defaction, ParseLoc location);

/* Utility: NameListToString */
char *NameListToString(List *names);


#endif /* PG_AST_NODES_H */
