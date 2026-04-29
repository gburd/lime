# Embedded Language Support Guide

This document covers the embedded language extensions available for the
Lime grammar extension framework. Embedded languages allow non-SQL syntax
to appear directly within SQL statements, parsed by dedicated grammar
rules and translated into appropriate SQL representations.

## Available Embedded Languages

| Extension       | Directory               | Status   | Use Case                |
|-----------------|-------------------------|----------|-------------------------|
| EDN Literals    | `contrib/edn_literals/` | Complete | Clojure-style data      |
| XQuery/XPath    | `contrib/xml_query/`    | Complete | XML querying in SQL      |

## EDN Literals

**Location:** `contrib/edn_literals/`

**Files:**
- `edn_grammar.lime` -- Grammar rules for EDN syntax
- `edn_semantics.h/c` -- Semantic actions (EDN to jsonb/array conversion)
- `edn_literals.c` -- Extension registration with context switching
- `test_queries.sql` -- 30+ test queries

### What is EDN?

EDN (Extensible Data Notation) is a data format from the Clojure ecosystem.
The EDN extension allows EDN literals to appear directly in SQL as
first-class values. This is useful for applications that store Clojure-style
data in PostgreSQL jsonb columns.

### Supported EDN Types

| EDN Syntax       | SQL Mapping         | Example                     |
|------------------|---------------------|-----------------------------|
| `:keyword`       | String enum value   | `WHERE status = :active`    |
| `{:k v ...}`     | jsonb object        | `{:theme "dark" :size 14}`  |
| `[e1 e2 ...]`    | jsonb/SQL array     | `[:admin :editor :viewer]`  |
| `#{e1 e2 ...}`   | Array (unique)      | `#{:read :write :delete}`   |
| `nil`            | SQL NULL            | `WHERE deleted_at = nil`    |
| `true` / `false` | SQL boolean         | `WHERE enabled = true`      |
| `42`, `3.14`     | Numeric literal     | Standard numbers            |
| `"hello"`        | String literal      | Double-quoted strings       |

### Context Switching

The EDN extension uses grammar context switching to enter EDN parsing
mode when boundary tokens are encountered:

- `{:` (LBRACE + KEYWORD) triggers map mode
- `[` in value position triggers vector mode
- `#{` triggers set mode
- Matching closing delimiter exits EDN mode

This is configured using `DISAMBIG_PRIORITY` with priority 4, placing
EDN after core SQL dialects but before user extensions. EDN delimiters
do not conflict with standard SQL syntax, so no fork-resolve is needed.

### Example Queries

```sql
-- Keywords as enum values
SELECT * FROM users WHERE status = :active;
SELECT * FROM config WHERE key IN (:debug :verbose :trace);

-- Namespaced keywords
SELECT * FROM entities WHERE type = :db/user;

-- Map literals (converted to jsonb)
SELECT * FROM users
WHERE preferences = {:theme "dark" :lang "en" :notifications true};

INSERT INTO config (key, data) VALUES
('limits', {:timeout 30 :retries 3 :max-connections 100});

-- Vector literals (converted to arrays)
SELECT * FROM posts WHERE tags @> [:postgres :sql :database];
INSERT INTO users (name, roles) VALUES ('admin', [:admin :editor :viewer]);

-- Set literals (unique collections)
SELECT * FROM roles WHERE permissions && #{:read :write :delete};

-- Nested structures
INSERT INTO config (key, value) VALUES
('user_prefs', {
    :display {:theme "dark" :font-size 14 :line-height 1.5}
    :editor {:tab-size 4 :auto-save true :word-wrap false}
    :shortcuts [:save :run :debug :format]
});

-- EDN in CASE expressions
SELECT id, name,
    CASE status
        WHEN :active THEN 'Active'
        WHEN :pending THEN 'Pending'
        ELSE 'Unknown'
    END AS status_label
FROM users;

-- EDN with JOIN conditions and subqueries
SELECT * FROM users
WHERE id IN (
    SELECT user_id FROM audit_log
    WHERE action = :login
      AND metadata @> {:success true}
);
```

### Registration

