# ALTER TABLE


ALTER TABLE grammar: ADD/DROP/ALTER COLUMN, constraints,
identity columns, replica identity, SET/RESET options,
and composite type alterations.


```yaml
name: pg-ddl-alter-table
version: 17.0.0
description: ALTER TABLE, ALTER TYPE, and tablespace alterations
provides: [pg-ddl-alter-table]
depends: [pg-type-decls, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
alterTableStmt(A) ::= ALTER TABLE relation_expr(D) alter_table_cmds(E). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = D;
    					n->cmds = E;
    					n->objtype = OBJECT_TABLE;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER TABLE IF_P EXISTS relation_expr(F) alter_table_cmds(G). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = F;
    					n->cmds = G;
    					n->objtype = OBJECT_TABLE;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER TABLE relation_expr(D) partition_cmd(E). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = D;
    					n->cmds = list_make1(E);
    					n->objtype = OBJECT_TABLE;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER TABLE IF_P EXISTS relation_expr(F) partition_cmd(G). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = F;
    					n->cmds = list_make1(G);
    					n->objtype = OBJECT_TABLE;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER TABLE ALL IN_P TABLESPACE name(G) SET TABLESPACE name(J) opt_nowait(K). {
    AlterTableMoveAllStmt *n =
    						makeNode(AlterTableMoveAllStmt);

    					n->orig_tablespacename = G;
    					n->objtype = OBJECT_TABLE;
    					n->roles = NIL;
    					n->new_tablespacename = J;
    					n->nowait = K;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER TABLE ALL IN_P TABLESPACE name(G) OWNED BY role_list(J) SET TABLESPACE name(M) opt_nowait(N). {
    AlterTableMoveAllStmt *n =
    						makeNode(AlterTableMoveAllStmt);

    					n->orig_tablespacename = G;
    					n->objtype = OBJECT_TABLE;
    					n->roles = J;
    					n->new_tablespacename = M;
    					n->nowait = N;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER INDEX qualified_name(D) alter_table_cmds(E). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = D;
    					n->cmds = E;
    					n->objtype = OBJECT_INDEX;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER INDEX IF_P EXISTS qualified_name(F) alter_table_cmds(G). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = F;
    					n->cmds = G;
    					n->objtype = OBJECT_INDEX;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER INDEX qualified_name(D) index_partition_cmd(E). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = D;
    					n->cmds = list_make1(E);
    					n->objtype = OBJECT_INDEX;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER INDEX ALL IN_P TABLESPACE name(G) SET TABLESPACE name(J) opt_nowait(K). {
    AlterTableMoveAllStmt *n =
    						makeNode(AlterTableMoveAllStmt);

    					n->orig_tablespacename = G;
    					n->objtype = OBJECT_INDEX;
    					n->roles = NIL;
    					n->new_tablespacename = J;
    					n->nowait = K;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER INDEX ALL IN_P TABLESPACE name(G) OWNED BY role_list(J) SET TABLESPACE name(M) opt_nowait(N). {
    AlterTableMoveAllStmt *n =
    						makeNode(AlterTableMoveAllStmt);

    					n->orig_tablespacename = G;
    					n->objtype = OBJECT_INDEX;
    					n->roles = J;
    					n->new_tablespacename = M;
    					n->nowait = N;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER SEQUENCE qualified_name(D) alter_table_cmds(E). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = D;
    					n->cmds = E;
    					n->objtype = OBJECT_SEQUENCE;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER SEQUENCE IF_P EXISTS qualified_name(F) alter_table_cmds(G). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = F;
    					n->cmds = G;
    					n->objtype = OBJECT_SEQUENCE;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER VIEW qualified_name(D) alter_table_cmds(E). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = D;
    					n->cmds = E;
    					n->objtype = OBJECT_VIEW;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER VIEW IF_P EXISTS qualified_name(F) alter_table_cmds(G). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = F;
    					n->cmds = G;
    					n->objtype = OBJECT_VIEW;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER MATERIALIZED VIEW qualified_name(E) alter_table_cmds(F). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = E;
    					n->cmds = F;
    					n->objtype = OBJECT_MATVIEW;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER MATERIALIZED VIEW IF_P EXISTS qualified_name(G) alter_table_cmds(H). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = G;
    					n->cmds = H;
    					n->objtype = OBJECT_MATVIEW;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER MATERIALIZED VIEW ALL IN_P TABLESPACE name(H) SET TABLESPACE name(K) opt_nowait(L). {
    AlterTableMoveAllStmt *n =
    						makeNode(AlterTableMoveAllStmt);

    					n->orig_tablespacename = H;
    					n->objtype = OBJECT_MATVIEW;
    					n->roles = NIL;
    					n->new_tablespacename = K;
    					n->nowait = L;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER MATERIALIZED VIEW ALL IN_P TABLESPACE name(H) OWNED BY role_list(K) SET TABLESPACE name(N) opt_nowait(O). {
    AlterTableMoveAllStmt *n =
    						makeNode(AlterTableMoveAllStmt);

    					n->orig_tablespacename = H;
    					n->objtype = OBJECT_MATVIEW;
    					n->roles = K;
    					n->new_tablespacename = N;
    					n->nowait = O;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER FOREIGN TABLE relation_expr(E) alter_table_cmds(F). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = E;
    					n->cmds = F;
    					n->objtype = OBJECT_FOREIGN_TABLE;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterTableStmt(A) ::= ALTER FOREIGN TABLE IF_P EXISTS relation_expr(G) alter_table_cmds(H). {
    alterTableStmt *n = makeNode(alterTableStmt);

    					n->relation = G;
    					n->cmds = H;
    					n->objtype = OBJECT_FOREIGN_TABLE;
    					n->missing_ok = true;
    					A = (Node *) n;
}

/* ----- alter_table_cmds ----- */

alter_table_cmds(A) ::= alter_table_cmd(B). {
    A = list_make1(B);
}

alter_table_cmds(A) ::= alter_table_cmds(B) COMMA alter_table_cmd(D). {
    A = lappend(B, D);
}

/* ----- partitions_list ----- */

partition_cmd(A) ::= ATTACH PARTITION qualified_name(D) partitionBoundSpec(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					PartitionCmd *cmd = makeNode(PartitionCmd);

    					n->subtype = AT_AttachPartition;
    					cmd->name = D;
    					cmd->bound = E;
    					cmd->partlist = NIL;
    					cmd->concurrent = false;
    					n->def = (Node *) cmd;

    					A = (Node *) n;
}

partition_cmd(A) ::= DETACH PARTITION qualified_name(D) opt_concurrently(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					PartitionCmd *cmd = makeNode(PartitionCmd);

    					n->subtype = AT_DetachPartition;
    					cmd->name = D;
    					cmd->bound = NULL;
    					cmd->partlist = NIL;
    					cmd->concurrent = E;
    					n->def = (Node *) cmd;

    					A = (Node *) n;
}

partition_cmd(A) ::= DETACH PARTITION qualified_name(D) FINALIZE. {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					PartitionCmd *cmd = makeNode(PartitionCmd);

    					n->subtype = AT_DetachPartitionFinalize;
    					cmd->name = D;
    					cmd->bound = NULL;
    					cmd->partlist = NIL;
    					cmd->concurrent = false;
    					n->def = (Node *) cmd;
    					A = (Node *) n;
}

partition_cmd(A) ::= SPLIT PARTITION qualified_name(D) INTO LPAREN partitions_list(G) RPAREN. {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					PartitionCmd *cmd = makeNode(PartitionCmd);

    					n->subtype = AT_SplitPartition;
    					cmd->name = D;
    					cmd->bound = NULL;
    					cmd->partlist = G;
    					cmd->concurrent = false;
    					n->def = (Node *) cmd;
    					A = (Node *) n;
}

partition_cmd(A) ::= MERGE PARTITIONS LPAREN qualified_name_list(E) RPAREN INTO qualified_name(H). {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					PartitionCmd *cmd = makeNode(PartitionCmd);

    					n->subtype = AT_MergePartitions;
    					cmd->name = H;
    					cmd->bound = NULL;
    					cmd->partlist = E;
    					cmd->concurrent = false;
    					n->def = (Node *) cmd;
    					A = (Node *) n;
}

/* ----- index_partition_cmd ----- */

index_partition_cmd(A) ::= ATTACH PARTITION qualified_name(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					PartitionCmd *cmd = makeNode(PartitionCmd);

    					n->subtype = AT_AttachPartition;
    					cmd->name = D;
    					cmd->bound = NULL;
    					cmd->partlist = NIL;
    					cmd->concurrent = false;
    					n->def = (Node *) cmd;

    					A = (Node *) n;
}

/* ----- alter_table_cmd ----- */

alter_table_cmd(A) ::= ADD_P columnDef(C). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_AddColumn;
    					n->def = C;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ADD_P IF_P NOT EXISTS columnDef(F). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_AddColumn;
    					n->def = F;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ADD_P COLUMN columnDef(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_AddColumn;
    					n->def = D;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ADD_P COLUMN IF_P NOT EXISTS columnDef(G). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_AddColumn;
    					n->def = G;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) alter_column_default(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_ColumnDefault;
    					n->name = D;
    					n->def = E;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) DROP NOT NULL_P. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropNotNull;
    					n->name = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) SET NOT NULL_P. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetNotNull;
    					n->name = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) SET EXPRESSION AS LPAREN a_expr(I) RPAREN. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetExpression;
    					n->name = D;
    					n->def = I;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) DROP EXPRESSION. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropExpression;
    					n->name = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) DROP EXPRESSION IF_P EXISTS. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropExpression;
    					n->name = D;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) SET STATISTICS set_statistics_value(G). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetStatistics;
    					n->name = D;
    					n->def = G;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) iconst(D) SET STATISTICS set_statistics_value(G). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					if (D <= 0 || D > PG_INT16_MAX)
    						ereport(ERROR,
    								(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
    								 errmsg("column number must be in range from 1 to %d", PG_INT16_MAX),
    								 parser_errposition(LOC(D))));

    					n->subtype = AT_SetStatistics;
    					n->num = (int16) D;
    					n->def = G;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) SET reloptions(F). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetOptions;
    					n->name = D;
    					n->def = (Node *) F;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) RESET reloptions(F). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_ResetOptions;
    					n->name = D;
    					n->def = (Node *) F;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) SET column_storage(F). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetStorage;
    					n->name = D;
    					n->def = (Node *) makeString(F);
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) SET column_compression(F). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetCompression;
    					n->name = D;
    					n->def = (Node *) makeString(F);
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) ADD_P GENERATED generated_when(G) AS IDENTITY_P optParenthesizedSeqOptList(J). {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					Constraint *c = makeNode(Constraint);

    					c->contype = CONSTR_IDENTITY;
    					c->generated_when = G;
    					c->options = J;
    					c->location = LOC(F);

    					n->subtype = AT_AddIdentity;
    					n->name = D;
    					n->def = (Node *) c;

    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) alter_identity_column_option_list(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetIdentity;
    					n->name = D;
    					n->def = (Node *) E;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) DROP IDENTITY_P. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropIdentity;
    					n->name = D;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) DROP IDENTITY_P IF_P EXISTS. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropIdentity;
    					n->name = D;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= DROP opt_column(C) IF_P EXISTS colId(F) opt_drop_behavior(G). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropColumn;
    					n->name = F;
    					n->behavior = G;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= DROP opt_column(C) colId(D) opt_drop_behavior(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropColumn;
    					n->name = D;
    					n->behavior = E;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) opt_set_data(E) TYPE_P typename(G) opt_collate_clause(H) alter_using(I). {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					ColumnDef *def = makeNode(ColumnDef);

    					n->subtype = AT_AlterColumnType;
    					n->name = D;
    					n->def = (Node *) def;

    					def->typeName = G;
    					def->collClause = (CollateClause *) H;
    					def->raw_default = I;
    					def->location = LOC(D);
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER opt_column(C) colId(D) alter_generic_options(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_AlterColumnGenericOptions;
    					n->name = D;
    					n->def = (Node *) E;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ADD_P tableConstraint(C). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_AddConstraint;
    					n->def = C;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER CONSTRAINT name(D) constraintAttributeSpec(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					ATAlterConstraint *c = makeNode(ATAlterConstraint);

    					n->subtype = AT_AlterConstraint;
    					n->def = (Node *) c;
    					c->conname = D;
    					if (E & (CAS_NOT_ENFORCED | CAS_ENFORCED))
    						c->alterEnforceability = true;
    					if (E & (CAS_DEFERRABLE | CAS_NOT_DEFERRABLE |
    							  CAS_INITIALLY_DEFERRED | CAS_INITIALLY_IMMEDIATE))
    						c->alterDeferrability = true;
    					if (E & CAS_NO_INHERIT)
    						c->alterInheritability = true;

    					if (E & CAS_NOT_VALID)
    						ereport(ERROR,
    								errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								errmsg("constraints cannot be altered to be NOT VALID"),
    								parser_errposition(LOC(E)));
    					processCASbits(E, LOC(E), "FOREIGN KEY",
    									&c->deferrable,
    									&c->initdeferred,
    									&c->is_enforced,
    									NULL,
    									&c->noinherit,
    									yyscanner);
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ALTER CONSTRAINT name(D) INHERIT. {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					ATAlterConstraint *c = makeNode(ATAlterConstraint);

    					n->subtype = AT_AlterConstraint;
    					n->def = (Node *) c;
    					c->conname = D;
    					c->alterInheritability = true;
    					c->noinherit = false;

    					A = (Node *) n;
}

alter_table_cmd(A) ::= VALIDATE CONSTRAINT name(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_ValidateConstraint;
    					n->name = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= DROP CONSTRAINT IF_P EXISTS name(F) opt_drop_behavior(G). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropConstraint;
    					n->name = F;
    					n->behavior = G;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= DROP CONSTRAINT name(D) opt_drop_behavior(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropConstraint;
    					n->name = D;
    					n->behavior = E;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= SET WITHOUT OIDS. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropOids;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= CLUSTER ON name(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_ClusterOn;
    					n->name = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= SET WITHOUT CLUSTER. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropCluster;
    					n->name = NULL;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= SET LOGGED. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetLogged;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= SET UNLOGGED. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetUnLogged;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ENABLE_P TRIGGER name(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_EnableTrig;
    					n->name = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ENABLE_P ALWAYS TRIGGER name(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_EnableAlwaysTrig;
    					n->name = E;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ENABLE_P REPLICA TRIGGER name(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_EnableReplicaTrig;
    					n->name = E;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ENABLE_P TRIGGER ALL. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_EnableTrigAll;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ENABLE_P TRIGGER USER. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_EnableTrigUser;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= DISABLE_P TRIGGER name(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DisableTrig;
    					n->name = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= DISABLE_P TRIGGER ALL. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DisableTrigAll;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= DISABLE_P TRIGGER USER. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DisableTrigUser;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ENABLE_P RULE name(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_EnableRule;
    					n->name = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ENABLE_P ALWAYS RULE name(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_EnableAlwaysRule;
    					n->name = E;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ENABLE_P REPLICA RULE name(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_EnableReplicaRule;
    					n->name = E;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= DISABLE_P RULE name(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DisableRule;
    					n->name = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= INHERIT qualified_name(C). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_AddInherit;
    					n->def = (Node *) C;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= NO INHERIT qualified_name(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropInherit;
    					n->def = (Node *) D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= OF any_name(C). {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					TypeName   *def = makeTypeNameFromNameList(C);

    					def->location = LOC(C);
    					n->subtype = AT_AddOf;
    					n->def = (Node *) def;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= NOT OF. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropOf;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= OWNER TO roleSpec(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_ChangeOwner;
    					n->newowner = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= SET ACCESS METHOD set_access_method_name(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetAccessMethod;
    					n->name = E;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= SET TABLESPACE name(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetTableSpace;
    					n->name = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= SET reloptions(C). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_SetRelOptions;
    					n->def = (Node *) C;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= RESET reloptions(C). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_ResetRelOptions;
    					n->def = (Node *) C;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= REPLICA IDENTITY_P replica_identity(D). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_ReplicaIdentity;
    					n->def = D;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= ENABLE_P ROW LEVEL SECURITY. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_EnableRowSecurity;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= DISABLE_P ROW LEVEL SECURITY. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DisableRowSecurity;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= FORCE ROW LEVEL SECURITY. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_ForceRowSecurity;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= NO FORCE ROW LEVEL SECURITY. {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_NoForceRowSecurity;
    					A = (Node *) n;
}

alter_table_cmd(A) ::= alter_generic_options(B). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_GenericOptions;
    					n->def = (Node *) B;
    					A = (Node *) n;
}

/* ----- alter_column_default ----- */

alter_column_default(A) ::= SET DEFAULT a_expr(D). {
    A = D;
}

alter_column_default(A) ::= DROP DEFAULT. {
    A = NULL;
}

/* ----- opt_collate_clause ----- */

opt_collate_clause(A) ::= COLLATE any_name(C). {
    CollateClause *n = makeNode(CollateClause);

    					n->arg = NULL;
    					n->collname = C;
    					n->location = LOC(B);
    					A = (Node *) n;
}

opt_collate_clause(A) ::= . {
    A = NULL;
}

/* ----- alter_using ----- */

alter_using(A) ::= USING a_expr(C). {
    A = C;
}

alter_using(A) ::= . {
    A = NULL;
}

/* ----- replica_identity ----- */

replica_identity(A) ::= NOTHING. {
    ReplicaIdentityStmt *n = makeNode(ReplicaIdentityStmt);

    					n->identity_type = REPLICA_IDENTITY_NOTHING;
    					n->name = NULL;
    					A = (Node *) n;
}

replica_identity(A) ::= FULL. {
    ReplicaIdentityStmt *n = makeNode(ReplicaIdentityStmt);

    					n->identity_type = REPLICA_IDENTITY_FULL;
    					n->name = NULL;
    					A = (Node *) n;
}

replica_identity(A) ::= DEFAULT. {
    ReplicaIdentityStmt *n = makeNode(ReplicaIdentityStmt);

    					n->identity_type = REPLICA_IDENTITY_DEFAULT;
    					n->name = NULL;
    					A = (Node *) n;
}

replica_identity(A) ::= USING INDEX name(D). {
    ReplicaIdentityStmt *n = makeNode(ReplicaIdentityStmt);

    					n->identity_type = REPLICA_IDENTITY_INDEX;
    					n->name = D;
    					A = (Node *) n;
}

/* ----- reloptions ----- */

alter_identity_column_option_list(A) ::= alter_identity_column_option(B). {
    A = list_make1(B);
}

alter_identity_column_option_list(A) ::= alter_identity_column_option_list(B) alter_identity_column_option(C). {
    A = lappend(B, C);
}

/* ----- alter_identity_column_option ----- */

alter_identity_column_option(A) ::= RESTART. {
    A = makeDefElem("restart", NULL, LOC(B));
}

alter_identity_column_option(A) ::= RESTART opt_with(C) numericOnly(D). {
    A = makeDefElem("restart", (Node *) D, LOC(B));
}

alter_identity_column_option(A) ::= SET seqOptElem(C). {
    if (strcmp(C->defname, "as") == 0 ||
    						strcmp(C->defname, "restart") == 0 ||
    						strcmp(C->defname, "owned_by") == 0)
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("sequence option \"%s\" not supported here", C->defname),
    								 parser_errposition(LOC(C))));
    					A = C;
}

alter_identity_column_option(A) ::= SET GENERATED generated_when(D). {
    A = makeDefElem("generated", (Node *) makeInteger(D), LOC(B));
}

/* ----- set_statistics_value ----- */

set_statistics_value(A) ::= signedIconst(B). {
    A = (Node *) makeInteger(B);
}

set_statistics_value(A) ::= DEFAULT. {
    A = NULL;
}

/* ----- set_access_method_name ----- */

set_access_method_name(A) ::= colId(B). {
    A = B;
}

set_access_method_name(A) ::= DEFAULT. {
    A = NULL;
}

/* ----- partitionBoundSpec ----- */

alterCompositeTypeStmt(A) ::= ALTER TYPE_P any_name(D) alter_type_cmds(E). {
    alterTableStmt *n = makeNode(alterTableStmt);


    					n->relation = makeRangeVarFromAnyName(D, LOC(D), yyscanner);
    					n->cmds = E;
    					n->objtype = OBJECT_TYPE;
    					A = (Node *) n;
}

/* ----- alter_type_cmds ----- */

alter_type_cmds(A) ::= alter_type_cmd(B). {
    A = list_make1(B);
}

alter_type_cmds(A) ::= alter_type_cmds(B) COMMA alter_type_cmd(D). {
    A = lappend(B, D);
}

/* ----- alter_type_cmd ----- */

alter_type_cmd(A) ::= ADD_P ATTRIBUTE tableFuncElement(D) opt_drop_behavior(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_AddColumn;
    					n->def = D;
    					n->behavior = E;
    					A = (Node *) n;
}

alter_type_cmd(A) ::= DROP ATTRIBUTE IF_P EXISTS colId(F) opt_drop_behavior(G). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropColumn;
    					n->name = F;
    					n->behavior = G;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alter_type_cmd(A) ::= DROP ATTRIBUTE colId(D) opt_drop_behavior(E). {
    AlterTableCmd *n = makeNode(AlterTableCmd);

    					n->subtype = AT_DropColumn;
    					n->name = D;
    					n->behavior = E;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alter_type_cmd(A) ::= ALTER ATTRIBUTE colId(D) opt_set_data(E) TYPE_P typename(G) opt_collate_clause(H) opt_drop_behavior(I). {
    AlterTableCmd *n = makeNode(AlterTableCmd);
    					ColumnDef *def = makeNode(ColumnDef);

    					n->subtype = AT_AlterColumnType;
    					n->name = D;
    					n->def = (Node *) def;
    					n->behavior = I;

    					def->typeName = G;
    					def->collClause = (CollateClause *) H;
    					def->raw_default = NULL;
    					def->location = LOC(D);
    					A = (Node *) n;
}

/* ----- closePortalStmt ----- */

alterTblSpcStmt(A) ::= ALTER TABLESPACE name(D) SET reloptions(F). {
    AlterTableSpaceOptionsStmt *n =
    						makeNode(AlterTableSpaceOptionsStmt);

    					n->tablespacename = D;
    					n->options = F;
    					n->isReset = false;
    					A = (Node *) n;
}

alterTblSpcStmt(A) ::= ALTER TABLESPACE name(D) RESET reloptions(F). {
    AlterTableSpaceOptionsStmt *n =
    						makeNode(AlterTableSpaceOptionsStmt);

    					n->tablespacename = D;
    					n->options = F;
    					n->isReset = true;
    					A = (Node *) n;
}

/* ----- renameStmt ----- */
```

