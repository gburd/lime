# Keyword Classifications


Rules that classify tokens as column identifiers, type names,
or labels. These large union rules list which keywords can
be used in each identifier context.


```yaml
name: pg-keywords
version: 17.0.0
description: Keyword classification rules (colId, reserved, unreserved)
provides: [pg-keywords]
depends: [pg-type-decls]
```

## Production Rules

```lime rules
colId(A) ::= IDENT. {
    A = B;
}

colId(A) ::= unreserved_keyword(B). {
    A = pstrdup(B);
}

colId(A) ::= col_name_keyword(B). {
    A = pstrdup(B);
}

/* ----- type_function_name ----- */

type_function_name(A) ::= IDENT. {
    A = B;
}

type_function_name(A) ::= unreserved_keyword(B). {
    A = pstrdup(B);
}

type_function_name(A) ::= type_func_name_keyword(B). {
    A = pstrdup(B);
}

/* ----- nonReservedWord ----- */

nonReservedWord(A) ::= IDENT. {
    A = B;
}

nonReservedWord(A) ::= unreserved_keyword(B). {
    A = pstrdup(B);
}

nonReservedWord(A) ::= col_name_keyword(B). {
    A = pstrdup(B);
}

nonReservedWord(A) ::= type_func_name_keyword(B). {
    A = pstrdup(B);
}

/* ----- colLabel ----- */

colLabel(A) ::= IDENT. {
    A = B;
}

colLabel(A) ::= unreserved_keyword(B). {
    A = pstrdup(B);
}

colLabel(A) ::= col_name_keyword(B). {
    A = pstrdup(B);
}

colLabel(A) ::= type_func_name_keyword(B). {
    A = pstrdup(B);
}

colLabel(A) ::= reserved_keyword(B). {
    A = pstrdup(B);
}

/* ----- bareColLabel ----- */

bareColLabel(A) ::= IDENT. {
    A = B;
}

bareColLabel(A) ::= bare_label_keyword(B). {
    A = pstrdup(B);
}

/* ----- unreserved_keyword ----- */

unreserved_keyword(A) ::= ABORT_P.

unreserved_keyword(A) ::= ABSENT.

unreserved_keyword(A) ::= ABSOLUTE_P.

unreserved_keyword(A) ::= ACCESS.

unreserved_keyword(A) ::= ACTION.

unreserved_keyword(A) ::= ADD_P.

unreserved_keyword(A) ::= ADMIN.

unreserved_keyword(A) ::= AFTER.

unreserved_keyword(A) ::= AGGREGATE.

unreserved_keyword(A) ::= ALSO.

unreserved_keyword(A) ::= ALTER.

unreserved_keyword(A) ::= ALWAYS.

unreserved_keyword(A) ::= ASENSITIVE.

unreserved_keyword(A) ::= ASSERTION.

unreserved_keyword(A) ::= ASSIGNMENT.

unreserved_keyword(A) ::= AT.

unreserved_keyword(A) ::= ATOMIC.

unreserved_keyword(A) ::= ATTACH.

unreserved_keyword(A) ::= ATTRIBUTE.

unreserved_keyword(A) ::= BACKWARD.

unreserved_keyword(A) ::= BEFORE.

unreserved_keyword(A) ::= BEGIN_P.

unreserved_keyword(A) ::= BREADTH.

unreserved_keyword(A) ::= BY.

unreserved_keyword(A) ::= CACHE.

unreserved_keyword(A) ::= CALL.

unreserved_keyword(A) ::= CALLED.

unreserved_keyword(A) ::= CASCADE.

unreserved_keyword(A) ::= CASCADED.

unreserved_keyword(A) ::= CATALOG_P.

unreserved_keyword(A) ::= CHAIN.

unreserved_keyword(A) ::= CHARACTERISTICS.

unreserved_keyword(A) ::= CHECKPOINT.

unreserved_keyword(A) ::= CLASS.

unreserved_keyword(A) ::= CLOSE.

unreserved_keyword(A) ::= CLUSTER.

unreserved_keyword(A) ::= COLUMNS.

unreserved_keyword(A) ::= COMMENT.

unreserved_keyword(A) ::= COMMENTS.

unreserved_keyword(A) ::= COMMIT.

unreserved_keyword(A) ::= COMMITTED.

unreserved_keyword(A) ::= COMPRESSION.

unreserved_keyword(A) ::= CONDITIONAL.

unreserved_keyword(A) ::= CONFIGURATION.

unreserved_keyword(A) ::= CONFLICT.

unreserved_keyword(A) ::= CONNECTION.

unreserved_keyword(A) ::= CONSTRAINTS.

unreserved_keyword(A) ::= CONTENT_P.

unreserved_keyword(A) ::= CONTINUE_P.

unreserved_keyword(A) ::= CONVERSION_P.

unreserved_keyword(A) ::= COPY.

unreserved_keyword(A) ::= COST.

unreserved_keyword(A) ::= CSV.

unreserved_keyword(A) ::= CUBE.

unreserved_keyword(A) ::= CURRENT_P.

unreserved_keyword(A) ::= CURSOR.

unreserved_keyword(A) ::= CYCLE.

unreserved_keyword(A) ::= DATA_P.

unreserved_keyword(A) ::= DATABASE.

unreserved_keyword(A) ::= DAY_P.

unreserved_keyword(A) ::= DEALLOCATE.

unreserved_keyword(A) ::= DECLARE.

unreserved_keyword(A) ::= DEFAULTS.

unreserved_keyword(A) ::= DEFERRED.

unreserved_keyword(A) ::= DEFINER.

unreserved_keyword(A) ::= DELETE_P.

unreserved_keyword(A) ::= DELIMITER.

unreserved_keyword(A) ::= DELIMITERS.

unreserved_keyword(A) ::= DEPENDS.

unreserved_keyword(A) ::= DEPTH.

unreserved_keyword(A) ::= DESTINATION.

unreserved_keyword(A) ::= DETACH.

unreserved_keyword(A) ::= DICTIONARY.

unreserved_keyword(A) ::= DISABLE_P.

unreserved_keyword(A) ::= DISCARD.

unreserved_keyword(A) ::= DOCUMENT_P.

unreserved_keyword(A) ::= DOMAIN_P.

unreserved_keyword(A) ::= DOUBLE_P.

unreserved_keyword(A) ::= DROP.

unreserved_keyword(A) ::= EACH.

unreserved_keyword(A) ::= EDGE.

unreserved_keyword(A) ::= EMPTY_P.

unreserved_keyword(A) ::= ENABLE_P.

unreserved_keyword(A) ::= ENCODING.

unreserved_keyword(A) ::= ENCRYPTED.

unreserved_keyword(A) ::= ENFORCED.

unreserved_keyword(A) ::= ENUM_P.

unreserved_keyword(A) ::= ERROR_P.

unreserved_keyword(A) ::= ESCAPE.

unreserved_keyword(A) ::= EVENT.

unreserved_keyword(A) ::= EXCLUDE.

unreserved_keyword(A) ::= EXCLUDING.

unreserved_keyword(A) ::= EXCLUSIVE.

unreserved_keyword(A) ::= EXECUTE.

unreserved_keyword(A) ::= EXPLAIN.

unreserved_keyword(A) ::= EXPRESSION.

unreserved_keyword(A) ::= EXTENSION.

unreserved_keyword(A) ::= EXTERNAL.

unreserved_keyword(A) ::= FAMILY.

unreserved_keyword(A) ::= FILTER.

unreserved_keyword(A) ::= FINALIZE.

unreserved_keyword(A) ::= FIRST_P.

unreserved_keyword(A) ::= FOLLOWING.

unreserved_keyword(A) ::= FORCE.

unreserved_keyword(A) ::= FORMAT.

unreserved_keyword(A) ::= FORWARD.

unreserved_keyword(A) ::= FUNCTION.

unreserved_keyword(A) ::= FUNCTIONS.

unreserved_keyword(A) ::= GENERATED.

unreserved_keyword(A) ::= GLOBAL.

unreserved_keyword(A) ::= GRANTED.

unreserved_keyword(A) ::= GRAPH.

unreserved_keyword(A) ::= GROUPS.

unreserved_keyword(A) ::= HANDLER.

unreserved_keyword(A) ::= HEADER_P.

unreserved_keyword(A) ::= HOLD.

unreserved_keyword(A) ::= HOUR_P.

unreserved_keyword(A) ::= IDENTITY_P.

unreserved_keyword(A) ::= IF_P.

unreserved_keyword(A) ::= IGNORE_P.

unreserved_keyword(A) ::= IMMEDIATE.

unreserved_keyword(A) ::= IMMUTABLE.

unreserved_keyword(A) ::= IMPLICIT_P.

unreserved_keyword(A) ::= IMPORT_P.

unreserved_keyword(A) ::= INCLUDE.

unreserved_keyword(A) ::= INCLUDING.

unreserved_keyword(A) ::= INCREMENT.

unreserved_keyword(A) ::= INDENT.

unreserved_keyword(A) ::= INDEX.

unreserved_keyword(A) ::= INDEXES.

unreserved_keyword(A) ::= INHERIT.

unreserved_keyword(A) ::= INHERITS.

unreserved_keyword(A) ::= INLINE_P.

unreserved_keyword(A) ::= INPUT_P.

unreserved_keyword(A) ::= INSENSITIVE.

unreserved_keyword(A) ::= INSERT.

unreserved_keyword(A) ::= INSTEAD.

unreserved_keyword(A) ::= INVOKER.

unreserved_keyword(A) ::= ISOLATION.

unreserved_keyword(A) ::= KEEP.

unreserved_keyword(A) ::= KEY.

unreserved_keyword(A) ::= KEYS.

unreserved_keyword(A) ::= LABEL.

unreserved_keyword(A) ::= LANGUAGE.

unreserved_keyword(A) ::= LARGE_P.

unreserved_keyword(A) ::= LAST_P.

unreserved_keyword(A) ::= LEAKPROOF.

unreserved_keyword(A) ::= LEVEL.

unreserved_keyword(A) ::= LISTEN.

unreserved_keyword(A) ::= LOAD.

unreserved_keyword(A) ::= LOCAL.

unreserved_keyword(A) ::= LOCATION.

unreserved_keyword(A) ::= LOCK_P.

unreserved_keyword(A) ::= LOCKED.

unreserved_keyword(A) ::= LOGGED.

unreserved_keyword(A) ::= LSN_P.

unreserved_keyword(A) ::= MAPPING.

unreserved_keyword(A) ::= MATCH.

unreserved_keyword(A) ::= MATCHED.

unreserved_keyword(A) ::= MATERIALIZED.

unreserved_keyword(A) ::= MAXVALUE.

unreserved_keyword(A) ::= MERGE.

unreserved_keyword(A) ::= METHOD.

unreserved_keyword(A) ::= MINUTE_P.

unreserved_keyword(A) ::= MINVALUE.

unreserved_keyword(A) ::= MODE.

unreserved_keyword(A) ::= MONTH_P.

unreserved_keyword(A) ::= MOVE.

unreserved_keyword(A) ::= NAME_P.

unreserved_keyword(A) ::= NAMES.

unreserved_keyword(A) ::= NESTED.

unreserved_keyword(A) ::= NEW.

unreserved_keyword(A) ::= NEXT.

unreserved_keyword(A) ::= NFC.

unreserved_keyword(A) ::= NFD.

unreserved_keyword(A) ::= NFKC.

unreserved_keyword(A) ::= NFKD.

unreserved_keyword(A) ::= NO.

unreserved_keyword(A) ::= NODE.

unreserved_keyword(A) ::= NORMALIZED.

unreserved_keyword(A) ::= NOTHING.

unreserved_keyword(A) ::= NOTIFY.

unreserved_keyword(A) ::= NOWAIT.

unreserved_keyword(A) ::= NULLS_P.

unreserved_keyword(A) ::= OBJECT_P.

unreserved_keyword(A) ::= OBJECTS_P.

unreserved_keyword(A) ::= OF.

unreserved_keyword(A) ::= OFF.

unreserved_keyword(A) ::= OIDS.

unreserved_keyword(A) ::= OLD.

unreserved_keyword(A) ::= OMIT.

unreserved_keyword(A) ::= OPERATOR.

unreserved_keyword(A) ::= OPTION.

unreserved_keyword(A) ::= OPTIONS.

unreserved_keyword(A) ::= ORDINALITY.

unreserved_keyword(A) ::= OTHERS.

unreserved_keyword(A) ::= OVER.

unreserved_keyword(A) ::= OVERRIDING.

unreserved_keyword(A) ::= OWNED.

unreserved_keyword(A) ::= OWNER.

unreserved_keyword(A) ::= PARALLEL.

unreserved_keyword(A) ::= PARAMETER.

unreserved_keyword(A) ::= PARSER.

unreserved_keyword(A) ::= PARTIAL.

unreserved_keyword(A) ::= PARTITION.

unreserved_keyword(A) ::= PARTITIONS.

unreserved_keyword(A) ::= PASSING.

unreserved_keyword(A) ::= PASSWORD.

unreserved_keyword(A) ::= PATH.

unreserved_keyword(A) ::= PERIOD.

unreserved_keyword(A) ::= PLAN.

unreserved_keyword(A) ::= PLANS.

unreserved_keyword(A) ::= POLICY.

unreserved_keyword(A) ::= PRECEDING.

unreserved_keyword(A) ::= PREPARE.

unreserved_keyword(A) ::= PREPARED.

unreserved_keyword(A) ::= PRESERVE.

unreserved_keyword(A) ::= PRIOR.

unreserved_keyword(A) ::= PRIVILEGES.

unreserved_keyword(A) ::= PROCEDURAL.

unreserved_keyword(A) ::= PROCEDURE.

unreserved_keyword(A) ::= PROCEDURES.

unreserved_keyword(A) ::= PROGRAM.

unreserved_keyword(A) ::= PROPERTIES.

unreserved_keyword(A) ::= PROPERTY.

unreserved_keyword(A) ::= PUBLICATION.

unreserved_keyword(A) ::= QUOTE.

unreserved_keyword(A) ::= QUOTES.

unreserved_keyword(A) ::= RANGE.

unreserved_keyword(A) ::= READ.

unreserved_keyword(A) ::= REASSIGN.

unreserved_keyword(A) ::= RECURSIVE.

unreserved_keyword(A) ::= REF_P.

unreserved_keyword(A) ::= REFERENCING.

unreserved_keyword(A) ::= REFRESH.

unreserved_keyword(A) ::= REINDEX.

unreserved_keyword(A) ::= RELATIONSHIP.

unreserved_keyword(A) ::= RELATIVE_P.

unreserved_keyword(A) ::= RELEASE.

unreserved_keyword(A) ::= RENAME.

unreserved_keyword(A) ::= REPACK.

unreserved_keyword(A) ::= REPEATABLE.

unreserved_keyword(A) ::= REPLACE.

unreserved_keyword(A) ::= REPLICA.

unreserved_keyword(A) ::= RESET.

unreserved_keyword(A) ::= RESPECT_P.

unreserved_keyword(A) ::= RESTART.

unreserved_keyword(A) ::= RESTRICT.

unreserved_keyword(A) ::= RETURN.

unreserved_keyword(A) ::= RETURNS.

unreserved_keyword(A) ::= REVOKE.

unreserved_keyword(A) ::= ROLE.

unreserved_keyword(A) ::= ROLLBACK.

unreserved_keyword(A) ::= ROLLUP.

unreserved_keyword(A) ::= ROUTINE.

unreserved_keyword(A) ::= ROUTINES.

unreserved_keyword(A) ::= ROWS.

unreserved_keyword(A) ::= RULE.

unreserved_keyword(A) ::= SAVEPOINT.

unreserved_keyword(A) ::= SCALAR.

unreserved_keyword(A) ::= SCHEMA.

unreserved_keyword(A) ::= SCHEMAS.

unreserved_keyword(A) ::= SCROLL.

unreserved_keyword(A) ::= SEARCH.

unreserved_keyword(A) ::= SECOND_P.

unreserved_keyword(A) ::= SECURITY.

unreserved_keyword(A) ::= SEQUENCE.

unreserved_keyword(A) ::= SEQUENCES.

unreserved_keyword(A) ::= SERIALIZABLE.

unreserved_keyword(A) ::= SERVER.

unreserved_keyword(A) ::= SESSION.

unreserved_keyword(A) ::= SET.

unreserved_keyword(A) ::= SETS.

unreserved_keyword(A) ::= SHARE.

unreserved_keyword(A) ::= SHOW.

unreserved_keyword(A) ::= SIMPLE.

unreserved_keyword(A) ::= SKIP.

unreserved_keyword(A) ::= SNAPSHOT.

unreserved_keyword(A) ::= SOURCE.

unreserved_keyword(A) ::= SPLIT.

unreserved_keyword(A) ::= SQL_P.

unreserved_keyword(A) ::= STABLE.

unreserved_keyword(A) ::= STANDALONE_P.

unreserved_keyword(A) ::= START.

unreserved_keyword(A) ::= STATEMENT.

unreserved_keyword(A) ::= STATISTICS.

unreserved_keyword(A) ::= STDIN.

unreserved_keyword(A) ::= STDOUT.

unreserved_keyword(A) ::= STORAGE.

unreserved_keyword(A) ::= STORED.

unreserved_keyword(A) ::= STRICT_P.

unreserved_keyword(A) ::= STRING_P.

unreserved_keyword(A) ::= STRIP_P.

unreserved_keyword(A) ::= SUBSCRIPTION.

unreserved_keyword(A) ::= SUPPORT.

unreserved_keyword(A) ::= SYSID.

unreserved_keyword(A) ::= SYSTEM_P.

unreserved_keyword(A) ::= TABLES.

unreserved_keyword(A) ::= TABLESPACE.

unreserved_keyword(A) ::= TARGET.

unreserved_keyword(A) ::= TEMP.

unreserved_keyword(A) ::= TEMPLATE.

unreserved_keyword(A) ::= TEMPORARY.

unreserved_keyword(A) ::= TEXT_P.

unreserved_keyword(A) ::= TIES.

unreserved_keyword(A) ::= TRANSACTION.

unreserved_keyword(A) ::= TRANSFORM.

unreserved_keyword(A) ::= TRIGGER.

unreserved_keyword(A) ::= TRUNCATE.

unreserved_keyword(A) ::= TRUSTED.

unreserved_keyword(A) ::= TYPE_P.

unreserved_keyword(A) ::= TYPES_P.

unreserved_keyword(A) ::= UESCAPE.

unreserved_keyword(A) ::= UNBOUNDED.

unreserved_keyword(A) ::= UNCOMMITTED.

unreserved_keyword(A) ::= UNCONDITIONAL.

unreserved_keyword(A) ::= UNENCRYPTED.

unreserved_keyword(A) ::= UNKNOWN.

unreserved_keyword(A) ::= UNLISTEN.

unreserved_keyword(A) ::= UNLOGGED.

unreserved_keyword(A) ::= UNTIL.

unreserved_keyword(A) ::= UPDATE.

unreserved_keyword(A) ::= VACUUM.

unreserved_keyword(A) ::= VALID.

unreserved_keyword(A) ::= VALIDATE.

unreserved_keyword(A) ::= VALIDATOR.

unreserved_keyword(A) ::= VALUE_P.

unreserved_keyword(A) ::= VARYING.

unreserved_keyword(A) ::= VERSION_P.

unreserved_keyword(A) ::= VERTEX.

unreserved_keyword(A) ::= VIEW.

unreserved_keyword(A) ::= VIEWS.

unreserved_keyword(A) ::= VIRTUAL.

unreserved_keyword(A) ::= VOLATILE.

unreserved_keyword(A) ::= WAIT.

unreserved_keyword(A) ::= WHITESPACE_P.

unreserved_keyword(A) ::= WITHIN.

unreserved_keyword(A) ::= WITHOUT.

unreserved_keyword(A) ::= WORK.

unreserved_keyword(A) ::= WRAPPER.

unreserved_keyword(A) ::= WRITE.

unreserved_keyword(A) ::= XML_P.

unreserved_keyword(A) ::= YEAR_P.

unreserved_keyword(A) ::= YES_P.

unreserved_keyword(A) ::= ZONE.

/* ----- col_name_keyword ----- */

col_name_keyword(A) ::= BETWEEN.

col_name_keyword(A) ::= BIGINT.

col_name_keyword(A) ::= BIT.

col_name_keyword(A) ::= BOOLEAN_P.

col_name_keyword(A) ::= CHAR_P.

col_name_keyword(A) ::= CHARACTER.

col_name_keyword(A) ::= COALESCE.

col_name_keyword(A) ::= DEC.

col_name_keyword(A) ::= DECIMAL_P.

col_name_keyword(A) ::= EXISTS.

col_name_keyword(A) ::= EXTRACT.

col_name_keyword(A) ::= FLOAT_P.

col_name_keyword(A) ::= GRAPH_TABLE.

col_name_keyword(A) ::= GREATEST.

col_name_keyword(A) ::= GROUPING.

col_name_keyword(A) ::= INOUT.

col_name_keyword(A) ::= INT_P.

col_name_keyword(A) ::= INTEGER.

col_name_keyword(A) ::= INTERVAL.

col_name_keyword(A) ::= JSON.

col_name_keyword(A) ::= JSON_ARRAY.

col_name_keyword(A) ::= JSON_ARRAYAGG.

col_name_keyword(A) ::= JSON_EXISTS.

col_name_keyword(A) ::= JSON_OBJECT.

col_name_keyword(A) ::= JSON_OBJECTAGG.

col_name_keyword(A) ::= JSON_QUERY.

col_name_keyword(A) ::= JSON_SCALAR.

col_name_keyword(A) ::= JSON_SERIALIZE.

col_name_keyword(A) ::= JSON_TABLE.

col_name_keyword(A) ::= JSON_VALUE.

col_name_keyword(A) ::= LEAST.

col_name_keyword(A) ::= MERGE_ACTION.

col_name_keyword(A) ::= NATIONAL.

col_name_keyword(A) ::= NCHAR.

col_name_keyword(A) ::= NONE.

col_name_keyword(A) ::= NORMALIZE.

col_name_keyword(A) ::= NULLIF.

col_name_keyword(A) ::= NUMERIC.

col_name_keyword(A) ::= OUT_P.

col_name_keyword(A) ::= OVERLAY.

col_name_keyword(A) ::= POSITION.

col_name_keyword(A) ::= PRECISION.

col_name_keyword(A) ::= REAL.

col_name_keyword(A) ::= ROW.

col_name_keyword(A) ::= SETOF.

col_name_keyword(A) ::= SMALLINT.

col_name_keyword(A) ::= SUBSTRING.

col_name_keyword(A) ::= TIME.

col_name_keyword(A) ::= TIMESTAMP.

col_name_keyword(A) ::= TREAT.

col_name_keyword(A) ::= TRIM.

col_name_keyword(A) ::= VALUES.

col_name_keyword(A) ::= VARCHAR.

col_name_keyword(A) ::= XMLATTRIBUTES.

col_name_keyword(A) ::= XMLCONCAT.

col_name_keyword(A) ::= XMLELEMENT.

col_name_keyword(A) ::= XMLEXISTS.

col_name_keyword(A) ::= XMLFOREST.

col_name_keyword(A) ::= XMLNAMESPACES.

col_name_keyword(A) ::= XMLPARSE.

col_name_keyword(A) ::= XMLPI.

col_name_keyword(A) ::= XMLROOT.

col_name_keyword(A) ::= XMLSERIALIZE.

col_name_keyword(A) ::= XMLTABLE.

/* ----- type_func_name_keyword ----- */

type_func_name_keyword(A) ::= AUTHORIZATION.

type_func_name_keyword(A) ::= BINARY.

type_func_name_keyword(A) ::= COLLATION.

type_func_name_keyword(A) ::= CONCURRENTLY.

type_func_name_keyword(A) ::= CROSS.

type_func_name_keyword(A) ::= CURRENT_SCHEMA.

type_func_name_keyword(A) ::= FREEZE.

type_func_name_keyword(A) ::= FULL.

type_func_name_keyword(A) ::= ILIKE.

type_func_name_keyword(A) ::= INNER_P.

type_func_name_keyword(A) ::= IS.

type_func_name_keyword(A) ::= ISNULL.

type_func_name_keyword(A) ::= JOIN.

type_func_name_keyword(A) ::= LEFT.

type_func_name_keyword(A) ::= LIKE.

type_func_name_keyword(A) ::= NATURAL.

type_func_name_keyword(A) ::= NOTNULL.

type_func_name_keyword(A) ::= OUTER_P.

type_func_name_keyword(A) ::= OVERLAPS.

type_func_name_keyword(A) ::= RIGHT.

type_func_name_keyword(A) ::= SIMILAR.

type_func_name_keyword(A) ::= TABLESAMPLE.

type_func_name_keyword(A) ::= VERBOSE.

/* ----- reserved_keyword ----- */

reserved_keyword(A) ::= ALL.

reserved_keyword(A) ::= ANALYSE.

reserved_keyword(A) ::= ANALYZE.

reserved_keyword(A) ::= AND.

reserved_keyword(A) ::= ANY.

reserved_keyword(A) ::= ARRAY.

reserved_keyword(A) ::= AS.

reserved_keyword(A) ::= ASC.

reserved_keyword(A) ::= ASYMMETRIC.

reserved_keyword(A) ::= BOTH.

reserved_keyword(A) ::= CASE.

reserved_keyword(A) ::= CAST.

reserved_keyword(A) ::= CHECK.

reserved_keyword(A) ::= COLLATE.

reserved_keyword(A) ::= COLUMN.

reserved_keyword(A) ::= CONSTRAINT.

reserved_keyword(A) ::= CREATE.

reserved_keyword(A) ::= CURRENT_CATALOG.

reserved_keyword(A) ::= CURRENT_DATE.

reserved_keyword(A) ::= CURRENT_ROLE.

reserved_keyword(A) ::= CURRENT_TIME.

reserved_keyword(A) ::= CURRENT_TIMESTAMP.

reserved_keyword(A) ::= CURRENT_USER.

reserved_keyword(A) ::= DEFAULT.

reserved_keyword(A) ::= DEFERRABLE.

reserved_keyword(A) ::= DESC.

reserved_keyword(A) ::= DISTINCT.

reserved_keyword(A) ::= DO.

reserved_keyword(A) ::= ELSE.

reserved_keyword(A) ::= END_P.

reserved_keyword(A) ::= EXCEPT.

reserved_keyword(A) ::= FALSE_P.

reserved_keyword(A) ::= FETCH.

reserved_keyword(A) ::= FOR.

reserved_keyword(A) ::= FOREIGN.

reserved_keyword(A) ::= FROM.

reserved_keyword(A) ::= GRANT.

reserved_keyword(A) ::= GROUP_P.

reserved_keyword(A) ::= HAVING.

reserved_keyword(A) ::= IN_P.

reserved_keyword(A) ::= INITIALLY.

reserved_keyword(A) ::= INTERSECT.

reserved_keyword(A) ::= INTO.

reserved_keyword(A) ::= LATERAL_P.

reserved_keyword(A) ::= LEADING.

reserved_keyword(A) ::= LIMIT.

reserved_keyword(A) ::= LOCALTIME.

reserved_keyword(A) ::= LOCALTIMESTAMP.

reserved_keyword(A) ::= NOT.

reserved_keyword(A) ::= NULL_P.

reserved_keyword(A) ::= OFFSET.

reserved_keyword(A) ::= ON.

reserved_keyword(A) ::= ONLY.

reserved_keyword(A) ::= OR.

reserved_keyword(A) ::= ORDER.

reserved_keyword(A) ::= PLACING.

reserved_keyword(A) ::= PRIMARY.

reserved_keyword(A) ::= REFERENCES.

reserved_keyword(A) ::= RETURNING.

reserved_keyword(A) ::= SELECT.

reserved_keyword(A) ::= SESSION_USER.

reserved_keyword(A) ::= SOME.

reserved_keyword(A) ::= SYMMETRIC.

reserved_keyword(A) ::= SYSTEM_USER.

reserved_keyword(A) ::= TABLE.

reserved_keyword(A) ::= THEN.

reserved_keyword(A) ::= TO.

reserved_keyword(A) ::= TRAILING.

reserved_keyword(A) ::= TRUE_P.

reserved_keyword(A) ::= UNION.

reserved_keyword(A) ::= UNIQUE.

reserved_keyword(A) ::= USER.

reserved_keyword(A) ::= USING.

reserved_keyword(A) ::= VARIADIC.

reserved_keyword(A) ::= WHEN.

reserved_keyword(A) ::= WHERE.

reserved_keyword(A) ::= WINDOW.

reserved_keyword(A) ::= WITH.

/* ----- bare_label_keyword ----- */

bare_label_keyword(A) ::= ABORT_P.

bare_label_keyword(A) ::= ABSENT.

bare_label_keyword(A) ::= ABSOLUTE_P.

bare_label_keyword(A) ::= ACCESS.

bare_label_keyword(A) ::= ACTION.

bare_label_keyword(A) ::= ADD_P.

bare_label_keyword(A) ::= ADMIN.

bare_label_keyword(A) ::= AFTER.

bare_label_keyword(A) ::= AGGREGATE.

bare_label_keyword(A) ::= ALL.

bare_label_keyword(A) ::= ALSO.

bare_label_keyword(A) ::= ALTER.

bare_label_keyword(A) ::= ALWAYS.

bare_label_keyword(A) ::= ANALYSE.

bare_label_keyword(A) ::= ANALYZE.

bare_label_keyword(A) ::= AND.

bare_label_keyword(A) ::= ANY.

bare_label_keyword(A) ::= ASC.

bare_label_keyword(A) ::= ASENSITIVE.

bare_label_keyword(A) ::= ASSERTION.

bare_label_keyword(A) ::= ASSIGNMENT.

bare_label_keyword(A) ::= ASYMMETRIC.

bare_label_keyword(A) ::= AT.

bare_label_keyword(A) ::= ATOMIC.

bare_label_keyword(A) ::= ATTACH.

bare_label_keyword(A) ::= ATTRIBUTE.

bare_label_keyword(A) ::= AUTHORIZATION.

bare_label_keyword(A) ::= BACKWARD.

bare_label_keyword(A) ::= BEFORE.

bare_label_keyword(A) ::= BEGIN_P.

bare_label_keyword(A) ::= BETWEEN.

bare_label_keyword(A) ::= BIGINT.

bare_label_keyword(A) ::= BINARY.

bare_label_keyword(A) ::= BIT.

bare_label_keyword(A) ::= BOOLEAN_P.

bare_label_keyword(A) ::= BOTH.

bare_label_keyword(A) ::= BREADTH.

bare_label_keyword(A) ::= BY.

bare_label_keyword(A) ::= CACHE.

bare_label_keyword(A) ::= CALL.

bare_label_keyword(A) ::= CALLED.

bare_label_keyword(A) ::= CASCADE.

bare_label_keyword(A) ::= CASCADED.

bare_label_keyword(A) ::= CASE.

bare_label_keyword(A) ::= CAST.

bare_label_keyword(A) ::= CATALOG_P.

bare_label_keyword(A) ::= CHAIN.

bare_label_keyword(A) ::= CHARACTERISTICS.

bare_label_keyword(A) ::= CHECK.

bare_label_keyword(A) ::= CHECKPOINT.

bare_label_keyword(A) ::= CLASS.

bare_label_keyword(A) ::= CLOSE.

bare_label_keyword(A) ::= CLUSTER.

bare_label_keyword(A) ::= COALESCE.

bare_label_keyword(A) ::= COLLATE.

bare_label_keyword(A) ::= COLLATION.

bare_label_keyword(A) ::= COLUMN.

bare_label_keyword(A) ::= COLUMNS.

bare_label_keyword(A) ::= COMMENT.

bare_label_keyword(A) ::= COMMENTS.

bare_label_keyword(A) ::= COMMIT.

bare_label_keyword(A) ::= COMMITTED.

bare_label_keyword(A) ::= COMPRESSION.

bare_label_keyword(A) ::= CONCURRENTLY.

bare_label_keyword(A) ::= CONDITIONAL.

bare_label_keyword(A) ::= CONFIGURATION.

bare_label_keyword(A) ::= CONFLICT.

bare_label_keyword(A) ::= CONNECTION.

bare_label_keyword(A) ::= CONSTRAINT.

bare_label_keyword(A) ::= CONSTRAINTS.

bare_label_keyword(A) ::= CONTENT_P.

bare_label_keyword(A) ::= CONTINUE_P.

bare_label_keyword(A) ::= CONVERSION_P.

bare_label_keyword(A) ::= COPY.

bare_label_keyword(A) ::= COST.

bare_label_keyword(A) ::= CROSS.

bare_label_keyword(A) ::= CSV.

bare_label_keyword(A) ::= CUBE.

bare_label_keyword(A) ::= CURRENT_P.

bare_label_keyword(A) ::= CURRENT_CATALOG.

bare_label_keyword(A) ::= CURRENT_DATE.

bare_label_keyword(A) ::= CURRENT_ROLE.

bare_label_keyword(A) ::= CURRENT_SCHEMA.

bare_label_keyword(A) ::= CURRENT_TIME.

bare_label_keyword(A) ::= CURRENT_TIMESTAMP.

bare_label_keyword(A) ::= CURRENT_USER.

bare_label_keyword(A) ::= CURSOR.

bare_label_keyword(A) ::= CYCLE.

bare_label_keyword(A) ::= DATA_P.

bare_label_keyword(A) ::= DATABASE.

bare_label_keyword(A) ::= DEALLOCATE.

bare_label_keyword(A) ::= DEC.

bare_label_keyword(A) ::= DECIMAL_P.

bare_label_keyword(A) ::= DECLARE.

bare_label_keyword(A) ::= DEFAULT.

bare_label_keyword(A) ::= DEFAULTS.

bare_label_keyword(A) ::= DEFERRABLE.

bare_label_keyword(A) ::= DEFERRED.

bare_label_keyword(A) ::= DEFINER.

bare_label_keyword(A) ::= DELETE_P.

bare_label_keyword(A) ::= DELIMITER.

bare_label_keyword(A) ::= DELIMITERS.

bare_label_keyword(A) ::= DEPENDS.

bare_label_keyword(A) ::= DEPTH.

bare_label_keyword(A) ::= DESC.

bare_label_keyword(A) ::= DESTINATION.

bare_label_keyword(A) ::= DETACH.

bare_label_keyword(A) ::= DICTIONARY.

bare_label_keyword(A) ::= DISABLE_P.

bare_label_keyword(A) ::= DISCARD.

bare_label_keyword(A) ::= DISTINCT.

bare_label_keyword(A) ::= DO.

bare_label_keyword(A) ::= DOCUMENT_P.

bare_label_keyword(A) ::= DOMAIN_P.

bare_label_keyword(A) ::= DOUBLE_P.

bare_label_keyword(A) ::= DROP.

bare_label_keyword(A) ::= EACH.

bare_label_keyword(A) ::= EDGE.

bare_label_keyword(A) ::= ELSE.

bare_label_keyword(A) ::= EMPTY_P.

bare_label_keyword(A) ::= ENABLE_P.

bare_label_keyword(A) ::= ENCODING.

bare_label_keyword(A) ::= ENCRYPTED.

bare_label_keyword(A) ::= END_P.

bare_label_keyword(A) ::= ENFORCED.

bare_label_keyword(A) ::= ENUM_P.

bare_label_keyword(A) ::= ERROR_P.

bare_label_keyword(A) ::= ESCAPE.

bare_label_keyword(A) ::= EVENT.

bare_label_keyword(A) ::= EXCLUDE.

bare_label_keyword(A) ::= EXCLUDING.

bare_label_keyword(A) ::= EXCLUSIVE.

bare_label_keyword(A) ::= EXECUTE.

bare_label_keyword(A) ::= EXISTS.

bare_label_keyword(A) ::= EXPLAIN.

bare_label_keyword(A) ::= EXPRESSION.

bare_label_keyword(A) ::= EXTENSION.

bare_label_keyword(A) ::= EXTERNAL.

bare_label_keyword(A) ::= EXTRACT.

bare_label_keyword(A) ::= FALSE_P.

bare_label_keyword(A) ::= FAMILY.

bare_label_keyword(A) ::= FINALIZE.

bare_label_keyword(A) ::= FIRST_P.

bare_label_keyword(A) ::= FLOAT_P.

bare_label_keyword(A) ::= FOLLOWING.

bare_label_keyword(A) ::= FORCE.

bare_label_keyword(A) ::= FOREIGN.

bare_label_keyword(A) ::= FORMAT.

bare_label_keyword(A) ::= FORWARD.

bare_label_keyword(A) ::= FREEZE.

bare_label_keyword(A) ::= FULL.

bare_label_keyword(A) ::= FUNCTION.

bare_label_keyword(A) ::= FUNCTIONS.

bare_label_keyword(A) ::= GENERATED.

bare_label_keyword(A) ::= GLOBAL.

bare_label_keyword(A) ::= GRANTED.

bare_label_keyword(A) ::= GRAPH.

bare_label_keyword(A) ::= GRAPH_TABLE.

bare_label_keyword(A) ::= GREATEST.

bare_label_keyword(A) ::= GROUPING.

bare_label_keyword(A) ::= GROUPS.

bare_label_keyword(A) ::= HANDLER.

bare_label_keyword(A) ::= HEADER_P.

bare_label_keyword(A) ::= HOLD.

bare_label_keyword(A) ::= IDENTITY_P.

bare_label_keyword(A) ::= IF_P.

bare_label_keyword(A) ::= ILIKE.

bare_label_keyword(A) ::= IMMEDIATE.

bare_label_keyword(A) ::= IMMUTABLE.

bare_label_keyword(A) ::= IMPLICIT_P.

bare_label_keyword(A) ::= IMPORT_P.

bare_label_keyword(A) ::= IN_P.

bare_label_keyword(A) ::= INCLUDE.

bare_label_keyword(A) ::= INCLUDING.

bare_label_keyword(A) ::= INCREMENT.

bare_label_keyword(A) ::= INDENT.

bare_label_keyword(A) ::= INDEX.

bare_label_keyword(A) ::= INDEXES.

bare_label_keyword(A) ::= INHERIT.

bare_label_keyword(A) ::= INHERITS.

bare_label_keyword(A) ::= INITIALLY.

bare_label_keyword(A) ::= INLINE_P.

bare_label_keyword(A) ::= INNER_P.

bare_label_keyword(A) ::= INOUT.

bare_label_keyword(A) ::= INPUT_P.

bare_label_keyword(A) ::= INSENSITIVE.

bare_label_keyword(A) ::= INSERT.

bare_label_keyword(A) ::= INSTEAD.

bare_label_keyword(A) ::= INT_P.

bare_label_keyword(A) ::= INTEGER.

bare_label_keyword(A) ::= INTERVAL.

bare_label_keyword(A) ::= INVOKER.

bare_label_keyword(A) ::= IS.

bare_label_keyword(A) ::= ISOLATION.

bare_label_keyword(A) ::= JOIN.

bare_label_keyword(A) ::= JSON.

bare_label_keyword(A) ::= JSON_ARRAY.

bare_label_keyword(A) ::= JSON_ARRAYAGG.

bare_label_keyword(A) ::= JSON_EXISTS.

bare_label_keyword(A) ::= JSON_OBJECT.

bare_label_keyword(A) ::= JSON_OBJECTAGG.

bare_label_keyword(A) ::= JSON_QUERY.

bare_label_keyword(A) ::= JSON_SCALAR.

bare_label_keyword(A) ::= JSON_SERIALIZE.

bare_label_keyword(A) ::= JSON_TABLE.

bare_label_keyword(A) ::= JSON_VALUE.

bare_label_keyword(A) ::= KEEP.

bare_label_keyword(A) ::= KEY.

bare_label_keyword(A) ::= KEYS.

bare_label_keyword(A) ::= LABEL.

bare_label_keyword(A) ::= LANGUAGE.

bare_label_keyword(A) ::= LARGE_P.

bare_label_keyword(A) ::= LAST_P.

bare_label_keyword(A) ::= LATERAL_P.

bare_label_keyword(A) ::= LEADING.

bare_label_keyword(A) ::= LEAKPROOF.

bare_label_keyword(A) ::= LEAST.

bare_label_keyword(A) ::= LEFT.

bare_label_keyword(A) ::= LEVEL.

bare_label_keyword(A) ::= LIKE.

bare_label_keyword(A) ::= LISTEN.

bare_label_keyword(A) ::= LOAD.

bare_label_keyword(A) ::= LOCAL.

bare_label_keyword(A) ::= LOCALTIME.

bare_label_keyword(A) ::= LOCALTIMESTAMP.

bare_label_keyword(A) ::= LOCATION.

bare_label_keyword(A) ::= LOCK_P.

bare_label_keyword(A) ::= LOCKED.

bare_label_keyword(A) ::= LOGGED.

bare_label_keyword(A) ::= LSN_P.

bare_label_keyword(A) ::= MAPPING.

bare_label_keyword(A) ::= MATCH.

bare_label_keyword(A) ::= MATCHED.

bare_label_keyword(A) ::= MATERIALIZED.

bare_label_keyword(A) ::= MAXVALUE.

bare_label_keyword(A) ::= MERGE.

bare_label_keyword(A) ::= MERGE_ACTION.

bare_label_keyword(A) ::= METHOD.

bare_label_keyword(A) ::= MINVALUE.

bare_label_keyword(A) ::= MODE.

bare_label_keyword(A) ::= MOVE.

bare_label_keyword(A) ::= NAME_P.

bare_label_keyword(A) ::= NAMES.

bare_label_keyword(A) ::= NATIONAL.

bare_label_keyword(A) ::= NATURAL.

bare_label_keyword(A) ::= NCHAR.

bare_label_keyword(A) ::= NESTED.

bare_label_keyword(A) ::= NEW.

bare_label_keyword(A) ::= NEXT.

bare_label_keyword(A) ::= NFC.

bare_label_keyword(A) ::= NFD.

bare_label_keyword(A) ::= NFKC.

bare_label_keyword(A) ::= NFKD.

bare_label_keyword(A) ::= NO.

bare_label_keyword(A) ::= NODE.

bare_label_keyword(A) ::= NONE.

bare_label_keyword(A) ::= NORMALIZE.

bare_label_keyword(A) ::= NORMALIZED.

bare_label_keyword(A) ::= NOT.

bare_label_keyword(A) ::= NOTHING.

bare_label_keyword(A) ::= NOTIFY.

bare_label_keyword(A) ::= NOWAIT.

bare_label_keyword(A) ::= NULL_P.

bare_label_keyword(A) ::= NULLIF.

bare_label_keyword(A) ::= NULLS_P.

bare_label_keyword(A) ::= NUMERIC.

bare_label_keyword(A) ::= OBJECT_P.

bare_label_keyword(A) ::= OBJECTS_P.

bare_label_keyword(A) ::= OF.

bare_label_keyword(A) ::= OFF.

bare_label_keyword(A) ::= OIDS.

bare_label_keyword(A) ::= OLD.

bare_label_keyword(A) ::= OMIT.

bare_label_keyword(A) ::= ONLY.

bare_label_keyword(A) ::= OPERATOR.

bare_label_keyword(A) ::= OPTION.

bare_label_keyword(A) ::= OPTIONS.

bare_label_keyword(A) ::= OR.

bare_label_keyword(A) ::= ORDINALITY.

bare_label_keyword(A) ::= OTHERS.

bare_label_keyword(A) ::= OUT_P.

bare_label_keyword(A) ::= OUTER_P.

bare_label_keyword(A) ::= OVERLAY.

bare_label_keyword(A) ::= OVERRIDING.

bare_label_keyword(A) ::= OWNED.

bare_label_keyword(A) ::= OWNER.

bare_label_keyword(A) ::= PARALLEL.

bare_label_keyword(A) ::= PARAMETER.

bare_label_keyword(A) ::= PARSER.

bare_label_keyword(A) ::= PARTIAL.

bare_label_keyword(A) ::= PARTITION.

bare_label_keyword(A) ::= PARTITIONS.

bare_label_keyword(A) ::= PASSING.

bare_label_keyword(A) ::= PASSWORD.

bare_label_keyword(A) ::= PATH.

bare_label_keyword(A) ::= PERIOD.

bare_label_keyword(A) ::= PLACING.

bare_label_keyword(A) ::= PLAN.

bare_label_keyword(A) ::= PLANS.

bare_label_keyword(A) ::= POLICY.

bare_label_keyword(A) ::= POSITION.

bare_label_keyword(A) ::= PRECEDING.

bare_label_keyword(A) ::= PREPARE.

bare_label_keyword(A) ::= PREPARED.

bare_label_keyword(A) ::= PRESERVE.

bare_label_keyword(A) ::= PRIMARY.

bare_label_keyword(A) ::= PRIOR.

bare_label_keyword(A) ::= PRIVILEGES.

bare_label_keyword(A) ::= PROCEDURAL.

bare_label_keyword(A) ::= PROCEDURE.

bare_label_keyword(A) ::= PROCEDURES.

bare_label_keyword(A) ::= PROGRAM.

bare_label_keyword(A) ::= PROPERTIES.

bare_label_keyword(A) ::= PROPERTY.

bare_label_keyword(A) ::= PUBLICATION.

bare_label_keyword(A) ::= QUOTE.

bare_label_keyword(A) ::= QUOTES.

bare_label_keyword(A) ::= RANGE.

bare_label_keyword(A) ::= READ.

bare_label_keyword(A) ::= REAL.

bare_label_keyword(A) ::= REASSIGN.

bare_label_keyword(A) ::= RECURSIVE.

bare_label_keyword(A) ::= REF_P.

bare_label_keyword(A) ::= REFERENCES.

bare_label_keyword(A) ::= REFERENCING.

bare_label_keyword(A) ::= REFRESH.

bare_label_keyword(A) ::= REINDEX.

bare_label_keyword(A) ::= RELATIONSHIP.

bare_label_keyword(A) ::= RELATIVE_P.

bare_label_keyword(A) ::= RELEASE.

bare_label_keyword(A) ::= RENAME.

bare_label_keyword(A) ::= REPACK.

bare_label_keyword(A) ::= REPEATABLE.

bare_label_keyword(A) ::= REPLACE.

bare_label_keyword(A) ::= REPLICA.

bare_label_keyword(A) ::= RESET.

bare_label_keyword(A) ::= RESTART.

bare_label_keyword(A) ::= RESTRICT.

bare_label_keyword(A) ::= RETURN.

bare_label_keyword(A) ::= RETURNS.

bare_label_keyword(A) ::= REVOKE.

bare_label_keyword(A) ::= RIGHT.

bare_label_keyword(A) ::= ROLE.

bare_label_keyword(A) ::= ROLLBACK.

bare_label_keyword(A) ::= ROLLUP.

bare_label_keyword(A) ::= ROUTINE.

bare_label_keyword(A) ::= ROUTINES.

bare_label_keyword(A) ::= ROW.

bare_label_keyword(A) ::= ROWS.

bare_label_keyword(A) ::= RULE.

bare_label_keyword(A) ::= SAVEPOINT.

bare_label_keyword(A) ::= SCALAR.

bare_label_keyword(A) ::= SCHEMA.

bare_label_keyword(A) ::= SCHEMAS.

bare_label_keyword(A) ::= SCROLL.

bare_label_keyword(A) ::= SEARCH.

bare_label_keyword(A) ::= SECURITY.

bare_label_keyword(A) ::= SELECT.

bare_label_keyword(A) ::= SEQUENCE.

bare_label_keyword(A) ::= SEQUENCES.

bare_label_keyword(A) ::= SERIALIZABLE.

bare_label_keyword(A) ::= SERVER.

bare_label_keyword(A) ::= SESSION.

bare_label_keyword(A) ::= SESSION_USER.

bare_label_keyword(A) ::= SET.

bare_label_keyword(A) ::= SETOF.

bare_label_keyword(A) ::= SETS.

bare_label_keyword(A) ::= SHARE.

bare_label_keyword(A) ::= SHOW.

bare_label_keyword(A) ::= SIMILAR.

bare_label_keyword(A) ::= SIMPLE.

bare_label_keyword(A) ::= SKIP.

bare_label_keyword(A) ::= SMALLINT.

bare_label_keyword(A) ::= SNAPSHOT.

bare_label_keyword(A) ::= SOME.

bare_label_keyword(A) ::= SOURCE.

bare_label_keyword(A) ::= SPLIT.

bare_label_keyword(A) ::= SQL_P.

bare_label_keyword(A) ::= STABLE.

bare_label_keyword(A) ::= STANDALONE_P.

bare_label_keyword(A) ::= START.

bare_label_keyword(A) ::= STATEMENT.

bare_label_keyword(A) ::= STATISTICS.

bare_label_keyword(A) ::= STDIN.

bare_label_keyword(A) ::= STDOUT.

bare_label_keyword(A) ::= STORAGE.

bare_label_keyword(A) ::= STORED.

bare_label_keyword(A) ::= STRICT_P.

bare_label_keyword(A) ::= STRING_P.

bare_label_keyword(A) ::= STRIP_P.

bare_label_keyword(A) ::= SUBSCRIPTION.

bare_label_keyword(A) ::= SUBSTRING.

bare_label_keyword(A) ::= SUPPORT.

bare_label_keyword(A) ::= SYMMETRIC.

bare_label_keyword(A) ::= SYSID.

bare_label_keyword(A) ::= SYSTEM_P.

bare_label_keyword(A) ::= SYSTEM_USER.

bare_label_keyword(A) ::= TABLE.

bare_label_keyword(A) ::= TABLES.

bare_label_keyword(A) ::= TABLESAMPLE.

bare_label_keyword(A) ::= TABLESPACE.

bare_label_keyword(A) ::= TARGET.

bare_label_keyword(A) ::= TEMP.

bare_label_keyword(A) ::= TEMPLATE.

bare_label_keyword(A) ::= TEMPORARY.

bare_label_keyword(A) ::= TEXT_P.

bare_label_keyword(A) ::= THEN.

bare_label_keyword(A) ::= TIES.

bare_label_keyword(A) ::= TIME.

bare_label_keyword(A) ::= TIMESTAMP.

bare_label_keyword(A) ::= TRAILING.

bare_label_keyword(A) ::= TRANSACTION.

bare_label_keyword(A) ::= TRANSFORM.

bare_label_keyword(A) ::= TREAT.

bare_label_keyword(A) ::= TRIGGER.

bare_label_keyword(A) ::= TRIM.

bare_label_keyword(A) ::= TRUE_P.

bare_label_keyword(A) ::= TRUNCATE.

bare_label_keyword(A) ::= TRUSTED.

bare_label_keyword(A) ::= TYPE_P.

bare_label_keyword(A) ::= TYPES_P.

bare_label_keyword(A) ::= UESCAPE.

bare_label_keyword(A) ::= UNBOUNDED.

bare_label_keyword(A) ::= UNCOMMITTED.

bare_label_keyword(A) ::= UNCONDITIONAL.

bare_label_keyword(A) ::= UNENCRYPTED.

bare_label_keyword(A) ::= UNIQUE.

bare_label_keyword(A) ::= UNKNOWN.

bare_label_keyword(A) ::= UNLISTEN.

bare_label_keyword(A) ::= UNLOGGED.

bare_label_keyword(A) ::= UNTIL.

bare_label_keyword(A) ::= UPDATE.

bare_label_keyword(A) ::= USER.

bare_label_keyword(A) ::= USING.

bare_label_keyword(A) ::= VACUUM.

bare_label_keyword(A) ::= VALID.

bare_label_keyword(A) ::= VALIDATE.

bare_label_keyword(A) ::= VALIDATOR.

bare_label_keyword(A) ::= VALUE_P.

bare_label_keyword(A) ::= VALUES.

bare_label_keyword(A) ::= VARCHAR.

bare_label_keyword(A) ::= VARIADIC.

bare_label_keyword(A) ::= VERBOSE.

bare_label_keyword(A) ::= VERSION_P.

bare_label_keyword(A) ::= VERTEX.

bare_label_keyword(A) ::= VIEW.

bare_label_keyword(A) ::= VIEWS.

bare_label_keyword(A) ::= VIRTUAL.

bare_label_keyword(A) ::= VOLATILE.

bare_label_keyword(A) ::= WAIT.

bare_label_keyword(A) ::= WHEN.

bare_label_keyword(A) ::= WHITESPACE_P.

bare_label_keyword(A) ::= WORK.

bare_label_keyword(A) ::= WRAPPER.

bare_label_keyword(A) ::= WRITE.

bare_label_keyword(A) ::= XML_P.

bare_label_keyword(A) ::= XMLATTRIBUTES.

bare_label_keyword(A) ::= XMLCONCAT.

bare_label_keyword(A) ::= XMLELEMENT.

bare_label_keyword(A) ::= XMLEXISTS.

bare_label_keyword(A) ::= XMLFOREST.

bare_label_keyword(A) ::= XMLNAMESPACES.

bare_label_keyword(A) ::= XMLPARSE.

bare_label_keyword(A) ::= XMLPI.

bare_label_keyword(A) ::= XMLROOT.

bare_label_keyword(A) ::= XMLSERIALIZE.

bare_label_keyword(A) ::= XMLTABLE.

bare_label_keyword(A) ::= YES_P.

bare_label_keyword(A) ::= ZONE.


/* End of grammar: 782 non-terminals, 3584 alternatives */
```

