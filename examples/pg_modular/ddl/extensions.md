# Extensions and Foreign Data


CREATE/ALTER EXTENSION, Foreign Data Wrappers, Foreign
Servers, User Mappings, IMPORT FOREIGN SCHEMA, and
tablespace management.


```yaml
name: pg-ddl-extensions
version: 17.0.0
description: Extensions, FDW, foreign servers, user mappings, tablespaces
provides: [pg-ddl-extensions]
depends: [pg-type-decls, pg-base-helpers]
```

## Production Rules

```lime rules
createTableSpaceStmt(A) ::= CREATE TABLESPACE name(D) optTableSpaceOwner(E) LOCATION sconst(G) opt_reloptions(H). {
    createTableSpaceStmt *n = makeNode(createTableSpaceStmt);

    					n->tablespacename = D;
    					n->owner = E;
    					n->location = G;
    					n->options = H;
    					A = (Node *) n;
}

/* ----- optTableSpaceOwner ----- */

optTableSpaceOwner(A) ::= OWNER roleSpec(C). {
    A = C;
}

optTableSpaceOwner(A) ::= . {
    A = NULL;
}

/* ----- dropTableSpaceStmt ----- */

createExtensionStmt(A) ::= CREATE EXTENSION name(D) opt_with(E) create_extension_opt_list(F). {
    createExtensionStmt *n = makeNode(createExtensionStmt);

    					n->extname = D;
    					n->if_not_exists = false;
    					n->options = F;
    					A = (Node *) n;
}

createExtensionStmt(A) ::= CREATE EXTENSION IF_P NOT EXISTS name(G) opt_with(H) create_extension_opt_list(I). {
    createExtensionStmt *n = makeNode(createExtensionStmt);

    					n->extname = G;
    					n->if_not_exists = true;
    					n->options = I;
    					A = (Node *) n;
}

/* ----- create_extension_opt_list ----- */

create_extension_opt_list(A) ::= create_extension_opt_list(B) create_extension_opt_item(C). {
    A = lappend(B, C);
}

create_extension_opt_list(A) ::= . {
    A = NIL;
}

/* ----- create_extension_opt_item ----- */

create_extension_opt_item(A) ::= SCHEMA name(C). {
    A = makeDefElem("schema", (Node *) makeString(C), LOC(B));
}

create_extension_opt_item(A) ::= VERSION_P nonReservedWord_or_Sconst(C). {
    A = makeDefElem("new_version", (Node *) makeString(C), LOC(B));
}

create_extension_opt_item(A) ::= FROM nonReservedWord_or_Sconst(C). {
    ereport(ERROR,
    							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    							 errmsg("CREATE EXTENSION ... FROM is no longer supported"),
    							 parser_errposition(LOC(B))));
}

create_extension_opt_item(A) ::= CASCADE. {
    A = makeDefElem("cascade", (Node *) makeBoolean(true), LOC(B));
}

/* ----- alterExtensionStmt ----- */

alterExtensionStmt(A) ::= ALTER EXTENSION name(D) UPDATE alter_extension_opt_list(F). {
    alterExtensionStmt *n = makeNode(alterExtensionStmt);

    					n->extname = D;
    					n->options = F;
    					A = (Node *) n;
}

/* ----- alter_extension_opt_list ----- */

alter_extension_opt_list(A) ::= alter_extension_opt_list(B) alter_extension_opt_item(C). {
    A = lappend(B, C);
}

alter_extension_opt_list(A) ::= . {
    A = NIL;
}

/* ----- alter_extension_opt_item ----- */

alter_extension_opt_item(A) ::= TO nonReservedWord_or_Sconst(C). {
    A = makeDefElem("new_version", (Node *) makeString(C), LOC(B));
}

/* ----- alterExtensionContentsStmt ----- */

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) object_type_name(F) name(G). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = F;
    					n->object = (Node *) makeString(G);
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) object_type_any_name(F) any_name(G). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = F;
    					n->object = (Node *) G;
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) AGGREGATE aggregate_with_argtypes(G). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = OBJECT_AGGREGATE;
    					n->object = (Node *) G;
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) CAST LPAREN typename(H) AS typename(J) RPAREN. {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = OBJECT_CAST;
    					n->object = (Node *) list_make2(H, J);
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) DOMAIN_P typename(G). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = OBJECT_DOMAIN;
    					n->object = (Node *) G;
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) FUNCTION function_with_argtypes(G). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = OBJECT_FUNCTION;
    					n->object = (Node *) G;
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) OPERATOR operator_with_argtypes(G). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = OBJECT_OPERATOR;
    					n->object = (Node *) G;
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) OPERATOR CLASS any_name(H) USING name(J). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = OBJECT_OPCLASS;
    					n->object = (Node *) lcons(makeString(J), H);
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) OPERATOR FAMILY any_name(H) USING name(J). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = OBJECT_OPFAMILY;
    					n->object = (Node *) lcons(makeString(J), H);
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) PROCEDURE function_with_argtypes(G). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = OBJECT_PROCEDURE;
    					n->object = (Node *) G;
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) ROUTINE function_with_argtypes(G). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = OBJECT_ROUTINE;
    					n->object = (Node *) G;
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) TRANSFORM FOR typename(H) LANGUAGE name(J). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = OBJECT_TRANSFORM;
    					n->object = (Node *) list_make2(H, makeString(J));
    					A = (Node *) n;
}

alterExtensionContentsStmt(A) ::= ALTER EXTENSION name(D) add_drop(E) TYPE_P typename(G). {
    alterExtensionContentsStmt *n = makeNode(alterExtensionContentsStmt);

    					n->extname = D;
    					n->action = E;
    					n->objtype = OBJECT_TYPE;
    					n->object = (Node *) G;
    					A = (Node *) n;
}

/* ----- createFdwStmt ----- */

createFdwStmt(A) ::= CREATE FOREIGN DATA_P WRAPPER name(F) opt_fdw_options(G) create_generic_options(H). {
    createFdwStmt *n = makeNode(createFdwStmt);

    					n->fdwname = F;
    					n->func_options = G;
    					n->options = H;
    					A = (Node *) n;
}

/* ----- fdw_option ----- */

fdw_option(A) ::= HANDLER handler_name(C). {
    A = makeDefElem("handler", (Node *) C, LOC(B));
}

fdw_option(A) ::= NO HANDLER. {
    A = makeDefElem("handler", NULL, LOC(B));
}

fdw_option(A) ::= VALIDATOR handler_name(C). {
    A = makeDefElem("validator", (Node *) C, LOC(B));
}

fdw_option(A) ::= NO VALIDATOR. {
    A = makeDefElem("validator", NULL, LOC(B));
}

fdw_option(A) ::= CONNECTION handler_name(C). {
    A = makeDefElem("connection", (Node *) C, LOC(B));
}

fdw_option(A) ::= NO CONNECTION. {
    A = makeDefElem("connection", NULL, LOC(B));
}

/* ----- fdw_options ----- */

fdw_options(A) ::= fdw_option(B). {
    A = list_make1(B);
}

fdw_options(A) ::= fdw_options(B) fdw_option(C). {
    A = lappend(B, C);
}

/* ----- opt_fdw_options ----- */

opt_fdw_options(A) ::= fdw_options(B). {
    A = B;
}

opt_fdw_options(A) ::= . {
    A = NIL;
}

/* ----- alterFdwStmt ----- */

alterFdwStmt(A) ::= ALTER FOREIGN DATA_P WRAPPER name(F) opt_fdw_options(G) alter_generic_options(H). {
    alterFdwStmt *n = makeNode(alterFdwStmt);

    					n->fdwname = F;
    					n->func_options = G;
    					n->options = H;
    					A = (Node *) n;
}

alterFdwStmt(A) ::= ALTER FOREIGN DATA_P WRAPPER name(F) fdw_options(G). {
    alterFdwStmt *n = makeNode(alterFdwStmt);

    					n->fdwname = F;
    					n->func_options = G;
    					n->options = NIL;
    					A = (Node *) n;
}

/* ----- create_generic_options ----- */

create_generic_options(A) ::= OPTIONS LPAREN generic_option_list(D) RPAREN. {
    A = D;
}

create_generic_options(A) ::= . {
    A = NIL;
}

/* ----- generic_option_list ----- */

generic_option_list(A) ::= generic_option_elem(B). {
    A = list_make1(B);
}

generic_option_list(A) ::= generic_option_list(B) COMMA generic_option_elem(D). {
    A = lappend(B, D);
}

/* ----- alter_generic_options ----- */

alter_generic_options(A) ::= OPTIONS LPAREN alter_generic_option_list(D) RPAREN. {
    A = D;
}

/* ----- alter_generic_option_list ----- */

alter_generic_option_list(A) ::= alter_generic_option_elem(B). {
    A = list_make1(B);
}

alter_generic_option_list(A) ::= alter_generic_option_list(B) COMMA alter_generic_option_elem(D). {
    A = lappend(B, D);
}

/* ----- alter_generic_option_elem ----- */

alter_generic_option_elem(A) ::= generic_option_elem(B). {
    A = B;
}

alter_generic_option_elem(A) ::= SET generic_option_elem(C). {
    A = C;
    					A->defaction = DEFELEM_SET;
}

alter_generic_option_elem(A) ::= ADD_P generic_option_elem(C). {
    A = C;
    					A->defaction = DEFELEM_ADD;
}

alter_generic_option_elem(A) ::= DROP generic_option_name(C). {
    A = makeDefElemExtended(NULL, C, NULL, DEFELEM_DROP, LOC(C));
}

/* ----- generic_option_elem ----- */

generic_option_elem(A) ::= generic_option_name(B) generic_option_arg(C). {
    A = makeDefElem(B, C, LOC(B));
}

/* ----- generic_option_name ----- */

generic_option_name(A) ::= colLabel(B). {
    A = B;
}

/* ----- generic_option_arg ----- */

generic_option_arg(A) ::= sconst(B). {
    A = (Node *) makeString(B);
}

/* ----- createForeignServerStmt ----- */

createForeignServerStmt(A) ::= CREATE SERVER name(D) opt_type(E) opt_foreign_server_version(F) FOREIGN DATA_P WRAPPER name(J) create_generic_options(K). {
    createForeignServerStmt *n = makeNode(createForeignServerStmt);

    					n->servername = D;
    					n->servertype = E;
    					n->version = F;
    					n->fdwname = J;
    					n->options = K;
    					n->if_not_exists = false;
    					A = (Node *) n;
}

createForeignServerStmt(A) ::= CREATE SERVER IF_P NOT EXISTS name(G) opt_type(H) opt_foreign_server_version(I) FOREIGN DATA_P WRAPPER name(M) create_generic_options(N). {
    createForeignServerStmt *n = makeNode(createForeignServerStmt);

    					n->servername = G;
    					n->servertype = H;
    					n->version = I;
    					n->fdwname = M;
    					n->options = N;
    					n->if_not_exists = true;
    					A = (Node *) n;
}

/* ----- opt_type ----- */

opt_type(A) ::= TYPE_P sconst(C). {
    A = C;
}

opt_type(A) ::= . {
    A = NULL;
}

/* ----- foreign_server_version ----- */

foreign_server_version(A) ::= VERSION_P sconst(C). {
    A = C;
}

foreign_server_version(A) ::= VERSION_P NULL_P. {
    A = NULL;
}

/* ----- opt_foreign_server_version ----- */

opt_foreign_server_version(A) ::= foreign_server_version(B). {
    A = B;
}

opt_foreign_server_version(A) ::= . {
    A = NULL;
}

/* ----- alterForeignServerStmt ----- */

alterForeignServerStmt(A) ::= ALTER SERVER name(D) foreign_server_version(E) alter_generic_options(F). {
    alterForeignServerStmt *n = makeNode(alterForeignServerStmt);

    					n->servername = D;
    					n->version = E;
    					n->options = F;
    					n->has_version = true;
    					A = (Node *) n;
}

alterForeignServerStmt(A) ::= ALTER SERVER name(D) foreign_server_version(E). {
    alterForeignServerStmt *n = makeNode(alterForeignServerStmt);

    					n->servername = D;
    					n->version = E;
    					n->has_version = true;
    					A = (Node *) n;
}

alterForeignServerStmt(A) ::= ALTER SERVER name(D) alter_generic_options(E). {
    alterForeignServerStmt *n = makeNode(alterForeignServerStmt);

    					n->servername = D;
    					n->options = E;
    					A = (Node *) n;
}

/* ----- createForeignTableStmt ----- */

importForeignSchemaStmt(A) ::= IMPORT_P FOREIGN SCHEMA name(E) import_qualification(F) FROM SERVER name(I) INTO name(K) create_generic_options(L). {
    importForeignSchemaStmt *n = makeNode(importForeignSchemaStmt);

    				n->server_name = I;
    				n->remote_schema = E;
    				n->local_schema = K;
    				n->list_type = F->type;
    				n->table_list = F->table_names;
    				n->options = L;
    				A = (Node *) n;
}

/* ----- import_qualification_type ----- */

import_qualification_type(A) ::= LIMIT TO. {
    A = FDW_IMPORT_SCHEMA_LIMIT_TO;
}

import_qualification_type(A) ::= EXCEPT. {
    A = FDW_IMPORT_SCHEMA_EXCEPT;
}

/* ----- import_qualification ----- */

import_qualification(A) ::= import_qualification_type(B) LPAREN relation_expr_list(D) RPAREN. {
    ImportQual *n = palloc_object(ImportQual);

    				n->type = B;
    				n->table_names = D;
    				A = n;
}

import_qualification(A) ::= . {
    ImportQual *n = palloc_object(ImportQual);
    				n->type = FDW_IMPORT_SCHEMA_ALL;
    				n->table_names = NIL;
    				A = n;
}

/* ----- createUserMappingStmt ----- */

createUserMappingStmt(A) ::= CREATE USER MAPPING FOR auth_ident(F) SERVER name(H) create_generic_options(I). {
    createUserMappingStmt *n = makeNode(createUserMappingStmt);

    					n->user = F;
    					n->servername = H;
    					n->options = I;
    					n->if_not_exists = false;
    					A = (Node *) n;
}

createUserMappingStmt(A) ::= CREATE USER MAPPING IF_P NOT EXISTS FOR auth_ident(I) SERVER name(K) create_generic_options(L). {
    createUserMappingStmt *n = makeNode(createUserMappingStmt);

    					n->user = I;
    					n->servername = K;
    					n->options = L;
    					n->if_not_exists = true;
    					A = (Node *) n;
}

/* ----- auth_ident ----- */

auth_ident(A) ::= roleSpec(B). {
    A = B;
}

auth_ident(A) ::= USER. {
    A = makeRoleSpec(ROLESPEC_CURRENT_USER, LOC(B));
}

/* ----- dropUserMappingStmt ----- */

alterUserMappingStmt(A) ::= ALTER USER MAPPING FOR auth_ident(F) SERVER name(H) alter_generic_options(I). {
    alterUserMappingStmt *n = makeNode(alterUserMappingStmt);

    					n->user = F;
    					n->servername = H;
    					n->options = I;
    					A = (Node *) n;
}

/* ----- createPolicyStmt ----- */
```

