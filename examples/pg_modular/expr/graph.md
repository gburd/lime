# Property Graphs


SQL/PGQ property graph support: CREATE/ALTER PROPERTY GRAPH,
graph pattern matching, path patterns, label expressions,
and vertex/edge table definitions.


```yaml
name: pg-graph
version: 17.0.0
description: Property graph and graph pattern matching (SQL/PGQ)
provides: [pg-graph]
depends: [pg-type-decls, pg-expressions, pg-base-helpers]
```

## Production Rules

```lime rules
createPropGraphStmt(A) ::= CREATE optTemp(C) PROPERTY GRAPH qualified_name(F) opt_vertex_tables_clause(G) opt_edge_tables_clause(H). {
    createPropGraphStmt *n = makeNode(createPropGraphStmt);

    					n->pgname = F;
    					n->pgname->relpersistence = C;
    					n->vertex_tables = G;
    					n->edge_tables = H;

    					A = (Node *)n;
}

/* ----- opt_vertex_tables_clause ----- */

opt_vertex_tables_clause(A) ::= vertex_tables_clause(B). {
    A = B;
}

opt_vertex_tables_clause(A) ::= . {
    A = NIL;
}

/* ----- vertex_tables_clause ----- */

vertex_tables_clause(A) ::= vertex_synonym(B) TABLES LPAREN vertex_table_list(E) RPAREN. {
    A = E;
}

/* ----- vertex_synonym ----- */

vertex_synonym(A) ::= NODE.

vertex_synonym(A) ::= VERTEX.

/* ----- vertex_table_list ----- */

vertex_table_list(A) ::= vertex_table_definition(B). {
    A = list_make1(B);
}

vertex_table_list(A) ::= vertex_table_list(B) COMMA vertex_table_definition(D). {
    A = lappend(B, D);
}

/* ----- vertex_table_definition ----- */

vertex_table_definition(A) ::= qualified_name(B) opt_propgraph_table_alias(C) opt_graph_table_key_clause(D) opt_element_table_label_and_properties(E). {
    PropGraphVertex *n = makeNode(PropGraphVertex);

    					B->alias = C;
    					n->vtable = B;
    					n->vkey = D;
    					n->labels = E;
    					n->location = LOC(B);

    					A = (Node *) n;
}

/* ----- opt_propgraph_table_alias ----- */

opt_propgraph_table_alias(A) ::= AS name(C). {
    A = makeNode(Alias);
    					A->aliasname = C;
}

opt_propgraph_table_alias(A) ::= . {
    A = NULL;
}

/* ----- opt_graph_table_key_clause ----- */

opt_graph_table_key_clause(A) ::= KEY LPAREN columnList(D) RPAREN. {
    A = D;
}

opt_graph_table_key_clause(A) ::= . {
    A = NIL;
}

/* ----- opt_edge_tables_clause ----- */

opt_edge_tables_clause(A) ::= edge_tables_clause(B). {
    A = B;
}

opt_edge_tables_clause(A) ::= . {
    A = NIL;
}

/* ----- edge_tables_clause ----- */

edge_tables_clause(A) ::= edge_synonym(B) TABLES LPAREN edge_table_list(E) RPAREN. {
    A = E;
}

/* ----- edge_synonym ----- */

edge_synonym(A) ::= EDGE.

edge_synonym(A) ::= RELATIONSHIP.

/* ----- edge_table_list ----- */

edge_table_list(A) ::= edge_table_definition(B). {
    A = list_make1(B);
}

edge_table_list(A) ::= edge_table_list(B) COMMA edge_table_definition(D). {
    A = lappend(B, D);
}

/* ----- edge_table_definition ----- */

edge_table_definition(A) ::= qualified_name(B) opt_propgraph_table_alias(C) opt_graph_table_key_clause(D) source_vertex_table(E) destination_vertex_table(F) opt_element_table_label_and_properties(G). {
    PropGraphEdge *n = makeNode(PropGraphEdge);

    					B->alias = C;
    					n->etable = B;
    					n->ekey = D;
    					n->esrckey = linitial(E);
    					n->esrcvertex = lsecond(E);
    					n->esrcvertexcols = lthird(E);
    					n->edestkey = linitial(F);
    					n->edestvertex = lsecond(F);
    					n->edestvertexcols = lthird(F);
    					n->labels = G;
    					n->location = LOC(B);

    					A = (Node *) n;
}

/* ----- source_vertex_table ----- */

source_vertex_table(A) ::= SOURCE name(C). {
    A = list_make3(NULL, C, NULL);
}

source_vertex_table(A) ::= SOURCE KEY LPAREN columnList(E) RPAREN REFERENCES name(H) LPAREN columnList(J) RPAREN. {
    A = list_make3(E, H, J);
}

/* ----- destination_vertex_table ----- */

destination_vertex_table(A) ::= DESTINATION name(C). {
    A = list_make3(NULL, C, NULL);
}

destination_vertex_table(A) ::= DESTINATION KEY LPAREN columnList(E) RPAREN REFERENCES name(H) LPAREN columnList(J) RPAREN. {
    A = list_make3(E, H, J);
}

/* ----- opt_element_table_label_and_properties ----- */

opt_element_table_label_and_properties(A) ::= element_table_properties(B). {
    PropGraphLabelAndProperties *lp = makeNode(PropGraphLabelAndProperties);

    					lp->properties = (PropGraphProperties *) B;
    					lp->location = LOC(B);

    					A = list_make1(lp);
}

opt_element_table_label_and_properties(A) ::= label_and_properties_list(B). {
    A = B;
}

opt_element_table_label_and_properties(A) ::= . {
    PropGraphLabelAndProperties *lp = makeNode(PropGraphLabelAndProperties);
    					PropGraphProperties *pr = makeNode(PropGraphProperties);

    					pr->all = true;
    					pr->location = -1;
    					lp->properties = pr;
    					lp->location = -1;

    					A = list_make1(lp);
}

/* ----- element_table_properties ----- */

element_table_properties(A) ::= NO PROPERTIES. {
    PropGraphProperties *pr = makeNode(PropGraphProperties);

    					pr->properties = NIL;
    					pr->location = LOC(B);

    					A = (Node *) pr;
}

element_table_properties(A) ::= PROPERTIES ALL COLUMNS. {
    PropGraphProperties *pr = makeNode(PropGraphProperties);

    					pr->all = true;
    					pr->location = LOC(B);

    					A = (Node *) pr;
}

element_table_properties(A) ::= PROPERTIES LPAREN labeled_expr_list(D) RPAREN. {
    PropGraphProperties *pr = makeNode(PropGraphProperties);

    					pr->properties = D;
    					pr->location = LOC(B);

    					A = (Node *) pr;
}

/* ----- label_and_properties_list ----- */

label_and_properties_list(A) ::= label_and_properties(B). {
    A = list_make1(B);
}

label_and_properties_list(A) ::= label_and_properties_list(B) label_and_properties(C). {
    A = lappend(B, C);
}

/* ----- label_and_properties ----- */

label_and_properties(A) ::= element_table_label_clause(B). {
    PropGraphLabelAndProperties *lp = makeNode(PropGraphLabelAndProperties);
    					PropGraphProperties *pr = makeNode(PropGraphProperties);

    					pr->all = true;
    					pr->location = -1;

    					lp->label = B;
    					lp->properties = pr;
    					lp->location = LOC(B);

    					A = (Node *) lp;
}

label_and_properties(A) ::= element_table_label_clause(B) element_table_properties(C). {
    PropGraphLabelAndProperties *lp = makeNode(PropGraphLabelAndProperties);

    					lp->label = B;
    					lp->properties = (PropGraphProperties *) C;
    					lp->location = LOC(B);

    					A = (Node *) lp;
}

/* ----- element_table_label_clause ----- */

element_table_label_clause(A) ::= LABEL name(C). {
    A = C;
}

element_table_label_clause(A) ::= DEFAULT LABEL. {
    A = NULL;
}

/* ----- alterPropGraphStmt ----- */

alterPropGraphStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) ADD_P vertex_tables_clause(G). {
    alterPropGraphStmt *n = makeNode(alterPropGraphStmt);

    					n->pgname = E;
    					n->add_vertex_tables = G;

    					A = (Node *) n;
}

alterPropGraphStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) ADD_P vertex_tables_clause(G) ADD_P edge_tables_clause(I). {
    alterPropGraphStmt *n = makeNode(alterPropGraphStmt);

    					n->pgname = E;
    					n->add_vertex_tables = G;
    					n->add_edge_tables = I;

    					A = (Node *) n;
}

alterPropGraphStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) ADD_P edge_tables_clause(G). {
    alterPropGraphStmt *n = makeNode(alterPropGraphStmt);

    					n->pgname = E;
    					n->add_edge_tables = G;

    					A = (Node *) n;
}

alterPropGraphStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) DROP vertex_synonym(G) TABLES LPAREN name_list(J) RPAREN opt_drop_behavior(L). {
    alterPropGraphStmt *n = makeNode(alterPropGraphStmt);

    					n->pgname = E;
    					n->drop_vertex_tables = J;
    					n->drop_behavior = L;

    					A = (Node *) n;
}

alterPropGraphStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) DROP edge_synonym(G) TABLES LPAREN name_list(J) RPAREN opt_drop_behavior(L). {
    alterPropGraphStmt *n = makeNode(alterPropGraphStmt);

    					n->pgname = E;
    					n->drop_edge_tables = J;
    					n->drop_behavior = L;

    					A = (Node *) n;
}

alterPropGraphStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) ALTER vertex_or_edge(G) TABLE name(I) add_label_list(J). {
    alterPropGraphStmt *n = makeNode(alterPropGraphStmt);

    					n->pgname = E;
    					n->element_kind = G;
    					n->element_alias = I;
    					n->add_labels = J;

    					A = (Node *) n;
}

alterPropGraphStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) ALTER vertex_or_edge(G) TABLE name(I) DROP LABEL name(L) opt_drop_behavior(M). {
    alterPropGraphStmt *n = makeNode(alterPropGraphStmt);

    					n->pgname = E;
    					n->element_kind = G;
    					n->element_alias = I;
    					n->drop_label = L;
    					n->drop_behavior = M;

    					A = (Node *) n;
}

alterPropGraphStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) ALTER vertex_or_edge(G) TABLE name(I) ALTER LABEL name(L) ADD_P PROPERTIES LPAREN labeled_expr_list(P) RPAREN. {
    alterPropGraphStmt *n = makeNode(alterPropGraphStmt);
    					PropGraphProperties *pr = makeNode(PropGraphProperties);

    					n->pgname = E;
    					n->element_kind = G;
    					n->element_alias = I;
    					n->alter_label = L;

    					pr->properties = P;
    					pr->location = LOC(N);
    					n->add_properties = pr;

    					A = (Node *) n;
}

alterPropGraphStmt(A) ::= ALTER PROPERTY GRAPH qualified_name(E) ALTER vertex_or_edge(G) TABLE name(I) ALTER LABEL name(L) DROP PROPERTIES LPAREN name_list(P) RPAREN opt_drop_behavior(R). {
    alterPropGraphStmt *n = makeNode(alterPropGraphStmt);

    					n->pgname = E;
    					n->element_kind = G;
    					n->element_alias = I;
    					n->alter_label = L;
    					n->drop_properties = P;
    					n->drop_behavior = R;

    					A = (Node *) n;
}

/* ----- vertex_or_edge ----- */

vertex_or_edge(A) ::= vertex_synonym(B). {
    A = PROPGRAPH_ELEMENT_KIND_VERTEX;
}

vertex_or_edge(A) ::= edge_synonym(B). {
    A = PROPGRAPH_ELEMENT_KIND_EDGE;
}

/* ----- add_label_list ----- */

add_label_list(A) ::= add_label(B). {
    A = list_make1(B);
}

add_label_list(A) ::= add_label_list(B) add_label(C). {
    A = lappend(B, C);
}

/* ----- add_label ----- */

add_label(A) ::= ADD_P LABEL name(D) element_table_properties(E). {
    PropGraphLabelAndProperties *lp = makeNode(PropGraphLabelAndProperties);

    					lp->label = D;
    					lp->properties = (PropGraphProperties *) E;
    					lp->location = LOC(B);

    					A = (Node *) lp;
}

/* ----- createTransformStmt ----- */

graph_pattern(A) ::= path_pattern_list(B) where_clause(C). {
    GraphPattern *gp = makeNode(GraphPattern);

    					gp->path_pattern_list = B;
    					gp->whereClause = C;
    					A = (Node *) gp;
}

/* ----- path_pattern_list ----- */

path_pattern_list(A) ::= path_pattern(B). {
    A = list_make1(B);
}

path_pattern_list(A) ::= path_pattern_list(B) COMMA path_pattern(D). {
    A = lappend(B, D);
}

/* ----- path_pattern ----- */

path_pattern(A) ::= path_pattern_expression(B). {
    A = B;
}

/* ----- path_pattern_expression ----- */

path_pattern_expression(A) ::= path_term(B). {
    A = B;
}

/* ----- path_term ----- */

path_term(A) ::= path_factor(B). {
    A = list_make1(B);
}

path_term(A) ::= path_term(B) path_factor(C). {
    A = lappend(B, C);
}

/* ----- path_factor ----- */

path_factor(A) ::= path_primary(B) opt_graph_pattern_quantifier(C). {
    GraphElementPattern *gep = (GraphElementPattern *) B;

    					gep->quantifier = C;

    					A = (Node *) gep;
}

/* ----- path_primary ----- */

path_primary(A) ::= LPAREN opt_colid(C) opt_is_label_expression(D) where_clause(E) RPAREN. {
    GraphElementPattern *gep = makeNode(GraphElementPattern);

    					gep->kind = VERTEX_PATTERN;
    					gep->variable = C;
    					gep->labelexpr = D;
    					gep->whereClause = E;
    					gep->location = LOC(B);

    					A = (Node *) gep;
}

path_primary(A) ::= LT MINUS LBRACKET opt_colid(E) opt_is_label_expression(F) where_clause(G) RBRACKET MINUS. {
    GraphElementPattern *gep = makeNode(GraphElementPattern);

    					gep->kind = EDGE_PATTERN_LEFT;
    					gep->variable = E;
    					gep->labelexpr = F;
    					gep->whereClause = G;
    					gep->location = LOC(B);

    					A = (Node *) gep;
}

path_primary(A) ::= MINUS LBRACKET opt_colid(D) opt_is_label_expression(E) where_clause(F) RBRACKET MINUS GT. {
    GraphElementPattern *gep = makeNode(GraphElementPattern);

    					gep->kind = EDGE_PATTERN_RIGHT;
    					gep->variable = D;
    					gep->labelexpr = E;
    					gep->whereClause = F;
    					gep->location = LOC(B);

    					A = (Node *) gep;
}

path_primary(A) ::= MINUS LBRACKET opt_colid(D) opt_is_label_expression(E) where_clause(F) RBRACKET RIGHT_ARROW. {
    GraphElementPattern *gep = makeNode(GraphElementPattern);

    					gep->kind = EDGE_PATTERN_RIGHT;
    					gep->variable = D;
    					gep->labelexpr = E;
    					gep->whereClause = F;
    					gep->location = LOC(B);

    					A = (Node *) gep;
}

path_primary(A) ::= MINUS LBRACKET opt_colid(D) opt_is_label_expression(E) where_clause(F) RBRACKET MINUS. {
    GraphElementPattern *gep = makeNode(GraphElementPattern);

    					gep->kind = EDGE_PATTERN_ANY;
    					gep->variable = D;
    					gep->labelexpr = E;
    					gep->whereClause = F;
    					gep->location = LOC(B);

    					A = (Node *) gep;
}

path_primary(A) ::= LT MINUS. {
    GraphElementPattern *gep = makeNode(GraphElementPattern);

    					gep->kind = EDGE_PATTERN_LEFT;
    					gep->location = LOC(B);

    					A = (Node *) gep;
}

path_primary(A) ::= MINUS GT. {
    GraphElementPattern *gep = makeNode(GraphElementPattern);

    					gep->kind = EDGE_PATTERN_RIGHT;
    					gep->location = LOC(B);

    					A = (Node *) gep;
}

path_primary(A) ::= RIGHT_ARROW. {
    GraphElementPattern *gep = makeNode(GraphElementPattern);

    					gep->kind = EDGE_PATTERN_RIGHT;
    					gep->location = LOC(B);

    					A = (Node *) gep;
}

path_primary(A) ::= MINUS. {
    GraphElementPattern *gep = makeNode(GraphElementPattern);

    					gep->kind = EDGE_PATTERN_ANY;
    					gep->location = LOC(B);

    					A = (Node *) gep;
}

path_primary(A) ::= LPAREN path_pattern_expression(C) where_clause(D) RPAREN. {
    GraphElementPattern *gep = makeNode(GraphElementPattern);

    					gep->kind = PAREN_EXPR;
    					gep->subexpr = C;
    					gep->whereClause = D;
    					gep->location = LOC(B);

    					A = (Node *) gep;
}

/* ----- opt_colid ----- */

opt_colid(A) ::= colId(B). {
    A = B;
}

opt_colid(A) ::= . {
    A = NULL;
}

/* ----- opt_is_label_expression ----- */

opt_is_label_expression(A) ::= IS label_expression(C). {
    A = C;
}

opt_is_label_expression(A) ::= . {
    A = NULL;
}

/* ----- opt_graph_pattern_quantifier ----- */

opt_graph_pattern_quantifier(A) ::= LBRACE iconst(C) RBRACE. {
    A = list_make2_int(C, C);
}

opt_graph_pattern_quantifier(A) ::= LBRACE COMMA iconst(D) RBRACE. {
    A = list_make2_int(0, D);
}

opt_graph_pattern_quantifier(A) ::= LBRACE iconst(C) COMMA iconst(E) RBRACE. {
    A = list_make2_int(C, E);
}

opt_graph_pattern_quantifier(A) ::= . {
    A = NULL;
}

/* ----- label_expression ----- */

label_expression(A) ::= label_term(B).

label_expression(A) ::= label_disjunction(B).

/* ----- label_disjunction ----- */

label_disjunction(A) ::= label_expression(B) PIPE label_term(D). {
    A = makeOrExpr(B, D, LOC(C));
}

/* ----- label_term ----- */

label_term(A) ::= name(B). {
    A = makeColumnRef(B, NIL, LOC(B), yyscanner);
}

/* ----- opt_target_list ----- */
```

