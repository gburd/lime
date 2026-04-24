# DROP and TRUNCATE


Generic DROP statement, object type classification for DROP,
and TRUNCATE. Covers DROP TABLE, VIEW, INDEX, FUNCTION, etc.


```yaml
name: pg-ddl-drop
version: 17.0.0
description: DROP statements, TRUNCATE, and object type classification
provides: [pg-ddl-drop]
depends: [pg-type-decls, pg-base-helpers]
```

## Production Rules

```lime rules
opt_drop_behavior(A) ::= CASCADE. {
    A = DROP_CASCADE;
}

opt_drop_behavior(A) ::= RESTRICT. {
    A = DROP_RESTRICT;
}

opt_drop_behavior(A) ::= . {
    A = DROP_RESTRICT;
}

/* ----- opt_utility_option_list ----- */

dropRoleStmt(A) ::= DROP ROLE role_list(D). {
    dropRoleStmt *n = makeNode(dropRoleStmt);

    					n->missing_ok = false;
    					n->roles = D;
    					A = (Node *) n;
}

dropRoleStmt(A) ::= DROP ROLE IF_P EXISTS role_list(F). {
    dropRoleStmt *n = makeNode(dropRoleStmt);

    					n->missing_ok = true;
    					n->roles = F;
    					A = (Node *) n;
}

dropRoleStmt(A) ::= DROP USER role_list(D). {
    dropRoleStmt *n = makeNode(dropRoleStmt);

    					n->missing_ok = false;
    					n->roles = D;
    					A = (Node *) n;
}

dropRoleStmt(A) ::= DROP USER IF_P EXISTS role_list(F). {
    dropRoleStmt *n = makeNode(dropRoleStmt);

    					n->roles = F;
    					n->missing_ok = true;
    					A = (Node *) n;
}

dropRoleStmt(A) ::= DROP GROUP_P role_list(D). {
    dropRoleStmt *n = makeNode(dropRoleStmt);

    					n->missing_ok = false;
    					n->roles = D;
    					A = (Node *) n;
}

dropRoleStmt(A) ::= DROP GROUP_P IF_P EXISTS role_list(F). {
    dropRoleStmt *n = makeNode(dropRoleStmt);

    					n->missing_ok = true;
    					n->roles = F;
    					A = (Node *) n;
}

/* ----- createGroupStmt ----- */

dropTableSpaceStmt(A) ::= DROP TABLESPACE name(D). {
    dropTableSpaceStmt *n = makeNode(dropTableSpaceStmt);

    					n->tablespacename = D;
    					n->missing_ok = false;
    					A = (Node *) n;
}

dropTableSpaceStmt(A) ::= DROP TABLESPACE IF_P EXISTS name(F). {
    dropTableSpaceStmt *n = makeNode(dropTableSpaceStmt);

    					n->tablespacename = F;
    					n->missing_ok = true;
    					A = (Node *) n;
}

/* ----- createExtensionStmt ----- */

dropUserMappingStmt(A) ::= DROP USER MAPPING FOR auth_ident(F) SERVER name(H). {
    dropUserMappingStmt *n = makeNode(dropUserMappingStmt);

    					n->user = F;
    					n->servername = H;
    					n->missing_ok = false;
    					A = (Node *) n;
}

dropUserMappingStmt(A) ::= DROP USER MAPPING IF_P EXISTS FOR auth_ident(H) SERVER name(J). {
    dropUserMappingStmt *n = makeNode(dropUserMappingStmt);

    					n->user = H;
    					n->servername = J;
    					n->missing_ok = true;
    					A = (Node *) n;
}

/* ----- alterUserMappingStmt ----- */

dropOpClassStmt(A) ::= DROP OPERATOR CLASS any_name(E) USING name(G) opt_drop_behavior(H). {
    dropStmt *n = makeNode(dropStmt);

    					n->objects = list_make1(lcons(makeString(G), E));
    					n->removeType = OBJECT_OPCLASS;
    					n->behavior = H;
    					n->missing_ok = false;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropOpClassStmt(A) ::= DROP OPERATOR CLASS IF_P EXISTS any_name(G) USING name(I) opt_drop_behavior(J). {
    dropStmt *n = makeNode(dropStmt);

    					n->objects = list_make1(lcons(makeString(I), G));
    					n->removeType = OBJECT_OPCLASS;
    					n->behavior = J;
    					n->missing_ok = true;
    					n->concurrent = false;
    					A = (Node *) n;
}

/* ----- dropOpFamilyStmt ----- */

dropOpFamilyStmt(A) ::= DROP OPERATOR FAMILY any_name(E) USING name(G) opt_drop_behavior(H). {
    dropStmt *n = makeNode(dropStmt);

    					n->objects = list_make1(lcons(makeString(G), E));
    					n->removeType = OBJECT_OPFAMILY;
    					n->behavior = H;
    					n->missing_ok = false;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropOpFamilyStmt(A) ::= DROP OPERATOR FAMILY IF_P EXISTS any_name(G) USING name(I) opt_drop_behavior(J). {
    dropStmt *n = makeNode(dropStmt);

    					n->objects = list_make1(lcons(makeString(I), G));
    					n->removeType = OBJECT_OPFAMILY;
    					n->behavior = J;
    					n->missing_ok = true;
    					n->concurrent = false;
    					A = (Node *) n;
}

/* ----- dropOwnedStmt ----- */

dropOwnedStmt(A) ::= DROP OWNED BY role_list(E) opt_drop_behavior(F). {
    dropOwnedStmt *n = makeNode(dropOwnedStmt);

    					n->roles = E;
    					n->behavior = F;
    					A = (Node *) n;
}

/* ----- reassignOwnedStmt ----- */

dropStmt(A) ::= DROP object_type_any_name(C) IF_P EXISTS any_name_list(F) opt_drop_behavior(G). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = C;
    					n->missing_ok = true;
    					n->objects = F;
    					n->behavior = G;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropStmt(A) ::= DROP object_type_any_name(C) any_name_list(D) opt_drop_behavior(E). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = C;
    					n->missing_ok = false;
    					n->objects = D;
    					n->behavior = E;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropStmt(A) ::= DROP drop_type_name(C) IF_P EXISTS name_list(F) opt_drop_behavior(G). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = C;
    					n->missing_ok = true;
    					n->objects = F;
    					n->behavior = G;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropStmt(A) ::= DROP drop_type_name(C) name_list(D) opt_drop_behavior(E). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = C;
    					n->missing_ok = false;
    					n->objects = D;
    					n->behavior = E;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropStmt(A) ::= DROP object_type_name_on_any_name(C) name(D) ON any_name(F) opt_drop_behavior(G). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = C;
    					n->objects = list_make1(lappend(F, makeString(D)));
    					n->behavior = G;
    					n->missing_ok = false;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropStmt(A) ::= DROP object_type_name_on_any_name(C) IF_P EXISTS name(F) ON any_name(H) opt_drop_behavior(I). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = C;
    					n->objects = list_make1(lappend(H, makeString(F)));
    					n->behavior = I;
    					n->missing_ok = true;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropStmt(A) ::= DROP TYPE_P type_name_list(D) opt_drop_behavior(E). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_TYPE;
    					n->missing_ok = false;
    					n->objects = D;
    					n->behavior = E;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropStmt(A) ::= DROP TYPE_P IF_P EXISTS type_name_list(F) opt_drop_behavior(G). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_TYPE;
    					n->missing_ok = true;
    					n->objects = F;
    					n->behavior = G;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropStmt(A) ::= DROP DOMAIN_P type_name_list(D) opt_drop_behavior(E). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_DOMAIN;
    					n->missing_ok = false;
    					n->objects = D;
    					n->behavior = E;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropStmt(A) ::= DROP DOMAIN_P IF_P EXISTS type_name_list(F) opt_drop_behavior(G). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_DOMAIN;
    					n->missing_ok = true;
    					n->objects = F;
    					n->behavior = G;
    					n->concurrent = false;
    					A = (Node *) n;
}

dropStmt(A) ::= DROP INDEX CONCURRENTLY any_name_list(E) opt_drop_behavior(F). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_INDEX;
    					n->missing_ok = false;
    					n->objects = E;
    					n->behavior = F;
    					n->concurrent = true;
    					A = (Node *) n;
}

dropStmt(A) ::= DROP INDEX CONCURRENTLY IF_P EXISTS any_name_list(G) opt_drop_behavior(H). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_INDEX;
    					n->missing_ok = true;
    					n->objects = G;
    					n->behavior = H;
    					n->concurrent = true;
    					A = (Node *) n;
}

/* ----- object_type_any_name ----- */

object_type_any_name(A) ::= TABLE. {
    A = OBJECT_TABLE;
}

object_type_any_name(A) ::= SEQUENCE. {
    A = OBJECT_SEQUENCE;
}

object_type_any_name(A) ::= VIEW. {
    A = OBJECT_VIEW;
}

object_type_any_name(A) ::= MATERIALIZED VIEW. {
    A = OBJECT_MATVIEW;
}

object_type_any_name(A) ::= INDEX. {
    A = OBJECT_INDEX;
}

object_type_any_name(A) ::= FOREIGN TABLE. {
    A = OBJECT_FOREIGN_TABLE;
}

object_type_any_name(A) ::= PROPERTY GRAPH. {
    A = OBJECT_PROPGRAPH;
}

object_type_any_name(A) ::= COLLATION. {
    A = OBJECT_COLLATION;
}

object_type_any_name(A) ::= CONVERSION_P. {
    A = OBJECT_CONVERSION;
}

object_type_any_name(A) ::= STATISTICS. {
    A = OBJECT_STATISTIC_EXT;
}

object_type_any_name(A) ::= TEXT_P SEARCH PARSER. {
    A = OBJECT_TSPARSER;
}

object_type_any_name(A) ::= TEXT_P SEARCH DICTIONARY. {
    A = OBJECT_TSDICTIONARY;
}

object_type_any_name(A) ::= TEXT_P SEARCH TEMPLATE. {
    A = OBJECT_TSTEMPLATE;
}

object_type_any_name(A) ::= TEXT_P SEARCH CONFIGURATION. {
    A = OBJECT_TSCONFIGURATION;
}

/* ----- object_type_name ----- */

object_type_name(A) ::= drop_type_name(B). {
    A = B;
}

object_type_name(A) ::= DATABASE. {
    A = OBJECT_DATABASE;
}

object_type_name(A) ::= ROLE. {
    A = OBJECT_ROLE;
}

object_type_name(A) ::= SUBSCRIPTION. {
    A = OBJECT_SUBSCRIPTION;
}

object_type_name(A) ::= TABLESPACE. {
    A = OBJECT_TABLESPACE;
}

/* ----- drop_type_name ----- */

drop_type_name(A) ::= ACCESS METHOD. {
    A = OBJECT_ACCESS_METHOD;
}

drop_type_name(A) ::= EVENT TRIGGER. {
    A = OBJECT_EVENT_TRIGGER;
}

drop_type_name(A) ::= EXTENSION. {
    A = OBJECT_EXTENSION;
}

drop_type_name(A) ::= FOREIGN DATA_P WRAPPER. {
    A = OBJECT_FDW;
}

drop_type_name(A) ::= opt_procedural(B) LANGUAGE. {
    A = OBJECT_LANGUAGE;
}

drop_type_name(A) ::= PUBLICATION. {
    A = OBJECT_PUBLICATION;
}

drop_type_name(A) ::= SCHEMA. {
    A = OBJECT_SCHEMA;
}

drop_type_name(A) ::= SERVER. {
    A = OBJECT_FOREIGN_SERVER;
}

/* ----- object_type_name_on_any_name ----- */

object_type_name_on_any_name(A) ::= POLICY. {
    A = OBJECT_POLICY;
}

object_type_name_on_any_name(A) ::= RULE. {
    A = OBJECT_RULE;
}

object_type_name_on_any_name(A) ::= TRIGGER. {
    A = OBJECT_TRIGGER;
}

/* ----- any_name_list ----- */

any_name_list(A) ::= any_name(B). {
    A = list_make1(B);
}

any_name_list(A) ::= any_name_list(B) COMMA any_name(D). {
    A = lappend(B, D);
}

/* ----- any_name ----- */

any_name(A) ::= colId(B). {
    A = list_make1(makeString(B));
}

any_name(A) ::= colId(B) attrs(C). {
    A = lcons(makeString(B), C);
}

/* ----- attrs ----- */

attrs(A) ::= DOT attr_name(C). {
    A = list_make1(makeString(C));
}

attrs(A) ::= attrs(B) DOT attr_name(D). {
    A = lappend(B, makeString(D));
}

/* ----- type_name_list ----- */

type_name_list(A) ::= typename(B). {
    A = list_make1(B);
}

type_name_list(A) ::= type_name_list(B) COMMA typename(D). {
    A = lappend(B, D);
}

/* ----- truncateStmt ----- */

truncateStmt(A) ::= TRUNCATE opt_table(C) relation_expr_list(D) opt_restart_seqs(E) opt_drop_behavior(F). {
    truncateStmt *n = makeNode(truncateStmt);

    					n->relations = D;
    					n->restart_seqs = E;
    					n->behavior = F;
    					A = (Node *) n;
}

/* ----- opt_restart_seqs ----- */

opt_restart_seqs(A) ::= CONTINUE_P IDENTITY_P. {
    A = false;
}

opt_restart_seqs(A) ::= RESTART IDENTITY_P. {
    A = true;
}

opt_restart_seqs(A) ::= . {
    A = false;
}

/* ----- commentStmt ----- */

dropCastStmt(A) ::= DROP CAST opt_if_exists(D) LPAREN typename(F) AS typename(H) RPAREN opt_drop_behavior(J). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_CAST;
    					n->objects = list_make1(list_make2(F, H));
    					n->behavior = J;
    					n->missing_ok = D;
    					n->concurrent = false;
    					A = (Node *) n;
}

/* ----- opt_if_exists ----- */

opt_if_exists(A) ::= IF_P EXISTS. {
    A = true;
}

opt_if_exists(A) ::= . {
    A = false;
}

/* ----- createPropGraphStmt ----- */

dropTransformStmt(A) ::= DROP TRANSFORM opt_if_exists(D) FOR typename(F) LANGUAGE name(H) opt_drop_behavior(I). {
    dropStmt *n = makeNode(dropStmt);

    					n->removeType = OBJECT_TRANSFORM;
    					n->objects = list_make1(list_make2(F, makeString(H)));
    					n->behavior = I;
    					n->missing_ok = D;
    					A = (Node *) n;
}

/* ----- reindexStmt ----- */

dropSubscriptionStmt(A) ::= DROP SUBSCRIPTION name(D) opt_drop_behavior(E). {
    dropSubscriptionStmt *n = makeNode(dropSubscriptionStmt);

    					n->subname = D;
    					n->missing_ok = false;
    					n->behavior = E;
    					A = (Node *) n;
}

dropSubscriptionStmt(A) ::= DROP SUBSCRIPTION IF_P EXISTS name(F) opt_drop_behavior(G). {
    dropSubscriptionStmt *n = makeNode(dropSubscriptionStmt);

    					n->subname = F;
    					n->missing_ok = true;
    					n->behavior = G;
    					A = (Node *) n;
}

/* ----- ruleStmt ----- */

dropdbStmt(A) ::= DROP DATABASE name(D). {
    dropdbStmt *n = makeNode(dropdbStmt);

    					n->dbname = D;
    					n->missing_ok = false;
    					n->options = NULL;
    					A = (Node *) n;
}

dropdbStmt(A) ::= DROP DATABASE IF_P EXISTS name(F). {
    dropdbStmt *n = makeNode(dropdbStmt);

    					n->dbname = F;
    					n->missing_ok = true;
    					n->options = NULL;
    					A = (Node *) n;
}

dropdbStmt(A) ::= DROP DATABASE name(D) opt_with(E) LPAREN drop_option_list(G) RPAREN. {
    dropdbStmt *n = makeNode(dropdbStmt);

    					n->dbname = D;
    					n->missing_ok = false;
    					n->options = G;
    					A = (Node *) n;
}

dropdbStmt(A) ::= DROP DATABASE IF_P EXISTS name(F) opt_with(G) LPAREN drop_option_list(I) RPAREN. {
    dropdbStmt *n = makeNode(dropdbStmt);

    					n->dbname = F;
    					n->missing_ok = true;
    					n->options = I;
    					A = (Node *) n;
}

/* ----- drop_option_list ----- */

drop_option_list(A) ::= drop_option(B). {
    A = list_make1((Node *) B);
}

drop_option_list(A) ::= drop_option_list(B) COMMA drop_option(D). {
    A = lappend(B, (Node *) D);
}

/* ----- drop_option ----- */

drop_option(A) ::= FORCE. {
    A = makeDefElem("force", NULL, LOC(B));
}

/* ----- alterCollationStmt ----- */
```

