# SQL Dialect Support Guide

This document covers the SQL dialect extensions available for the Lime
grammar extension framework. Each dialect is implemented as a self-contained
extension in `contrib/` that registers with the framework and adds
dialect-specific tokens, rules, and semantic actions.

## Available Dialects

| Extension          | Directory                  | Status    | Features |
|--------------------|----------------------------|-----------|----------|
| Oracle Compat      | `contrib/oracle_compat/`   | Complete  | 17 tokens, 20+ rules |
| SQLite Compat      | `contrib/sqlite_compat/`   | Complete  | Table options, type affinity |
| MySQL Compat       | `contrib/mysql_compat/`    | In progress | Backtick quoting, LIMIT |

## Oracle SQL Compatibility

**Location:** `contrib/oracle_compat/`

**Files:**
- `oracle_grammar.lime` -- Grammar rules in Lime format
- `oracle_semantics.h/c` -- Semantic action implementations
- `oracle_compat.h/c` -- Extension registration and lifecycle
- `test_oracle.c` -- Unit tests
- `test_queries.sql` -- 30+ test queries

### Supported Features

1. **ROWNUM / ROWID pseudo-columns** -- Row numbering and physical row IDs
2. **SYSDATE / SYSTIMESTAMP** -- Current date/time functions
3. **CONNECT BY hierarchical queries** -- Tree-walking with PRIOR, LEVEL,
   NOCYCLE, START WITH, ORDER SIBLINGS BY
4. **DECODE function** -- Multi-value conditional (like CASE)
5. **NVL / NVL2** -- NULL-handling functions
6. **Outer join (+) syntax** -- Legacy Oracle outer join operator
7. **Sequence references** -- `seq.NEXTVAL` / `seq.CURRVAL`
8. **MINUS set operator** -- Oracle's name for EXCEPT
9. **DUAL pseudo-table** -- Dummy table for expression evaluation
10. **ROWID pseudo-column** -- Physical row identifier

### Registration

```c
#include "oracle_compat.h"

// With high-level registry
ExtensionRegistry *reg = extension_registry_create();
oracle_compat_register(reg);

// With internal extension system
ExtensionRegistry *reg = create_extension_registry();
uint32_t id;
oracle_compat_register_ext(reg, &id);
```

### Disambiguation

The Oracle extension uses `DISAMBIG_FORK_RESOLVE` to handle cases where
Oracle keywords conflict with standard SQL. For example, `LEVEL` could be
a column name in standard SQL but a pseudo-column in Oracle hierarchical
queries. The fork-resolve strategy tries both interpretations and picks
the one that produces a valid parse.

### Example Queries

```sql
-- Hierarchical query with DECODE and NVL
SELECT LEVEL,
       DECODE(LEVEL, 1, 'Root', 'Child') AS node_type,
       NVL(manager_name, 'None') AS manager
FROM employees
START WITH manager_id IS NULL
CONNECT BY PRIOR employee_id = manager_id
ORDER SIBLINGS BY employee_name;

-- ROWNUM with SYSDATE
SELECT ROWNUM, employee_name
FROM employees
WHERE hire_date > SYSDATE - 365
  AND ROWNUM <= 20;

-- Legacy outer join
SELECT e.employee_name, NVL(d.department_name, 'No Dept')
FROM employees e, departments d
WHERE e.department_id = d.department_id(+);

-- Sequence references from DUAL
SELECT emp_seq.NEXTVAL FROM DUAL;
```

## SQLite Syntax Support

**Location:** `contrib/sqlite_compat/`

**Files:**
- `sqlite_grammar.lime` -- Grammar rules
- `sqlite_semantics.h/c` -- Semantic actions
- `sqlite_compat.c` -- Extension registration
- `test_sqlite_compat.c` -- Unit tests
- `test_queries.sql` -- Test queries

### Supported Features

1. **WITHOUT ROWID tables** -- Clustered tables without implicit rowid
2. **STRICT tables** -- Type-checked tables (SQLite 3.37+)
3. **AUTOINCREMENT** -- Monotonically increasing rowids
4. **Type affinity** -- SQLite's flexible type system
5. **GLOB operator** -- Pattern matching with Unix glob syntax
6. **VACUUM INTO** -- Compact database into a new file
7. **INDEXED BY / NOT INDEXED** -- Index selection hints
8. **UPSERT (ON CONFLICT)** -- INSERT with conflict resolution

### Example Queries

```sql
-- WITHOUT ROWID + STRICT table
CREATE TABLE settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
) STRICT, WITHOUT ROWID;

-- AUTOINCREMENT
CREATE TABLE events (
    event_id INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type TEXT NOT NULL,
    payload BLOB,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP
);
```

## MySQL Compatibility (In Progress)

**Location:** `contrib/mysql_compat/`

### Planned Features

1. **Backtick identifier quoting** -- `` `column_name` ``
2. **LIMIT / OFFSET** -- MySQL-style result limiting
3. **AUTO_INCREMENT** -- MySQL auto-increment syntax
4. **SHOW commands** -- SHOW TABLES, SHOW DATABASES, etc.
5. **GROUP_CONCAT** -- String aggregation function

## Loading Multiple Dialects

Multiple SQL dialects can be loaded simultaneously. The framework
handles conflicts through disambiguation:

```c
ExtensionRegistry *reg = extension_registry_create();

// Register base grammar first
// base_sql_register(reg);

// Register dialect extensions
oracle_compat_register(reg);
// sqlite_compat_register(reg);  // when available

// Validate all dependencies
char *error = NULL;
if (!extension_registry_check_dependencies(reg, &error)) {
    fprintf(stderr, "Error: %s\n", error);
    free(error);
}

// Get the safe load order
ExtensionOrder order;
extension_registry_get_order(reg, &order, NULL);

// Set up disambiguation for any conflicts
DisambiguationContext *dis = disambiguation_create(STRAT_PRIORITY, reg);
```

## Conflict Scenarios Between Dialects

When multiple dialects are loaded, certain tokens may conflict:

| Token    | Oracle        | SQLite          | Resolution          |
|----------|---------------|-----------------|---------------------|
| ROWID    | Pseudo-column | Implicit column | Priority or context |
| AUTOINCREMENT | N/A      | Table option    | No conflict         |
| LEVEL    | Pseudo-column | N/A             | Fork-resolve        |

The conflict detection system identifies these automatically:

```c
MultiGrammarConflictResult *result = multi_conflict_result_create();
detect_all_multi_grammar_conflicts(reg, result);

for (uint32_t i = 0; i < result->npoints; i++) {
    ConflictPoint *cp = &result->points[i];
    printf("Conflict: %s (level %d, %d interpretations)\n",
           cp->description, cp->level, cp->ncontexts);
}
multi_conflict_result_destroy(result);
```
