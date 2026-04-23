# PostgreSQL Grammar (gram.y) Analysis for Lime Conversion

## 1. Grammar Overview

- **Source file**: `_/src/backend/parser/gram.y` (PostgreSQL 18-dev, 2026)
- **Total lines**: 20,839
- **Grammar format**: Bison (LALR(1))
- **Declared `%expect 0`**: zero shift/reduce conflicts expected
- **Production rules**: 782 non-terminal definitions with ~2,773 alternatives
- **Semantic action blocks**: ~1,607
- **`%prec` annotations**: 63 explicit precedence overrides

### Structure

The file is divided into three sections separated by `%%`:

1. **Prologue (lines 1-948)**: C declarations, `%union`, `%token`, `%type`, `%left`/`%right`/`%nonassoc`
2. **Grammar rules (lines 949-19751)**: Production rules with embedded C actions
3. **Epilogue (lines 19752-20839)**: 39 static helper functions + `parser_init()`

---

## 2. Bison Directives

### Parser Configuration

| Directive | Value | Lime Equivalent |
|-----------|-------|-----------------|
| `%pure-parser` | (reentrant) | Not needed; Lime parsers are push-based |
| `%expect 0` | No conflicts | No equivalent; Lime reports all conflicts |
| `%name-prefix="base_yy"` | Prefix for generated symbols | `%name` directive |
| `%locations` | Track source locations | Lime has no built-in location tracking; must be done manually via extra arguments |
| `%parse-param {core_yyscan_t yyscanner}` | Extra parser parameter | Lime uses `%extra_argument` for this |
| `%lex-param {core_yyscan_t yyscanner}` | Extra lexer parameter | Handled by custom tokenizer integration |

### Memory Management

```c
#define YYMALLOC palloc
#define YYFREE   pfree
```

Lime uses `lime_malloc`/`lime_free` or can be overridden. PostgreSQL uses `palloc`/`pfree` for memory-context-based allocation.

---

## 3. Union Type Definition

The `%union` has 49 members spanning these categories:

### Primitive Types
- `int ival` - Integer values
- `char *str` - String values
- `const char *keyword` - Keyword strings
- `char chr` - Single characters
- `bool boolean` - Boolean flags

### Core YYSTYPE Overlap
- `core_YYSTYPE core_yystype` - Must match the core lexer's union

### AST Node Types
- `Node *node` - Generic AST node pointer (most common)
- `List *list` - List of nodes
- `TypeName *typnam` - Type name nodes
- `RangeVar *range` - Range variable (table reference)
- `Alias *alias` - Alias definitions
- `JoinExpr *jexpr` - Join expressions
- `SelectStmt *` (via node) - SELECT statements
- `InsertStmt *istmt` - INSERT statements

### Enum Types
- `JoinType jtype` - JOIN type enum
- `DropBehavior dbehavior` - CASCADE/RESTRICT
- `OnCommitAction oncommit` - ON COMMIT behavior
- `ObjectType objtype` - Object type classification
- `FunctionParameterMode fun_param_mode` - IN/OUT/INOUT
- `SetQuantifier setquantifier` - ALL/DISTINCT
- `MergeMatchKind mergematch` - MERGE MATCHED/NOT MATCHED
- `LimitOption` (via selectlimit) - WITH TIES
- `ReturningOptionKind retoptionkind` - RETURNING options

### Compound Types (grammar-private structs)
- `struct PrivTarget *privtarget` - GRANT/REVOKE target
- `struct ImportQual *importqual` - IMPORT FOREIGN SCHEMA qualification
- `struct SelectLimit *selectlimit` - LIMIT/OFFSET/FETCH
- `struct GroupClause *groupclause` - GROUP BY clause
- `struct KeyActions *keyactions` - Foreign key actions
- `struct KeyAction *keyaction` - Single FK action

### Other Node Types
- `FunctionParameter *fun_param`, `ObjectWithArgs *objwithargs`
- `DefElem *defelt`, `SortBy *sortby`, `WindowDef *windef`
- `IndexElem *ielem`, `StatsElem *selem`
- `IntoClause *into`, `WithClause *with`
- `InferClause *infer`, `OnConflictClause *onconflict`
- `A_Indices *aind`, `ResTarget *target`, `AccessPriv *accesspriv`
- `VariableSetStmt *vsetstmt`
- `PartitionElem *partelem`, `PartitionSpec *partspec`, `PartitionBoundSpec *partboundspec`
- `SinglePartitionSpec *singlepartspec`
- `RoleSpec *rolespec`
- `PublicationObjSpec *publicationobjectspec`
- `PublicationAllObjSpec *publicationallobjectspec`
- `MergeWhenClause *mergewhen`
- `ReturningClause *retclause`

