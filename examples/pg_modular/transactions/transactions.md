# Transaction Control


Transaction statements: BEGIN, COMMIT, ROLLBACK, SAVEPOINT,
RELEASE, SET CONSTRAINTS, CHECKPOINT, and LOCK TABLE.
Includes transaction isolation mode options.


```yaml
name: pg-transactions
version: 17.0.0
description: BEGIN, COMMIT, ROLLBACK, SAVEPOINT, LOCK, constraints
provides: [pg-transactions]
depends: [pg-type-decls, pg-base-helpers]
```

## Production Rules

```lime rules
constraintsSetStmt(A) ::= SET CONSTRAINTS constraints_set_list(D) constraints_set_mode(E). {
    constraintsSetStmt *n = makeNode(constraintsSetStmt);

    					n->constraints = D;
    					n->deferred = E;
    					A = (Node *) n;
}

/* ----- constraints_set_list ----- */

constraints_set_list(A) ::= ALL. {
    A = NIL;
}

constraints_set_list(A) ::= qualified_name_list(B). {
    A = B;
}

/* ----- constraints_set_mode ----- */

constraints_set_mode(A) ::= DEFERRED. {
    A = true;
}

constraints_set_mode(A) ::= IMMEDIATE. {
    A = false;
}

/* ----- checkPointStmt ----- */

checkPointStmt(A) ::= CHECKPOINT opt_utility_option_list(C). {
    checkPointStmt *n = makeNode(checkPointStmt);

    					A = (Node *) n;
    					n->options = C;
}

/* ----- discardStmt ----- */

transactionStmt(A) ::= ABORT_P opt_transaction(C) opt_transaction_chain(D). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_ROLLBACK;
    					n->options = NIL;
    					n->chain = D;
    					n->location = -1;
    					A = (Node *) n;
}

transactionStmt(A) ::= START TRANSACTION transaction_mode_list_or_empty(D). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_START;
    					n->options = D;
    					n->location = -1;
    					A = (Node *) n;
}

transactionStmt(A) ::= COMMIT opt_transaction(C) opt_transaction_chain(D). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_COMMIT;
    					n->options = NIL;
    					n->chain = D;
    					n->location = -1;
    					A = (Node *) n;
}

transactionStmt(A) ::= ROLLBACK opt_transaction(C) opt_transaction_chain(D). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_ROLLBACK;
    					n->options = NIL;
    					n->chain = D;
    					n->location = -1;
    					A = (Node *) n;
}

transactionStmt(A) ::= SAVEPOINT colId(C). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_SAVEPOINT;
    					n->savepoint_name = C;
    					n->location = LOC(C);
    					A = (Node *) n;
}

transactionStmt(A) ::= RELEASE SAVEPOINT colId(D). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_RELEASE;
    					n->savepoint_name = D;
    					n->location = LOC(D);
    					A = (Node *) n;
}

transactionStmt(A) ::= RELEASE colId(C). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_RELEASE;
    					n->savepoint_name = C;
    					n->location = LOC(C);
    					A = (Node *) n;
}

transactionStmt(A) ::= ROLLBACK opt_transaction(C) TO SAVEPOINT colId(F). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_ROLLBACK_TO;
    					n->savepoint_name = F;
    					n->location = LOC(F);
    					A = (Node *) n;
}

transactionStmt(A) ::= ROLLBACK opt_transaction(C) TO colId(E). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_ROLLBACK_TO;
    					n->savepoint_name = E;
    					n->location = LOC(E);
    					A = (Node *) n;
}

transactionStmt(A) ::= PREPARE TRANSACTION sconst(D). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_PREPARE;
    					n->gid = D;
    					n->location = LOC(D);
    					A = (Node *) n;
}

transactionStmt(A) ::= COMMIT PREPARED sconst(D). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_COMMIT_PREPARED;
    					n->gid = D;
    					n->location = LOC(D);
    					A = (Node *) n;
}

transactionStmt(A) ::= ROLLBACK PREPARED sconst(D). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_ROLLBACK_PREPARED;
    					n->gid = D;
    					n->location = LOC(D);
    					A = (Node *) n;
}

/* ----- transactionStmtLegacy ----- */

transactionStmtLegacy(A) ::= BEGIN_P opt_transaction(C) transaction_mode_list_or_empty(D). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_BEGIN;
    					n->options = D;
    					n->location = -1;
    					A = (Node *) n;
}

transactionStmtLegacy(A) ::= END_P opt_transaction(C) opt_transaction_chain(D). {
    transactionStmt *n = makeNode(transactionStmt);

    					n->kind = TRANS_STMT_COMMIT;
    					n->options = NIL;
    					n->chain = D;
    					n->location = -1;
    					A = (Node *) n;
}

/* ----- opt_transaction ----- */

opt_transaction(A) ::= WORK.

opt_transaction(A) ::= TRANSACTION.

/* ----- transaction_mode_item ----- */

transaction_mode_item(A) ::= ISOLATION LEVEL iso_level(D). {
    A = makeDefElem("transaction_isolation",
    									   makeStringConst(D, LOC(D)), LOC(B));
}

transaction_mode_item(A) ::= READ ONLY. {
    A = makeDefElem("transaction_read_only",
    									   makeIntConst(true, LOC(B)), LOC(B));
}

transaction_mode_item(A) ::= READ WRITE. {
    A = makeDefElem("transaction_read_only",
    									   makeIntConst(false, LOC(B)), LOC(B));
}

transaction_mode_item(A) ::= DEFERRABLE. {
    A = makeDefElem("transaction_deferrable",
    									   makeIntConst(true, LOC(B)), LOC(B));
}

transaction_mode_item(A) ::= NOT DEFERRABLE. {
    A = makeDefElem("transaction_deferrable",
    									   makeIntConst(false, LOC(B)), LOC(B));
}

/* ----- transaction_mode_list ----- */

transaction_mode_list(A) ::= transaction_mode_item(B). {
    A = list_make1(B);
}

transaction_mode_list(A) ::= transaction_mode_list(B) COMMA transaction_mode_item(D). {
    A = lappend(B, D);
}

transaction_mode_list(A) ::= transaction_mode_list(B) transaction_mode_item(C). {
    A = lappend(B, C);
}

/* ----- transaction_mode_list_or_empty ----- */

transaction_mode_list_or_empty(A) ::= transaction_mode_list(B).

transaction_mode_list_or_empty(A) ::= . {
    A = NIL;
}

/* ----- opt_transaction_chain ----- */

opt_transaction_chain(A) ::= AND CHAIN. {
    A = true;
}

opt_transaction_chain(A) ::= AND NO CHAIN. {
    A = false;
}

opt_transaction_chain(A) ::= . {
    A = false;
}

/* ----- viewStmt ----- */

lockStmt(A) ::= LOCK_P opt_table(C) relation_expr_list(D) opt_lock(E) opt_nowait(F). {
    lockStmt   *n = makeNode(lockStmt);

    					n->relations = D;
    					n->mode = E;
    					n->nowait = F;
    					A = (Node *) n;
}

/* ----- opt_lock ----- */

opt_lock(A) ::= IN_P lock_type(C) MODE. {
    A = C;
}

opt_lock(A) ::= . {
    A = AccessExclusiveLock;
}

/* ----- lock_type ----- */

lock_type(A) ::= ACCESS SHARE. {
    A = AccessShareLock;
}

lock_type(A) ::= ROW SHARE. {
    A = RowShareLock;
}

lock_type(A) ::= ROW EXCLUSIVE. {
    A = RowExclusiveLock;
}

lock_type(A) ::= SHARE UPDATE EXCLUSIVE. {
    A = ShareUpdateExclusiveLock;
}

lock_type(A) ::= SHARE. {
    A = ShareLock;
}

lock_type(A) ::= SHARE ROW EXCLUSIVE. {
    A = ShareRowExclusiveLock;
}

lock_type(A) ::= EXCLUSIVE. {
    A = ExclusiveLock;
}

lock_type(A) ::= ACCESS EXCLUSIVE. {
    A = AccessExclusiveLock;
}

/* ----- opt_nowait ----- */

opt_nowait(A) ::= NOWAIT. {
    A = true;
}

opt_nowait(A) ::= . {
    A = false;
}

/* ----- opt_nowait_or_skip ----- */

opt_nowait_or_skip(A) ::= NOWAIT. {
    A = LockWaitError;
}

opt_nowait_or_skip(A) ::= SKIP LOCKED. {
    A = LockWaitSkip;
}

opt_nowait_or_skip(A) ::= . {
    A = LockWaitBlock;
}

/* ----- updateStmt ----- */
```

