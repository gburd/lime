# Token Declarations


All SQL token definitions (keywords and operators) plus
precedence declarations. Converted from PostgreSQL gram.y.


```yaml
name: pg-tokens
version: 17.0.0
description: All token declarations and operator precedence
provides: [pg-tokens, pg-precedence]
```

## Token Definitions

```lime token-definitions
/*
 * PostgreSQL Token Definitions for Lime Parser Generator
 *
 * Converted from PostgreSQL gram.y (Bison format) to Lime format.
 * This file defines all tokens and their precedence levels.
 *
 * In Lime, tokens are implicitly defined when used as UPPERCASE symbols
 * in grammar rules. This file provides explicit declarations for
 * documentation and to establish the token numbering order.
 *
 * IMPORTANT: Non-keyword tokens must be listed first so their numeric
 * codes do not depend on the set of keywords. PL/pgSQL depends on this.
 */

/*
 * Token type: all tokens carry a PgToken value that includes the
 * semantic value and source location.
 */
%token_type {PgToken}

/*
 * Extra argument passed to the parser (replaces Bison's %parse-param).
 * This provides access to the scanner state and parse tree output.
 */
%extra_argument {PgParseState *pstate}

/*
 * Parser name prefix (replaces Bison's %name-prefix="base_yy").
 */
%name pg

/* ======================================================================
 * NON-KEYWORD TOKENS
 *
 * These are hard-wired into the flex lexer. They must be listed first
 * so that their numeric codes do not depend on the set of keywords.
 * ====================================================================== */

/* Identifiers and literals */
%token IDENT.
%token UIDENT.           /* Unicode identifier (reduced to IDENT by parser) */
%token FCONST.           /* Float constant */
%token SCONST.           /* String constant */
%token USCONST.          /* Unicode string constant (reduced to SCONST) */
%token BCONST.           /* Binary string constant */
%token XCONST.           /* Hex string constant */
%token OP.               /* Operator */
%token ICONST.           /* Integer constant */
%token PARAM.            /* Parameter placeholder ($N) */

/* Multi-character operators */
%token TYPECAST.         /* :: */
%token DOT_DOT.          /* .. (used by PL/pgSQL) */
%token COLON_EQUALS.     /* := */
%token EQUALS_GREATER.   /* => */
%token LESS_EQUALS.      /* <= */
%token GREATER_EQUALS.   /* >= */
%token NOT_EQUALS.       /* <> or != */
%token RIGHT_ARROW.      /* -> */

/* ======================================================================
 * KEYWORD TOKENS
 *
 * All SQL keywords, in alphabetical order. Keywords ending in _P have
 * the _P suffix to avoid conflicts with C reserved words or macros.
 * ====================================================================== */

/* A */
%token ABORT_P.
%token ABSENT.
%token ABSOLUTE_P.
%token ACCESS.
%token ACTION.
%token ADD_P.
%token ADMIN.
%token AFTER.
%token AGGREGATE.
%token ALL.
%token ALSO.
%token ALTER.
%token ALWAYS.
%token ANALYSE.
%token ANALYZE.
%token AND.
%token ANY.
%token ARRAY.
%token AS.
%token ASC.
%token ASENSITIVE.
%token ASSERTION.
%token ASSIGNMENT.
%token ASYMMETRIC.
%token AT.
%token ATOMIC.
%token ATTACH.
%token ATTRIBUTE.
%token AUTHORIZATION.

/* B */
%token BACKWARD.
%token BEFORE.
%token BEGIN_P.
%token BETWEEN.
%token BIGINT.
%token BINARY.
%token BIT.
%token BOOLEAN_P.
%token BOTH.
%token BREADTH.
%token BY.

/* C */
%token CACHE.
%token CALL.
%token CALLED.
%token CASCADE.
%token CASCADED.
%token CASE.
%token CAST.
%token CATALOG_P.
%token CHAIN.
%token CHAR_P.
%token CHARACTER.
%token CHARACTERISTICS.
%token CHECK.
%token CHECKPOINT.
%token CLASS.
%token CLOSE.
%token CLUSTER.
%token COALESCE.
%token COLLATE.
%token COLLATION.
%token COLUMN.
%token COLUMNS.
%token COMMENT.
%token COMMENTS.
%token COMMIT.
%token COMMITTED.
%token COMPRESSION.
%token CONCURRENTLY.
%token CONDITIONAL.
%token CONFIGURATION.
%token CONFLICT.
%token CONNECTION.
%token CONSTRAINT.
%token CONSTRAINTS.
%token CONTENT_P.
%token CONTINUE_P.
%token CONVERSION_P.
%token COPY.
%token COST.
%token CREATE.
%token CROSS.
%token CSV.
%token CUBE.
%token CURRENT_P.
%token CURRENT_CATALOG.
%token CURRENT_DATE.
%token CURRENT_ROLE.
%token CURRENT_SCHEMA.
%token CURRENT_TIME.
%token CURRENT_TIMESTAMP.
%token CURRENT_USER.
%token CURSOR.
%token CYCLE.

/* D */
%token DATA_P.
%token DATABASE.
%token DAY_P.
%token DEALLOCATE.
%token DEC.
%token DECIMAL_P.
%token DECLARE.
%token DEFAULT.
%token DEFAULTS.
%token DEFERRABLE.
%token DEFERRED.
%token DEFINER.
%token DELETE_P.
%token DELIMITER.
%token DELIMITERS.
%token DEPENDS.
%token DEPTH.
%token DESC.
%token DESTINATION.
%token DETACH.
%token DICTIONARY.
%token DISABLE_P.
%token DISCARD.
%token DISTINCT.
%token DO.
%token DOCUMENT_P.
%token DOMAIN_P.
%token DOUBLE_P.
%token DROP.

/* E */
%token EACH.
%token EDGE.
%token ELSE.
%token EMPTY_P.
%token ENABLE_P.
%token ENCODING.
%token ENCRYPTED.
%token END_P.
%token ENFORCED.
%token ENUM_P.
%token ERROR_P.
%token ESCAPE.
%token EVENT.
%token EXCEPT.
%token EXCLUDE.
%token EXCLUDING.
%token EXCLUSIVE.
%token EXECUTE.
%token EXISTS.
%token EXPLAIN.
%token EXPRESSION.
%token EXTENSION.
%token EXTERNAL.
%token EXTRACT.

/* F */
%token FALSE_P.
%token FAMILY.
%token FETCH.
%token FILTER.
%token FINALIZE.
%token FIRST_P.
%token FLOAT_P.
%token FOLLOWING.
%token FOR.
%token FORCE.
%token FOREIGN.
%token FORMAT.
%token FORWARD.
%token FREEZE.
%token FROM.
%token FULL.
%token FUNCTION.
%token FUNCTIONS.

/* G */
%token GENERATED.
%token GLOBAL.
%token GRANT.
%token GRANTED.
%token GRAPH.
%token GRAPH_TABLE.
%token GREATEST.
%token GROUP_P.
%token GROUPING.
%token GROUPS.

/* H */
%token HANDLER.
%token HAVING.
%token HEADER_P.
%token HOLD.
%token HOUR_P.

/* I */
%token IDENTITY_P.
%token IF_P.
%token IGNORE_P.
%token ILIKE.
%token IMMEDIATE.
%token IMMUTABLE.
%token IMPLICIT_P.
%token IMPORT_P.
%token IN_P.
%token INCLUDE.
%token INCLUDING.
%token INCREMENT.
%token INDENT.
%token INDEX.
%token INDEXES.
%token INHERIT.
%token INHERITS.
%token INITIALLY.
%token INLINE_P.
%token INNER_P.
%token INOUT.
%token INPUT_P.
%token INSENSITIVE.
%token INSERT.
%token INSTEAD.
%token INT_P.
%token INTEGER.
%token INTERSECT.
%token INTERVAL.
%token INTO.
%token INVOKER.
%token IS.
%token ISNULL.
%token ISOLATION.

/* J */
%token JOIN.
%token JSON.
%token JSON_ARRAY.
%token JSON_ARRAYAGG.
%token JSON_EXISTS.
%token JSON_OBJECT.
%token JSON_OBJECTAGG.
%token JSON_QUERY.
%token JSON_SCALAR.
%token JSON_SERIALIZE.
%token JSON_TABLE.
%token JSON_VALUE.

/* K */
%token KEEP.
%token KEY.
%token KEYS.

/* L */
%token LABEL.
%token LANGUAGE.
%token LARGE_P.
%token LAST_P.
%token LATERAL_P.
%token LEADING.
%token LEAKPROOF.
%token LEAST.
%token LEFT.
%token LEVEL.
%token LIKE.
%token LIMIT.
%token LISTEN.
%token LOAD.
%token LOCAL.
%token LOCALTIME.
%token LOCALTIMESTAMP.
%token LOCATION.
%token LOCK_P.
%token LOCKED.
%token LOGGED.
%token LSN_P.

/* M */
%token MAPPING.
%token MATCH.
%token MATCHED.
%token MATERIALIZED.
%token MAXVALUE.
%token MERGE.
%token MERGE_ACTION.
%token METHOD.
%token MINUTE_P.
%token MINVALUE.
%token MODE.
%token MONTH_P.
%token MOVE.

/* N */
%token NAME_P.
%token NAMES.
%token NATIONAL.
%token NATURAL.
%token NCHAR.
%token NESTED.
%token NEW.
%token NEXT.
%token NFC.
%token NFD.
%token NFKC.
%token NFKD.
%token NO.
%token NODE.
%token NONE.
%token NORMALIZE.
%token NORMALIZED.
%token NOT.
%token NOTHING.
%token NOTIFY.
%token NOTNULL.
%token NOWAIT.
%token NULL_P.
%token NULLIF.
%token NULLS_P.
%token NUMERIC.

/* O */
%token OBJECT_P.
%token OBJECTS_P.
%token OF.
%token OFF.
%token OFFSET.
%token OIDS.
%token OLD.
%token OMIT.
%token ON.
%token ONLY.
%token OPERATOR.
%token OPTION.
%token OPTIONS.
%token OR.
%token ORDER.
%token ORDINALITY.
%token OTHERS.
%token OUT_P.
%token OUTER_P.
%token OVER.
%token OVERLAPS.
%token OVERLAY.
%token OVERRIDING.
%token OWNED.
%token OWNER.

/* P */
%token PARALLEL.
%token PARAMETER.
%token PARSER.
%token PARTIAL.
%token PARTITION.
%token PARTITIONS.
%token PASSING.
%token PASSWORD.
%token PATH.
%token PERIOD.
%token PLACING.
%token PLAN.
%token PLANS.
%token POLICY.
%token POSITION.
%token PRECEDING.
%token PRECISION.
%token PREPARE.
%token PREPARED.
%token PRESERVE.
%token PRIMARY.
%token PRIOR.
%token PRIVILEGES.
%token PROCEDURAL.
%token PROCEDURE.
%token PROCEDURES.
%token PROGRAM.
%token PROPERTIES.
%token PROPERTY.
%token PUBLICATION.

/* Q */
%token QUOTE.
%token QUOTES.

/* R */
%token RANGE.
%token READ.
%token REAL.
%token REASSIGN.
%token RECURSIVE.
%token REF_P.
%token REFERENCES.
%token REFERENCING.
%token REFRESH.
%token REINDEX.
%token RELATIONSHIP.
%token RELATIVE_P.
%token RELEASE.
%token RENAME.
%token REPACK.
%token REPEATABLE.
%token REPLACE.
%token REPLICA.
%token RESET.
%token RESPECT_P.
%token RESTART.
%token RESTRICT.
%token RETURN.
%token RETURNING.
%token RETURNS.
%token REVOKE.
%token RIGHT.
%token ROLE.
%token ROLLBACK.
%token ROLLUP.
%token ROUTINE.
%token ROUTINES.
%token ROW.
%token ROWS.
%token RULE.

/* S */
%token SAVEPOINT.
%token SCALAR.
%token SCHEMA.
%token SCHEMAS.
%token SCROLL.
%token SEARCH.
%token SECOND_P.
%token SECURITY.
%token SELECT.
%token SEQUENCE.
%token SEQUENCES.
%token SERIALIZABLE.
%token SERVER.
%token SESSION.
%token SESSION_USER.
%token SET.
%token SETOF.
%token SETS.
%token SHARE.
%token SHOW.
%token SIMILAR.
%token SIMPLE.
%token SKIP.
%token SMALLINT.
%token SNAPSHOT.
%token SOME.
%token SOURCE.
%token SPLIT.
%token SQL_P.
%token STABLE.
%token STANDALONE_P.
%token START.
%token STATEMENT.
%token STATISTICS.
%token STDIN.
%token STDOUT.
%token STORAGE.
%token STORED.
%token STRICT_P.
%token STRING_P.
%token STRIP_P.
%token SUBSCRIPTION.
%token SUBSTRING.
%token SUPPORT.
%token SYMMETRIC.
%token SYSID.
%token SYSTEM_P.
%token SYSTEM_USER.

/* T */
%token TABLE.
%token TABLES.
%token TABLESAMPLE.
%token TABLESPACE.
%token TARGET.
%token TEMP.
%token TEMPLATE.
%token TEMPORARY.
%token TEXT_P.
%token THEN.
%token TIES.
%token TIME.
%token TIMESTAMP.
%token TO.
%token TRAILING.
%token TRANSACTION.
%token TRANSFORM.
%token TREAT.
%token TRIGGER.
%token TRIM.
%token TRUE_P.
%token TRUNCATE.
%token TRUSTED.
%token TYPE_P.
%token TYPES_P.

/* U */
%token UESCAPE.
%token UNBOUNDED.
%token UNCONDITIONAL.
%token UNCOMMITTED.
%token UNENCRYPTED.
%token UNION.
%token UNIQUE.
%token UNKNOWN.
%token UNLISTEN.
%token UNLOGGED.
%token UNTIL.
%token UPDATE.
%token USER.
%token USING.

/* V */
%token VACUUM.
%token VALID.
%token VALIDATE.
%token VALIDATOR.
%token VALUE_P.
%token VALUES.
%token VARCHAR.
%token VARIADIC.
%token VARYING.
%token VERBOSE.
%token VERSION_P.
%token VERTEX.
%token VIEW.
%token VIEWS.
%token VIRTUAL.
%token VOLATILE.

/* W */
%token WAIT.
%token WHEN.
%token WHERE.
%token WHITESPACE_P.
%token WINDOW.
%token WITH.
%token WITHIN.
%token WITHOUT.
%token WORK.
%token WRAPPER.
%token WRITE.

/* X */
%token XML_P.
%token XMLATTRIBUTES.
%token XMLCONCAT.
%token XMLELEMENT.
%token XMLEXISTS.
%token XMLFOREST.
%token XMLNAMESPACES.
%token XMLPARSE.
%token XMLPI.
%token XMLROOT.
%token XMLSERIALIZE.
%token XMLTABLE.

/* Y */
%token YEAR_P.
%token YES_P.

/* Z */
%token ZONE.

/* ======================================================================
 * LOOKAHEAD TOKENS
 *
 * These tokens are not in kwlist.h and can never be entered directly.
 * The filter in the tokenizer creates these when required (based on
 * looking one token ahead).
 *
 * NOT_LA exists so that productions such as NOT LIKE can be given the
 * same precedence as LIKE; otherwise they'd effectively have the same
 * precedence as NOT.
 *
 * FORMAT_LA, NULLS_LA, WITH_LA, and WITHOUT_LA are needed to make the
 * grammar LALR(1).
 * ====================================================================== */
%token FORMAT_LA.
%token NOT_LA.
%token NULLS_LA.
%token WITH_LA.
%token WITHOUT_LA.

/* ======================================================================
 * MODE TOKENS
 *
 * These tokens are never generated by the scanner. They can be injected
 * by the parser wrapper as the initial token of the string to tell the
 * grammar to parse something other than the usual list of SQL commands.
 * ====================================================================== */
%token MODE_TYPE_NAME.
%token MODE_PLPGSQL_EXPR.
%token MODE_PLPGSQL_ASSIGN1.
%token MODE_PLPGSQL_ASSIGN2.
%token MODE_PLPGSQL_ASSIGN3.

/* ======================================================================
 * PSEUDO-TOKEN FOR PRECEDENCE
 *
 * UMINUS is not a real token. It is used only with %prec (Lime: [UMINUS])
 * to give unary minus higher precedence than binary +/-.
 * ====================================================================== */
%token UMINUS.

/* ======================================================================
 * PRECEDENCE DECLARATIONS
 *
 * Listed from lowest to highest precedence. In Lime, %left, %right, and
 * %nonassoc have the same syntax as in Bison. Each successive declaration
 * defines a higher precedence level.
 *
 * In production rules, use [TOKEN] at the end of a rule to override
 * the default precedence (equivalent to Bison's %prec TOKEN).
 * ====================================================================== */

/* Set operations */
%left UNION EXCEPT.
%left INTERSECT.

/* Logical operators */
%left OR.
%left AND.
%right NOT.

/* IS predicates */
%nonassoc IS ISNULL NOTNULL.

/* Comparison operators */
%nonassoc LT GT EQ LESS_EQUALS GREATER_EQUALS NOT_EQUALS.

/* Pattern matching and range predicates */
%nonassoc BETWEEN IN_P LIKE ILIKE SIMILAR NOT_LA.
%nonassoc ESCAPE.

/* UNBOUNDED and NESTED need lower precedence than IDENT-class tokens */
%nonassoc UNBOUNDED NESTED.

/* Identifiers and keywords that need IDENT-level precedence */
%nonassoc IDENT PARTITION RANGE ROWS GROUPS PRECEDING FOLLOWING CUBE ROLLUP SET KEYS OBJECT_P SCALAR VALUE_P WITH WITHOUT PATH.

/* User-defined and multi-character operators */
%left OP OPERATOR RIGHT_ARROW PIPE.

/* Arithmetic operators */
%left PLUS MINUS.
%left STAR SLASH PERCENT.
%left CARET.

/* Special expression operators */
%left AT.
%left COLLATE.
%right UMINUS.

/* Subscript and grouping */
%left LBRACKET RBRACKET.
%left LPAREN RPAREN.
%left TYPECAST.
%left DOT.

/* JOIN operators (high precedence to support use as function names) */
%left JOIN CROSS LEFT FULL RIGHT INNER_P NATURAL.

/*
 * NOTE on single-character tokens:
 *
 * Bison uses literal characters like '<', '>', '+', '-', etc. directly.
 * Lime does not support single-character literal tokens in the same way.
 * In the Lime grammar, these must be named tokens:
 *
 *   Bison    Lime
 *   -----    ----
 *   '<'      LT
 *   '>'      GT
 *   '='      EQ
 *   '+'      PLUS
 *   '-'      MINUS
 *   '*'      STAR
 *   '/'      SLASH
 *   '%'      PERCENT
 *   '^'      CARET
 *   '|'      PIPE
 *   '['      LBRACKET
 *   ']'      RBRACKET
 *   '('      LPAREN
 *   ')'      RPAREN
 *   '.'      DOT
 *   ','      COMMA
 *   ';'      SEMICOLON
 *   ':'      COLON
 *   '#'      HASH
 *
 * The tokenizer must map these characters to the named tokens above.
 */
%token LT.
%token GT.
%token EQ.
%token PLUS.
%token MINUS.
%token STAR.
%token SLASH.
%token PERCENT.
%token CARET.
%token PIPE.
%token LBRACKET.
%token RBRACKET.
%token LPAREN.
%token RPAREN.
%token DOT.
%token COMMA.
%token SEMICOLON.
%token COLON.
%token HASH.
%token LBRACE.
%token RBRACE.
```