### Lime Conversion Strategy for Union

Lime does not support `%union`. Instead, each non-terminal gets its own C type via `%type`. For example:
```
%type stmt {Node*}
%type target_list {List*}
%type Iconst {int}
```

---

## 4. Token Declarations

### Non-keyword Tokens (hard-wired in lexer)

| Token | Type | Description |
|-------|------|-------------|
| IDENT | `<str>` | Identifier |
| UIDENT | `<str>` | Unicode identifier (reduced to IDENT in parser.c) |
| FCONST | `<str>` | Float constant |
| SCONST | `<str>` | String constant |
| USCONST | `<str>` | Unicode string constant (reduced to SCONST) |
| BCONST | `<str>` | Binary string constant |
| XCONST | `<str>` | Hex string constant |
| Op | `<str>` | Operator |
| ICONST | `<ival>` | Integer constant |
| PARAM | `<ival>` | Parameter ($N) |
| TYPECAST | (none) | `::` |
| DOT_DOT | (none) | `..` (used by PL/pgSQL) |
| COLON_EQUALS | (none) | `:=` |
| EQUALS_GREATER | (none) | `=>` |
| LESS_EQUALS | (none) | `<=` |
| GREATER_EQUALS | (none) | `>=` |
| NOT_EQUALS | (none) | `<>` or `!=` |

### Lookahead Tokens (injected by parser.c)

| Token | Purpose |
|-------|---------|
| FORMAT_LA | Makes grammar LALR(1) for FORMAT |
| NOT_LA | Gives NOT LIKE same precedence as LIKE |
| NULLS_LA | Makes grammar LALR(1) for NULLS |
| WITH_LA | Makes grammar LALR(1) for WITH |
| WITHOUT_LA | Makes grammar LALR(1) for WITHOUT |

### Mode Tokens (injected as initial token)

| Token | Purpose |
|-------|---------|
| MODE_TYPE_NAME | Parse a type name |
| MODE_PLPGSQL_EXPR | Parse a PL/pgSQL expression |
| MODE_PLPGSQL_ASSIGN1 | Parse 1-level PL/pgSQL assignment |
| MODE_PLPGSQL_ASSIGN2 | Parse 2-level PL/pgSQL assignment |
| MODE_PLPGSQL_ASSIGN3 | Parse 3-level PL/pgSQL assignment |

### Keyword Tokens

All keywords use `%token <keyword>`. Total keyword count: ~507 (across the alphabetical listing from ABORT_P to ZONE).

---

## 5. Keyword Categories

PostgreSQL classifies keywords into reservation levels:

| Category | Count | Description |
|----------|-------|-------------|
| `unreserved_keyword` | 345 | Can be used as any kind of name |
| `col_name_keyword` | 64 | Can be column/table names but not generic type/function names |
| `type_func_name_keyword` | 23 | Can be type or function names |
| `reserved_keyword` | 78 | Only usable as ColLabel (after AS) |
| `bare_label_keyword` | 469 | Can be used as bare column label (without AS) |

### Name Productions

The keyword categories feed into these identifier productions:

- **ColId** = IDENT | unreserved_keyword | col_name_keyword
- **type_function_name** = IDENT | unreserved_keyword | type_func_name_keyword
- **ColLabel** = IDENT | unreserved_keyword | col_name_keyword | type_func_name_keyword | reserved_keyword
- **BareColLabel** = IDENT | bare_label_keyword

---

## 6. Precedence Declarations (lowest to highest)

| Level | Associativity | Tokens |
|-------|--------------|--------|
| 1 | `%left` | UNION EXCEPT |
| 2 | `%left` | INTERSECT |
| 3 | `%left` | OR |
| 4 | `%left` | AND |
| 5 | `%right` | NOT |
| 6 | `%nonassoc` | IS ISNULL NOTNULL |
| 7 | `%nonassoc` | `<` `>` `=` LESS_EQUALS GREATER_EQUALS NOT_EQUALS |
| 8 | `%nonassoc` | BETWEEN IN_P LIKE ILIKE SIMILAR NOT_LA |
| 9 | `%nonassoc` | ESCAPE |
| 10 | `%nonassoc` | UNBOUNDED NESTED |
| 11 | `%nonassoc` | IDENT PARTITION RANGE ROWS GROUPS PRECEDING FOLLOWING CUBE ROLLUP SET KEYS OBJECT_P SCALAR VALUE_P WITH WITHOUT PATH |
| 12 | `%left` | Op OPERATOR RIGHT_ARROW `\|` |
| 13 | `%left` | `+` `-` |
| 14 | `%left` | `*` `/` `%` |
| 15 | `%left` | `^` |
| 16 | `%left` | AT |
| 17 | `%left` | COLLATE |
| 18 | `%right` | UMINUS |
| 19 | `%left` | `[` `]` |
| 20 | `%left` | `(` `)` |
| 21 | `%left` | TYPECAST |
| 22 | `%left` | `.` |
| 23 | `%left` | JOIN CROSS LEFT FULL RIGHT INNER_P NATURAL |

