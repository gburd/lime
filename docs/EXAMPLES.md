# Examples

Lime ships with examples ranging from a simple calculator to a full
PostgreSQL SQL grammar.  All examples are in the `examples/` directory;
SQL dialect extensions are in `contrib/`.

## Calculator (`examples/calc/`)

A four-operation calculator extended at runtime with shared library
plugins.  This is the best starting point for understanding Lime's
extension system.

**What it demonstrates:**
- Base grammar definition (`calc.lime`: `+`, `-`, `*`, `/`)
- Runtime extension loading via `dlopen`
- Conflicting extensions and conflict detection

Two extensions both define the `^` (caret) token:

| Extension | `^` means | `2 ^ 3` | Associativity |
|-----------|-----------|---------|---------------|
| `calc_power` | Exponentiation | 8 | Right |
| `calc_bitwise` | Bitwise XOR | 1 | Left |

The `main_conflicts.c` driver shows four resolution scenarios: power
only, bitwise only, both loaded with power winning, both loaded with
bitwise winning.

**Build:** `cd examples/calc && make all`

**Files:** `calc.lime`, `calc_power.lime`, `calc_bitwise.lime`,
`calc_power_ext.c`, `calc_bitwise_ext.c`, `main.c`,
`main_conflicts.c`, `tokenize.c`

## JSONB Extension (`examples/jsonb_extension.c`)

A standalone, heavily annotated example of writing a grammar extension.
Adds PostgreSQL-style JSONB operators (`->`, `->>`, `@>`, `<@`, `?`)
to an existing parser.

**What it demonstrates:**
- Defining `GrammarModification` structs (new tokens, new rules)
- The `get_modifications` callback pattern
- Registering and loading an extension

This file is intended as a copy-and-modify template for extension
developers.

## Plugin Template (`examples/plugin_template/`)

A minimal, reusable template for building SQL parser plugins.

**Files:**
- `sql_plugin.c` — plugin implementation skeleton
- `plugin_host.c` — host application that loads the plugin
- `meson.build` — build configuration

## PostgreSQL Full Grammar (`examples/pg/`)

The complete PostgreSQL SQL grammar converted from Bison to Lime format.
This is a production-scale example: 782 non-terminals, 3,584 production
rules, 561 terminal symbols, 3,841 parser states, zero conflicts.

**What it demonstrates:**
- Lime handles grammars as large as PostgreSQL's
- Conversion from Bison format (see `convert_gram.py`)
- Token definitions, type declarations, and semantic actions

**Files:** `gram.lime` (20,607 lines), `tokens.lime`, `types.lime`,
`tokenize.c/h`, `pg_gram_helpers.c/h`, `convert_gram.py`, `Makefile`

**Build:** `cd examples/pg && make`

## PostgreSQL Modular Grammar (`examples/pg_modular/`)

The same PostgreSQL grammar decomposed into 35 literate modules
organized by SQL domain:

```
pg_modular/
├── base/           keywords, type declarations, top-level rules
├── ddl/            CREATE/ALTER/DROP TABLE, INDEX, VIEW, ...
├── dml/            SELECT, INSERT, UPDATE, DELETE, MERGE
├── expr/           expressions, JSON/XML, graph queries
├── cte/            common table expressions
├── from_clause/    FROM clause and joins
├── functions/      function calls and definitions
├── window/         window functions
├── transactions/   BEGIN, COMMIT, ROLLBACK, savepoints
├── select_targets/ SELECT target lists and clauses
├── security/       GRANT, REVOKE, row-level security
└── utility/        SET, SHOW, EXPLAIN, VACUUM, ...
```

Modules declare dependencies via `%require` directives and are composed
with `tools/lime-compose` into a single grammar that produces the same
parser as the monolithic version.

**Build:** `cd examples/pg_modular && make`

## PostgreSQL Subsystem Parsers

Several examples parse specific PostgreSQL subsystems:

### pgbench Expressions (`examples/pgbench/`)

The pgbench expression parser, converted from `exprparse.y`.  Supports
arithmetic, comparison, bitwise, logical operators, IS predicates, CASE
expressions, function calls, and variables (`:varname`).

