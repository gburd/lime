# Miscellaneous ALTER/DDL


Miscellaneous DDL statements: RENAME, ALTER OWNER, ALTER
SCHEMA, ALTER DEPENDS, COMMENT, SECURITY LABEL, CREATE/ALTER
DATABASE, CREATE DOMAIN, CREATE CONVERSION, ALTER ENUM,
ALTER SYSTEM, ALTER COLLATION, REPACK, CLUSTER, and
text search dictionary/configuration alterations.


```yaml
name: pg-ddl-alter-misc
version: 17.0.0
description: RENAME, ALTER OWNER/SCHEMA/DEPENDS, COMMENT, SECURITY LABEL, etc.
provides: [pg-ddl-alter-misc]
depends: [pg-type-decls, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
alterStatsStmt(A) ::= ALTER STATISTICS any_name(D) SET STATISTICS set_statistics_value(G). {
    alterStatsStmt *n = makeNode(alterStatsStmt);

    					n->defnames = D;
    					n->missing_ok = false;
    					n->stxstattarget = G;
    					A = (Node *) n;
}

alterStatsStmt(A) ::= ALTER STATISTICS IF_P EXISTS any_name(F) SET STATISTICS set_statistics_value(I). {
    alterStatsStmt *n = makeNode(alterStatsStmt);

    					n->defnames = F;
    					n->missing_ok = true;
    					n->stxstattarget = I;
    					A = (Node *) n;
}

/* ----- createAsStmt ----- */

alterEnumStmt(A) ::= ALTER TYPE_P any_name(D) ADD_P VALUE_P opt_if_not_exists(G) sconst(H). {
    alterEnumStmt *n = makeNode(alterEnumStmt);

    				n->typeName = D;
    				n->oldVal = NULL;
    				n->newVal = H;
    				n->newValNeighbor = NULL;
    				n->newValIsAfter = true;
    				n->skipIfNewValExists = G;
    				A = (Node *) n;
}

alterEnumStmt(A) ::= ALTER TYPE_P any_name(D) ADD_P VALUE_P opt_if_not_exists(G) sconst(H) BEFORE sconst(J). {
    alterEnumStmt *n = makeNode(alterEnumStmt);

    				n->typeName = D;
    				n->oldVal = NULL;
    				n->newVal = H;
    				n->newValNeighbor = J;
    				n->newValIsAfter = false;
    				n->skipIfNewValExists = G;
    				A = (Node *) n;
}

alterEnumStmt(A) ::= ALTER TYPE_P any_name(D) ADD_P VALUE_P opt_if_not_exists(G) sconst(H) AFTER sconst(J). {
    alterEnumStmt *n = makeNode(alterEnumStmt);

    				n->typeName = D;
    				n->oldVal = NULL;
    				n->newVal = H;
    				n->newValNeighbor = J;
    				n->newValIsAfter = true;
    				n->skipIfNewValExists = G;
    				A = (Node *) n;
}

alterEnumStmt(A) ::= ALTER TYPE_P any_name(D) RENAME VALUE_P sconst(G) TO sconst(I). {
    alterEnumStmt *n = makeNode(alterEnumStmt);

    				n->typeName = D;
    				n->oldVal = G;
    				n->newVal = I;
    				n->newValNeighbor = NULL;
    				n->newValIsAfter = false;
    				n->skipIfNewValExists = false;
    				A = (Node *) n;
}

alterEnumStmt(A) ::= ALTER TYPE_P any_name(D) DROP VALUE_P sconst(G). {
    ereport(ERROR,
    						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    						 errmsg("dropping an enum value is not implemented"),
    						 parser_errposition(LOC(E))));
}

/* ----- opt_if_not_exists ----- */

opt_if_not_exists(A) ::= IF_P NOT EXISTS. {
    A = true;
}

opt_if_not_exists(A) ::= . {
    A = false;
}

/* ----- createOpClassStmt ----- */

commentStmt(A) ::= COMMENT ON object_type_any_name(D) any_name(E) IS comment_text(G). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = D;
    					n->object = (Node *) E;
    					n->comment = G;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON COLUMN any_name(E) IS comment_text(G). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_COLUMN;
    					n->object = (Node *) E;
    					n->comment = G;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON object_type_name(D) name(E) IS comment_text(G). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = D;
    					n->object = (Node *) makeString(E);
    					n->comment = G;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON TYPE_P typename(E) IS comment_text(G). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_TYPE;
    					n->object = (Node *) E;
    					n->comment = G;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON DOMAIN_P typename(E) IS comment_text(G). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_DOMAIN;
    					n->object = (Node *) E;
    					n->comment = G;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON AGGREGATE aggregate_with_argtypes(E) IS comment_text(G). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_AGGREGATE;
    					n->object = (Node *) E;
    					n->comment = G;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON FUNCTION function_with_argtypes(E) IS comment_text(G). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_FUNCTION;
    					n->object = (Node *) E;
    					n->comment = G;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON OPERATOR operator_with_argtypes(E) IS comment_text(G). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_OPERATOR;
    					n->object = (Node *) E;
    					n->comment = G;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON CONSTRAINT name(E) ON any_name(G) IS comment_text(I). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_TABCONSTRAINT;
    					n->object = (Node *) lappend(G, makeString(E));
    					n->comment = I;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON CONSTRAINT name(E) ON DOMAIN_P any_name(H) IS comment_text(J). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_DOMCONSTRAINT;





    					n->object = (Node *) list_make2(makeTypeNameFromNameList(H), makeString(E));
    					n->comment = J;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON object_type_name_on_any_name(D) name(E) ON any_name(G) IS comment_text(I). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = D;
    					n->object = (Node *) lappend(G, makeString(E));
    					n->comment = I;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON PROCEDURE function_with_argtypes(E) IS comment_text(G). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_PROCEDURE;
    					n->object = (Node *) E;
    					n->comment = G;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON ROUTINE function_with_argtypes(E) IS comment_text(G). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_ROUTINE;
    					n->object = (Node *) E;
    					n->comment = G;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON TRANSFORM FOR typename(F) LANGUAGE name(H) IS comment_text(J). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_TRANSFORM;
    					n->object = (Node *) list_make2(F, makeString(H));
    					n->comment = J;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON OPERATOR CLASS any_name(F) USING name(H) IS comment_text(J). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_OPCLASS;
    					n->object = (Node *) lcons(makeString(H), F);
    					n->comment = J;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON OPERATOR FAMILY any_name(F) USING name(H) IS comment_text(J). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_OPFAMILY;
    					n->object = (Node *) lcons(makeString(H), F);
    					n->comment = J;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON LARGE_P OBJECT_P numericOnly(F) IS comment_text(H). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_LARGEOBJECT;
    					n->object = (Node *) F;
    					n->comment = H;
    					A = (Node *) n;
}

commentStmt(A) ::= COMMENT ON CAST LPAREN typename(F) AS typename(H) RPAREN IS comment_text(K). {
    commentStmt *n = makeNode(commentStmt);

    					n->objtype = OBJECT_CAST;
    					n->object = (Node *) list_make2(F, H);
    					n->comment = K;
    					A = (Node *) n;
}

/* ----- comment_text ----- */

comment_text(A) ::= sconst(B). {
    A = B;
}

comment_text(A) ::= NULL_P. {
    A = NULL;
}

/* ----- secLabelStmt ----- */

secLabelStmt(A) ::= SECURITY LABEL opt_provider(D) ON object_type_any_name(F) any_name(G) IS security_label(I). {
    secLabelStmt *n = makeNode(secLabelStmt);

    					n->provider = D;
    					n->objtype = F;
    					n->object = (Node *) G;
    					n->label = I;
    					A = (Node *) n;
}

secLabelStmt(A) ::= SECURITY LABEL opt_provider(D) ON COLUMN any_name(G) IS security_label(I). {
    secLabelStmt *n = makeNode(secLabelStmt);

    					n->provider = D;
    					n->objtype = OBJECT_COLUMN;
    					n->object = (Node *) G;
    					n->label = I;
    					A = (Node *) n;
}

secLabelStmt(A) ::= SECURITY LABEL opt_provider(D) ON object_type_name(F) name(G) IS security_label(I). {
    secLabelStmt *n = makeNode(secLabelStmt);

    					n->provider = D;
    					n->objtype = F;
    					n->object = (Node *) makeString(G);
    					n->label = I;
    					A = (Node *) n;
}

secLabelStmt(A) ::= SECURITY LABEL opt_provider(D) ON TYPE_P typename(G) IS security_label(I). {
    secLabelStmt *n = makeNode(secLabelStmt);

    					n->provider = D;
    					n->objtype = OBJECT_TYPE;
    					n->object = (Node *) G;
    					n->label = I;
    					A = (Node *) n;
}

secLabelStmt(A) ::= SECURITY LABEL opt_provider(D) ON DOMAIN_P typename(G) IS security_label(I). {
    secLabelStmt *n = makeNode(secLabelStmt);

    					n->provider = D;
    					n->objtype = OBJECT_DOMAIN;
    					n->object = (Node *) G;
    					n->label = I;
    					A = (Node *) n;
}

secLabelStmt(A) ::= SECURITY LABEL opt_provider(D) ON AGGREGATE aggregate_with_argtypes(G) IS security_label(I). {
    secLabelStmt *n = makeNode(secLabelStmt);

    					n->provider = D;
    					n->objtype = OBJECT_AGGREGATE;
    					n->object = (Node *) G;
    					n->label = I;
    					A = (Node *) n;
}

secLabelStmt(A) ::= SECURITY LABEL opt_provider(D) ON FUNCTION function_with_argtypes(G) IS security_label(I). {
    secLabelStmt *n = makeNode(secLabelStmt);

    					n->provider = D;
    					n->objtype = OBJECT_FUNCTION;
    					n->object = (Node *) G;
    					n->label = I;
    					A = (Node *) n;
}

secLabelStmt(A) ::= SECURITY LABEL opt_provider(D) ON LARGE_P OBJECT_P numericOnly(H) IS security_label(J). {
    secLabelStmt *n = makeNode(secLabelStmt);

    					n->provider = D;
    					n->objtype = OBJECT_LARGEOBJECT;
    					n->object = (Node *) H;
    					n->label = J;
    					A = (Node *) n;
}

secLabelStmt(A) ::= SECURITY LABEL opt_provider(D) ON PROCEDURE function_with_argtypes(G) IS security_label(I). {
    secLabelStmt *n = makeNode(secLabelStmt);

    					n->provider = D;
    					n->objtype = OBJECT_PROCEDURE;
    					n->object = (Node *) G;
    					n->label = I;
    					A = (Node *) n;
}

secLabelStmt(A) ::= SECURITY LABEL opt_provider(D) ON ROUTINE function_with_argtypes(G) IS security_label(I). {
    secLabelStmt *n = makeNode(secLabelStmt);

    					n->provider = D;
    					n->objtype = OBJECT_ROUTINE;
    					n->object = (Node *) G;
    					n->label = I;
    					A = (Node *) n;
}

/* ----- opt_provider ----- */

opt_provider(A) ::= FOR nonReservedWord_or_Sconst(C). {
    A = C;
}

opt_provider(A) ::= . {
    A = NULL;
}

/* ----- security_label ----- */

security_label(A) ::= sconst(B). {
    A = B;
}

security_label(A) ::= NULL_P. {
    A = NULL;
}

/* ----- fetchStmt ----- */

renameStmt(A) ::= ALTER AGGREGATE aggregate_with_argtypes(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_AGGREGATE;
    					n->object = (Node *) D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER COLLATION any_name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_COLLATION;
    					n->object = (Node *) D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER CONVERSION_P any_name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_CONVERSION;
    					n->object = (Node *) D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER DATABASE name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_DATABASE;
    					n->subname = D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER DOMAIN_P any_name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_DOMAIN;
    					n->object = (Node *) D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER DOMAIN_P any_name(D) RENAME CONSTRAINT name(G) TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_DOMCONSTRAINT;
    					n->object = (Node *) D;
    					n->subname = G;
    					n->newname = I;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER FOREIGN DATA_P WRAPPER name(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_FDW;
    					n->object = (Node *) makeString(F);
    					n->newname = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER FUNCTION function_with_argtypes(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_FUNCTION;
    					n->object = (Node *) D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER GROUP_P roleId(D) RENAME TO roleId(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_ROLE;
    					n->subname = D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER opt_procedural(C) LANGUAGE name(E) RENAME TO name(H). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_LANGUAGE;
    					n->object = (Node *) makeString(E);
    					n->newname = H;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER OPERATOR CLASS any_name(E) USING name(G) RENAME TO name(J). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_OPCLASS;
    					n->object = (Node *) lcons(makeString(G), E);
    					n->newname = J;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER OPERATOR FAMILY any_name(E) USING name(G) RENAME TO name(J). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_OPFAMILY;
    					n->object = (Node *) lcons(makeString(G), E);
    					n->newname = J;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER POLICY name(D) ON qualified_name(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_POLICY;
    					n->relation = F;
    					n->subname = D;
    					n->newname = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER POLICY IF_P EXISTS name(F) ON qualified_name(H) RENAME TO name(K). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_POLICY;
    					n->relation = H;
    					n->subname = F;
    					n->newname = K;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER PROCEDURE function_with_argtypes(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_PROCEDURE;
    					n->object = (Node *) D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) RENAME TO name(H). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_PROPGRAPH;
    					n->relation = E;
    					n->newname = H;
    					n->missing_ok = false;
    					A = (Node *)n;
}

renameStmt(A) ::= ALTER PUBLICATION name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_PUBLICATION;
    					n->object = (Node *) makeString(D);
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER ROUTINE function_with_argtypes(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_ROUTINE;
    					n->object = (Node *) D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER SCHEMA name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_SCHEMA;
    					n->subname = D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER SERVER name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_FOREIGN_SERVER;
    					n->object = (Node *) makeString(D);
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER SUBSCRIPTION name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_SUBSCRIPTION;
    					n->object = (Node *) makeString(D);
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TABLE relation_expr(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_TABLE;
    					n->relation = D;
    					n->subname = NULL;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TABLE IF_P EXISTS relation_expr(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_TABLE;
    					n->relation = F;
    					n->subname = NULL;
    					n->newname = I;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER SEQUENCE qualified_name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_SEQUENCE;
    					n->relation = D;
    					n->subname = NULL;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER SEQUENCE IF_P EXISTS qualified_name(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_SEQUENCE;
    					n->relation = F;
    					n->subname = NULL;
    					n->newname = I;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER VIEW qualified_name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_VIEW;
    					n->relation = D;
    					n->subname = NULL;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER VIEW IF_P EXISTS qualified_name(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_VIEW;
    					n->relation = F;
    					n->subname = NULL;
    					n->newname = I;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER MATERIALIZED VIEW qualified_name(E) RENAME TO name(H). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_MATVIEW;
    					n->relation = E;
    					n->subname = NULL;
    					n->newname = H;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER MATERIALIZED VIEW IF_P EXISTS qualified_name(G) RENAME TO name(J). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_MATVIEW;
    					n->relation = G;
    					n->subname = NULL;
    					n->newname = J;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER INDEX qualified_name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_INDEX;
    					n->relation = D;
    					n->subname = NULL;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER INDEX IF_P EXISTS qualified_name(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_INDEX;
    					n->relation = F;
    					n->subname = NULL;
    					n->newname = I;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER FOREIGN TABLE relation_expr(E) RENAME TO name(H). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_FOREIGN_TABLE;
    					n->relation = E;
    					n->subname = NULL;
    					n->newname = H;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER FOREIGN TABLE IF_P EXISTS relation_expr(G) RENAME TO name(J). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_FOREIGN_TABLE;
    					n->relation = G;
    					n->subname = NULL;
    					n->newname = J;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TABLE relation_expr(D) RENAME opt_column(F) name(G) TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_COLUMN;
    					n->relationType = OBJECT_TABLE;
    					n->relation = D;
    					n->subname = G;
    					n->newname = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TABLE IF_P EXISTS relation_expr(F) RENAME opt_column(H) name(I) TO name(K). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_COLUMN;
    					n->relationType = OBJECT_TABLE;
    					n->relation = F;
    					n->subname = I;
    					n->newname = K;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER VIEW qualified_name(D) RENAME opt_column(F) name(G) TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_COLUMN;
    					n->relationType = OBJECT_VIEW;
    					n->relation = D;
    					n->subname = G;
    					n->newname = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER VIEW IF_P EXISTS qualified_name(F) RENAME opt_column(H) name(I) TO name(K). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_COLUMN;
    					n->relationType = OBJECT_VIEW;
    					n->relation = F;
    					n->subname = I;
    					n->newname = K;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER MATERIALIZED VIEW qualified_name(E) RENAME opt_column(G) name(H) TO name(J). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_COLUMN;
    					n->relationType = OBJECT_MATVIEW;
    					n->relation = E;
    					n->subname = H;
    					n->newname = J;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER MATERIALIZED VIEW IF_P EXISTS qualified_name(G) RENAME opt_column(I) name(J) TO name(L). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_COLUMN;
    					n->relationType = OBJECT_MATVIEW;
    					n->relation = G;
    					n->subname = J;
    					n->newname = L;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TABLE relation_expr(D) RENAME CONSTRAINT name(G) TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_TABCONSTRAINT;
    					n->relation = D;
    					n->subname = G;
    					n->newname = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TABLE IF_P EXISTS relation_expr(F) RENAME CONSTRAINT name(I) TO name(K). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_TABCONSTRAINT;
    					n->relation = F;
    					n->subname = I;
    					n->newname = K;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER FOREIGN TABLE relation_expr(E) RENAME opt_column(G) name(H) TO name(J). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_COLUMN;
    					n->relationType = OBJECT_FOREIGN_TABLE;
    					n->relation = E;
    					n->subname = H;
    					n->newname = J;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER FOREIGN TABLE IF_P EXISTS relation_expr(G) RENAME opt_column(I) name(J) TO name(L). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_COLUMN;
    					n->relationType = OBJECT_FOREIGN_TABLE;
    					n->relation = G;
    					n->subname = J;
    					n->newname = L;
    					n->missing_ok = true;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER RULE name(D) ON qualified_name(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_RULE;
    					n->relation = F;
    					n->subname = D;
    					n->newname = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TRIGGER name(D) ON qualified_name(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_TRIGGER;
    					n->relation = F;
    					n->subname = D;
    					n->newname = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER EVENT TRIGGER name(E) RENAME TO name(H). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_EVENT_TRIGGER;
    					n->object = (Node *) makeString(E);
    					n->newname = H;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER ROLE roleId(D) RENAME TO roleId(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_ROLE;
    					n->subname = D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER USER roleId(D) RENAME TO roleId(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_ROLE;
    					n->subname = D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TABLESPACE name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_TABLESPACE;
    					n->subname = D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER STATISTICS any_name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_STATISTIC_EXT;
    					n->object = (Node *) D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TEXT_P SEARCH PARSER any_name(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_TSPARSER;
    					n->object = (Node *) F;
    					n->newname = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TEXT_P SEARCH DICTIONARY any_name(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_TSDICTIONARY;
    					n->object = (Node *) F;
    					n->newname = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TEXT_P SEARCH TEMPLATE any_name(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_TSTEMPLATE;
    					n->object = (Node *) F;
    					n->newname = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TEXT_P SEARCH CONFIGURATION any_name(F) RENAME TO name(I). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_TSCONFIGURATION;
    					n->object = (Node *) F;
    					n->newname = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TYPE_P any_name(D) RENAME TO name(G). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_TYPE;
    					n->object = (Node *) D;
    					n->newname = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

renameStmt(A) ::= ALTER TYPE_P any_name(D) RENAME ATTRIBUTE name(G) TO name(I) opt_drop_behavior(J). {
    renameStmt *n = makeNode(renameStmt);

    					n->renameType = OBJECT_ATTRIBUTE;
    					n->relationType = OBJECT_TYPE;
    					n->relation = makeRangeVarFromAnyName(D, LOC(D), yyscanner);
    					n->subname = G;
    					n->newname = I;
    					n->behavior = J;
    					n->missing_ok = false;
    					A = (Node *) n;
}

/* ----- opt_column ----- */

opt_column(A) ::= COLUMN.

/* ----- opt_set_data ----- */

opt_set_data(A) ::= SET DATA_P. {
    A = 1;
}

opt_set_data(A) ::= . {
    A = 0;
}

/* ----- alterObjectDependsStmt ----- */

alterObjectDependsStmt(A) ::= ALTER FUNCTION function_with_argtypes(D) opt_no(E) DEPENDS ON EXTENSION name(I). {
    alterObjectDependsStmt *n = makeNode(alterObjectDependsStmt);

    					n->objectType = OBJECT_FUNCTION;
    					n->object = (Node *) D;
    					n->extname = makeString(I);
    					n->remove = E;
    					A = (Node *) n;
}

alterObjectDependsStmt(A) ::= ALTER PROCEDURE function_with_argtypes(D) opt_no(E) DEPENDS ON EXTENSION name(I). {
    alterObjectDependsStmt *n = makeNode(alterObjectDependsStmt);

    					n->objectType = OBJECT_PROCEDURE;
    					n->object = (Node *) D;
    					n->extname = makeString(I);
    					n->remove = E;
    					A = (Node *) n;
}

alterObjectDependsStmt(A) ::= ALTER ROUTINE function_with_argtypes(D) opt_no(E) DEPENDS ON EXTENSION name(I). {
    alterObjectDependsStmt *n = makeNode(alterObjectDependsStmt);

    					n->objectType = OBJECT_ROUTINE;
    					n->object = (Node *) D;
    					n->extname = makeString(I);
    					n->remove = E;
    					A = (Node *) n;
}

alterObjectDependsStmt(A) ::= ALTER TRIGGER name(D) ON qualified_name(F) opt_no(G) DEPENDS ON EXTENSION name(K). {
    alterObjectDependsStmt *n = makeNode(alterObjectDependsStmt);

    					n->objectType = OBJECT_TRIGGER;
    					n->relation = F;
    					n->object = (Node *) list_make1(makeString(D));
    					n->extname = makeString(K);
    					n->remove = G;
    					A = (Node *) n;
}

alterObjectDependsStmt(A) ::= ALTER MATERIALIZED VIEW qualified_name(E) opt_no(F) DEPENDS ON EXTENSION name(J). {
    alterObjectDependsStmt *n = makeNode(alterObjectDependsStmt);

    					n->objectType = OBJECT_MATVIEW;
    					n->relation = E;
    					n->extname = makeString(J);
    					n->remove = F;
    					A = (Node *) n;
}

alterObjectDependsStmt(A) ::= ALTER INDEX qualified_name(D) opt_no(E) DEPENDS ON EXTENSION name(I). {
    alterObjectDependsStmt *n = makeNode(alterObjectDependsStmt);

    					n->objectType = OBJECT_INDEX;
    					n->relation = D;
    					n->extname = makeString(I);
    					n->remove = E;
    					A = (Node *) n;
}

/* ----- opt_no ----- */

alterObjectSchemaStmt(A) ::= ALTER AGGREGATE aggregate_with_argtypes(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_AGGREGATE;
    					n->object = (Node *) D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER COLLATION any_name(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_COLLATION;
    					n->object = (Node *) D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER CONVERSION_P any_name(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_CONVERSION;
    					n->object = (Node *) D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER DOMAIN_P any_name(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_DOMAIN;
    					n->object = (Node *) D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER EXTENSION name(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_EXTENSION;
    					n->object = (Node *) makeString(D);
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER FUNCTION function_with_argtypes(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_FUNCTION;
    					n->object = (Node *) D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER OPERATOR operator_with_argtypes(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_OPERATOR;
    					n->object = (Node *) D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER OPERATOR CLASS any_name(E) USING name(G) SET SCHEMA name(J). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_OPCLASS;
    					n->object = (Node *) lcons(makeString(G), E);
    					n->newschema = J;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER OPERATOR FAMILY any_name(E) USING name(G) SET SCHEMA name(J). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_OPFAMILY;
    					n->object = (Node *) lcons(makeString(G), E);
    					n->newschema = J;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER PROCEDURE function_with_argtypes(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_PROCEDURE;
    					n->object = (Node *) D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) SET SCHEMA name(H). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_PROPGRAPH;
    					n->relation = E;
    					n->newschema = H;
    					n->missing_ok = false;
    					A = (Node *)n;
}

alterObjectSchemaStmt(A) ::= ALTER PROPERTY GRAPH IF_P EXISTS qualified_name(G) SET SCHEMA name(J). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_PROPGRAPH;
    					n->relation = G;
    					n->newschema = J;
    					n->missing_ok = true;
    					A = (Node *)n;
}

alterObjectSchemaStmt(A) ::= ALTER ROUTINE function_with_argtypes(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_ROUTINE;
    					n->object = (Node *) D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER TABLE relation_expr(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_TABLE;
    					n->relation = D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER TABLE IF_P EXISTS relation_expr(F) SET SCHEMA name(I). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_TABLE;
    					n->relation = F;
    					n->newschema = I;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER STATISTICS any_name(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_STATISTIC_EXT;
    					n->object = (Node *) D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER TEXT_P SEARCH PARSER any_name(F) SET SCHEMA name(I). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_TSPARSER;
    					n->object = (Node *) F;
    					n->newschema = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER TEXT_P SEARCH DICTIONARY any_name(F) SET SCHEMA name(I). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_TSDICTIONARY;
    					n->object = (Node *) F;
    					n->newschema = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER TEXT_P SEARCH TEMPLATE any_name(F) SET SCHEMA name(I). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_TSTEMPLATE;
    					n->object = (Node *) F;
    					n->newschema = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER TEXT_P SEARCH CONFIGURATION any_name(F) SET SCHEMA name(I). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_TSCONFIGURATION;
    					n->object = (Node *) F;
    					n->newschema = I;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER SEQUENCE qualified_name(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_SEQUENCE;
    					n->relation = D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER SEQUENCE IF_P EXISTS qualified_name(F) SET SCHEMA name(I). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_SEQUENCE;
    					n->relation = F;
    					n->newschema = I;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER VIEW qualified_name(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_VIEW;
    					n->relation = D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER VIEW IF_P EXISTS qualified_name(F) SET SCHEMA name(I). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_VIEW;
    					n->relation = F;
    					n->newschema = I;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER MATERIALIZED VIEW qualified_name(E) SET SCHEMA name(H). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_MATVIEW;
    					n->relation = E;
    					n->newschema = H;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER MATERIALIZED VIEW IF_P EXISTS qualified_name(G) SET SCHEMA name(J). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_MATVIEW;
    					n->relation = G;
    					n->newschema = J;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER FOREIGN TABLE relation_expr(E) SET SCHEMA name(H). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_FOREIGN_TABLE;
    					n->relation = E;
    					n->newschema = H;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER FOREIGN TABLE IF_P EXISTS relation_expr(G) SET SCHEMA name(J). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_FOREIGN_TABLE;
    					n->relation = G;
    					n->newschema = J;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterObjectSchemaStmt(A) ::= ALTER TYPE_P any_name(D) SET SCHEMA name(G). {
    alterObjectSchemaStmt *n = makeNode(alterObjectSchemaStmt);

    					n->objectType = OBJECT_TYPE;
    					n->object = (Node *) D;
    					n->newschema = G;
    					n->missing_ok = false;
    					A = (Node *) n;
}

/* ----- alterOperatorStmt ----- */

alterOperatorStmt(A) ::= ALTER OPERATOR operator_with_argtypes(D) SET LPAREN operator_def_list(G) RPAREN. {
    alterOperatorStmt *n = makeNode(alterOperatorStmt);

    					n->opername = D;
    					n->options = G;
    					A = (Node *) n;
}

/* ----- operator_def_list ----- */

operator_def_list(A) ::= operator_def_elem(B). {
    A = list_make1(B);
}

operator_def_list(A) ::= operator_def_list(B) COMMA operator_def_elem(D). {
    A = lappend(B, D);
}

/* ----- operator_def_elem ----- */

operator_def_elem(A) ::= colLabel(B) EQ NONE. {
    A = makeDefElem(B, NULL, LOC(B));
}

operator_def_elem(A) ::= colLabel(B) EQ operator_def_arg(D). {
    A = makeDefElem(B, (Node *) D, LOC(B));
}

operator_def_elem(A) ::= colLabel(B). {
    A = makeDefElem(B, NULL, LOC(B));
}

/* ----- operator_def_arg ----- */

operator_def_arg(A) ::= func_type(B). {
    A = (Node *) B;
}

operator_def_arg(A) ::= reserved_keyword(B). {
    A = (Node *) makeString(pstrdup(B));
}

operator_def_arg(A) ::= qual_all_Op(B). {
    A = (Node *) B;
}

operator_def_arg(A) ::= numericOnly(B). {
    A = (Node *) B;
}

operator_def_arg(A) ::= sconst(B). {
    A = (Node *) makeString(B);
}

/* ----- alterTypeStmt ----- */

alterTypeStmt(A) ::= ALTER TYPE_P any_name(D) SET LPAREN operator_def_list(G) RPAREN. {
    alterTypeStmt *n = makeNode(alterTypeStmt);

    					n->typeName = D;
    					n->options = G;
    					A = (Node *) n;
}

/* ----- alterOwnerStmt ----- */

alterOwnerStmt(A) ::= ALTER AGGREGATE aggregate_with_argtypes(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_AGGREGATE;
    					n->object = (Node *) D;
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER COLLATION any_name(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_COLLATION;
    					n->object = (Node *) D;
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER CONVERSION_P any_name(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_CONVERSION;
    					n->object = (Node *) D;
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER DATABASE name(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_DATABASE;
    					n->object = (Node *) makeString(D);
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER DOMAIN_P any_name(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_DOMAIN;
    					n->object = (Node *) D;
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER FUNCTION function_with_argtypes(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_FUNCTION;
    					n->object = (Node *) D;
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER opt_procedural(C) LANGUAGE name(E) OWNER TO roleSpec(H). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_LANGUAGE;
    					n->object = (Node *) makeString(E);
    					n->newowner = H;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER LARGE_P OBJECT_P numericOnly(E) OWNER TO roleSpec(H). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_LARGEOBJECT;
    					n->object = (Node *) E;
    					n->newowner = H;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER OPERATOR operator_with_argtypes(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_OPERATOR;
    					n->object = (Node *) D;
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER OPERATOR CLASS any_name(E) USING name(G) OWNER TO roleSpec(J). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_OPCLASS;
    					n->object = (Node *) lcons(makeString(G), E);
    					n->newowner = J;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER OPERATOR FAMILY any_name(E) USING name(G) OWNER TO roleSpec(J). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_OPFAMILY;
    					n->object = (Node *) lcons(makeString(G), E);
    					n->newowner = J;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER PROCEDURE function_with_argtypes(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_PROCEDURE;
    					n->object = (Node *) D;
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) OWNER TO roleSpec(H). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_PROPGRAPH;
    					n->relation = E;
    					n->newowner = H;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER ROUTINE function_with_argtypes(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_ROUTINE;
    					n->object = (Node *) D;
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER SCHEMA name(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_SCHEMA;
    					n->object = (Node *) makeString(D);
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER TYPE_P any_name(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_TYPE;
    					n->object = (Node *) D;
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER TABLESPACE name(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_TABLESPACE;
    					n->object = (Node *) makeString(D);
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER STATISTICS any_name(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_STATISTIC_EXT;
    					n->object = (Node *) D;
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER TEXT_P SEARCH DICTIONARY any_name(F) OWNER TO roleSpec(I). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_TSDICTIONARY;
    					n->object = (Node *) F;
    					n->newowner = I;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER TEXT_P SEARCH CONFIGURATION any_name(F) OWNER TO roleSpec(I). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_TSCONFIGURATION;
    					n->object = (Node *) F;
    					n->newowner = I;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER FOREIGN DATA_P WRAPPER name(F) OWNER TO roleSpec(I). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_FDW;
    					n->object = (Node *) makeString(F);
    					n->newowner = I;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER SERVER name(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_FOREIGN_SERVER;
    					n->object = (Node *) makeString(D);
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER EVENT TRIGGER name(E) OWNER TO roleSpec(H). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_EVENT_TRIGGER;
    					n->object = (Node *) makeString(E);
    					n->newowner = H;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER PUBLICATION name(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_PUBLICATION;
    					n->object = (Node *) makeString(D);
    					n->newowner = G;
    					A = (Node *) n;
}

alterOwnerStmt(A) ::= ALTER SUBSCRIPTION name(D) OWNER TO roleSpec(G). {
    alterOwnerStmt *n = makeNode(alterOwnerStmt);

    					n->objectType = OBJECT_SUBSCRIPTION;
    					n->object = (Node *) makeString(D);
    					n->newowner = G;
    					A = (Node *) n;
}

/* ----- createPublicationStmt ----- */

createdbStmt(A) ::= CREATE DATABASE name(D) opt_with(E) createdb_opt_list(F). {
    createdbStmt *n = makeNode(createdbStmt);

    					n->dbname = D;
    					n->options = F;
    					A = (Node *) n;
}

/* ----- createdb_opt_list ----- */

createdb_opt_list(A) ::= createdb_opt_items(B). {
    A = B;
}

createdb_opt_list(A) ::= . {
    A = NIL;
}

/* ----- createdb_opt_items ----- */

createdb_opt_items(A) ::= createdb_opt_item(B). {
    A = list_make1(B);
}

createdb_opt_items(A) ::= createdb_opt_items(B) createdb_opt_item(C). {
    A = lappend(B, C);
}

/* ----- createdb_opt_item ----- */

createdb_opt_item(A) ::= createdb_opt_name(B) opt_equal(C) numericOnly(D). {
    A = makeDefElem(B, D, LOC(B));
}

createdb_opt_item(A) ::= createdb_opt_name(B) opt_equal(C) opt_boolean_or_string(D). {
    A = makeDefElem(B, (Node *) makeString(D), LOC(B));
}

createdb_opt_item(A) ::= createdb_opt_name(B) opt_equal(C) DEFAULT. {
    A = makeDefElem(B, NULL, LOC(B));
}

/* ----- createdb_opt_name ----- */

createdb_opt_name(A) ::= IDENT. {
    A = B;
}

createdb_opt_name(A) ::= CONNECTION LIMIT. {
    A = pstrdup("connection_limit");
}

createdb_opt_name(A) ::= ENCODING. {
    A = pstrdup(B);
}

createdb_opt_name(A) ::= LOCATION. {
    A = pstrdup(B);
}

createdb_opt_name(A) ::= OWNER. {
    A = pstrdup(B);
}

createdb_opt_name(A) ::= TABLESPACE. {
    A = pstrdup(B);
}

createdb_opt_name(A) ::= TEMPLATE. {
    A = pstrdup(B);
}

/* ----- opt_equal ----- */

opt_equal(A) ::= EQ.

/* ----- alterDatabaseStmt ----- */

alterDatabaseStmt(A) ::= ALTER DATABASE name(D) WITH createdb_opt_list(F). {
    alterDatabaseStmt *n = makeNode(alterDatabaseStmt);

    					n->dbname = D;
    					n->options = F;
    					A = (Node *) n;
}

alterDatabaseStmt(A) ::= ALTER DATABASE name(D) createdb_opt_list(E). {
    alterDatabaseStmt *n = makeNode(alterDatabaseStmt);

    					n->dbname = D;
    					n->options = E;
    					A = (Node *) n;
}

alterDatabaseStmt(A) ::= ALTER DATABASE name(D) SET TABLESPACE name(G). {
    alterDatabaseStmt *n = makeNode(alterDatabaseStmt);

    					n->dbname = D;
    					n->options = list_make1(makeDefElem("tablespace",
    														(Node *) makeString(G), LOC(G)));
    					A = (Node *) n;
}

alterDatabaseStmt(A) ::= ALTER DATABASE name(D) REFRESH COLLATION VERSION_P. {
    AlterDatabaseRefreshCollStmt *n = makeNode(AlterDatabaseRefreshCollStmt);

    					n->dbname = D;
    					A = (Node *) n;
}

/* ----- alterDatabaseSetStmt ----- */

alterDatabaseSetStmt(A) ::= ALTER DATABASE name(D) setResetClause(E). {
    alterDatabaseSetStmt *n = makeNode(alterDatabaseSetStmt);

    					n->dbname = D;
    					n->setstmt = E;
    					A = (Node *) n;
}

/* ----- dropdbStmt ----- */

alterCollationStmt(A) ::= ALTER COLLATION any_name(D) REFRESH VERSION_P. {
    alterCollationStmt *n = makeNode(alterCollationStmt);

    					n->collname = D;
    					A = (Node *) n;
}

/* ----- alterSystemStmt ----- */

alterSystemStmt(A) ::= ALTER SYSTEM_P SET generic_set(E). {
    alterSystemStmt *n = makeNode(alterSystemStmt);

    					n->setstmt = E;
    					A = (Node *) n;
}

alterSystemStmt(A) ::= ALTER SYSTEM_P RESET generic_reset(E). {
    alterSystemStmt *n = makeNode(alterSystemStmt);

    					n->setstmt = E;
    					A = (Node *) n;
}

/* ----- createDomainStmt ----- */

createDomainStmt(A) ::= CREATE DOMAIN_P any_name(D) opt_as(E) typename(F) colQualList(G). {
    createDomainStmt *n = makeNode(createDomainStmt);

    					n->domainname = D;
    					n->typeName = F;
    					SplitColQualList(G, &n->constraints, &n->collClause,
    									 yyscanner);
    					A = (Node *) n;
}

/* ----- alterDomainStmt ----- */

alterDomainStmt(A) ::= ALTER DOMAIN_P any_name(D) alter_column_default(E). {
    alterDomainStmt *n = makeNode(alterDomainStmt);

    					n->subtype = AD_AlterDefault;
    					n->typeName = D;
    					n->def = E;
    					A = (Node *) n;
}

alterDomainStmt(A) ::= ALTER DOMAIN_P any_name(D) DROP NOT NULL_P. {
    alterDomainStmt *n = makeNode(alterDomainStmt);

    					n->subtype = AD_DropNotNull;
    					n->typeName = D;
    					A = (Node *) n;
}

alterDomainStmt(A) ::= ALTER DOMAIN_P any_name(D) SET NOT NULL_P. {
    alterDomainStmt *n = makeNode(alterDomainStmt);

    					n->subtype = AD_SetNotNull;
    					n->typeName = D;
    					A = (Node *) n;
}

alterDomainStmt(A) ::= ALTER DOMAIN_P any_name(D) ADD_P domainConstraint(F). {
    alterDomainStmt *n = makeNode(alterDomainStmt);

    					n->subtype = AD_AddConstraint;
    					n->typeName = D;
    					n->def = F;
    					A = (Node *) n;
}

alterDomainStmt(A) ::= ALTER DOMAIN_P any_name(D) DROP CONSTRAINT name(G) opt_drop_behavior(H). {
    alterDomainStmt *n = makeNode(alterDomainStmt);

    					n->subtype = AD_DropConstraint;
    					n->typeName = D;
    					n->name = G;
    					n->behavior = H;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterDomainStmt(A) ::= ALTER DOMAIN_P any_name(D) DROP CONSTRAINT IF_P EXISTS name(I) opt_drop_behavior(J). {
    alterDomainStmt *n = makeNode(alterDomainStmt);

    					n->subtype = AD_DropConstraint;
    					n->typeName = D;
    					n->name = I;
    					n->behavior = J;
    					n->missing_ok = true;
    					A = (Node *) n;
}

alterDomainStmt(A) ::= ALTER DOMAIN_P any_name(D) VALIDATE CONSTRAINT name(G). {
    alterDomainStmt *n = makeNode(alterDomainStmt);

    					n->subtype = AD_ValidateConstraint;
    					n->typeName = D;
    					n->name = G;
    					A = (Node *) n;
}

/* ----- opt_as ----- */

opt_as(A) ::= AS.

/* ----- alterTSDictionaryStmt ----- */

alterTSDictionaryStmt(A) ::= ALTER TEXT_P SEARCH DICTIONARY any_name(F) definition(G). {
    alterTSDictionaryStmt *n = makeNode(alterTSDictionaryStmt);

    					n->dictname = F;
    					n->options = G;
    					A = (Node *) n;
}

/* ----- alterTSConfigurationStmt ----- */

alterTSConfigurationStmt(A) ::= ALTER TEXT_P SEARCH CONFIGURATION any_name(F) ADD_P MAPPING FOR name_list(J) any_with(K) any_name_list(L). {
    alterTSConfigurationStmt *n = makeNode(alterTSConfigurationStmt);

    					n->kind = ALTER_TSCONFIG_ADD_MAPPING;
    					n->cfgname = F;
    					n->tokentype = J;
    					n->dicts = L;
    					n->override = false;
    					n->replace = false;
    					A = (Node *) n;
}

alterTSConfigurationStmt(A) ::= ALTER TEXT_P SEARCH CONFIGURATION any_name(F) ALTER MAPPING FOR name_list(J) any_with(K) any_name_list(L). {
    alterTSConfigurationStmt *n = makeNode(alterTSConfigurationStmt);

    					n->kind = ALTER_TSCONFIG_ALTER_MAPPING_FOR_TOKEN;
    					n->cfgname = F;
    					n->tokentype = J;
    					n->dicts = L;
    					n->override = true;
    					n->replace = false;
    					A = (Node *) n;
}

alterTSConfigurationStmt(A) ::= ALTER TEXT_P SEARCH CONFIGURATION any_name(F) ALTER MAPPING REPLACE any_name(J) any_with(K) any_name(L). {
    alterTSConfigurationStmt *n = makeNode(alterTSConfigurationStmt);

    					n->kind = ALTER_TSCONFIG_REPLACE_DICT;
    					n->cfgname = F;
    					n->tokentype = NIL;
    					n->dicts = list_make2(J,L);
    					n->override = false;
    					n->replace = true;
    					A = (Node *) n;
}

alterTSConfigurationStmt(A) ::= ALTER TEXT_P SEARCH CONFIGURATION any_name(F) ALTER MAPPING FOR name_list(J) REPLACE any_name(L) any_with(M) any_name(N). {
    alterTSConfigurationStmt *n = makeNode(alterTSConfigurationStmt);

    					n->kind = ALTER_TSCONFIG_REPLACE_DICT_FOR_TOKEN;
    					n->cfgname = F;
    					n->tokentype = J;
    					n->dicts = list_make2(L,N);
    					n->override = false;
    					n->replace = true;
    					A = (Node *) n;
}

alterTSConfigurationStmt(A) ::= ALTER TEXT_P SEARCH CONFIGURATION any_name(F) DROP MAPPING FOR name_list(J). {
    alterTSConfigurationStmt *n = makeNode(alterTSConfigurationStmt);

    					n->kind = ALTER_TSCONFIG_DROP_MAPPING;
    					n->cfgname = F;
    					n->tokentype = J;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterTSConfigurationStmt(A) ::= ALTER TEXT_P SEARCH CONFIGURATION any_name(F) DROP MAPPING IF_P EXISTS FOR name_list(L). {
    alterTSConfigurationStmt *n = makeNode(alterTSConfigurationStmt);

    					n->kind = ALTER_TSCONFIG_DROP_MAPPING;
    					n->cfgname = F;
    					n->tokentype = L;
    					n->missing_ok = true;
    					A = (Node *) n;
}

/* ----- any_with ----- */

createConversionStmt(A) ::= CREATE opt_default(C) CONVERSION_P any_name(E) FOR sconst(G) TO sconst(I) FROM any_name(K). {
    createConversionStmt *n = makeNode(createConversionStmt);

    				n->conversion_name = E;
    				n->for_encoding_name = G;
    				n->to_encoding_name = I;
    				n->func_name = K;
    				n->def = C;
    				A = (Node *) n;
}

/* ----- repackStmt ----- */

repackStmt(A) ::= REPACK opt_utility_option_list(C) vacuum_relation(D) USING INDEX name(G). {
    repackStmt *n = makeNode(repackStmt);

    					n->command = REPACK_COMMAND_REPACK;
    					n->relation = (VacuumRelation *) D;
    					n->indexname = G;
    					n->usingindex = true;
    					n->params = C;
    					A = (Node *) n;
}

repackStmt(A) ::= REPACK opt_utility_option_list(C) vacuum_relation(D) opt_usingindex(E). {
    repackStmt *n = makeNode(repackStmt);

    					n->command = REPACK_COMMAND_REPACK;
    					n->relation = (VacuumRelation *) D;
    					n->indexname = NULL;
    					n->usingindex = E;
    					n->params = C;
    					A = (Node *) n;
}

repackStmt(A) ::= REPACK opt_utility_option_list(C) opt_usingindex(D). {
    repackStmt *n = makeNode(repackStmt);

    					n->command = REPACK_COMMAND_REPACK;
    					n->relation = NULL;
    					n->indexname = NULL;
    					n->usingindex = D;
    					n->params = C;
    					A = (Node *) n;
}

repackStmt(A) ::= CLUSTER LPAREN utility_option_list(D) RPAREN qualified_name(F) cluster_index_specification(G). {
    repackStmt *n = makeNode(repackStmt);

    					n->command = REPACK_COMMAND_CLUSTER;
    					n->relation = makeNode(VacuumRelation);
    					n->relation->relation = F;
    					n->indexname = G;
    					n->usingindex = true;
    					n->params = D;
    					A = (Node *) n;
}

repackStmt(A) ::= CLUSTER opt_utility_option_list(C). {
    repackStmt *n = makeNode(repackStmt);

    					n->command = REPACK_COMMAND_CLUSTER;
    					n->relation = NULL;
    					n->indexname = NULL;
    					n->usingindex = true;
    					n->params = C;
    					A = (Node *) n;
}

repackStmt(A) ::= CLUSTER opt_verbose(C) qualified_name(D) cluster_index_specification(E). {
    repackStmt *n = makeNode(repackStmt);

    					n->command = REPACK_COMMAND_CLUSTER;
    					n->relation = makeNode(VacuumRelation);
    					n->relation->relation = D;
    					n->indexname = E;
    					n->usingindex = true;
    					if (C)
    						n->params = list_make1(makeDefElem("verbose", NULL, LOC(C)));
    					A = (Node *) n;
}

repackStmt(A) ::= CLUSTER VERBOSE. {
    repackStmt *n = makeNode(repackStmt);

    					n->command = REPACK_COMMAND_CLUSTER;
    					n->relation = NULL;
    					n->indexname = NULL;
    					n->usingindex = true;
    					n->params = list_make1(makeDefElem("verbose", NULL, LOC(C)));
    					A = (Node *) n;
}

repackStmt(A) ::= CLUSTER opt_verbose(C) name(D) ON qualified_name(F). {
    repackStmt *n = makeNode(repackStmt);

    					n->command = REPACK_COMMAND_CLUSTER;
    					n->relation = makeNode(VacuumRelation);
    					n->relation->relation = F;
    					n->indexname = D;
    					n->usingindex = true;
    					if (C)
    						n->params = list_make1(makeDefElem("verbose", NULL, LOC(C)));
    					A = (Node *) n;
}

/* ----- cluster_index_specification ----- */

cluster_index_specification(A) ::= USING name(C). {
    A = C;
}

cluster_index_specification(A) ::= . {
    A = NULL;
}

/* ----- vacuumStmt ----- */
```