Note: UMINUS is a pseudo-token used only with `%prec` to give unary minus higher precedence than binary `+`/`-`.

### Lime Conversion

Lime uses the same `%left`, `%right`, `%nonassoc` directives. The syntax is identical. The `%prec` annotations on individual rules are also supported by Lime with the `[TOKENNAME]` syntax at the end of a rule.

---

## 7. Non-Terminal Type Distribution

243 `%type` declaration lines defining types for non-terminals:

| Union Member | Count | Description |
|-------------|-------|-------------|
| `<list>` | 54 | List-valued non-terminals |
| `<node>` | 45 | Generic Node* non-terminals |
| `<str>` | 30 | String-valued non-terminals |
| `<ival>` | 23 | Integer-valued non-terminals |
| `<defelt>` | 17 | DefElem* non-terminals |
| `<boolean>` | 17 | Boolean-valued non-terminals |
| `<range>` | 5 | RangeVar* non-terminals |
| `<keyword>` | 3 | Keyword category non-terminals |
| Others | 49 | Various specialized types |

---

## 8. Grammar Organization (Production Rules)

### Top-Level Structure

```
parse_toplevel -> stmtmulti | MODE_TYPE_NAME Typename | MODE_PLPGSQL_* ...
stmtmulti -> stmtmulti ';' toplevel_stmt | toplevel_stmt
toplevel_stmt -> stmt | TransactionStmtLegacy
stmt -> AlterEventTrigStmt | ... | ViewStmt | /* empty */
```

The `stmt` production has ~95 alternatives covering all SQL statement types.

### Major Statement Categories

**DDL Statements (~35 types)**:
- CREATE: Table, Index, View, Function, Trigger, Schema, Domain, Type, Sequence, etc.
- ALTER: Table, Column, Domain, Type, Sequence, Owner, Schema, etc.
- DROP: Generic via DropStmt, plus specialized drops

**DML Statements (~6 types)**:
- SelectStmt, InsertStmt, UpdateStmt, DeleteStmt, MergeStmt, CopyStmt

**TCL Statements (~3 types)**:
- TransactionStmt, TransactionStmtLegacy (BEGIN/END at top level)

**DCL Statements (~4 types)**:
- GrantStmt, RevokeStmt, GrantRoleStmt, RevokeRoleStmt

**Utility Statements (~40+ types)**:
- EXPLAIN, ANALYZE, VACUUM, REINDEX, CLUSTER, CHECKPOINT, etc.

### Expression Hierarchy

```
a_expr -> c_expr | a_expr op a_expr | ...     (full expression)
b_expr -> c_expr | b_expr op b_expr | ...     (restricted, no subquery operators)
c_expr -> columnref | AexprConst | PARAM | ... (primary expressions)
```

The `a_expr` production has the most alternatives (~100+), covering:
- Arithmetic operators (+, -, *, /, %, ^)
- Comparison operators (<, >, =, <=, >=, <>)
- Logical operators (AND, OR, NOT)
- Type casts (::)
- IS NULL / IS NOT NULL / IS TRUE / IS FALSE
- BETWEEN, IN, LIKE, ILIKE, SIMILAR TO
- CASE expressions
- Subquery expressions (EXISTS, IN, ANY, ALL)
- Array constructors and subscripts
- JSON expressions

### Type System

```
Typename -> SimpleTypename opt_array_bounds
SimpleTypename -> GenericType | Numeric | Bit | Character | ConstDatetime | ConstInterval | JsonType
GenericType -> type_function_name opt_type_modifiers | ...
```

---

## 9. Helper Functions Inventory

39 static helper functions defined in the epilogue (after second `%%`):

### AST Node Construction