```c
#include "edn_literals.c"  // Or include the header when available

ExtensionRegistry *reg = extension_registry_create();
// Register base SQL first ...

GrammarExtensionMetadata edn_meta = {
    .name     = "edn_literals",
    .version  = "1.0.0",
    .strategy = DISAMBIG_PRIORITY,
    .priority = 4,
    .policy   = EXEC_SEQUENTIAL,
    .requires = (const char *[]){"sql_base", NULL},
};
extension_registry_register(reg, &edn_meta);
```

## XQuery/XPath Embedded Language

**Location:** `contrib/xml_query/`

**Files:**
- `xquery_grammar.lime` -- XQuery grammar rules
- `xpath_grammar.lime` -- XPath grammar rules
- `xml_semantics.c` -- Semantic actions
- `xml_query.c` -- Extension registration and demo
- `Makefile` -- Build configuration
- `tests/` -- Test files (FLWOR, constructors, expressions, declarations)

### What is XQuery/XPath?

XQuery and XPath are W3C languages for querying and transforming XML data.
The XQuery extension allows XQuery/XPath expressions to appear in SQL
statements, useful for applications that store XML data in PostgreSQL's
xml type.

### Supported Features

**XPath:**
- Path expressions (`/bookstore/book/title`)
- Predicates (`/book[@price > 10]`)
- Axes (child, descendant, parent, ancestor, etc.)
- Functions (`count()`, `string()`, `contains()`)

**XQuery:**
- FLWOR expressions (`for`, `let`, `where`, `order by`, `return`)
- Element constructors (`<element>{expr}</element>`)
- Conditional expressions (`if ... then ... else`)
- Quantified expressions (`some`, `every`)
- Module declarations

### Test Queries

The XQuery extension includes test files organized by feature area:

- `tests/xpath_compat.txt` -- XPath compatibility expressions
- `tests/flwor.txt` -- FLWOR expression tests
- `tests/constructors.txt` -- Element/attribute constructors
- `tests/expressions.txt` -- General expression tests
- `tests/declarations.txt` -- Module and namespace declarations

## Loading Multiple Embedded Languages

Embedded language extensions can be loaded alongside SQL dialect
extensions without conflicts, since their syntax is distinct from SQL:

```c
ExtensionRegistry *reg = extension_registry_create();

// Register SQL base
// base_sql_register(reg);

// Register SQL dialect
oracle_compat_register(reg);

// Register embedded languages -- these use different delimiters
// and don't conflict with SQL or each other
GrammarExtensionMetadata edn = {
    .name = "edn_literals", .version = "1.0.0",
    .strategy = DISAMBIG_PRIORITY, .priority = 4,
    .requires = (const char *[]){"sql_base", NULL},
};
extension_registry_register(reg, &edn);

// Validate everything
char *error = NULL;
extension_registry_check_dependencies(reg, &error);
```

## How Context Switching Works

When the parser encounters a token that could begin an embedded language
construct, the grammar context switching layer activates:

1. **Detection** -- The context switch module monitors the token stream
   for entry triggers (e.g., `{:` for EDN maps, `<` for XML constructors).

2. **Push** -- The current grammar context is pushed onto a stack, and
   the parser switches to the embedded language's grammar rules.

3. **Parse** -- Tokens are parsed according to the embedded language
   grammar. Nesting is tracked so that inner constructs are handled
   correctly.

4. **Pop** -- When the matching closing delimiter is reached at the
   correct nesting depth, the context is popped and parsing resumes
   with the original SQL grammar.

This mechanism is implemented in `src/grammar_context.c` and
`src/context_switch.c`.

## Conflict Detection with Embedded Languages

Embedded languages rarely conflict with SQL because they use distinct
delimiters. However, edge cases can arise:

| Scenario                          | Resolution                       |
|-----------------------------------|----------------------------------|
| `[` as SQL array vs EDN vector    | Context-aware: check position    |
| `{` as SQL block vs EDN map       | Requires `:` after `{` for EDN   |
| XML `<` vs SQL less-than          | Fork-resolve if ambiguous        |

The multi-grammar conflict detector identifies these:

```c
MultiGrammarConflictResult *result = multi_conflict_result_create();
detect_all_multi_grammar_conflicts(reg, result);

if (result->token_conflicts > 0) {
    // Handle token-level ambiguities
    printf("%u token conflicts detected\n", result->token_conflicts);
}
multi_conflict_result_destroy(result);
```