**Build:** `cd examples/pgbench && make`
**Test:** `./pgbench_parse tests/*.expr`

### Bootstrap (BKI) Parser (`examples/bootstrap/`)

Parses PostgreSQL BKI (Backend Interface) files used by `initdb` to
initialize system catalogs.

### Isolation Test Parser (`examples/isolation/`)

Parses `.spec` files for PostgreSQL's isolation test framework
(concurrent session testing).

### Synchronous Replication Config (`examples/syncrep/`)

Parses the `synchronous_standby_names` GUC setting (priority-based and
quorum-based standby selection).

### Replication Protocol (`examples/replication/`)

Parses PostgreSQL replication commands: `IDENTIFY_SYSTEM`,
`BASE_BACKUP`, `START_REPLICATION`, `CREATE_REPLICATION_SLOT`, etc.

## Query Language Parsers

### JSONPath (`examples/jsonpath/`)

PostgreSQL's JSONPath grammar converted from `jsonpath_gram.y`.
Approximately 430 rules covering the SQL/JSON path language.

### XPath 1.0 (`examples/xpath/`)

A complete XPath 1.0 parser (W3C Recommendation) covering all 13 axes,
predicates, and the full operator precedence hierarchy.

### XQuery 1.0 (`examples/xquery/`)

Extends the XPath grammar with FLWOR expressions (`for`, `let`,
`where`, `order by`, `return`), element constructors, and function
declarations.

### MongoDB Query Language (`examples/mongodb/`)

A parser for MongoDB's query language including queries, updates, and
aggregation pipeline stages.

### Datalog/EDN (`examples/datalog/`)

A Datalog parser with EDN (Extensible Data Notation) data types.
Written in literate grammar format.

## LLM Oracle Disambiguation (`examples/llm_oracle/`)

Demonstrates using an LLM (OpenAI or Anthropic) as a disambiguation
strategy.  When multiple grammar extensions conflict, the LLM is
queried to decide which interpretation is correct.

**Files:**
- `llm_client.c/h` — HTTP client using libcurl for OpenAI/Anthropic APIs
- `nlsql_extension.c` — natural-language SQL extension with LLM
  disambiguation strategy
- `Makefile` — builds with or without libcurl

**Build:** `cd examples/llm_oracle && make` (requires libcurl) or
`make no-curl` (stub mode for testing without network)

## Literate Grammar Format (`examples/literate/`)

A minimal example of the literate grammar format: grammar rules and
token definitions embedded in Markdown files.

**Files:** `grammar.md`, `tokens.md`

## SQL Dialect Extensions (`contrib/`)

Production-ready grammar extensions for SQL dialect compatibility:

| Extension | Directory | Description |
|-----------|-----------|-------------|
| Oracle | `contrib/oracle_compat/` | `CONNECT BY`, `(+)` outer join, `ROWNUM`, `NVL`, `DECODE` |
| SQLite | `contrib/sqlite_compat/` | `ON CONFLICT`, `WITHOUT ROWID`, `ATTACH`/`DETACH`, `PRAGMA` |
| MySQL | `contrib/mysql_compat/` | Backtick quoting, `LIMIT offset,count`, `SHOW`, `USE` |
| DuckDB | `contrib/duckdb_features/` | `STRUCT`, `LIST`, `MAP` types, `COPY ... TO` |
| EDN | `contrib/edn_literals/` | EDN (Extensible Data Notation) literal syntax in SQL |
| XQuery/XPath | `contrib/xml_query/` | `xmlquery()`, `xpath()` function grammar |

Each extension includes a `.lime` grammar, a C plugin implementation,
semantic action handlers, test queries, and a Makefile.

**Build any extension:** `cd contrib/<name> && make`

## PostgreSQL Integration Guide (`examples/lime_postgres/`)

Not a parser itself, but a set of guides for integrating the Lime
extension framework with PostgreSQL:

- `README.md` — architecture overview and quick start
- `EXTENSION_AUTHORING.md` — writing custom extensions
- `DIALECT_SUPPORT.md` — dialect-specific extension patterns
- `EMBEDDED_LANGUAGES.md` — embedding non-SQL languages in SQL
