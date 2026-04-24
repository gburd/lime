# Schema Operations


CREATE SCHEMA with optional schema element list and
schema-qualified statement types.


```yaml
name: pg-ddl-schema
version: 17.0.0
description: CREATE SCHEMA with contained objects
provides: [pg-ddl-schema]
depends: [pg-type-decls, pg-base-helpers]
```

## Production Rules

```lime rules
createSchemaStmt(A) ::= CREATE SCHEMA opt_single_name(D) AUTHORIZATION roleSpec(F) optSchemaEltList(G). {
    createSchemaStmt *n = makeNode(createSchemaStmt);


    					n->schemaname = D;
    					n->authrole = F;
    					n->schemaElts = G;
    					n->if_not_exists = false;
    					A = (Node *) n;
}

createSchemaStmt(A) ::= CREATE SCHEMA colId(D) optSchemaEltList(E). {
    createSchemaStmt *n = makeNode(createSchemaStmt);


    					n->schemaname = D;
    					n->authrole = NULL;
    					n->schemaElts = E;
    					n->if_not_exists = false;
    					A = (Node *) n;
}

createSchemaStmt(A) ::= CREATE SCHEMA IF_P NOT EXISTS opt_single_name(G) AUTHORIZATION roleSpec(I) optSchemaEltList(J). {
    createSchemaStmt *n = makeNode(createSchemaStmt);


    					n->schemaname = G;
    					n->authrole = I;
    					if (J != NIL)
    						ereport(ERROR,
    								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								 errmsg("CREATE SCHEMA IF NOT EXISTS cannot include schema elements"),
    								 parser_errposition(LOC(J))));
    					n->schemaElts = J;
    					n->if_not_exists = true;
    					A = (Node *) n;
}

createSchemaStmt(A) ::= CREATE SCHEMA IF_P NOT EXISTS colId(G) optSchemaEltList(H). {
    createSchemaStmt *n = makeNode(createSchemaStmt);


    					n->schemaname = G;
    					n->authrole = NULL;
    					if (H != NIL)
    						ereport(ERROR,
    								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
    								 errmsg("CREATE SCHEMA IF NOT EXISTS cannot include schema elements"),
    								 parser_errposition(LOC(H))));
    					n->schemaElts = H;
    					n->if_not_exists = true;
    					A = (Node *) n;
}

/* ----- optSchemaEltList ----- */

optSchemaEltList(A) ::= optSchemaEltList(B) schema_stmt(C). {
    A = lappend(B, C);
}

optSchemaEltList(A) ::= . {
    A = NIL;
}

/* ----- schema_stmt ----- */

schema_stmt(A) ::= createStmt(B).

schema_stmt(A) ::= indexStmt(B).

schema_stmt(A) ::= createSeqStmt(B).

schema_stmt(A) ::= createTrigStmt(B).

schema_stmt(A) ::= grantStmt(B).

schema_stmt(A) ::= viewStmt(B).

/* ----- variableSetStmt ----- */
```

