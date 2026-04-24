# Publications and Subscriptions


Logical replication support: CREATE/ALTER PUBLICATION,
CREATE/ALTER/DROP SUBSCRIPTION, and publication object
specifications.


```yaml
name: pg-pub-sub
version: 17.0.0
description: CREATE/ALTER PUBLICATION and SUBSCRIPTION
provides: [pg-pub-sub]
depends: [pg-type-decls, pg-base-helpers]
```

## Production Rules

```lime rules
createPublicationStmt(A) ::= CREATE PUBLICATION name(D) opt_definition(E). {
    createPublicationStmt *n = makeNode(createPublicationStmt);

    					n->pubname = D;
    					n->options = E;
    					A = (Node *) n;
}

createPublicationStmt(A) ::= CREATE PUBLICATION name(D) FOR pub_all_obj_type_list(F) opt_definition(G). {
    createPublicationStmt *n = makeNode(createPublicationStmt);

    					n->pubname = D;
    					preprocess_pub_all_objtype_list(F, &n->pubobjects,
    													&n->for_all_tables,
    													&n->for_all_sequences,
    													yyscanner);
    					n->options = G;
    					A = (Node *) n;
}

createPublicationStmt(A) ::= CREATE PUBLICATION name(D) FOR pub_obj_list(F) opt_definition(G). {
    createPublicationStmt *n = makeNode(createPublicationStmt);

    					n->pubname = D;
    					n->options = G;
    					n->pubobjects = (List *) F;
    					preprocess_pubobj_list(n->pubobjects, yyscanner);
    					A = (Node *) n;
}

/* ----- publicationObjSpec ----- */

publicationObjSpec(A) ::= TABLE relation_expr(C) opt_column_list(D) optWhereClause(E). {
    A = makeNode(publicationObjSpec);
    					A->pubobjtype = PUBLICATIONOBJ_TABLE;
    					A->pubtable = makeNode(PublicationTable);
    					A->pubtable->relation = C;
    					A->pubtable->columns = D;
    					A->pubtable->whereClause = E;
}

publicationObjSpec(A) ::= TABLES IN_P SCHEMA colId(E). {
    A = makeNode(publicationObjSpec);
    					A->pubobjtype = PUBLICATIONOBJ_TABLES_IN_SCHEMA;
    					A->name = E;
    					A->location = LOC(E);
}

publicationObjSpec(A) ::= TABLES IN_P SCHEMA CURRENT_SCHEMA. {
    A = makeNode(publicationObjSpec);
    					A->pubobjtype = PUBLICATIONOBJ_TABLES_IN_CUR_SCHEMA;
    					A->location = LOC(E);
}

publicationObjSpec(A) ::= colId(B) opt_column_list(C) optWhereClause(D). {
    A = makeNode(publicationObjSpec);
    					A->pubobjtype = PUBLICATIONOBJ_CONTINUATION;




    					if (C || D)
    					{






    						A->pubtable = makeNode(PublicationTable);
    						A->pubtable->relation = makeRangeVar(NULL, B, LOC(B));
    						A->pubtable->columns = C;
    						A->pubtable->whereClause = D;
    					}
    					else
    					{
    						A->name = B;
    					}
    					A->location = LOC(B);
}

publicationObjSpec(A) ::= colId(B) indirection(C) opt_column_list(D) optWhereClause(E). {
    A = makeNode(publicationObjSpec);
    					A->pubobjtype = PUBLICATIONOBJ_CONTINUATION;
    					A->pubtable = makeNode(PublicationTable);
    					A->pubtable->relation = makeRangeVarFromQualifiedName(B, C, LOC(B), yyscanner);
    					A->pubtable->columns = D;
    					A->pubtable->whereClause = E;
    					A->location = LOC(B);
}

publicationObjSpec(A) ::= extended_relation_expr(B) opt_column_list(C) optWhereClause(D). {
    A = makeNode(publicationObjSpec);
    					A->pubobjtype = PUBLICATIONOBJ_CONTINUATION;
    					A->pubtable = makeNode(PublicationTable);
    					A->pubtable->relation = B;
    					A->pubtable->columns = C;
    					A->pubtable->whereClause = D;
}

publicationObjSpec(A) ::= CURRENT_SCHEMA. {
    A = makeNode(publicationObjSpec);
    					A->pubobjtype = PUBLICATIONOBJ_CONTINUATION;
    					A->location = LOC(B);
}

/* ----- pub_obj_list ----- */

pub_obj_list(A) ::= publicationObjSpec(B). {
    A = list_make1(B);
}

pub_obj_list(A) ::= pub_obj_list(B) COMMA publicationObjSpec(D). {
    A = lappend(B, D);
}

/* ----- opt_pub_except_clause ----- */

opt_pub_except_clause(A) ::= EXCEPT LPAREN TABLE pub_except_obj_list(E) RPAREN. {
    A = E;
}

opt_pub_except_clause(A) ::= . {
    A = NIL;
}

/* ----- publicationAllObjSpec ----- */

publicationAllObjSpec(A) ::= ALL TABLES opt_pub_except_clause(D). {
    A = makeNode(publicationAllObjSpec);
    						A->pubobjtype = PUBLICATION_ALL_TABLES;
    						A->except_tables = D;
    						A->location = LOC(B);
}

publicationAllObjSpec(A) ::= ALL SEQUENCES. {
    A = makeNode(publicationAllObjSpec);
    						A->pubobjtype = PUBLICATION_ALL_SEQUENCES;
    						A->location = LOC(B);
}

/* ----- pub_all_obj_type_list ----- */

pub_all_obj_type_list(A) ::= publicationAllObjSpec(B). {
    A = list_make1(B);
}

pub_all_obj_type_list(A) ::= pub_all_obj_type_list(B) COMMA publicationAllObjSpec(D). {
    A = lappend(B, D);
}

/* ----- publicationExceptObjSpec ----- */

publicationExceptObjSpec(A) ::= relation_expr(B). {
    A = makeNode(publicationObjSpec);
    					A->pubobjtype = PUBLICATIONOBJ_EXCEPT_TABLE;
    					A->pubtable = makeNode(PublicationTable);
    					A->pubtable->except = true;
    					A->pubtable->relation = B;
    					A->location = LOC(B);
}

/* ----- pub_except_obj_list ----- */

pub_except_obj_list(A) ::= publicationExceptObjSpec(B). {
    A = list_make1(B);
}

pub_except_obj_list(A) ::= pub_except_obj_list(B) COMMA opt_table(D) publicationExceptObjSpec(E). {
    A = lappend(B, E);
}

/* ----- alterPublicationStmt ----- */

alterPublicationStmt(A) ::= ALTER PUBLICATION name(D) SET definition(F). {
    alterPublicationStmt *n = makeNode(alterPublicationStmt);

    					n->pubname = D;
    					n->options = F;
    					n->for_all_tables = false;
    					A = (Node *) n;
}

alterPublicationStmt(A) ::= ALTER PUBLICATION name(D) ADD_P pub_obj_list(F). {
    alterPublicationStmt *n = makeNode(alterPublicationStmt);

    					n->pubname = D;
    					n->pubobjects = F;
    					preprocess_pubobj_list(n->pubobjects, yyscanner);
    					n->action = AP_AddObjects;
    					n->for_all_tables = false;
    					A = (Node *) n;
}

alterPublicationStmt(A) ::= ALTER PUBLICATION name(D) SET pub_obj_list(F). {
    alterPublicationStmt *n = makeNode(alterPublicationStmt);

    					n->pubname = D;
    					n->pubobjects = F;
    					preprocess_pubobj_list(n->pubobjects, yyscanner);
    					n->action = AP_SetObjects;
    					n->for_all_tables = false;
    					A = (Node *) n;
}

alterPublicationStmt(A) ::= ALTER PUBLICATION name(D) SET pub_all_obj_type_list(F). {
    alterPublicationStmt *n = makeNode(alterPublicationStmt);

    					n->pubname = D;
    					n->action = AP_SetObjects;
    					preprocess_pub_all_objtype_list(F, &n->pubobjects,
    													&n->for_all_tables,
    													&n->for_all_sequences,
    													yyscanner);
    					A = (Node *) n;
}

alterPublicationStmt(A) ::= ALTER PUBLICATION name(D) DROP pub_obj_list(F). {
    alterPublicationStmt *n = makeNode(alterPublicationStmt);

    					n->pubname = D;
    					n->pubobjects = F;
    					preprocess_pubobj_list(n->pubobjects, yyscanner);
    					n->action = AP_DropObjects;
    					n->for_all_tables = false;
    					A = (Node *) n;
}

/* ----- createSubscriptionStmt ----- */

createSubscriptionStmt(A) ::= CREATE SUBSCRIPTION name(D) CONNECTION sconst(F) PUBLICATION name_list(H) opt_definition(I). {
    createSubscriptionStmt *n =
    						makeNode(createSubscriptionStmt);
    					n->subname = D;
    					n->conninfo = F;
    					n->publication = H;
    					n->options = I;
    					A = (Node *) n;
}

createSubscriptionStmt(A) ::= CREATE SUBSCRIPTION name(D) SERVER name(F) PUBLICATION name_list(H) opt_definition(I). {
    createSubscriptionStmt *n =
    						makeNode(createSubscriptionStmt);
    					n->subname = D;
    					n->servername = F;
    					n->publication = H;
    					n->options = I;
    					A = (Node *) n;
}

/* ----- alterSubscriptionStmt ----- */

alterSubscriptionStmt(A) ::= ALTER SUBSCRIPTION name(D) SET definition(F). {
    alterSubscriptionStmt *n =
    						makeNode(alterSubscriptionStmt);

    					n->kind = ALTER_SUBSCRIPTION_OPTIONS;
    					n->subname = D;
    					n->options = F;
    					A = (Node *) n;
}

alterSubscriptionStmt(A) ::= ALTER SUBSCRIPTION name(D) CONNECTION sconst(F). {
    alterSubscriptionStmt *n =
    						makeNode(alterSubscriptionStmt);

    					n->kind = ALTER_SUBSCRIPTION_CONNECTION;
    					n->subname = D;
    					n->conninfo = F;
    					A = (Node *) n;
}

alterSubscriptionStmt(A) ::= ALTER SUBSCRIPTION name(D) SERVER name(F). {
    alterSubscriptionStmt *n =
    						makeNode(alterSubscriptionStmt);

    					n->kind = ALTER_SUBSCRIPTION_SERVER;
    					n->subname = D;
    					n->servername = F;
    					A = (Node *) n;
}

alterSubscriptionStmt(A) ::= ALTER SUBSCRIPTION name(D) REFRESH PUBLICATION opt_definition(G). {
    alterSubscriptionStmt *n =
    						makeNode(alterSubscriptionStmt);

    					n->kind = ALTER_SUBSCRIPTION_REFRESH_PUBLICATION;
    					n->subname = D;
    					n->options = G;
    					A = (Node *) n;
}

alterSubscriptionStmt(A) ::= ALTER SUBSCRIPTION name(D) REFRESH SEQUENCES. {
    alterSubscriptionStmt *n =
    						makeNode(alterSubscriptionStmt);

    					n->kind = ALTER_SUBSCRIPTION_REFRESH_SEQUENCES;
    					n->subname = D;
    					A = (Node *) n;
}

alterSubscriptionStmt(A) ::= ALTER SUBSCRIPTION name(D) ADD_P PUBLICATION name_list(G) opt_definition(H). {
    alterSubscriptionStmt *n =
    						makeNode(alterSubscriptionStmt);

    					n->kind = ALTER_SUBSCRIPTION_ADD_PUBLICATION;
    					n->subname = D;
    					n->publication = G;
    					n->options = H;
    					A = (Node *) n;
}

alterSubscriptionStmt(A) ::= ALTER SUBSCRIPTION name(D) DROP PUBLICATION name_list(G) opt_definition(H). {
    alterSubscriptionStmt *n =
    						makeNode(alterSubscriptionStmt);

    					n->kind = ALTER_SUBSCRIPTION_DROP_PUBLICATION;
    					n->subname = D;
    					n->publication = G;
    					n->options = H;
    					A = (Node *) n;
}

alterSubscriptionStmt(A) ::= ALTER SUBSCRIPTION name(D) SET PUBLICATION name_list(G) opt_definition(H). {
    alterSubscriptionStmt *n =
    						makeNode(alterSubscriptionStmt);

    					n->kind = ALTER_SUBSCRIPTION_SET_PUBLICATION;
    					n->subname = D;
    					n->publication = G;
    					n->options = H;
    					A = (Node *) n;
}

alterSubscriptionStmt(A) ::= ALTER SUBSCRIPTION name(D) ENABLE_P. {
    alterSubscriptionStmt *n =
    						makeNode(alterSubscriptionStmt);

    					n->kind = ALTER_SUBSCRIPTION_ENABLED;
    					n->subname = D;
    					n->options = list_make1(makeDefElem("enabled",
    											(Node *) makeBoolean(true), LOC(B)));
    					A = (Node *) n;
}

alterSubscriptionStmt(A) ::= ALTER SUBSCRIPTION name(D) DISABLE_P. {
    alterSubscriptionStmt *n =
    						makeNode(alterSubscriptionStmt);

    					n->kind = ALTER_SUBSCRIPTION_ENABLED;
    					n->subname = D;
    					n->options = list_make1(makeDefElem("enabled",
    											(Node *) makeBoolean(false), LOC(B)));
    					A = (Node *) n;
}

alterSubscriptionStmt(A) ::= ALTER SUBSCRIPTION name(D) SKIP definition(F). {
    alterSubscriptionStmt *n =
    						makeNode(alterSubscriptionStmt);

    					n->kind = ALTER_SUBSCRIPTION_SKIP;
    					n->subname = D;
    					n->options = F;
    					A = (Node *) n;
}

/* ----- dropSubscriptionStmt ----- */
```