| Function | Purpose |
|----------|---------|
| `makeRawStmt()` | Wrap a statement Node in a RawStmt with location |
| `updateRawStmtEnd()` | Set the end position of a RawStmt |
| `makeColumnRef()` | Create ColumnRef node, handling indirection |
| `makeTypeCast()` | Create TypeCast node |
| `makeStringConstCast()` | String constant with type cast |
| `makeIntConst()` | Integer constant (A_Const with Integer) |
| `makeFloatConst()` | Float constant (A_Const with Float) |
| `makeBoolAConst()` | Boolean constant (TypeCast of string "t"/"f") |
| `makeBitStringConst()` | Bit string constant |
| `makeNullAConst()` | NULL constant |
| `makeAConst()` | Generic A_Const from value node |
| `makeRoleSpec()` | Create RoleSpec node |

### Expression Construction

| Function | Purpose |
|----------|---------|
| `makeSetOp()` | Create UNION/INTERSECT/EXCEPT node |
| `doNegate()` | Negate a numeric expression |
| `doNegateFloat()` | Negate a Float value in place |
| `makeAndExpr()` | Create BoolExpr AND node |
| `makeOrExpr()` | Create BoolExpr OR node |
| `makeNotExpr()` | Create BoolExpr NOT node |
| `makeAArrayExpr()` | Create ARRAY[] constructor |
| `makeSQLValueFunction()` | Create CURRENT_DATE/TIME/USER etc. |
| `makeXmlExpr()` | Create XML function expression |
| `makeRecursiveViewSelect()` | Build SELECT for CREATE RECURSIVE VIEW |

### Validation and Extraction

| Function | Purpose |
|----------|---------|
| `check_qualified_name()` | Validate dotted name components |
| `check_func_name()` | Validate function name format |
| `check_indirection()` | Validate indirection list |
| `extractArgTypes()` | Extract type list from function parameters |
| `extractAggrArgTypes()` | Extract aggregate argument types |
| `makeOrderedSetArgs()` | Validate/merge ordered-set aggregate args |
| `insertSelectOptions()` | Attach ORDER BY/LIMIT/FOR UPDATE to SELECT |
| `mergeTableFuncParameters()` | Merge function args with table columns |
| `TableFuncTypeName()` | Build TypeName for table function |
| `makeRangeVarFromAnyName()` | Convert name list to RangeVar |
| `makeRangeVarFromQualifiedName()` | Convert qualified name to RangeVar |
| `SplitColQualList()` | Separate constraints from collation in column qualifiers |
| `processCASbits()` | Process constraint attribute bitmask |
| `parsePartitionStrategy()` | Validate partition strategy string |

### Publication Helpers

| Function | Purpose |
|----------|---------|
| `preprocess_pub_all_objtype_list()` | Process publication ALL TABLES/SEQUENCES |
| `preprocess_pubobj_list()` | Process publication object list |

### Error Handling

| Function | Purpose |
|----------|---------|
| `base_yyerror()` | Bison error callback (delegates to scanner_yyerror) |

---

## 10. PostgreSQL-Specific Constructs

### Private Structs (defined in prologue, not in standard headers)

- `PrivTarget` - GRANT/REVOKE target (targtype, objtype, objs)
- `ImportQual` - IMPORT FOREIGN SCHEMA qualification
- `SelectLimit` - LIMIT/OFFSET with locations and WITH TIES
- `GroupClause` - GROUP BY with DISTINCT/ALL
- `KeyAction` / `KeyActions` - Foreign key ON UPDATE/DELETE actions

### Constraint Attribute Bitmask

```c
#define CAS_NOT_DEFERRABLE    0x01
#define CAS_DEFERRABLE        0x02
#define CAS_INITIALLY_IMMEDIATE 0x04
#define CAS_INITIALLY_DEFERRED 0x08
#define CAS_NOT_VALID         0x10
#define CAS_NO_INHERIT        0x20
#define CAS_NOT_ENFORCED      0x40
#define CAS_ENFORCED          0x80
```

### Location Tracking

The grammar uses `@N` references (704 occurrences) to track source locations. This is Bison's `%locations` feature. Lime does not have built-in location tracking, so this must be handled differently (see conversion strategy below).

### Scanner Integration

- `yyscanner` passed as extra parameter to both parser and lexer
- `pg_yyget_extra(yyscanner)` accesses the parser state
- Lookahead token injection via `parser.c` (FORMAT_LA, NOT_LA, etc.)

---

## 11. Bison vs Lime Conversion Strategy

### Syntax Differences

