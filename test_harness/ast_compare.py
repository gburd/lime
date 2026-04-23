"""AST comparison utilities for SQL parser testing.

Supports generic AST comparison as well as PostgreSQL-specific node type
handling for comparing parse trees from different parser implementations.
"""

from typing import Any, Callable, Dict, List, Optional, Set, Tuple


# PostgreSQL parse tree location fields that vary between parser
# implementations and should generally be ignored during comparison.
PG_LOCATION_FIELDS = {
    "location",
    "stmt_location",
    "stmt_len",
}

# PostgreSQL node type wrapper keys. pg_query wraps each node in a
# dict like {"SelectStmt": {...}}. These are recognized node types.
PG_STATEMENT_NODE_TYPES = {
    "SelectStmt",
    "InsertStmt",
    "UpdateStmt",
    "DeleteStmt",
    "CreateStmt",
    "AlterTableStmt",
    "DropStmt",
    "IndexStmt",
    "CreateFunctionStmt",
    "ViewStmt",
    "RuleStmt",
    "TruncateStmt",
    "CopyStmt",
    "GrantStmt",
    "RevokeStmt",
    "TransactionStmt",
    "ExplainStmt",
    "VacuumStmt",
    "LockStmt",
    "VariableSetStmt",
    "VariableShowStmt",
    "DoStmt",
    "CreateTableAsStmt",
    "PrepareStmt",
    "ExecuteStmt",
    "DeallocateStmt",
    "DeclareCursorStmt",
    "FetchStmt",
    "ClosePortalStmt",
    "NotifyStmt",
    "ListenStmt",
    "UnlistenStmt",
    "CommentStmt",
    "CreateSchemaStmt",
    "CreateSeqStmt",
    "AlterSeqStmt",
    "DefineStmt",
    "CompositeTypeStmt",
    "CreateEnumStmt",
    "CreateRangeStmt",
    "AlterEnumStmt",
    "CreateDomainStmt",
    "AlterDomainStmt",
    "CreateOpClassStmt",
    "CreateOpFamilyStmt",
    "AlterOpFamilyStmt",
    "CreateConversionStmt",
    "CreateCastStmt",
    "CreateTrigStmt",
    "CreateEventTrigStmt",
    "AlterEventTrigStmt",
    "CreatePLangStmt",
    "CreateRoleStmt",
    "AlterRoleStmt",
    "AlterRoleSetStmt",
    "DropRoleStmt",
    "CreatedbStmt",
    "AlterDatabaseStmt",
    "AlterDatabaseSetStmt",
    "DropdbStmt",
    "CreateTableSpaceStmt",
    "DropTableSpaceStmt",
    "AlterTableSpaceOptionsStmt",
    "CreateExtensionStmt",
    "AlterExtensionStmt",
    "AlterExtensionContentsStmt",
    "CreateFdwStmt",
    "AlterFdwStmt",
    "CreateForeignServerStmt",
    "AlterForeignServerStmt",
    "CreateForeignTableStmt",
    "CreateUserMappingStmt",
    "AlterUserMappingStmt",
    "DropUserMappingStmt",
    "ImportForeignSchemaStmt",
    "CreatePolicyStmt",
    "AlterPolicyStmt",
    "CreateAmStmt",
    "CreatePublicationStmt",
    "AlterPublicationStmt",
    "CreateSubscriptionStmt",
    "AlterSubscriptionStmt",
    "DropSubscriptionStmt",
    "CreateStatsStmt",
    "AlterStatsStmt",
    "CreateTransformStmt",
    "RawStmt",
}

# PostgreSQL expression node types
PG_EXPR_NODE_TYPES = {
    "ColumnRef",
    "A_Const",
    "A_Expr",
    "BoolExpr",
    "FuncCall",
    "SubLink",
    "TypeCast",
    "CaseExpr",
    "CaseWhen",
    "CoalesceExpr",
    "MinMaxExpr",
    "NullTest",
    "BooleanTest",
    "ParamRef",
    "A_ArrayExpr",
    "A_Indirection",
    "A_Star",
    "ResTarget",
    "SortBy",
    "WindowDef",
    "RangeVar",
    "RangeSubselect",
    "RangeFunction",
    "RangeTableFunc",
    "RangeTableFuncCol",
    "RangeTableSample",
    "JoinExpr",
    "GroupingSet",
    "WithClause",
    "CommonTableExpr",
    "InferClause",
    "OnConflictClause",
    "SetToDefault",
    "CurrentOfExpr",
    "CollateClause",
    "GroupingFunc",
    "RowExpr",
    "XmlExpr",
    "XmlSerialize",
    "String",
    "Integer",
    "Float",
    "Boolean",
    "BitString",
    "Null",
    "List",
}

ALL_PG_NODE_TYPES = PG_STATEMENT_NODE_TYPES | PG_EXPR_NODE_TYPES


