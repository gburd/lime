# Lime Grammar Extension Framework -- Integration Guide

## Overview

The Lime Parser Generator includes an extensible grammar plugin framework that
allows multiple grammar extensions to coexist in a single parser. Extensions can
add SQL dialect features (Oracle, SQLite, MySQL), embed domain-specific languages
(EDN, XQuery/XPath), or define custom syntax.

This guide covers how to integrate the framework into your project, load
extensions, handle conflicts, and configure disambiguation.

## Architecture

The framework is organized in three layers:

```
+-------------------------------------------------------+
|  Layer 3: Extension Examples                           |
|  (contrib/oracle_compat, contrib/edn_literals, ...)    |
+-------------------------------------------------------+
|  Layer 2: Conflict Detection & Disambiguation          |
|  (conflict_detector, disambiguation, execution_policy) |
+-------------------------------------------------------+
|  Layer 1: Extension Registry & Infrastructure          |
|  (extension, extension_registry, snapshot, token_table)|
+-------------------------------------------------------+
```

**Layer 1 -- Infrastructure** manages extension registration, dependency
resolution, grammar snapshots, and token tables. Thread-safe with
read-write locking.

**Layer 2 -- Disambiguation** detects conflicts at three levels (token,
rule, semantic) and resolves them using pluggable strategies (priority,
fork-resolve, Bayesian, LLM oracle, or custom).

**Layer 3 -- Extensions** are self-contained grammar plugins in `contrib/`
that register with the framework and provide grammar modifications.

## Directory Structure

```
lime/
  include/              Public headers
    extension.h         Extension public API
    extension_registry.h  Rich registry with metadata
    disambiguation.h    Strategy interface
    execution_policy.h  Execution policy engine
    parser_fork.h       Parser state cloning
    parse_context.h     Parse context management
  src/                  Core implementation
    extension.c         Internal extension system
    extension_registry.c  Registry with dependency management
    conflict.c          Basic conflict detection
    conflict_detector.c Multi-grammar conflict detection
    disambiguation.c    Strategy dispatch framework
    strategy_priority.c Priority-based resolution
    strategy_fork_resolve.c  Fork-resolve strategy
    execution_policy.c  Semantic action policies
    parser_fork.c       Parser state cloning
    grammar_context.c   Grammar context definitions
    context_switch.c    Context switching logic
    snapshot.c          Parser snapshot (immutable state)
    snapshot_modify.c   Snapshot modification pipeline
    token_table.c       Thread-safe token table
  contrib/              Extension examples
    oracle_compat/      Oracle SQL compatibility
    sqlite_compat/      SQLite syntax support
    edn_literals/       EDN embedded language
    xml_query/          XQuery/XPath embedded language
    mysql_compat/       MySQL compatibility
  tests/                Test suite (26 test executables)
```

## Building

The framework uses Meson as its build system:

```sh
# Configure
meson setup builddir

# Build everything (library + tests + examples)
meson compile -C builddir

# Run the test suite
meson test -C builddir
```

The core library is built as `liblime_parser.a` and can be linked into
your project:

```meson
# In your project's meson.build
lime_dep = dependency('lime-parser')
executable('my_parser', 'main.c', dependencies: lime_dep)
```

Or compile directly with GCC/Clang:

```sh
gcc -I/path/to/lime/include -L/path/to/lime/builddir/src \
    -o my_parser main.c -llime_parser -lpthread -ldl
```

## Loading Extensions Programmatically

### Using the High-Level Registry

The `extension_registry.h` API provides rich metadata, dependency
management, and topological ordering:

