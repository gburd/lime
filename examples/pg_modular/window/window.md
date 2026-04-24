# Window Functions


Window function support: OVER clauses, PARTITION BY, ORDER BY
within windows, frame specifications (ROWS, RANGE, GROUPS),
and frame exclusion.


```yaml
name: pg-window
version: 17.0.0
description: Window functions (OVER, PARTITION BY, frame specifications)
provides: [pg-window]
depends: [pg-type-decls, pg-expressions]
```

## Production Rules

```lime rules
within_group_clause(A) ::= WITHIN GROUP_P LPAREN sort_clause(E) RPAREN. {
    A = E;
}

within_group_clause(A) ::= . {
    A = NIL;
}

/* ----- filter_clause ----- */

filter_clause(A) ::= FILTER LPAREN WHERE a_expr(E) RPAREN. {
    A = E;
}

filter_clause(A) ::= . {
    A = NULL;
}

/* ----- null_treatment ----- */

null_treatment(A) ::= IGNORE_P NULLS_P. {
    A = PARSER_IGNORE_NULLS;
}

null_treatment(A) ::= RESPECT_P NULLS_P. {
    A = PARSER_RESPECT_NULLS;
}

null_treatment(A) ::= . {
    A = NO_NULLTREATMENT;
}

/* ----- window_clause ----- */

window_clause(A) ::= WINDOW window_definition_list(C). {
    A = C;
}

window_clause(A) ::= . {
    A = NIL;
}

/* ----- window_definition_list ----- */

window_definition_list(A) ::= window_definition(B). {
    A = list_make1(B);
}

window_definition_list(A) ::= window_definition_list(B) COMMA window_definition(D). {
    A = lappend(B, D);
}

/* ----- window_definition ----- */

window_definition(A) ::= colId(B) AS window_specification(D). {
    WindowDef  *n = D;

    					n->name = B;
    					A = n;
}

/* ----- over_clause ----- */

over_clause(A) ::= OVER window_specification(C). {
    A = C;
}

over_clause(A) ::= OVER colId(C). {
    WindowDef  *n = makeNode(WindowDef);

    					n->name = C;
    					n->refname = NULL;
    					n->partitionClause = NIL;
    					n->orderClause = NIL;
    					n->frameOptions = FRAMEOPTION_DEFAULTS;
    					n->startOffset = NULL;
    					n->endOffset = NULL;
    					n->location = LOC(C);
    					A = n;
}

over_clause(A) ::= . {
    A = NULL;
}

/* ----- window_specification ----- */

window_specification(A) ::= LPAREN opt_existing_window_name(C) opt_partition_clause(D) opt_sort_clause(E) opt_frame_clause(F) RPAREN. {
    WindowDef  *n = makeNode(WindowDef);

    					n->name = NULL;
    					n->refname = C;
    					n->partitionClause = D;
    					n->orderClause = E;

    					n->frameOptions = F->frameOptions;
    					n->startOffset = F->startOffset;
    					n->endOffset = F->endOffset;
    					n->location = LOC(B);
    					A = n;
}

/* ----- opt_existing_window_name ----- */

opt_existing_window_name(A) ::= colId(B). {
    A = B;
}

opt_existing_window_name(A) ::= . [OP] {
    A = NULL;
}

/* ----- opt_partition_clause ----- */

opt_partition_clause(A) ::= PARTITION BY expr_list(D). {
    A = D;
}

opt_partition_clause(A) ::= . {
    A = NIL;
}

/* ----- opt_frame_clause ----- */

opt_frame_clause(A) ::= RANGE frame_extent(C) opt_window_exclusion_clause(D). {
    WindowDef  *n = C;

    					n->frameOptions |= FRAMEOPTION_NONDEFAULT | FRAMEOPTION_RANGE;
    					n->frameOptions |= D;
    					A = n;
}

opt_frame_clause(A) ::= ROWS frame_extent(C) opt_window_exclusion_clause(D). {
    WindowDef  *n = C;

    					n->frameOptions |= FRAMEOPTION_NONDEFAULT | FRAMEOPTION_ROWS;
    					n->frameOptions |= D;
    					A = n;
}

opt_frame_clause(A) ::= GROUPS frame_extent(C) opt_window_exclusion_clause(D). {
    WindowDef  *n = C;

    					n->frameOptions |= FRAMEOPTION_NONDEFAULT | FRAMEOPTION_GROUPS;
    					n->frameOptions |= D;
    					A = n;
}

opt_frame_clause(A) ::= . {
    WindowDef  *n = makeNode(WindowDef);

    					n->frameOptions = FRAMEOPTION_DEFAULTS;
    					n->startOffset = NULL;
    					n->endOffset = NULL;
    					A = n;
}

/* ----- frame_extent ----- */

frame_extent(A) ::= frame_bound(B). {
    WindowDef  *n = B;


    					if (n->frameOptions & FRAMEOPTION_START_UNBOUNDED_FOLLOWING)
    						ereport(ERROR,
    								(errcode(ERRCODE_WINDOWING_ERROR),
    								 errmsg("frame start cannot be UNBOUNDED FOLLOWING"),
    								 parser_errposition(LOC(B))));
    					if (n->frameOptions & FRAMEOPTION_START_OFFSET_FOLLOWING)
    						ereport(ERROR,
    								(errcode(ERRCODE_WINDOWING_ERROR),
    								 errmsg("frame starting from following row cannot end with current row"),
    								 parser_errposition(LOC(B))));
    					n->frameOptions |= FRAMEOPTION_END_CURRENT_ROW;
    					A = n;
}

frame_extent(A) ::= BETWEEN frame_bound(C) AND frame_bound(E). {
    WindowDef  *n1 = C;
    					WindowDef  *n2 = E;


    					int		frameOptions = n1->frameOptions;

    					frameOptions |= n2->frameOptions << 1;
    					frameOptions |= FRAMEOPTION_BETWEEN;

    					if (frameOptions & FRAMEOPTION_START_UNBOUNDED_FOLLOWING)
    						ereport(ERROR,
    								(errcode(ERRCODE_WINDOWING_ERROR),
    								 errmsg("frame start cannot be UNBOUNDED FOLLOWING"),
    								 parser_errposition(LOC(C))));
    					if (frameOptions & FRAMEOPTION_END_UNBOUNDED_PRECEDING)
    						ereport(ERROR,
    								(errcode(ERRCODE_WINDOWING_ERROR),
    								 errmsg("frame end cannot be UNBOUNDED PRECEDING"),
    								 parser_errposition(LOC(E))));
    					if ((frameOptions & FRAMEOPTION_START_CURRENT_ROW) &&
    						(frameOptions & FRAMEOPTION_END_OFFSET_PRECEDING))
    						ereport(ERROR,
    								(errcode(ERRCODE_WINDOWING_ERROR),
    								 errmsg("frame starting from current row cannot have preceding rows"),
    								 parser_errposition(LOC(E))));
    					if ((frameOptions & FRAMEOPTION_START_OFFSET_FOLLOWING) &&
    						(frameOptions & (FRAMEOPTION_END_OFFSET_PRECEDING |
    										 FRAMEOPTION_END_CURRENT_ROW)))
    						ereport(ERROR,
    								(errcode(ERRCODE_WINDOWING_ERROR),
    								 errmsg("frame starting from following row cannot have preceding rows"),
    								 parser_errposition(LOC(E))));
    					n1->frameOptions = frameOptions;
    					n1->endOffset = n2->startOffset;
    					A = n1;
}

/* ----- frame_bound ----- */

frame_bound(A) ::= UNBOUNDED PRECEDING. {
    WindowDef  *n = makeNode(WindowDef);

    					n->frameOptions = FRAMEOPTION_START_UNBOUNDED_PRECEDING;
    					n->startOffset = NULL;
    					n->endOffset = NULL;
    					A = n;
}

frame_bound(A) ::= UNBOUNDED FOLLOWING. {
    WindowDef  *n = makeNode(WindowDef);

    					n->frameOptions = FRAMEOPTION_START_UNBOUNDED_FOLLOWING;
    					n->startOffset = NULL;
    					n->endOffset = NULL;
    					A = n;
}

frame_bound(A) ::= CURRENT_P ROW. {
    WindowDef  *n = makeNode(WindowDef);

    					n->frameOptions = FRAMEOPTION_START_CURRENT_ROW;
    					n->startOffset = NULL;
    					n->endOffset = NULL;
    					A = n;
}

frame_bound(A) ::= a_expr(B) PRECEDING. {
    WindowDef  *n = makeNode(WindowDef);

    					n->frameOptions = FRAMEOPTION_START_OFFSET_PRECEDING;
    					n->startOffset = B;
    					n->endOffset = NULL;
    					A = n;
}

frame_bound(A) ::= a_expr(B) FOLLOWING. {
    WindowDef  *n = makeNode(WindowDef);

    					n->frameOptions = FRAMEOPTION_START_OFFSET_FOLLOWING;
    					n->startOffset = B;
    					n->endOffset = NULL;
    					A = n;
}

/* ----- opt_window_exclusion_clause ----- */

opt_window_exclusion_clause(A) ::= EXCLUDE CURRENT_P ROW. {
    A = FRAMEOPTION_EXCLUDE_CURRENT_ROW;
}

opt_window_exclusion_clause(A) ::= EXCLUDE GROUP_P. {
    A = FRAMEOPTION_EXCLUDE_GROUP;
}

opt_window_exclusion_clause(A) ::= EXCLUDE TIES. {
    A = FRAMEOPTION_EXCLUDE_TIES;
}

opt_window_exclusion_clause(A) ::= EXCLUDE NO OTHERS. {
    A = 0;
}

opt_window_exclusion_clause(A) ::= . {
    A = 0;
}

/* ----- row ----- */
```