class ASTComparator:
    """Compare ASTs with configurable ignore fields.

    Supports two comparison strategies:
    - Strict: all non-ignored fields must match exactly.
    - Structural: node types and key structure must match, but allows
      minor representation differences between parser implementations.
    """

    def __init__(
        self,
        ignore_fields: Optional[Set[str]] = None,
        strict: bool = True,
        node_type_aliases: Optional[Dict[str, str]] = None,
    ):
        """
        Args:
            ignore_fields: Field names to skip during comparison.
            strict: If False, allow structural differences like missing
                    extra keys in actual that are not in expected.
            node_type_aliases: Map of equivalent node type names between
                    parser implementations (e.g., pg_query vs Lime).
        """
        self.ignore_fields = ignore_fields or {"location", "line", "column", "pos"}
        self.strict = strict
        self.node_type_aliases = node_type_aliases or {}

    def compare(
        self, expected: Any, actual: Any, path: str = "root"
    ) -> Tuple[bool, str]:
        """
        Compare two ASTs.
        Returns (is_equal, error_message).
        """
        if type(expected) != type(actual):
            return (
                False,
                f"{path}: type mismatch: "
                f"{type(expected).__name__} vs {type(actual).__name__}",
            )

        if isinstance(expected, dict):
            return self._compare_dicts(expected, actual, path)

        elif isinstance(expected, list):
            return self._compare_lists(expected, actual, path)

        else:
            if expected == actual:
                return (True, "")
            else:
                return (
                    False,
                    f"{path}: value mismatch: {expected!r} vs {actual!r}",
                )

    def _compare_dicts(
        self, expected: dict, actual: dict, path: str
    ) -> Tuple[bool, str]:
        """Compare two dict nodes, respecting ignore_fields and aliases."""
        for key in expected:
            if key in self.ignore_fields:
                continue

            # Check for node type aliases
            actual_key = self.node_type_aliases.get(key, key)

            if actual_key not in actual and key not in actual:
                return (False, f"{path}.{key}: missing in actual")

            actual_val = actual.get(actual_key, actual.get(key))
            equal, msg = self.compare(
                expected[key], actual_val, f"{path}.{key}"
            )
            if not equal:
                return (False, msg)

        # In strict mode, check for unexpected extra keys in actual
        if self.strict:
            for key in actual:
                if key in self.ignore_fields:
                    continue
                # Check reverse aliases
                reverse_aliases = {v: k for k, v in self.node_type_aliases.items()}
                expected_key = reverse_aliases.get(key, key)
                if expected_key not in expected and key not in expected:
                    return (False, f"{path}.{key}: unexpected key in actual")

        return (True, "")

    def _compare_lists(
        self, expected: list, actual: list, path: str
    ) -> Tuple[bool, str]:
        """Compare two list nodes."""
        if len(expected) != len(actual):
            return (
                False,
                f"{path}: length mismatch: {len(expected)} vs {len(actual)}",
            )
        for i, (e, a) in enumerate(zip(expected, actual)):
            equal, msg = self.compare(e, a, f"{path}[{i}]")
            if not equal:
                return (False, msg)
        return (True, "")

    def diff(
        self, expected: Any, actual: Any, path: str = "root"
    ) -> List[str]:
        """
        Collect all differences between two ASTs.
        Unlike compare(), this does not short-circuit on first difference.
        Returns a list of difference descriptions.
        """
        diffs: List[str] = []
        self._collect_diffs(expected, actual, path, diffs)
        return diffs

    def _collect_diffs(
        self, expected: Any, actual: Any, path: str, diffs: List[str]
    ):
        """Recursively collect all differences."""
        if type(expected) != type(actual):
            diffs.append(
                f"{path}: type mismatch: "
                f"{type(expected).__name__} vs {type(actual).__name__}"
            )
            return

        if isinstance(expected, dict):
            all_keys = set(expected.keys()) | set(actual.keys())
            for key in sorted(all_keys):
                if key in self.ignore_fields:
                    continue
                if key not in expected:
                    if self.strict:
                        diffs.append(f"{path}.{key}: unexpected key in actual")
                    continue
                if key not in actual:
                    diffs.append(f"{path}.{key}: missing in actual")
                    continue
                self._collect_diffs(
                    expected[key], actual[key], f"{path}.{key}", diffs
                )

        elif isinstance(expected, list):
            if len(expected) != len(actual):
                diffs.append(
                    f"{path}: length mismatch: "
                    f"{len(expected)} vs {len(actual)}"
                )
                # Compare up to the shorter length
                for i in range(min(len(expected), len(actual))):
                    self._collect_diffs(
                        expected[i], actual[i], f"{path}[{i}]", diffs
                    )
            else:
                for i, (e, a) in enumerate(zip(expected, actual)):
                    self._collect_diffs(e, a, f"{path}[{i}]", diffs)

        else:
            if expected != actual:
                diffs.append(
                    f"{path}: value mismatch: {expected!r} vs {actual!r}"
                )
