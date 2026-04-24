# Top-Level Grammar


The entry point for parsing: parse_toplevel, stmtmulti,
toplevel_stmt, and the stmt dispatch rule that connects
all statement types.


```yaml
name: pg-toplevel
version: 17.0.0
description: Top-level parse entry, statement list, and stmt dispatch
provides: [pg-toplevel]
depends: [pg-type-decls]
```

## Production Rules

```lime rules
parse_toplevel(A) ::= stmtmulti(B). {
    pg_yyget_extra(yyscanner)->parsetree = B;
    				(void) yynerrs;
}

parse_toplevel(A) ::= MODE_TYPE_NAME typename(C). {
    pg_yyget_extra(yyscanner)->parsetree = list_make1(C);
}

parse_toplevel(A) ::= MODE_PLPGSQL_EXPR pLpgSQL_Expr(C). {
    pg_yyget_extra(yyscanner)->parsetree =
    					list_make1(makeRawStmt(C, LOC(C)));
}

parse_toplevel(A) ::= MODE_PLPGSQL_ASSIGN1 pLAssignStmt(C). {
    pLAssignStmt *n = (pLAssignStmt *) C;

    				n->nnames = 1;
    				pg_yyget_extra(yyscanner)->parsetree =
    					list_make1(makeRawStmt((Node *) n, LOC(C)));
}

parse_toplevel(A) ::= MODE_PLPGSQL_ASSIGN2 pLAssignStmt(C). {
    pLAssignStmt *n = (pLAssignStmt *) C;

    				n->nnames = 2;
    				pg_yyget_extra(yyscanner)->parsetree =
    					list_make1(makeRawStmt((Node *) n, LOC(C)));
}

parse_toplevel(A) ::= MODE_PLPGSQL_ASSIGN3 pLAssignStmt(C). {
    pLAssignStmt *n = (pLAssignStmt *) C;

    				n->nnames = 3;
    				pg_yyget_extra(yyscanner)->parsetree =
    					list_make1(makeRawStmt((Node *) n, LOC(C)));
}

/* ----- stmtmulti ----- */

stmtmulti(A) ::= stmtmulti(B) SEMICOLON toplevel_stmt(D). {
    if (B != NIL)
    					{

    						updateRawStmtEnd(llast_node(RawStmt, B), LOC(C));
    					}
    					if (D != NULL)
    						A = lappend(B, makeRawStmt(D, LOC(D)));
    					else
    						A = B;
}

stmtmulti(A) ::= toplevel_stmt(B). {
    if (B != NULL)
    						A = list_make1(makeRawStmt(B, LOC(B)));
    					else
    						A = NIL;
}

/* ----- toplevel_stmt ----- */

toplevel_stmt(A) ::= stmt(B).

toplevel_stmt(A) ::= transactionStmtLegacy(B).

/* ----- stmt ----- */

stmt(A) ::= alterEventTrigStmt(B).

stmt(A) ::= alterCollationStmt(B).

stmt(A) ::= alterDatabaseStmt(B).

stmt(A) ::= alterDatabaseSetStmt(B).

stmt(A) ::= alterDefaultPrivilegesStmt(B).

stmt(A) ::= alterDomainStmt(B).

stmt(A) ::= alterEnumStmt(B).

stmt(A) ::= alterExtensionStmt(B).

stmt(A) ::= alterExtensionContentsStmt(B).

stmt(A) ::= alterFdwStmt(B).

stmt(A) ::= alterForeignServerStmt(B).

stmt(A) ::= alterFunctionStmt(B).

stmt(A) ::= alterGroupStmt(B).

stmt(A) ::= alterObjectDependsStmt(B).

stmt(A) ::= alterObjectSchemaStmt(B).

stmt(A) ::= alterOwnerStmt(B).

stmt(A) ::= alterOperatorStmt(B).

stmt(A) ::= alterTypeStmt(B).

stmt(A) ::= alterPolicyStmt(B).

stmt(A) ::= alterPropGraphStmt(B).

stmt(A) ::= alterSeqStmt(B).

stmt(A) ::= alterSystemStmt(B).

stmt(A) ::= alterTableStmt(B).

stmt(A) ::= alterTblSpcStmt(B).

stmt(A) ::= alterCompositeTypeStmt(B).

stmt(A) ::= alterPublicationStmt(B).

stmt(A) ::= alterRoleSetStmt(B).

stmt(A) ::= alterRoleStmt(B).

stmt(A) ::= alterSubscriptionStmt(B).

stmt(A) ::= alterStatsStmt(B).

stmt(A) ::= alterTSConfigurationStmt(B).

stmt(A) ::= alterTSDictionaryStmt(B).

stmt(A) ::= alterUserMappingStmt(B).

stmt(A) ::= analyzeStmt(B).

stmt(A) ::= callStmt(B).

stmt(A) ::= checkPointStmt(B).

stmt(A) ::= closePortalStmt(B).

stmt(A) ::= commentStmt(B).

stmt(A) ::= constraintsSetStmt(B).

stmt(A) ::= copyStmt(B).

stmt(A) ::= createAmStmt(B).

stmt(A) ::= createAsStmt(B).

stmt(A) ::= createAssertionStmt(B).

stmt(A) ::= createCastStmt(B).

stmt(A) ::= createConversionStmt(B).

stmt(A) ::= createDomainStmt(B).

stmt(A) ::= createExtensionStmt(B).

stmt(A) ::= createFdwStmt(B).

stmt(A) ::= createForeignServerStmt(B).

stmt(A) ::= createForeignTableStmt(B).

stmt(A) ::= createFunctionStmt(B).

stmt(A) ::= createGroupStmt(B).

stmt(A) ::= createMatViewStmt(B).

stmt(A) ::= createOpClassStmt(B).

stmt(A) ::= createOpFamilyStmt(B).

stmt(A) ::= createPublicationStmt(B).

stmt(A) ::= alterOpFamilyStmt(B).

stmt(A) ::= createPolicyStmt(B).

stmt(A) ::= createPLangStmt(B).

stmt(A) ::= createPropGraphStmt(B).

stmt(A) ::= createSchemaStmt(B).

stmt(A) ::= createSeqStmt(B).

stmt(A) ::= createStmt(B).

stmt(A) ::= createSubscriptionStmt(B).

stmt(A) ::= createStatsStmt(B).

stmt(A) ::= createTableSpaceStmt(B).

stmt(A) ::= createTransformStmt(B).

stmt(A) ::= createTrigStmt(B).

stmt(A) ::= createEventTrigStmt(B).

stmt(A) ::= createRoleStmt(B).

stmt(A) ::= createUserStmt(B).

stmt(A) ::= createUserMappingStmt(B).

stmt(A) ::= createdbStmt(B).

stmt(A) ::= deallocateStmt(B).

stmt(A) ::= declareCursorStmt(B).

stmt(A) ::= defineStmt(B).

stmt(A) ::= deleteStmt(B).

stmt(A) ::= discardStmt(B).

stmt(A) ::= doStmt(B).

stmt(A) ::= dropCastStmt(B).

stmt(A) ::= dropOpClassStmt(B).

stmt(A) ::= dropOpFamilyStmt(B).

stmt(A) ::= dropOwnedStmt(B).

stmt(A) ::= dropStmt(B).

stmt(A) ::= dropSubscriptionStmt(B).

stmt(A) ::= dropTableSpaceStmt(B).

stmt(A) ::= dropTransformStmt(B).

stmt(A) ::= dropRoleStmt(B).

stmt(A) ::= dropUserMappingStmt(B).

stmt(A) ::= dropdbStmt(B).

stmt(A) ::= executeStmt(B).

stmt(A) ::= explainStmt(B).

stmt(A) ::= fetchStmt(B).

stmt(A) ::= grantStmt(B).

stmt(A) ::= grantRoleStmt(B).

stmt(A) ::= importForeignSchemaStmt(B).

stmt(A) ::= indexStmt(B).

stmt(A) ::= insertStmt(B).

stmt(A) ::= listenStmt(B).

stmt(A) ::= refreshMatViewStmt(B).

stmt(A) ::= loadStmt(B).

stmt(A) ::= lockStmt(B).

stmt(A) ::= mergeStmt(B).

stmt(A) ::= notifyStmt(B).

stmt(A) ::= prepareStmt(B).

stmt(A) ::= reassignOwnedStmt(B).

stmt(A) ::= reindexStmt(B).

stmt(A) ::= removeAggrStmt(B).

stmt(A) ::= removeFuncStmt(B).

stmt(A) ::= removeOperStmt(B).

stmt(A) ::= renameStmt(B).

stmt(A) ::= repackStmt(B).

stmt(A) ::= revokeStmt(B).

stmt(A) ::= revokeRoleStmt(B).

stmt(A) ::= ruleStmt(B).

stmt(A) ::= secLabelStmt(B).

stmt(A) ::= selectStmt(B).

stmt(A) ::= transactionStmt(B).

stmt(A) ::= truncateStmt(B).

stmt(A) ::= unlistenStmt(B).

stmt(A) ::= updateStmt(B).

stmt(A) ::= vacuumStmt(B).

stmt(A) ::= variableResetStmt(B).

stmt(A) ::= variableSetStmt(B).

stmt(A) ::= variableShowStmt(B).

stmt(A) ::= viewStmt(B).

stmt(A) ::= waitStmt(B).

stmt(A) ::= . {
    A = NULL;
}

/* ----- opt_single_name ----- */
```

