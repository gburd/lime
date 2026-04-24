# Security


GRANT, REVOKE, CREATE/ALTER/DROP ROLE, row-level security
policies, default privileges, and role specifications.


```yaml
name: pg-security
version: 17.0.0
description: GRANT, REVOKE, roles, row-level security policies
provides: [pg-security]
depends: [pg-type-decls, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
createRoleStmt(A) ::= CREATE ROLE roleId(D) opt_with(E) optRoleList(F). {
    createRoleStmt *n = makeNode(createRoleStmt);

    					n->stmt_type = ROLESTMT_ROLE;
    					n->role = D;
    					n->options = F;
    					A = (Node *) n;
}

/* ----- opt_with ----- */

opt_with(A) ::= WITH.

opt_with(A) ::= WITH_LA.

/* ----- optRoleList ----- */

optRoleList(A) ::= optRoleList(B) createOptRoleElem(C). {
    A = lappend(B, C);
}

optRoleList(A) ::= . {
    A = NIL;
}

/* ----- alterOptRoleList ----- */

alterOptRoleList(A) ::= alterOptRoleList(B) alterOptRoleElem(C). {
    A = lappend(B, C);
}

alterOptRoleList(A) ::= . {
    A = NIL;
}

/* ----- alterOptRoleElem ----- */

alterOptRoleElem(A) ::= PASSWORD sconst(C). {
    A = makeDefElem("password",
    									 (Node *) makeString(C), LOC(B));
}

alterOptRoleElem(A) ::= PASSWORD NULL_P. {
    A = makeDefElem("password", NULL, LOC(B));
}

alterOptRoleElem(A) ::= ENCRYPTED PASSWORD sconst(D). {
    A = makeDefElem("password",
    									 (Node *) makeString(D), LOC(B));
}

alterOptRoleElem(A) ::= UNENCRYPTED PASSWORD sconst(D). {
    ereport(ERROR,
    							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    							 errmsg("UNENCRYPTED PASSWORD is no longer supported"),
    							 errhint("Remove UNENCRYPTED to store the password in encrypted form instead."),
    							 parser_errposition(LOC(B))));
}

alterOptRoleElem(A) ::= INHERIT. {
    A = makeDefElem("inherit", (Node *) makeBoolean(true), LOC(B));
}

alterOptRoleElem(A) ::= CONNECTION LIMIT signedIconst(D). {
    A = makeDefElem("connectionlimit", (Node *) makeInteger(D), LOC(B));
}

alterOptRoleElem(A) ::= VALID UNTIL sconst(D). {
    A = makeDefElem("validUntil", (Node *) makeString(D), LOC(B));
}

alterOptRoleElem(A) ::= USER role_list(C). {
    A = makeDefElem("rolemembers", (Node *) C, LOC(B));
}

alterOptRoleElem(A) ::= IDENT. {
    if (strcmp(B, "superuser") == 0)
    						A = makeDefElem("superuser", (Node *) makeBoolean(true), LOC(B));
    					else if (strcmp(B, "nosuperuser") == 0)
    						A = makeDefElem("superuser", (Node *) makeBoolean(false), LOC(B));
    					else if (strcmp(B, "createrole") == 0)
    						A = makeDefElem("createrole", (Node *) makeBoolean(true), LOC(B));
    					else if (strcmp(B, "nocreaterole") == 0)
    						A = makeDefElem("createrole", (Node *) makeBoolean(false), LOC(B));
    					else if (strcmp(B, "replication") == 0)
    						A = makeDefElem("isreplication", (Node *) makeBoolean(true), LOC(B));
    					else if (strcmp(B, "noreplication") == 0)
    						A = makeDefElem("isreplication", (Node *) makeBoolean(false), LOC(B));
    					else if (strcmp(B, "createdb") == 0)
    						A = makeDefElem("createdb", (Node *) makeBoolean(true), LOC(B));
    					else if (strcmp(B, "nocreatedb") == 0)
    						A = makeDefElem("createdb", (Node *) makeBoolean(false), LOC(B));
    					else if (strcmp(B, "login") == 0)
    						A = makeDefElem("canlogin", (Node *) makeBoolean(true), LOC(B));
    					else if (strcmp(B, "nologin") == 0)
    						A = makeDefElem("canlogin", (Node *) makeBoolean(false), LOC(B));
    					else if (strcmp(B, "bypassrls") == 0)
    						A = makeDefElem("bypassrls", (Node *) makeBoolean(true), LOC(B));
    					else if (strcmp(B, "nobypassrls") == 0)
    						A = makeDefElem("bypassrls", (Node *) makeBoolean(false), LOC(B));
    					else if (strcmp(B, "noinherit") == 0)
    					{




    						A = makeDefElem("inherit", (Node *) makeBoolean(false), LOC(B));
    					}
    					else
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("unrecognized role option \"%s\"", B),
    									 parser_errposition(LOC(B))));
}

/* ----- createOptRoleElem ----- */

createOptRoleElem(A) ::= alterOptRoleElem(B). {
    A = B;
}

createOptRoleElem(A) ::= SYSID iconst(C). {
    A = makeDefElem("sysid", (Node *) makeInteger(C), LOC(B));
}

createOptRoleElem(A) ::= ADMIN role_list(C). {
    A = makeDefElem("adminmembers", (Node *) C, LOC(B));
}

createOptRoleElem(A) ::= ROLE role_list(C). {
    A = makeDefElem("rolemembers", (Node *) C, LOC(B));
}

createOptRoleElem(A) ::= IN_P ROLE role_list(D). {
    A = makeDefElem("addroleto", (Node *) D, LOC(B));
}

createOptRoleElem(A) ::= IN_P GROUP_P role_list(D). {
    A = makeDefElem("addroleto", (Node *) D, LOC(B));
}

/* ----- createUserStmt ----- */

createUserStmt(A) ::= CREATE USER roleId(D) opt_with(E) optRoleList(F). {
    createRoleStmt *n = makeNode(createRoleStmt);

    					n->stmt_type = ROLESTMT_USER;
    					n->role = D;
    					n->options = F;
    					A = (Node *) n;
}

/* ----- alterRoleStmt ----- */

alterRoleStmt(A) ::= ALTER ROLE roleSpec(D) opt_with(E) alterOptRoleList(F). {
    alterRoleStmt *n = makeNode(alterRoleStmt);

    					n->role = D;
    					n->action = +1;	
    					n->options = F;
    					A = (Node *) n;
}

alterRoleStmt(A) ::= ALTER USER roleSpec(D) opt_with(E) alterOptRoleList(F). {
    alterRoleStmt *n = makeNode(alterRoleStmt);

    					n->role = D;
    					n->action = +1;	
    					n->options = F;
    					A = (Node *) n;
}

/* ----- opt_in_database ----- */

opt_in_database(A) ::= . {
    A = NULL;
}

opt_in_database(A) ::= IN_P DATABASE name(D). {
    A = D;
}

/* ----- alterRoleSetStmt ----- */

alterRoleSetStmt(A) ::= ALTER ROLE roleSpec(D) opt_in_database(E) setResetClause(F). {
    alterRoleSetStmt *n = makeNode(alterRoleSetStmt);

    					n->role = D;
    					n->database = E;
    					n->setstmt = F;
    					A = (Node *) n;
}

alterRoleSetStmt(A) ::= ALTER ROLE ALL opt_in_database(E) setResetClause(F). {
    alterRoleSetStmt *n = makeNode(alterRoleSetStmt);

    					n->role = NULL;
    					n->database = E;
    					n->setstmt = F;
    					A = (Node *) n;
}

alterRoleSetStmt(A) ::= ALTER USER roleSpec(D) opt_in_database(E) setResetClause(F). {
    alterRoleSetStmt *n = makeNode(alterRoleSetStmt);

    					n->role = D;
    					n->database = E;
    					n->setstmt = F;
    					A = (Node *) n;
}

alterRoleSetStmt(A) ::= ALTER USER ALL opt_in_database(E) setResetClause(F). {
    alterRoleSetStmt *n = makeNode(alterRoleSetStmt);

    					n->role = NULL;
    					n->database = E;
    					n->setstmt = F;
    					A = (Node *) n;
}

/* ----- dropRoleStmt ----- */

createGroupStmt(A) ::= CREATE GROUP_P roleId(D) opt_with(E) optRoleList(F). {
    createRoleStmt *n = makeNode(createRoleStmt);

    					n->stmt_type = ROLESTMT_GROUP;
    					n->role = D;
    					n->options = F;
    					A = (Node *) n;
}

/* ----- alterGroupStmt ----- */

alterGroupStmt(A) ::= ALTER GROUP_P roleSpec(D) add_drop(E) USER role_list(G). {
    alterRoleStmt *n = makeNode(alterRoleStmt);

    					n->role = D;
    					n->action = E;
    					n->options = list_make1(makeDefElem("rolemembers",
    														(Node *) G, LOC(G)));
    					A = (Node *) n;
}

/* ----- add_drop ----- */

add_drop(A) ::= ADD_P. {
    A = +1;
}

add_drop(A) ::= DROP. {
    A = -1;
}

/* ----- createSchemaStmt ----- */

createPolicyStmt(A) ::= CREATE POLICY name(D) ON qualified_name(F) rowSecurityDefaultPermissive(G) rowSecurityDefaultForCmd(H) rowSecurityDefaultToRole(I) rowSecurityOptionalExpr(J) rowSecurityOptionalWithCheck(K). {
    createPolicyStmt *n = makeNode(createPolicyStmt);

    					n->policy_name = D;
    					n->table = F;
    					n->permissive = G;
    					n->cmd_name = H;
    					n->roles = I;
    					n->qual = J;
    					n->with_check = K;
    					A = (Node *) n;
}

/* ----- alterPolicyStmt ----- */

alterPolicyStmt(A) ::= ALTER POLICY name(D) ON qualified_name(F) rowSecurityOptionalToRole(G) rowSecurityOptionalExpr(H) rowSecurityOptionalWithCheck(I). {
    alterPolicyStmt *n = makeNode(alterPolicyStmt);

    					n->policy_name = D;
    					n->table = F;
    					n->roles = G;
    					n->qual = H;
    					n->with_check = I;
    					A = (Node *) n;
}

/* ----- rowSecurityOptionalExpr ----- */

rowSecurityOptionalExpr(A) ::= USING LPAREN a_expr(D) RPAREN. {
    A = D;
}

rowSecurityOptionalExpr(A) ::= . {
    A = NULL;
}

/* ----- rowSecurityOptionalWithCheck ----- */

rowSecurityOptionalWithCheck(A) ::= WITH CHECK LPAREN a_expr(E) RPAREN. {
    A = E;
}

rowSecurityOptionalWithCheck(A) ::= . {
    A = NULL;
}

/* ----- rowSecurityDefaultToRole ----- */

rowSecurityDefaultToRole(A) ::= TO role_list(C). {
    A = C;
}

rowSecurityDefaultToRole(A) ::= . {
    A = list_make1(makeRoleSpec(ROLESPEC_PUBLIC, -1));
}

/* ----- rowSecurityOptionalToRole ----- */

rowSecurityOptionalToRole(A) ::= TO role_list(C). {
    A = C;
}

rowSecurityOptionalToRole(A) ::= . {
    A = NULL;
}

/* ----- rowSecurityDefaultPermissive ----- */

rowSecurityDefaultPermissive(A) ::= AS IDENT. {
    if (strcmp(C, "permissive") == 0)
    						A = true;
    					else if (strcmp(C, "restrictive") == 0)
    						A = false;
    					else
    						ereport(ERROR,
    								(errcode(ERRCODE_SYNTAX_ERROR),
    								 errmsg("unrecognized row security option \"%s\"", C),
    								 errhint("Only PERMISSIVE or RESTRICTIVE policies are supported currently."),
    								 parser_errposition(LOC(C))));
}

rowSecurityDefaultPermissive(A) ::= . {
    A = true;
}

/* ----- rowSecurityDefaultForCmd ----- */

rowSecurityDefaultForCmd(A) ::= FOR row_security_cmd(C). {
    A = C;
}

rowSecurityDefaultForCmd(A) ::= . {
    A = "all";
}

/* ----- row_security_cmd ----- */

row_security_cmd(A) ::= ALL. {
    A = "all";
}

row_security_cmd(A) ::= SELECT. {
    A = "select";
}

row_security_cmd(A) ::= INSERT. {
    A = "insert";
}

row_security_cmd(A) ::= UPDATE. {
    A = "update";
}

row_security_cmd(A) ::= DELETE_P. {
    A = "delete";
}

/* ----- createAmStmt ----- */

reassignOwnedStmt(A) ::= REASSIGN OWNED BY role_list(E) TO roleSpec(G). {
    reassignOwnedStmt *n = makeNode(reassignOwnedStmt);

    					n->roles = E;
    					n->newrole = G;
    					A = (Node *) n;
}

/* ----- dropStmt ----- */

grantStmt(A) ::= GRANT privileges(C) ON privilege_target(E) TO grantee_list(G) opt_grant_grant_option(H) opt_granted_by(I). {
    grantStmt *n = makeNode(grantStmt);

    					n->is_grant = true;
    					n->privileges = C;
    					n->targtype = (E)->targtype;
    					n->objtype = (E)->objtype;
    					n->objects = (E)->objs;
    					n->grantees = G;
    					n->grant_option = H;
    					n->grantor = I;
    					A = (Node *) n;
}

/* ----- revokeStmt ----- */

revokeStmt(A) ::= REVOKE privileges(C) ON privilege_target(E) FROM grantee_list(G) opt_granted_by(H) opt_drop_behavior(I). {
    grantStmt *n = makeNode(grantStmt);

    					n->is_grant = false;
    					n->grant_option = false;
    					n->privileges = C;
    					n->targtype = (E)->targtype;
    					n->objtype = (E)->objtype;
    					n->objects = (E)->objs;
    					n->grantees = G;
    					n->grantor = H;
    					n->behavior = I;
    					A = (Node *) n;
}

revokeStmt(A) ::= REVOKE GRANT OPTION FOR privileges(F) ON privilege_target(H) FROM grantee_list(J) opt_granted_by(K) opt_drop_behavior(L). {
    grantStmt *n = makeNode(grantStmt);

    					n->is_grant = false;
    					n->grant_option = true;
    					n->privileges = F;
    					n->targtype = (H)->targtype;
    					n->objtype = (H)->objtype;
    					n->objects = (H)->objs;
    					n->grantees = J;
    					n->grantor = K;
    					n->behavior = L;
    					A = (Node *) n;
}

/* ----- privileges ----- */

privileges(A) ::= privilege_list(B). {
    A = B;
}

privileges(A) ::= ALL. {
    A = NIL;
}

privileges(A) ::= ALL PRIVILEGES. {
    A = NIL;
}

privileges(A) ::= ALL LPAREN columnList(D) RPAREN. {
    AccessPriv *n = makeNode(AccessPriv);

    					n->priv_name = NULL;
    					n->cols = D;
    					A = list_make1(n);
}

privileges(A) ::= ALL PRIVILEGES LPAREN columnList(E) RPAREN. {
    AccessPriv *n = makeNode(AccessPriv);

    					n->priv_name = NULL;
    					n->cols = E;
    					A = list_make1(n);
}

/* ----- privilege_list ----- */

privilege_list(A) ::= privilege(B). {
    A = list_make1(B);
}

privilege_list(A) ::= privilege_list(B) COMMA privilege(D). {
    A = lappend(B, D);
}

/* ----- privilege ----- */

privilege(A) ::= SELECT opt_column_list(C). {
    AccessPriv *n = makeNode(AccessPriv);

    				n->priv_name = pstrdup(B);
    				n->cols = C;
    				A = n;
}

privilege(A) ::= REFERENCES opt_column_list(C). {
    AccessPriv *n = makeNode(AccessPriv);

    				n->priv_name = pstrdup(B);
    				n->cols = C;
    				A = n;
}

privilege(A) ::= CREATE opt_column_list(C). {
    AccessPriv *n = makeNode(AccessPriv);

    				n->priv_name = pstrdup(B);
    				n->cols = C;
    				A = n;
}

privilege(A) ::= ALTER SYSTEM_P. {
    AccessPriv *n = makeNode(AccessPriv);
    				n->priv_name = pstrdup("alter system");
    				n->cols = NIL;
    				A = n;
}

privilege(A) ::= colId(B) opt_column_list(C). {
    AccessPriv *n = makeNode(AccessPriv);

    				n->priv_name = B;
    				n->cols = C;
    				A = n;
}

/* ----- parameter_name_list ----- */

privilege_target(A) ::= qualified_name_list(B). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_TABLE;
    					n->objs = B;
    					A = n;
}

privilege_target(A) ::= TABLE qualified_name_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_TABLE;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= SEQUENCE qualified_name_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_SEQUENCE;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= FOREIGN DATA_P WRAPPER name_list(E). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_FDW;
    					n->objs = E;
    					A = n;
}

privilege_target(A) ::= FOREIGN SERVER name_list(D). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_FOREIGN_SERVER;
    					n->objs = D;
    					A = n;
}

privilege_target(A) ::= FUNCTION function_with_argtypes_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_FUNCTION;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= PROCEDURE function_with_argtypes_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_PROCEDURE;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= ROUTINE function_with_argtypes_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_ROUTINE;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= DATABASE name_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_DATABASE;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= DOMAIN_P any_name_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_DOMAIN;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= LANGUAGE name_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_LANGUAGE;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= LARGE_P OBJECT_P numericOnly_list(D). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_LARGEOBJECT;
    					n->objs = D;
    					A = n;
}

privilege_target(A) ::= PARAMETER parameter_name_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);
    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_PARAMETER_ACL;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= PROPERTY GRAPH qualified_name_list(D). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_PROPGRAPH;
    					n->objs = D;
    					A = n;
}

privilege_target(A) ::= SCHEMA name_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_SCHEMA;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= TABLESPACE name_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_TABLESPACE;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= TYPE_P any_name_list(C). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_OBJECT;
    					n->objtype = OBJECT_TYPE;
    					n->objs = C;
    					A = n;
}

privilege_target(A) ::= ALL TABLES IN_P SCHEMA name_list(F). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_ALL_IN_SCHEMA;
    					n->objtype = OBJECT_TABLE;
    					n->objs = F;
    					A = n;
}

privilege_target(A) ::= ALL SEQUENCES IN_P SCHEMA name_list(F). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_ALL_IN_SCHEMA;
    					n->objtype = OBJECT_SEQUENCE;
    					n->objs = F;
    					A = n;
}

privilege_target(A) ::= ALL FUNCTIONS IN_P SCHEMA name_list(F). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_ALL_IN_SCHEMA;
    					n->objtype = OBJECT_FUNCTION;
    					n->objs = F;
    					A = n;
}

privilege_target(A) ::= ALL PROCEDURES IN_P SCHEMA name_list(F). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_ALL_IN_SCHEMA;
    					n->objtype = OBJECT_PROCEDURE;
    					n->objs = F;
    					A = n;
}

privilege_target(A) ::= ALL ROUTINES IN_P SCHEMA name_list(F). {
    PrivTarget *n = palloc_object(PrivTarget);

    					n->targtype = ACL_TARGET_ALL_IN_SCHEMA;
    					n->objtype = OBJECT_ROUTINE;
    					n->objs = F;
    					A = n;
}

/* ----- grantee_list ----- */

grantee_list(A) ::= grantee(B). {
    A = list_make1(B);
}

grantee_list(A) ::= grantee_list(B) COMMA grantee(D). {
    A = lappend(B, D);
}

/* ----- grantee ----- */

grantee(A) ::= roleSpec(B). {
    A = B;
}

grantee(A) ::= GROUP_P roleSpec(C). {
    A = C;
}

/* ----- opt_grant_grant_option ----- */

opt_grant_grant_option(A) ::= WITH GRANT OPTION. {
    A = true;
}

opt_grant_grant_option(A) ::= . {
    A = false;
}

/* ----- grantRoleStmt ----- */

grantRoleStmt(A) ::= GRANT privilege_list(C) TO role_list(E) opt_granted_by(F). {
    grantRoleStmt *n = makeNode(grantRoleStmt);

    					n->is_grant = true;
    					n->granted_roles = C;
    					n->grantee_roles = E;
    					n->opt = NIL;
    					n->grantor = F;
    					A = (Node *) n;
}

grantRoleStmt(A) ::= GRANT privilege_list(C) TO role_list(E) WITH grant_role_opt_list(G) opt_granted_by(H). {
    grantRoleStmt *n = makeNode(grantRoleStmt);

    					n->is_grant = true;
    					n->granted_roles = C;
    					n->grantee_roles = E;
    					n->opt = G;
    					n->grantor = H;
    					A = (Node *) n;
}

/* ----- revokeRoleStmt ----- */

revokeRoleStmt(A) ::= REVOKE privilege_list(C) FROM role_list(E) opt_granted_by(F) opt_drop_behavior(G). {
    grantRoleStmt *n = makeNode(grantRoleStmt);

    					n->is_grant = false;
    					n->opt = NIL;
    					n->granted_roles = C;
    					n->grantee_roles = E;
    					n->grantor = F;
    					n->behavior = G;
    					A = (Node *) n;
}

revokeRoleStmt(A) ::= REVOKE colId(C) OPTION FOR privilege_list(F) FROM role_list(H) opt_granted_by(I) opt_drop_behavior(J). {
    grantRoleStmt *n = makeNode(grantRoleStmt);
    					DefElem *opt;

    					opt = makeDefElem(pstrdup(C),
    									  (Node *) makeBoolean(false), LOC(C));
    					n->is_grant = false;
    					n->opt = list_make1(opt);
    					n->granted_roles = F;
    					n->grantee_roles = H;
    					n->grantor = I;
    					n->behavior = J;
    					A = (Node *) n;
}

/* ----- grant_role_opt_list ----- */

grant_role_opt_list(A) ::= grant_role_opt_list(B) COMMA grant_role_opt(D). {
    A = lappend(B, D);
}

grant_role_opt_list(A) ::= grant_role_opt(B). {
    A = list_make1(B);
}

/* ----- grant_role_opt ----- */

grant_role_opt(A) ::= colLabel(B) grant_role_opt_value(C). {
    A = makeDefElem(pstrdup(B), C, LOC(B));
}

/* ----- grant_role_opt_value ----- */

grant_role_opt_value(A) ::= OPTION. {
    A = (Node *) makeBoolean(true);
}

grant_role_opt_value(A) ::= TRUE_P. {
    A = (Node *) makeBoolean(true);
}

grant_role_opt_value(A) ::= FALSE_P. {
    A = (Node *) makeBoolean(false);
}

/* ----- opt_granted_by ----- */

opt_granted_by(A) ::= GRANTED BY roleSpec(D). {
    A = D;
}

opt_granted_by(A) ::= . {
    A = NULL;
}

/* ----- alterDefaultPrivilegesStmt ----- */

alterDefaultPrivilegesStmt(A) ::= ALTER DEFAULT PRIVILEGES defACLOptionList(E) defACLAction(F). {
    alterDefaultPrivilegesStmt *n = makeNode(alterDefaultPrivilegesStmt);

    					n->options = E;
    					n->action = (grantStmt *) F;
    					A = (Node *) n;
}

/* ----- defACLOptionList ----- */

defACLOptionList(A) ::= defACLOptionList(B) defACLOption(C). {
    A = lappend(B, C);
}

defACLOptionList(A) ::= . {
    A = NIL;
}

/* ----- defACLOption ----- */

defACLOption(A) ::= IN_P SCHEMA name_list(D). {
    A = makeDefElem("schemas", (Node *) D, LOC(B));
}

defACLOption(A) ::= FOR ROLE role_list(D). {
    A = makeDefElem("roles", (Node *) D, LOC(B));
}

defACLOption(A) ::= FOR USER role_list(D). {
    A = makeDefElem("roles", (Node *) D, LOC(B));
}

/* ----- defACLAction ----- */

defACLAction(A) ::= GRANT privileges(C) ON defacl_privilege_target(E) TO grantee_list(G) opt_grant_grant_option(H). {
    grantStmt *n = makeNode(grantStmt);

    					n->is_grant = true;
    					n->privileges = C;
    					n->targtype = ACL_TARGET_DEFAULTS;
    					n->objtype = E;
    					n->objects = NIL;
    					n->grantees = G;
    					n->grant_option = H;
    					A = (Node *) n;
}

defACLAction(A) ::= REVOKE privileges(C) ON defacl_privilege_target(E) FROM grantee_list(G) opt_drop_behavior(H). {
    grantStmt *n = makeNode(grantStmt);

    					n->is_grant = false;
    					n->grant_option = false;
    					n->privileges = C;
    					n->targtype = ACL_TARGET_DEFAULTS;
    					n->objtype = E;
    					n->objects = NIL;
    					n->grantees = G;
    					n->behavior = H;
    					A = (Node *) n;
}

defACLAction(A) ::= REVOKE GRANT OPTION FOR privileges(F) ON defacl_privilege_target(H) FROM grantee_list(J) opt_drop_behavior(K). {
    grantStmt *n = makeNode(grantStmt);

    					n->is_grant = false;
    					n->grant_option = true;
    					n->privileges = F;
    					n->targtype = ACL_TARGET_DEFAULTS;
    					n->objtype = H;
    					n->objects = NIL;
    					n->grantees = J;
    					n->behavior = K;
    					A = (Node *) n;
}

/* ----- defacl_privilege_target ----- */

defacl_privilege_target(A) ::= TABLES. {
    A = OBJECT_TABLE;
}

defacl_privilege_target(A) ::= FUNCTIONS. {
    A = OBJECT_FUNCTION;
}

defacl_privilege_target(A) ::= ROUTINES. {
    A = OBJECT_FUNCTION;
}

defacl_privilege_target(A) ::= SEQUENCES. {
    A = OBJECT_SEQUENCE;
}

defacl_privilege_target(A) ::= TYPES_P. {
    A = OBJECT_TYPE;
}

defacl_privilege_target(A) ::= SCHEMAS. {
    A = OBJECT_SCHEMA;
}

defacl_privilege_target(A) ::= LARGE_P OBJECTS_P. {
    A = OBJECT_LARGEOBJECT;
}

/* ----- indexStmt ----- */

roleId(A) ::= roleSpec(B). {
    roleSpec   *spc = (roleSpec *) B;

    					switch (spc->roletype)
    					{
    						case ROLESPEC_CSTRING:
    							A = spc->rolename;
    							break;
    						case ROLESPEC_PUBLIC:
    							ereport(ERROR,
    									(errcode(ERRCODE_RESERVED_NAME),
    									 errmsg("role name \"%s\" is reserved",
    											"public"),
    									 parser_errposition(LOC(B))));
    							break;
    						case ROLESPEC_SESSION_USER:
    							ereport(ERROR,
    									(errcode(ERRCODE_RESERVED_NAME),
    									 errmsg("%s cannot be used as a role name here",
    											"SESSION_USER"),
    									 parser_errposition(LOC(B))));
    							break;
    						case ROLESPEC_CURRENT_USER:
    							ereport(ERROR,
    									(errcode(ERRCODE_RESERVED_NAME),
    									 errmsg("%s cannot be used as a role name here",
    											"CURRENT_USER"),
    									 parser_errposition(LOC(B))));
    							break;
    						case ROLESPEC_CURRENT_ROLE:
    							ereport(ERROR,
    									(errcode(ERRCODE_RESERVED_NAME),
    									 errmsg("%s cannot be used as a role name here",
    											"CURRENT_ROLE"),
    									 parser_errposition(LOC(B))));
    							break;
    					}
}

/* ----- roleSpec ----- */

roleSpec(A) ::= nonReservedWord(B). {
    roleSpec   *n;

    					if (strcmp(B, "public") == 0)
    					{
    						n = (roleSpec *) makeRoleSpec(ROLESPEC_PUBLIC, LOC(B));
    						n->roletype = ROLESPEC_PUBLIC;
    					}
    					else if (strcmp(B, "none") == 0)
    					{
    						ereport(ERROR,
    								(errcode(ERRCODE_RESERVED_NAME),
    								 errmsg("role name \"%s\" is reserved",
    										"none"),
    								 parser_errposition(LOC(B))));
    					}
    					else
    					{
    						n = makeRoleSpec(ROLESPEC_CSTRING, LOC(B));
    						n->rolename = pstrdup(B);
    					}
    					A = n;
}

roleSpec(A) ::= CURRENT_ROLE. {
    A = makeRoleSpec(ROLESPEC_CURRENT_ROLE, LOC(B));
}

roleSpec(A) ::= CURRENT_USER. {
    A = makeRoleSpec(ROLESPEC_CURRENT_USER, LOC(B));
}

roleSpec(A) ::= SESSION_USER. {
    A = makeRoleSpec(ROLESPEC_SESSION_USER, LOC(B));
}

/* ----- role_list ----- */

role_list(A) ::= roleSpec(B). {
    A = list_make1(B);
}

role_list(A) ::= role_list(B) COMMA roleSpec(D). {
    A = lappend(B, D);
}

/* ----- pLpgSQL_Expr ----- */
```