| Bison | Lime | Notes |
|-------|------|-------|
| `%union { ... }` | No union; use `%type NT {CType}` per non-terminal | Major rewrite |
| `$$ = expr;` | Assign via named argument, e.g., `A = expr;` | All actions rewritten |
| `$1, $2, $3` | Named: `A(a). { ... a ... }` | Named references |
| `@1, @2, @3` | No equivalent | Must add location as explicit parameter or use `%extra_argument` |
| `%token <type> NAME` | `%token_type {type}` + define tokens | Different approach |
| `%type <member> NT` | `%type NT {CType}` | Per-nonterminal |
| `%left`, `%right`, `%nonassoc` | `%left`, `%right`, `%nonassoc` | Same syntax |
| `%prec TOKEN` | `[TOKEN]` at end of rule | Different syntax |
| `%pure-parser` | Always push-parser | Not needed |
| `%parse-param {T arg}` | `%extra_argument {T *arg}` | Similar |
| `%lex-param {T arg}` | Handled by tokenizer function | Custom integration |
| `%expect N` | No equivalent | Lime always reports conflicts |
| `%name-prefix="xxx"` | `%name xxx` | Simpler |
| `%locations` | Not supported | Must implement manually |
| `YYERROR;` | No direct equivalent | Use error token or `%syntax_error` |
| `/* empty */` | Omit (Lime handles empty rules) | Simpler syntax |

### Key Conversion Challenges

1. **Location tracking (`@N`)**: 704 uses. Lime has no `%locations`. Options:
   - Add location as a field in each AST node (already done by PG)
   - Pass location through `%extra_argument`
   - Create wrapper macros that extract location from token metadata
   - Most practical: make the tokenizer attach location to each token value

2. **Union elimination**: The `%union` with 49 members must be replaced with per-nonterminal `%type` declarations. Each of the 243 `%type` lines needs conversion.

3. **Semantic action rewriting**: ~3,095 `$N` references and ~2,702 `$$` assignments need conversion to Lime's named-argument style.

4. **Scanner integration**: Bison's `%pure-parser` + `%lex-param`/`%parse-param` pattern must be replaced with Lime's `%extra_argument` and custom token-feeding mechanism.

5. **Lookahead token injection**: The FORMAT_LA, NOT_LA, NULLS_LA, WITH_LA, WITHOUT_LA tokens are injected by `parser.c` looking one token ahead. This mechanism must be replicated in the Lime tokenizer wrapper.

6. **Error recovery**: Bison's `error` token and `YYERROR` macro need replacement with Lime's `%syntax_error` and `%parse_failure` directives.

7. **Keyword reservation levels**: The 5 keyword category productions (unreserved, col_name, type_func_name, reserved, bare_label) are large but mechanically convertible.

8. **Grammar size**: 782 productions with ~2,773 alternatives. This is one of the largest real-world grammars. Lime can handle it, but build time and table size should be monitored.

### Recommended Conversion Order

1. Convert token definitions (`%token` -> Lime token declarations)
2. Convert type system (`%union` + `%type` -> per-NT `%type`)
3. Convert precedence declarations (mostly identical syntax)
4. Port helper functions to a separate C file (no grammar changes needed)
5. Convert production rules incrementally by category:
   a. Start with expression grammar (a_expr, b_expr, c_expr)
   b. Then simple statements (SELECT, INSERT, UPDATE, DELETE)
   c. Then DDL statements
   d. Then utility statements
   e. Finally, keyword category lists
6. Implement location tracking mechanism
7. Implement tokenizer integration with lookahead injection

### Estimated Scope

- ~507 token definitions to convert
- ~243 type declarations to convert
- ~23 precedence lines (mostly 1:1)
- ~782 non-terminal definitions (~2,773 alternatives) with semantic actions
- ~39 helper functions to port (can be a separate .c file)
- ~5,800 semantic value references ($$ and $N) to convert to named arguments
- ~704 location references (@N) to replace with alternative mechanism

---

## 12. Grammar Statistics Summary

| Metric | Count |
|--------|-------|
| Total lines | 20,839 |
| Token types (non-keyword) | 17 |
| Keyword tokens | ~507 |
| Lookahead tokens | 5 |
| Mode tokens | 5 |
| Union members | 49 |
| %type declaration lines | 243 |
| Precedence levels | 23 |
| %prec annotations | 63 |
| Non-terminal definitions | 782 |
| Production alternatives | ~2,773 |
| Semantic action blocks | ~1,607 |
| $$ assignments | ~2,702 |
| $N references | ~3,095 |
| @N location references | ~704 |
| Helper functions | 39 |
| Keyword categories | 5 |
| Unreserved keywords | 345 |
| Column-name keywords | 64 |
| Type/function-name keywords | 23 |
| Reserved keywords | 78 |
| Bare-label keywords | 469 |