```c
#include "extension_registry.h"

// Create a registry
ExtensionRegistry *reg = extension_registry_create();

// Register extensions with metadata
GrammarExtensionMetadata oracle_meta = {
    .name     = "oracle_compat",
    .version  = "1.0.0",
    .strategy = DISAMBIG_FORK_RESOLVE,
    .priority = 2,
    .policy   = EXEC_SEQUENTIAL,
    .requires = (const char *[]){"postgres_base", NULL},
};
extension_registry_register(reg, &oracle_meta);

GrammarExtensionMetadata edn_meta = {
    .name     = "edn_literals",
    .version  = "1.0.0",
    .strategy = DISAMBIG_PRIORITY,
    .priority = 4,
    .policy   = EXEC_SEQUENTIAL,
    .requires = (const char *[]){"sql_base", NULL},
};
extension_registry_register(reg, &edn_meta);

// Validate dependencies (checks for missing deps, conflicts, cycles)
char *error = NULL;
if (!extension_registry_check_dependencies(reg, &error)) {
    fprintf(stderr, "Dependency error: %s\n", error);
    free(error);
}

// Get load order (topological sort)
ExtensionOrder order;
extension_registry_get_order(reg, &order, NULL);
for (uint32_t i = 0; i < order.count; i++) {
    printf("Load: %s\n", order.names[i]);
}
extension_order_destroy(&order);

// Cleanup
extension_registry_destroy(reg);
```

### Using the Internal Extension System

The `src/extension.h` API is lower-level and used within the library:

```c
#include "extension.h"

ExtensionRegistry *reg = create_extension_registry();

ExtensionInfo info = {
    .name = "oracle_compat",
    .version = "1.0.0",
    .get_modifications = oracle_get_modifications,
    .on_conflict = oracle_on_conflict,
    .on_unload = oracle_cleanup,
    .user_data = &oracle_state,
};

ExtensionID id;
register_extension(reg, &info, &id);

char *error = NULL;
load_extension(reg, id, NULL, &error);

// ... use the parser ...

unload_extension(reg, id);
destroy_extension_registry(reg);
```

### Using Extension Convenience Functions

Each contrib extension provides a registration function:

```c
#include "oracle_compat.h"

ExtensionRegistry *reg = extension_registry_create();
// Register base grammar first ...
oracle_compat_register(reg);
```

## Conflict Detection

When multiple extensions are loaded, the framework detects conflicts at
three levels:

| Level    | Description                          | Example                              |
|----------|--------------------------------------|--------------------------------------|
| Token    | Same lexeme in different grammars    | `ARRAY` in PostgreSQL vs TypeScript  |
| Rule     | Same token parseable multiple ways   | `foo * bar;` as decl vs expr in C    |
| Semantic | Same syntax, different actions       | `::` as cast (PG) vs scope (Oracle)  |

### Running Conflict Detection

```c
#include "conflict.h"

// Detect all conflicts across loaded extensions
MultiGrammarConflictResult *result = multi_conflict_result_create();
bool found = detect_all_multi_grammar_conflicts(reg, result);

printf("Token conflicts:    %u\n", result->token_conflicts);
printf("Rule conflicts:     %u\n", result->rule_conflicts);
printf("Semantic conflicts: %u\n", result->semantic_conflicts);

// Check a specific token
ConflictPoint cp = detect_conflict(reg, TOKEN_ARRAY, state);
if (cp.ncontexts > 1) {
    printf("Ambiguous: %d grammars can handle this token\n",
           cp.ncontexts);
}
conflict_point_destroy(&cp);

multi_conflict_result_destroy(result);
```

## Disambiguation Strategies

When conflicts are detected, the disambiguation framework resolves them:

| Strategy        | Use Case                                         |
|-----------------|--------------------------------------------------|
| `STRAT_PRIORITY`     | Static ordering by extension priority       |
| `STRAT_FORK_RESOLVE` | Fork parser state, try both, pick winner    |
| `STRAT_BAYESIAN`     | Accumulate evidence over time               |
| `STRAT_LLM`          | Query an LLM oracle for guidance            |
| `STRAT_CUSTOM`       | User-supplied vtable                        |

### Example: Priority Strategy

```c
#include "disambiguation.h"

DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);

ConflictPoint cp = detect_conflict(reg, token, state);
if (cp.ncontexts > 1) {
    StrategyResult result = disambiguation_resolve(ctx, &cp, parse_ctx);
    if (result.nwinners > 0) {
        printf("Winner: ext %u (confidence %.2f)\n",
               result.winning_contexts[0].ext_id,
               result.confidence);
    }
    strategy_result_cleanup(&result);
}

conflict_point_destroy(&cp);
disambiguation_destroy(ctx);
```

