# Extension Authoring Guide

This guide walks through creating a grammar extension for the Lime parser
generator's extensible plugin framework. Grammar extensions add new tokens,
production rules, precedence changes, and semantic actions to an existing
parser at runtime.

## Table of Contents

1. [Overview](#overview)
2. [Step 1: Define Grammar Modifications](#step-1-define-grammar-modifications)
3. [Step 2: Implement Semantic Actions](#step-2-implement-semantic-actions)
4. [Step 3: Register Extension Metadata](#step-3-register-extension-metadata)
5. [Step 4: Build as Shared Library](#step-4-build-as-shared-library)
6. [Step 5: Load and Test](#step-5-load-and-test)
7. [API Reference](#api-reference)
8. [Choosing a Disambiguation Strategy](#choosing-a-disambiguation-strategy)
9. [Choosing an Execution Policy](#choosing-an-execution-policy)
10. [Best Practices](#best-practices)
11. [Common Pitfalls](#common-pitfalls)
12. [Debugging Tips](#debugging-tips)
13. [Examples](#examples)

---

## Overview

The extension system is organized in three layers:

```
+---------------------+
| Extension Registry  |  Manages metadata, dependencies, load ordering
+---------------------+
         |
+---------------------+
| Disambiguation      |  Resolves conflicts between extensions
+---------------------+
         |
+---------------------+
| Execution Policy    |  Controls which semantic actions execute
+---------------------+
```

An extension consists of:

- **Grammar modifications** -- tokens, rules, precedence changes
- **Semantic actions** -- C code executed when rules fire
- **Metadata** -- name, version, priority, strategy, policy, dependencies

The framework handles:

- Conflict detection across extensions (token collisions, duplicate rules,
  precedence clashes)
- Disambiguation when multiple extensions can handle the same input
- Execution ordering when multiple extensions' semantic actions need to run


## Step 1: Define Grammar Modifications

Grammar modifications describe what your extension contributes to the parser.
Each modification is a `GrammarModification` struct (from `src/extension.h`).

### Modification types

| Type                   | Purpose                                |
|------------------------|----------------------------------------|
| `MOD_ADD_TOKEN`        | Add a new terminal token               |
| `MOD_ADD_RULE`         | Add a new production rule              |
| `MOD_MODIFY_PRECEDENCE`| Change precedence of an existing symbol|
| `MOD_ADD_TYPE`         | Add a new non-terminal type            |
| `MOD_REMOVE_RULE`      | Remove an existing rule                |

### Adding tokens

```c
static GrammarModification my_mods[] = {
    {
        .type = MOD_ADD_TOKEN,
        .description = "JSONB arrow operator (->)",
        .u.add_token = {
            .name       = "ARROW",      /* Token name in the grammar    */
            .lexeme     = "->",         /* Literal text to match        */
            .token_code = -1,           /* -1 = auto-assign code        */
        },
    },
};
```

Set `token_code` to `-1` to let the framework assign a unique code
automatically. Only specify an explicit code if you need interoperability
with a fixed token numbering scheme.

### Adding rules

```c
static const char *rhs_arrow[] = { "a_expr", "ARROW", "a_expr", NULL };

static GrammarModification my_mods[] = {
    {
        .type = MOD_ADD_RULE,
        .description = "a_expr -> a_expr ARROW a_expr",
        .u.add_rule = {
            .lhs        = "a_expr",           /* Left-hand side         */
            .rhs        = rhs_arrow,          /* NULL-terminated RHS    */
            .nrhs       = 3,                  /* Number of RHS symbols  */
            .code       = "{ A = json_arrow(B, D); }",  /* Action code */
            .precedence = -1,                 /* -1 = inherit from grammar */
        },
    },
};
```

The `rhs` array must be NULL-terminated. The `nrhs` count should match the
number of non-NULL entries.

### Modifying precedence

```c
{
    .type = MOD_MODIFY_PRECEDENCE,
    .description = "Set ARROW to left-associative, level 3",
    .u.modify_prec = {
        .symbol         = "ARROW",
        .new_precedence = 3,
        .new_assoc      = 1,   /* 0=none, 1=left, 2=right, 3=nonassoc */
    },
},
```


## Step 2: Implement Semantic Actions

Extensions provide three callbacks. Only `get_modifications` is required.

### get_modifications (required)

Called when the extension is loaded. Returns the modifications array.

```c
static bool my_get_modifications(
    void *user_data,
    const struct ParserSnapshot *base_snapshot,
    GrammarModification **mods_out,
    uint32_t *nmods_out
) {
    (void)user_data;
    (void)base_snapshot;

    *mods_out  = my_mods;
    *nmods_out = sizeof(my_mods) / sizeof(my_mods[0]);
    return true;
}
```

The `base_snapshot` parameter gives you read access to the current parser
state. You can inspect it to adapt your modifications (for example, checking
whether a token already exists before adding it).

If your modifications are computed dynamically (for example, generated from
a configuration file), allocate them in this callback and free them in
`on_unload`.

### on_conflict (optional)

Called when your modifications conflict with another extension.

```c
static ConflictResolution my_on_conflict(
    void *user_data,
    const ConflictInfo *info
) {
    (void)user_data;
    /* Options: CONFLICT_KEEP_EXISTING, CONFLICT_USE_NEW, CONFLICT_MERGE */
    return CONFLICT_KEEP_EXISTING;
}
```

If not provided, the default behavior is `CONFLICT_KEEP_EXISTING` (the
first extension to load wins).

### on_unload (optional)

Called when the extension is removed. Free any dynamically allocated
modifications here.

```c
static void my_on_unload(void *user_data) {
    MyState *state = (MyState *)user_data;
    free(state->dynamic_mods);
    free(state);
}
```


## Step 3: Register Extension Metadata

There are two registration APIs depending on your needs.

### Simple registration (internal API)

For extensions that only need basic modification support:

```c
#include "extension.h"   /* src/extension.h */

const ExtensionInfo my_extension_info = {
    .name               = "my_extension",
    .version            = "1.0.0",
    .get_modifications  = my_get_modifications,
    .on_conflict        = my_on_conflict,     /* NULL ok */
    .on_unload          = my_on_unload,       /* NULL ok */
    .user_data          = NULL,
};
```

Register and load:

```c
ExtensionRegistry *reg = create_extension_registry();

ExtensionID id;
register_extension(reg, &my_extension_info, &id);
load_extension(reg, id, base_snapshot, &error);
```

### Rich registration (public API)

For extensions that need disambiguation strategies, execution policies,
and dependency management:

```c
#include "extension_registry.h"   /* include/extension_registry.h */

GrammarExtensionMetadata meta = {
    /* Identity */
    .name    = "my_extension",
    .version = "1.0.0",

    /* Disambiguation */
    .strategy = DISAMBIG_PRIORITY,
    .priority = 100,           /* Higher = more preferred */

    /* Execution */
    .policy = EXEC_SEQUENTIAL,

    /* Oracle (for DISAMBIG_ORACLE strategy) */
    .oracle = NULL,

    /* Conflict tolerance: 0.0 = no conflicts, 1.0 = all ok */
    .conflict_threshold = 0.1,

    /* Dependencies: NULL-terminated list of required extensions */
    .requires = (const char *[]){ "base_sql", NULL },

    /* Incompatibilities */
    .conflicts_with = (const char *[]){ "legacy_json", NULL },

    /* Grammar modifications */
    .modifications  = my_mods,
    .nmodifications = sizeof(my_mods) / sizeof(my_mods[0]),
};
```

Register and validate:

```c
ExtensionRegistry *reg = extension_registry_create();
extension_registry_register(reg, &meta);

char *error = NULL;
if (!extension_registry_check_dependencies(reg, &error)) {
    fprintf(stderr, "Dependency error: %s\n", error);
    free(error);
}
```


## Step 4: Build as Shared Library

### With GCC/Clang

```makefile
CC = cc
CFLAGS = -std=c11 -Wall -Wextra -fPIC
INCLUDES = -I../../include -I../../src
LDFLAGS = -shared

my_extension.so: my_extension.c
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $<
```

### With Meson

```meson
shared_library('my_extension',
  'my_extension.c',
  include_directories : [inc, include_directories('../../src')],
  dependencies : lime_parser_dep,
  install : true,
)
```

### Static linking

If you prefer to link the extension statically, include the source file
in your application's build and call the registration function directly.
No shared library is needed.


## Step 5: Load and Test

### Loading an extension

```c
#include "extension.h"
#include "parser.h"

/* Initialize the registry */
lemon_extension_registry_init();

/* Create a base snapshot from a grammar file */
char *error = NULL;
ParserSnapshot *snap = lemon_snapshot_create("base_grammar.y", &error);

/* Create the internal registry and register your extension */
ExtensionRegistry *reg = create_extension_registry();
ExtensionID id;
register_extension(reg, &my_extension_info, &id);
load_extension(reg, id, snap, &error);

/* Parse with the extended grammar */
ParseContext *ctx = parse_begin(snap);
parse_token(ctx, MY_TOKEN, &value, token_offset);
parse_token(ctx, 0, NULL, LIME_LOC_UNKNOWN);  /* end of input */
parse_end(ctx);

/* Cleanup */
snapshot_release(snap);
destroy_extension_registry(reg);
```

### Testing in isolation

Always test your extension in isolation before combining with others.
This makes it easier to identify whether conflicts come from your
extension or from interactions.

```c
/* Register only your extension */
ExtensionRegistry *reg = create_extension_registry();
ExtensionID id;
register_extension(reg, &my_extension_info, &id);
load_extension(reg, id, snap, &error);

/* Verify modifications were applied */
const Extension *ext = find_extension(reg, id);
assert(ext->nmodifications > 0);
assert(ext->state == EXT_LOADED);
```


## API Reference

### GrammarExtensionMetadata fields

| Field               | Type                     | Description                              |
|---------------------|--------------------------|------------------------------------------|
| `name`              | `const char *`           | Unique extension name (required)         |
| `version`           | `const char *`           | Semver string (required)                 |
| `strategy`          | `DisambiguationStrategy` | How to resolve ambiguities               |
| `priority`          | `int`                    | Priority for `DISAMBIG_PRIORITY`         |
| `policy`            | `ExecutionPolicy`        | How to apply modifications               |
| `oracle`            | `OracleCallback`         | Callback for `DISAMBIG_ORACLE`           |
| `conflict_threshold`| `float`                  | Fraction of tolerable conflicts [0,1]    |
| `requires`          | `const char **`          | NULL-terminated dependency list           |
| `conflicts_with`    | `const char **`          | NULL-terminated incompatibility list      |
| `modifications`     | `GrammarModification *`  | Array of grammar changes                 |
| `nmodifications`    | `uint32_t`               | Number of modifications                  |

### DisambiguationStrategy options

| Strategy             | When to use                                     |
|----------------------|-------------------------------------------------|
| `DISAMBIG_PRIORITY`  | Simple ordering: higher priority wins            |
| `DISAMBIG_FORK_RESOLVE` | Need to try both parses to decide             |
| `DISAMBIG_ORACLE`    | External logic (LLM, user prompt) decides        |
| `DISAMBIG_CONTEXT`   | Grammar context (dialect switching) resolves it  |
| `DISAMBIG_NONE`      | Conflicts should be errors, not resolved         |

### Execution policy options (execution_policy.h)

The execution policy engine controls which semantic actions run after
disambiguation selects winners.

| Policy            | Behavior                                          |
|-------------------|---------------------------------------------------|
| `EXEC_FIRST_ONLY` | Only the highest-priority winner executes          |
| `EXEC_ALL`        | All winners execute independently                  |
| `EXEC_CHAIN`      | Winners execute in sequence; output feeds as input |
| `EXEC_CONDITIONAL` | Extension's `should_execute` callback decides     |

Configuration via `ExecutionPolicyConfig`:

| Field            | Type             | Description                           |
|------------------|------------------|---------------------------------------|
| `policy`         | `ExecutionPolicy`| Which policy to use                   |
| `execute_fn`     | `ParserExecuteFn`| Callback that runs a parser           |
| `stop_on_error`  | `bool`           | Stop at first error (ALL/CHAIN/COND)  |
| `max_executions` | `int`            | Safety limit (0 = unlimited)          |

### Extension registry operations

| Function                               | Description                        |
|----------------------------------------|------------------------------------|
| `extension_registry_create()`          | Create empty registry              |
| `extension_registry_register(reg, m)`  | Register with metadata             |
| `extension_registry_find(reg, name)`   | Look up by name                    |
| `extension_registry_check_dependencies(reg, &err)` | Validate deps      |
| `extension_registry_get_order(reg, &order, &err)` | Topological sort    |
| `extension_registry_unregister(reg, name)` | Remove an extension          |
| `extension_registry_count(reg)`        | Number of registered extensions    |
| `extension_registry_foreach(reg, fn, data)` | Iterate all extensions      |
| `extension_registry_destroy(reg)`      | Free everything                    |


## Choosing a Disambiguation Strategy

### DISAMBIG_PRIORITY (default)

Use when extensions have a clear ranking. The extension with the highest
`priority` value wins. This is the simplest strategy and introduces no
runtime overhead.

**Good for:** SQL dialect layering (e.g., base SQL at priority 0,
PostgreSQL extensions at priority 100, custom extensions at priority 200).

### DISAMBIG_FORK_RESOLVE

Use when the correct interpretation depends on parsing context that cannot
be determined from the current token alone. The framework clones the parser
state, tries each candidate, and picks the one that parses successfully.

**Good for:** Ambiguous syntax where both interpretations are grammatically
valid up to a certain lookahead depth (e.g., `foo * bar` as declaration vs
expression).

**Trade-off:** Higher runtime cost due to parser state cloning. Cost is
proportional to the number of candidates and lookahead depth.

### DISAMBIG_ORACLE

Use when disambiguation requires external knowledge. The framework calls
your `OracleCallback` with a description of the conflict and candidate
resolutions. The callback returns the index of the chosen candidate.

**Good for:** LLM-assisted parsing, user-interactive disambiguation, or
integration with external type systems.

### DISAMBIG_CONTEXT

Use when the correct interpretation depends on which grammar "dialect" is
active. The context switching layer tracks which grammar context the parser
is in and routes to the appropriate extension.

**Good for:** Multi-dialect systems where input explicitly switches between
grammars (e.g., embedded XQuery within SQL, or `SET dialect = 'oracle'`).

### DISAMBIG_NONE

Use when your extension should never conflict with other extensions.
Any conflict is treated as a registration error rather than resolved
at runtime.

**Good for:** Core extensions that must not be overridden.


## Choosing an Execution Policy

After disambiguation selects winners, the execution policy determines
how semantic actions run.

### EXEC_FIRST_ONLY

Only the highest-priority winner's semantic actions execute. All other
winners are discarded.

**Use when:** There is exactly one correct interpretation and you want
the fastest execution path.

### EXEC_ALL

All winners execute independently. Each receives the same input and
produces independent results.

**Use when:** You need to collect results from multiple extensions
(e.g., generating both an AST and a validation report from the same parse).

### EXEC_CHAIN

Winners execute in sequence. The output of parser[i] is fed as input to
parser[i+1]. If a parser fails, the chain breaks (with `stop_on_error`)
or continues with NULL input.

**Use when:** Extensions form a processing pipeline (e.g., parse -> typecheck
-> optimize).

### EXEC_CONDITIONAL

Each winner's `should_execute` callback is consulted before running.
Extensions that return `false` are skipped. Extensions without a callback
always execute.

**Use when:** Execution depends on runtime conditions (e.g., feature flags,
configuration, or the specific conflict that was resolved).


## Best Practices

### Memory management

- **Static modifications:** If your `GrammarModification` array is declared
  as file-scope `static`, you do not need an `on_unload` callback.

- **Dynamic modifications:** If you allocate modifications at runtime (in
  `get_modifications`), you must free them in `on_unload`. The framework
  does not free the modifications array.

- **String ownership:** The extension registry copies all strings from
  `GrammarExtensionMetadata` (name, version, dependency lists). You do not
  need to keep them alive after `extension_registry_register()` returns.
  However, the internal `ExtensionInfo` API does copy strings too, but the
  `GrammarModification` data (rule names, action code) must remain valid
  until `on_unload` is called.

### Error handling

- Always check return values from `register_extension()` and
  `load_extension()`.
- Pass a `char **error` to `load_extension()` to get diagnostic messages.
- Use `extension_registry_check_dependencies()` after registering all
  extensions to catch missing dependencies and circular references early.

### Naming conventions

- Use a unique prefix for your token names (e.g., `JSONB_ARROW` instead of
  `ARROW`) to reduce the chance of collisions with other extensions.
- Use descriptive `description` fields in modifications -- they appear in
  conflict reports and debugging output.

### Conflict management

- Set `conflict_threshold` to a value appropriate for your extension. A
  threshold of `0.0` means zero tolerance for conflicts; `0.1` allows up to
  10% of your modifications to conflict before the extension is rejected.
- Implement `on_conflict` when you have domain knowledge about which
  resolution is correct. Returning `CONFLICT_MERGE` is the most flexible
  option but requires you to produce the merged result.
- Use `conflict_set_unresolved_count()` to check how many conflicts remain
  after resolution.


## Common Pitfalls

### Forgetting NULL termination on RHS arrays

```c
/* WRONG: missing NULL terminator */
static const char *rhs[] = { "a_expr", "PLUS", "a_expr" };

/* RIGHT */
static const char *rhs[] = { "a_expr", "PLUS", "a_expr", NULL };
```

The parser will read past the array boundary without the NULL terminator.

### Mismatched nrhs count

```c
/* WRONG: nrhs says 2 but there are 3 symbols */
.u.add_rule = { .lhs = "expr", .rhs = rhs, .nrhs = 2, ... }

/* RIGHT */
.u.add_rule = { .lhs = "expr", .rhs = rhs, .nrhs = 3, ... }
```

### Circular dependencies

```c
/* Extension A requires B, and B requires A -- will be caught */
GrammarExtensionMetadata a = { .name = "A", .requires = (const char*[]){"B", NULL} };
GrammarExtensionMetadata b = { .name = "B", .requires = (const char*[]){"A", NULL} };
```

The registry's `check_dependencies()` detects cycles and reports the path.
Design your extension hierarchy as a DAG.

### Freeing static modifications in on_unload

```c
/* WRONG: jsonb_mods is a static array */
static void bad_unload(void *data) {
    free(jsonb_mods);  /* Undefined behavior! */
}
```

Only free modifications that were allocated with `malloc()` in
`get_modifications`.

### Forgetting to set priority with DISAMBIG_PRIORITY

If you use `DISAMBIG_PRIORITY` but leave `priority` at 0, your extension
will always lose to any extension with a positive priority. Set an
explicit priority value.

### Token code collisions

If you hardcode `token_code` values instead of using `-1` (auto-assign),
you risk collisions with other extensions. Prefer auto-assignment unless
you have a specific reason to use fixed codes.


## Debugging Tips

### Use the conflict detector

Run `detect_all_multi_grammar_conflicts()` after loading extensions to
get a comprehensive report of all conflicts:

```c
MultiGrammarConflictResult *result = multi_conflict_result_create();
detect_all_multi_grammar_conflicts(reg, result);

printf("Conflicts: %u token, %u rule, %u semantic\n",
       result->token_conflicts,
       result->rule_conflicts,
       result->semantic_conflicts);

for (uint32_t i = 0; i < result->npoints; i++) {
    ConflictPoint *cp = &result->points[i];
    printf("  [%s] token=%u state=%d: %d interpretations\n",
           cp->level == CONFLICT_LEVEL_TOKEN ? "TOKEN" :
           cp->level == CONFLICT_LEVEL_RULE  ? "RULE"  : "SEMANTIC",
           cp->token, cp->state, cp->ncontexts);
    if (cp->description) printf("    %s\n", cp->description);
}

multi_conflict_result_destroy(result);
```

### Test with a single extension first

Before loading your extension alongside others, load it alone and verify:

1. All modifications are present (`ext->nmodifications == expected`)
2. The extension state is `EXT_LOADED`
3. A basic parse succeeds with the new tokens/rules

### Validate dependencies early

Call `extension_registry_check_dependencies()` immediately after
registering all extensions. This catches:

- Missing dependencies
- Circular dependency chains
- Declared incompatibilities

```c
char *error = NULL;
if (!extension_registry_check_dependencies(reg, &error)) {
    fprintf(stderr, "Dependency validation failed: %s\n", error);
    free(error);
    exit(1);
}
```

### Check execution results

When using the execution policy engine, always check for errors:

```c
int nresults = 0;
ExecutionResult *results = execute_semantic_actions(
    &config, &strategy_result, parsers, extensions, &nresults);

for (int i = 0; i < nresults; i++) {
    if (results[i].error != NULL) {
        fprintf(stderr, "Extension %u failed: %s\n",
                results[i].extension_id, results[i].error);
    }
}
execution_results_free(results, nresults);
```

### Memory leak detection with Valgrind

The framework uses standard `malloc`/`free` for all allocations. Run your
extension under Valgrind to catch leaks:

```sh
valgrind --leak-check=full ./my_extension_test
```

Common sources of leaks:

- Forgetting `strategy_result_cleanup()` after `disambiguation_resolve()`
- Forgetting `execution_results_free()` after `execute_semantic_actions()`
- Forgetting `snapshot_release()` after `parse_end()`
- Not implementing `on_unload` for dynamically allocated modifications


## Examples

The following extension examples demonstrate different aspects of the
framework.

### JSONB operators (examples/jsonb_extension.c)

A straightforward extension that adds PostgreSQL-style JSONB operators
(`->`, `->>`, `@>`, `<@`, `?`). Demonstrates static modification arrays,
conflict callbacks, and the convenience `register_and_load` pattern.

- **Strategy:** `DISAMBIG_PRIORITY` (uses `on_conflict` callback)
- **Key patterns:** Static `GrammarModification[]`, `CONFLICT_KEEP_EXISTING`

### LLM oracle (examples/llm_oracle/)

A custom disambiguation strategy that queries an LLM to resolve ambiguous
natural-language SQL constructs. Demonstrates `DISAMBIG_ORACLE` and
custom `DisambiguationStrategyVTable` integration.

- **Strategy:** Custom vtable (LLM query)
- **Key patterns:** Dynamic modifications, `local_strdup` for portability

### Plugin template (examples/plugin_template/)

A complete parser plugin with both dynamic (shared library) and static
linking support. Demonstrates the `LimeParserPlugin` interface, snapshot
creation, and the `ParserManager` workflow.

- **Key patterns:** `LIME_PLUGIN_EXPORT`, `lime_plugin_entry()`, ABI versioning

### Oracle compatibility (contrib/oracle_compat/)

Full Oracle SQL dialect extension: ROWNUM, SYSDATE, CONNECT BY, DECODE,
NVL, dual-table, sequence currval/nextval, and the `(+)` outer join operator.

- **Strategy:** `DISAMBIG_FORK_RESOLVE` (Oracle tokens like ROWNUM
  conflict with identifier rules in base grammar)
- **Priority:** 50
- **Dependencies:** Requires `postgres_base`
- **Key patterns:** Rich `GrammarExtensionMetadata` with dependency
  declarations, fork-resolve for ambiguous identifiers, separate
  `oracle_semantics.c` for action implementations

### SQLite compatibility (contrib/sqlite_compat/)

SQLite SQL dialect: WITHOUT ROWID, AUTOINCREMENT, PRAGMA, UPSERT (ON
CONFLICT), JSON operators, common table expressions, window functions.

- **Strategy:** `DISAMBIG_FORK_RESOLVE` (SQLite has syntax overlaps
  with other dialects)
- **Policy:** `EXEC_SEQUENTIAL`
- **Key patterns:** Both registration APIs used (internal `ExtensionInfo`
  and rich `GrammarExtensionMetadata`), shared library build support

### EDN literals (contrib/edn_literals/)

Clojure-style EDN (Extensible Data Notation) literals embedded within SQL.
Supports nil, booleans, integers, floats, strings, keywords, symbols,
vectors, maps, and sets.

- **Strategy:** `DISAMBIG_PRIORITY` at priority 4 (EDN delimiters are
  unambiguous with SQL, so no fork-resolve needed)
- **Key patterns:** Grammar context switching (enters EDN mode on `{:`,
  `[`, `#{`), 27 grammar modifications, `grammar_context.h` integration

### XQuery/XPath (examples/xquery/)

Full XQuery 3.1 parser with FLWOR expressions, XPath compatibility,
element/attribute constructors, and quantified expressions.

- **Strategy:** `DISAMBIG_CONTEXT` (XQuery is embedded within SQL via
  explicit boundaries like XMLQUERY)
- **Key patterns:** Standalone grammar (`.lime` file), custom tokenizer,
  comprehensive test suite with test vectors

### Choosing an example to follow

| Your use case                        | Start from              |
|--------------------------------------|-------------------------|
| Add operators to existing grammar    | `jsonb_extension.c`     |
| Add a SQL dialect (tokens + rules)   | `oracle_compat/`        |
| Embed a different language           | `edn_literals/`, `xquery/` |
| Use runtime disambiguation           | `llm_oracle/`           |
| Build a dynamic plugin (.so)         | `plugin_template/`      |
