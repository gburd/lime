# FROM Clause


FROM clause parsing: table references, JOINs (INNER, LEFT,
RIGHT, FULL, CROSS, NATURAL), aliases, TABLESAMPLE, and
function-in-FROM.


```yaml
name: pg-from
version: 17.0.0
description: FROM clause, JOINs, table references, and TABLESAMPLE
provides: [pg-from-clause]
depends: [pg-type-decls, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
from_in(A) ::= FROM.

from_in(A) ::= IN_P.

/* ----- opt_from_in ----- */

opt_from_in(A) ::= from_in(B).

/* ----- grantStmt ----- */

from_clause(A) ::= FROM from_list(C). {
    A = C;
}

from_clause(A) ::= . {
    A = NIL;
}

/* ----- from_list ----- */

from_list(A) ::= table_ref(B). {
    A = list_make1(B);
}

from_list(A) ::= from_list(B) COMMA table_ref(D). {
    A = lappend(B, D);
}

/* ----- table_ref ----- */

table_ref(A) ::= relation_expr(B) opt_alias_clause(C). {
    B->alias = C;
    					A = (Node *) B;
}

table_ref(A) ::= relation_expr(B) opt_alias_clause(C) tablesample_clause(D). {
    RangeTableSample *n = (RangeTableSample *) D;

    					B->alias = C;

    					n->relation = (Node *) B;
    					A = (Node *) n;
}

table_ref(A) ::= func_table(B) func_alias_clause(C). {
    RangeFunction *n = (RangeFunction *) B;

    					n->alias = linitial(C);
    					n->coldeflist = lsecond(C);
    					A = (Node *) n;
}

table_ref(A) ::= LATERAL_P func_table(C) func_alias_clause(D). {
    RangeFunction *n = (RangeFunction *) C;

    					n->lateral = true;
    					n->alias = linitial(D);
    					n->coldeflist = lsecond(D);
    					A = (Node *) n;
}

table_ref(A) ::= xmltable(B) opt_alias_clause(C). {
    RangeTableFunc *n = (RangeTableFunc *) B;

    					n->alias = C;
    					A = (Node *) n;
}

table_ref(A) ::= LATERAL_P xmltable(C) opt_alias_clause(D). {
    RangeTableFunc *n = (RangeTableFunc *) C;

    					n->lateral = true;
    					n->alias = D;
    					A = (Node *) n;
}

table_ref(A) ::= GRAPH_TABLE LPAREN qualified_name(D) MATCH graph_pattern(F) COLUMNS LPAREN labeled_expr_list(I) RPAREN RPAREN opt_alias_clause(L). {
    RangeGraphTable *n = makeNode(RangeGraphTable);

    					n->graph_name = D;
    					n->graph_pattern = castNode(GraphPattern, F);
    					n->columns = I;
    					n->alias = L;
    					n->location = LOC(B);
    					A = (Node *) n;
}

table_ref(A) ::= select_with_parens(B) opt_alias_clause(C). {
    RangeSubselect *n = makeNode(RangeSubselect);

    					n->lateral = false;
    					n->subquery = B;
    					n->alias = C;
    					A = (Node *) n;
}

table_ref(A) ::= LATERAL_P select_with_parens(C) opt_alias_clause(D). {
    RangeSubselect *n = makeNode(RangeSubselect);

    					n->lateral = true;
    					n->subquery = C;
    					n->alias = D;
    					A = (Node *) n;
}

table_ref(A) ::= joined_table(B). {
    A = (Node *) B;
}

table_ref(A) ::= LPAREN joined_table(C) RPAREN alias_clause(E). {
    C->alias = E;
    					A = (Node *) C;
}

table_ref(A) ::= json_table(B) opt_alias_clause(C). {
    JsonTable  *jt = castNode(JsonTable, B);

    					jt->alias = C;
    					A = (Node *) jt;
}

table_ref(A) ::= LATERAL_P json_table(C) opt_alias_clause(D). {
    JsonTable  *jt = castNode(JsonTable, C);

    					jt->alias = D;
    					jt->lateral = true;
    					A = (Node *) jt;
}

/* ----- joined_table ----- */

joined_table(A) ::= LPAREN joined_table(C) RPAREN. {
    A = C;
}

joined_table(A) ::= table_ref(B) CROSS JOIN table_ref(E). {
    JoinExpr   *n = makeNode(JoinExpr);

    					n->jointype = JOIN_INNER;
    					n->isNatural = false;
    					n->larg = B;
    					n->rarg = E;
    					n->usingClause = NIL;
    					n->join_using_alias = NULL;
    					n->quals = NULL;
    					A = n;
}

joined_table(A) ::= table_ref(B) join_type(C) JOIN table_ref(E) join_qual(F). {
    JoinExpr   *n = makeNode(JoinExpr);

    					n->jointype = C;
    					n->isNatural = false;
    					n->larg = B;
    					n->rarg = E;
    					if (F != NULL && IsA(F, List))
    					{

    						n->usingClause = linitial_node(List, castNode(List, F));
    						n->join_using_alias = lsecond_node(Alias, castNode(List, F));
    					}
    					else
    					{

    						n->quals = F;
    					}
    					A = n;
}

joined_table(A) ::= table_ref(B) JOIN table_ref(D) join_qual(E). {
    JoinExpr   *n = makeNode(JoinExpr);

    					n->jointype = JOIN_INNER;
    					n->isNatural = false;
    					n->larg = B;
    					n->rarg = D;
    					if (E != NULL && IsA(E, List))
    					{

    						n->usingClause = linitial_node(List, castNode(List, E));
    						n->join_using_alias = lsecond_node(Alias, castNode(List, E));
    					}
    					else
    					{

    						n->quals = E;
    					}
    					A = n;
}

joined_table(A) ::= table_ref(B) NATURAL join_type(D) JOIN table_ref(F). {
    JoinExpr   *n = makeNode(JoinExpr);

    					n->jointype = D;
    					n->isNatural = true;
    					n->larg = B;
    					n->rarg = F;
    					n->usingClause = NIL; 
    					n->join_using_alias = NULL;
    					n->quals = NULL; 
    					A = n;
}

joined_table(A) ::= table_ref(B) NATURAL JOIN table_ref(E). {
    JoinExpr   *n = makeNode(JoinExpr);

    					n->jointype = JOIN_INNER;
    					n->isNatural = true;
    					n->larg = B;
    					n->rarg = E;
    					n->usingClause = NIL; 
    					n->join_using_alias = NULL;
    					n->quals = NULL; 
    					A = n;
}

/* ----- alias_clause ----- */

alias_clause(A) ::= AS colId(C) LPAREN name_list(E) RPAREN. {
    A = makeNode(Alias);
    					A->aliasname = C;
    					A->colnames = E;
}

alias_clause(A) ::= AS colId(C). {
    A = makeNode(Alias);
    					A->aliasname = C;
}

alias_clause(A) ::= colId(B) LPAREN name_list(D) RPAREN. {
    A = makeNode(Alias);
    					A->aliasname = B;
    					A->colnames = D;
}

alias_clause(A) ::= colId(B). {
    A = makeNode(Alias);
    					A->aliasname = B;
}

/* ----- opt_alias_clause ----- */

opt_alias_clause(A) ::= alias_clause(B). {
    A = B;
}

opt_alias_clause(A) ::= . {
    A = NULL;
}

/* ----- opt_alias_clause_for_join_using ----- */

opt_alias_clause_for_join_using(A) ::= AS colId(C). {
    A = makeNode(Alias);
    					A->aliasname = C;
}

opt_alias_clause_for_join_using(A) ::= . {
    A = NULL;
}

/* ----- func_alias_clause ----- */

func_alias_clause(A) ::= alias_clause(B). {
    A = list_make2(B, NIL);
}

func_alias_clause(A) ::= AS LPAREN tableFuncElementList(D) RPAREN. {
    A = list_make2(NULL, D);
}

func_alias_clause(A) ::= AS colId(C) LPAREN tableFuncElementList(E) RPAREN. {
    Alias	   *a = makeNode(Alias);

    					a->aliasname = C;
    					A = list_make2(a, E);
}

func_alias_clause(A) ::= colId(B) LPAREN tableFuncElementList(D) RPAREN. {
    Alias	   *a = makeNode(Alias);

    					a->aliasname = B;
    					A = list_make2(a, D);
}

func_alias_clause(A) ::= . {
    A = list_make2(NULL, NIL);
}

/* ----- join_type ----- */

join_type(A) ::= FULL opt_outer(C). {
    A = JOIN_FULL;
}

join_type(A) ::= LEFT opt_outer(C). {
    A = JOIN_LEFT;
}

join_type(A) ::= RIGHT opt_outer(C). {
    A = JOIN_RIGHT;
}

join_type(A) ::= INNER_P. {
    A = JOIN_INNER;
}

/* ----- opt_outer ----- */

opt_outer(A) ::= OUTER_P.

/* ----- join_qual ----- */

join_qual(A) ::= USING LPAREN name_list(D) RPAREN opt_alias_clause_for_join_using(F). {
    A = (Node *) list_make2(D, F);
}

join_qual(A) ::= ON a_expr(C). {
    A = C;
}

/* ----- relation_expr ----- */

relation_expr(A) ::= qualified_name(B). {
    A = B;
    					A->inh = true;
    					A->alias = NULL;
}

relation_expr(A) ::= extended_relation_expr(B). {
    A = B;
}

/* ----- extended_relation_expr ----- */

extended_relation_expr(A) ::= qualified_name(B) STAR. {
    A = B;
    					A->inh = true;
    					A->alias = NULL;
}

extended_relation_expr(A) ::= ONLY qualified_name(C). {
    A = C;
    					A->inh = false;
    					A->alias = NULL;
}

extended_relation_expr(A) ::= ONLY LPAREN qualified_name(D) RPAREN. {
    A = D;
    					A->inh = false;
    					A->alias = NULL;
}

/* ----- relation_expr_list ----- */

relation_expr_list(A) ::= relation_expr(B). {
    A = list_make1(B);
}

relation_expr_list(A) ::= relation_expr_list(B) COMMA relation_expr(D). {
    A = lappend(B, D);
}

/* ----- relation_expr_opt_alias ----- */

relation_expr_opt_alias(A) ::= relation_expr(B). [UMINUS] {
    A = B;
}

relation_expr_opt_alias(A) ::= relation_expr(B) colId(C). {
    Alias	   *alias = makeNode(Alias);

    					alias->aliasname = C;
    					B->alias = alias;
    					A = B;
}

relation_expr_opt_alias(A) ::= relation_expr(B) AS colId(D). {
    Alias	   *alias = makeNode(Alias);

    					alias->aliasname = D;
    					B->alias = alias;
    					A = B;
}

/* ----- tablesample_clause ----- */

tablesample_clause(A) ::= TABLESAMPLE func_name(C) LPAREN expr_list(E) RPAREN opt_repeatable_clause(G). {
    RangeTableSample *n = makeNode(RangeTableSample);


    					n->method = C;
    					n->args = E;
    					n->repeatable = G;
    					n->location = LOC(C);
    					A = (Node *) n;
}

/* ----- opt_repeatable_clause ----- */

opt_repeatable_clause(A) ::= REPEATABLE LPAREN a_expr(D) RPAREN. {
    A = (Node *) D;
}

opt_repeatable_clause(A) ::= . {
    A = NULL;
}

/* ----- func_table ----- */

func_table(A) ::= func_expr_windowless(B) opt_ordinality(C). {
    RangeFunction *n = makeNode(RangeFunction);

    					n->lateral = false;
    					n->ordinality = C;
    					n->is_rowsfrom = false;
    					n->functions = list_make1(list_make2(B, NIL));

    					A = (Node *) n;
}

func_table(A) ::= ROWS FROM LPAREN rowsfrom_list(E) RPAREN opt_ordinality(G). {
    RangeFunction *n = makeNode(RangeFunction);

    					n->lateral = false;
    					n->ordinality = G;
    					n->is_rowsfrom = true;
    					n->functions = E;

    					A = (Node *) n;
}

/* ----- rowsfrom_item ----- */

rowsfrom_item(A) ::= func_expr_windowless(B) opt_col_def_list(C). {
    A = list_make2(B, C);
}

/* ----- rowsfrom_list ----- */

rowsfrom_list(A) ::= rowsfrom_item(B). {
    A = list_make1(B);
}

rowsfrom_list(A) ::= rowsfrom_list(B) COMMA rowsfrom_item(D). {
    A = lappend(B, D);
}

/* ----- opt_col_def_list ----- */

opt_col_def_list(A) ::= AS LPAREN tableFuncElementList(D) RPAREN. {
    A = D;
}

opt_col_def_list(A) ::= . {
    A = NIL;
}

/* ----- opt_ordinality ----- */

opt_ordinality(A) ::= WITH_LA ORDINALITY. {
    A = true;
}

opt_ordinality(A) ::= . {
    A = false;
}

/* ----- where_clause ----- */
```