### Example: Custom Strategy

```c
static void *my_init(const Extension *const *exts, uint32_t n) {
    return my_state_create(exts, n);
}

static bool my_resolve(void *ctx, const ConflictPoint *cp,
                       ParseContext *pc, int la, StrategyResult *r) {
    // Your resolution logic here
    r->winning_contexts = malloc(sizeof(LimeContext));
    r->winning_contexts[0] = cp->contexts[0]; // pick first
    r->nwinners = 1;
    r->confidence = 0.8f;
    return true;
}

static void my_destroy(void *ctx) { my_state_free(ctx); }

DisambiguationStrategyVTable vtable = {
    .init = my_init, .resolve = my_resolve,
    .update = NULL, .destroy = my_destroy,
};
DisambiguationContext *ctx = disambiguation_create_custom(&vtable, reg);
```

## Execution Policies

After disambiguation selects a winner, the execution policy controls how
semantic actions run:

| Policy           | Behavior                                       |
|------------------|------------------------------------------------|
| `EXEC_FIRST_ONLY`  | Only the highest-priority winner executes    |
| `EXEC_ALL`         | All winners execute independently            |
| `EXEC_CHAIN`       | Winners execute in sequence, output chained  |
| `EXEC_CONDITIONAL` | Extension callback decides per-winner        |

```c
#include "execution_policy.h"

ExecutionPolicyConfig config;
execution_policy_config_init(&config);
config.policy = EXEC_FIRST_ONLY;
config.execute_fn = my_parser_execute;
config.stop_on_error = true;

int nresults = 0;
ExecutionResult *results = execute_semantic_actions(
    &config, &strat_result, parsers, extensions, &nresults);

for (int i = 0; i < nresults; i++) {
    if (results[i].error) {
        fprintf(stderr, "Error from ext %u: %s\n",
                results[i].extension_id, results[i].error);
    }
}
execution_results_free(results, nresults);
```

## Troubleshooting

### Extension fails to register

- Ensure the extension name is unique (no duplicates).
- Check that `get_modifications` callback is not NULL.
- Verify the name string is not NULL or empty.

### Dependency check fails

- All entries in `requires` must name registered extensions.
- Entries in `conflicts_with` must not name registered extensions.
- The dependency graph must be acyclic (no circular dependencies).

### Unexpected conflicts after loading

- Use `detect_all_multi_grammar_conflicts()` for a full scan.
- Check that unloaded extensions are not accidentally reloaded.
- Verify token names are unique across extensions, or configure
  disambiguation to resolve collisions.

### Fork-resolve is slow

- Fork-resolve clones the entire parser state for each candidate.
  Use `STRAT_PRIORITY` when static ordering is sufficient.
- Set `max_forks` on the ParseForkSet to limit parallelism.

### Memory leaks

- Call `conflict_point_destroy()` on every ConflictPoint.
- Call `strategy_result_cleanup()` on every StrategyResult.
- Call `multi_conflict_result_destroy()` on result sets.
- Call `execution_results_free()` on execution results.
- Call `disambiguation_destroy()` on disambiguation contexts.
- Call `extension_registry_destroy()` or `destroy_extension_registry()`
  on registries.

## Test Suite

The framework includes 26 test executables with 403+ assertions:

```sh
meson test -C builddir
```

Key test files:
- `test_conflict_detector` -- Multi-grammar conflict detection (95 tests)
- `test_disambiguation` -- Strategy framework (35 tests)
- `test_multi_extension` -- Integration scenarios (22 tests)
- `test_execution_policy` -- Policy engine (94 tests)
- `test_parser_fork` -- Parser cloning
- `test_fork_resolve` -- Fork-resolve strategy
- `test_extension` -- Extension lifecycle
- `test_extension_coverage` -- Edge cases
