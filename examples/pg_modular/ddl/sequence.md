# Sequence Operations


CREATE SEQUENCE and ALTER SEQUENCE grammar with all
sequence option elements (INCREMENT, MINVALUE, MAXVALUE,
START, CACHE, CYCLE, OWNED BY).


```yaml
name: pg-ddl-sequence
version: 17.0.0
description: CREATE/ALTER SEQUENCE
provides: [pg-ddl-sequence]
depends: [pg-type-decls, pg-base-helpers]
```

## Production Rules

```lime rules
createSeqStmt(A) ::= CREATE optTemp(C) SEQUENCE qualified_name(E) optSeqOptList(F). {
    createSeqStmt *n = makeNode(createSeqStmt);

    					E->relpersistence = C;
    					n->sequence = E;
    					n->options = F;
    					n->ownerId = InvalidOid;
    					n->if_not_exists = false;
    					A = (Node *) n;
}

createSeqStmt(A) ::= CREATE optTemp(C) SEQUENCE IF_P NOT EXISTS qualified_name(H) optSeqOptList(I). {
    createSeqStmt *n = makeNode(createSeqStmt);

    					H->relpersistence = C;
    					n->sequence = H;
    					n->options = I;
    					n->ownerId = InvalidOid;
    					n->if_not_exists = true;
    					A = (Node *) n;
}

/* ----- alterSeqStmt ----- */

alterSeqStmt(A) ::= ALTER SEQUENCE qualified_name(D) seqOptList(E). {
    alterSeqStmt *n = makeNode(alterSeqStmt);

    					n->sequence = D;
    					n->options = E;
    					n->missing_ok = false;
    					A = (Node *) n;
}

alterSeqStmt(A) ::= ALTER SEQUENCE IF_P EXISTS qualified_name(F) seqOptList(G). {
    alterSeqStmt *n = makeNode(alterSeqStmt);

    					n->sequence = F;
    					n->options = G;
    					n->missing_ok = true;
    					A = (Node *) n;
}

/* ----- optSeqOptList ----- */

optSeqOptList(A) ::= seqOptList(B). {
    A = B;
}

optSeqOptList(A) ::= . {
    A = NIL;
}

/* ----- optParenthesizedSeqOptList ----- */

optParenthesizedSeqOptList(A) ::= LPAREN seqOptList(C) RPAREN. {
    A = C;
}

optParenthesizedSeqOptList(A) ::= . {
    A = NIL;
}

/* ----- seqOptList ----- */

seqOptList(A) ::= seqOptElem(B). {
    A = list_make1(B);
}

seqOptList(A) ::= seqOptList(B) seqOptElem(C). {
    A = lappend(B, C);
}

/* ----- seqOptElem ----- */

seqOptElem(A) ::= AS simpleTypename(C). {
    A = makeDefElem("as", (Node *) C, LOC(B));
}

seqOptElem(A) ::= CACHE numericOnly(C). {
    A = makeDefElem("cache", (Node *) C, LOC(B));
}

seqOptElem(A) ::= CYCLE. {
    A = makeDefElem("cycle", (Node *) makeBoolean(true), LOC(B));
}

seqOptElem(A) ::= NO CYCLE. {
    A = makeDefElem("cycle", (Node *) makeBoolean(false), LOC(B));
}

seqOptElem(A) ::= INCREMENT opt_by(C) numericOnly(D). {
    A = makeDefElem("increment", (Node *) D, LOC(B));
}

seqOptElem(A) ::= LOGGED. {
    A = makeDefElem("logged", NULL, LOC(B));
}

seqOptElem(A) ::= MAXVALUE numericOnly(C). {
    A = makeDefElem("maxvalue", (Node *) C, LOC(B));
}

seqOptElem(A) ::= MINVALUE numericOnly(C). {
    A = makeDefElem("minvalue", (Node *) C, LOC(B));
}

seqOptElem(A) ::= NO MAXVALUE. {
    A = makeDefElem("maxvalue", NULL, LOC(B));
}

seqOptElem(A) ::= NO MINVALUE. {
    A = makeDefElem("minvalue", NULL, LOC(B));
}

seqOptElem(A) ::= OWNED BY any_name(D). {
    A = makeDefElem("owned_by", (Node *) D, LOC(B));
}

seqOptElem(A) ::= SEQUENCE NAME_P any_name(D). {
    A = makeDefElem("sequence_name", (Node *) D, LOC(B));
}

seqOptElem(A) ::= START opt_with(C) numericOnly(D). {
    A = makeDefElem("start", (Node *) D, LOC(B));
}

seqOptElem(A) ::= RESTART. {
    A = makeDefElem("restart", NULL, LOC(B));
}

seqOptElem(A) ::= RESTART opt_with(C) numericOnly(D). {
    A = makeDefElem("restart", (Node *) D, LOC(B));
}

seqOptElem(A) ::= UNLOGGED. {
    A = makeDefElem("unlogged", NULL, LOC(B));
}

/* ----- opt_by ----- */

opt_by(A) ::= BY.

/* ----- numericOnly ----- */

numericOnly(A) ::= FCONST. {
    A = (Node *) makeFloat(B);
}

numericOnly(A) ::= PLUS FCONST. {
    A = (Node *) makeFloat(C);
}

numericOnly(A) ::= MINUS FCONST. {
    Float	   *f = makeFloat(C);

    					doNegateFloat(f);
    					A = (Node *) f;
}

numericOnly(A) ::= signedIconst(B). {
    A = (Node *) makeInteger(B);
}

/* ----- numericOnly_list ----- */

numericOnly_list(A) ::= numericOnly(B). {
    A = list_make1(B);
}

numericOnly_list(A) ::= numericOnly_list(B) COMMA numericOnly(D). {
    A = lappend(B, D);
}

/* ----- createPLangStmt ----- */
```

