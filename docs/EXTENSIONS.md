# Extension Development Guide

This guide explains how to develop extensions for the Lemon extensible parser.
Extensions add new tokens, grammar rules, and precedence modifications to an
existing parser at runtime, without recompiling the base grammar.

## Overview

An extension is a set of **grammar modifications** bundled with lifecycle
callbacks. The extension system manages registration, loading (activation),
conflict resolution, and unloading.

```
register_extension()  -->  EXT_REGISTERED
load_extension()      -->  EXT_LOADED      (modifications applied)
unload_extension()    -->  EXT_UNLOADED    (modifications removed)
```

## Extension Lifecycle

### 1. Registration

Registration declares the extension to the system without activating it.
The extension system copies the name and version strings and assigns a
unique `ExtensionID`.

```c
#include "extension.h"

ExtensionRegistry *reg = create_extension_registry();

ExtensionInfo info = {
    .name               = "my_extension",
    .version            = "1.0.0",
    .get_modifications  = my_get_mods,    /* required */
    .on_conflict        = my_on_conflict, /* optional (NULL ok) */
    .on_unload          = my_on_unload,   /* optional (NULL ok) */
    .user_data          = NULL,           /* passed to callbacks */
};

ExtensionID id;
bool ok = register_extension(reg, &info, &id);
```

### 2. Loading

Loading activates the extension. The system calls your `get_modifications`
callback, passing the current base snapshot so you can inspect the existing
grammar. Your callback fills in the modifications array.

```c
char *error = NULL;
bool ok = load_extension(reg, id, base_snapshot, &error);
if (!ok) {
    fprintf(stderr, "Load failed: %s\n", error);
    free(error);
}
```

### 3. Unloading

Unloading deactivates the extension and removes its contributions. If you
provided an `on_unload` callback, it is called so you can free resources.

```c
unload_extension(reg, id);
```

### 4. Cleanup

When the registry is destroyed, all extensions are unloaded automatically.

```c
destroy_extension_registry(reg);
```

## Grammar Modifications

Each modification is a `GrammarModification` struct with a `type` field
and a type-specific payload in the `u` union.

### Adding Tokens (`MOD_ADD_TOKEN`)

Adds a new terminal symbol to the parser's token table.

```c
GrammarModification mod = {
    .type = MOD_ADD_TOKEN,
    .description = "JSONB arrow operator (->)",
    .u.add_token = {
        .name       = "ARROW",     /* Token name */
        .lexeme     = "->",        /* Literal text (for tokenizer) */
        .token_code = -1,          /* -1 = auto-assign */
    },
};
```

**Fields:**
- `name` -- The symbolic name used in grammar rules (e.g. `"ARROW"`).
- `lexeme` -- The literal text the tokenizer should recognize.
  May be NULL if tokenization is handled externally.
- `token_code` -- Set to -1 to let the system assign one automatically.
  Set to a specific value to force a particular code (use with caution --
  may conflict with existing tokens).

### Adding Grammar Rules (`MOD_ADD_RULE`)

Adds a new production rule.

```c
static const char *rhs[] = { "a_expr", "ARROW", "a_expr", NULL };

GrammarModification mod = {
    .type = MOD_ADD_RULE,
    .description = "a_expr -> a_expr ARROW a_expr",
    .u.add_rule = {
        .lhs        = "a_expr",      /* Left-hand side non-terminal */
        .rhs        = rhs,           /* NULL-terminated RHS symbols */
        .nrhs       = 3,             /* Number of RHS symbols */
        .code       = "{ A = jsonb_arrow(B, C); }",  /* Reduction action */
        .precedence = -1,            /* -1 = inherit from symbols */
    },
};
```

**Fields:**
- `lhs` -- The non-terminal being defined. Must already exist in the
  base grammar or be added by a preceding `MOD_ADD_TYPE` modification.
- `rhs` -- NULL-terminated array of symbol names. Can reference terminals
  (uppercase) or non-terminals (lowercase) from the base grammar or from
  other modifications in the same extension.
- `nrhs` -- Count of RHS symbols (not counting the NULL terminator).
- `code` -- C code to execute when this rule is reduced. Use `A`, `B`,
  `C`, etc. to refer to semantic values of RHS symbols.
- `precedence` -- Override precedence for the rule. Set to -1 to use
  the default behavior (inherit from the rightmost terminal).

### Modifying Precedence (`MOD_MODIFY_PRECEDENCE`)

Changes the precedence or associativity of an existing or newly-added symbol.

```c
GrammarModification mod = {
    .type = MOD_MODIFY_PRECEDENCE,
    .description = "Set ARROW to left-associative, level 3",
    .u.modify_prec = {
        .symbol         = "ARROW",
        .new_precedence = 3,
        .new_assoc      = 1,  /* 0=none, 1=left, 2=right, 3=nonassoc */
    },
};
```

### Adding Non-Terminal Types (`MOD_ADD_TYPE`)

Declares a new non-terminal symbol with its C data type.

```c
GrammarModification mod = {
    .type = MOD_ADD_TYPE,
    .description = "Add jsonb_value non-terminal",
    .u.add_type = {
        .name     = "jsonb_value",
        .datatype = "JsonbValue*",
    },
};
```

### Removing Rules (`MOD_REMOVE_RULE`)

Removes an existing production rule.

```c
GrammarModification mod = {
    .type = MOD_REMOVE_RULE,
    .description = "Remove default a_expr rule",
    .u.remove_rule = {
        .lhs        = "a_expr",
        .rule_index = 5,     /* Specific rule index, or -1 for all */
    },
};
```

Use this sparingly -- removing rules from the base grammar can break
existing SQL syntax.

## Implementing Callbacks

### get_modifications (required)

```c
static bool my_get_mods(
    void *user_data,
    const struct ParserSnapshot *base_snapshot,
    GrammarModification **mods_out,
    uint32_t *nmods_out
) {
    /* Option A: Return a static array (simplest) */
    *mods_out  = my_static_mods;
    *nmods_out = MY_MOD_COUNT;
    return true;

    /* Option B: Build dynamically based on base_snapshot */
    // GrammarModification *mods = malloc(...);
    // ... inspect base_snapshot to decide what to add ...
    // *mods_out = mods;
    // *nmods_out = count;
    // return true;
}
```

The `base_snapshot` parameter lets you inspect the current grammar state.
For example, you could check if a token already exists before adding it,
or adapt your rules based on the number of existing states.

The modifications array must remain valid until `on_unload` is called.
If you allocated it dynamically, free it in `on_unload`.

### on_conflict (optional)

Called when a modification conflicts with one from another extension.

```c
static ConflictResolution my_on_conflict(
    void *user_data,
    const ConflictInfo *info
) {
    /* Inspect the conflict */
    if (info->existing_mod->type == MOD_ADD_TOKEN &&
        info->new_mod->type == MOD_ADD_TOKEN) {
        /* Another extension already added the same token.
        ** Keep the existing one. */
        return CONFLICT_KEEP_EXISTING;
    }

    /* Replace with our version */
    return CONFLICT_USE_NEW;
}
```

**Resolution options:**
- `CONFLICT_KEEP_EXISTING` -- Discard our modification, keep what's there.
- `CONFLICT_USE_NEW` -- Replace the existing modification with ours.
- `CONFLICT_MERGE` -- Provide a merged result (advanced use case).
- `CONFLICT_UNRESOLVED` -- Signal that the conflict cannot be resolved
  (may cause load failure depending on policy).

If `on_conflict` is NULL, the system defaults to `CONFLICT_KEEP_EXISTING`.

### on_unload (optional)

Called when the extension is removed. Free any resources allocated during
`get_modifications`.

```c
static void my_on_unload(void *user_data) {
    MyContext *ctx = (MyContext *)user_data;
    free(ctx->dynamic_mods);
    free(ctx);
}
```

## Complete Walkthrough: JSONB Extension

The file `examples/jsonb_extension.c` is a complete, compilable extension
that adds PostgreSQL-style JSONB operators to the parser. This section
walks through every part of the implementation.

### What the extension adds

| Operator | Token name | Meaning                              |
|----------|-----------|--------------------------------------|
| `->`     | ARROW     | Extract JSON object field by key     |
| `->>`    | DARROW    | Extract JSON object field as text    |
| `@>`     | CONTAINS  | Does left JSON value contain right?  |
| `<@`     | WITHIN    | Is left JSON value contained by right? |
| `?`      | QMARK     | Does key exist in JSON object?       |

### Step 1: Define the modifications array

The extension declares its modifications as a static array of
`GrammarModification` structs. This is the simplest approach when the
set of modifications is known at compile time.

**Tokens** -- Each operator gets a `MOD_ADD_TOKEN` entry. All use
`token_code = -1` for automatic assignment:

```c
static GrammarModification jsonb_mods[] = {
    {
        .type = MOD_ADD_TOKEN,
        .description = "JSONB arrow operator (->)",
        .u.add_token = {
            .name       = "ARROW",
            .lexeme     = "->",
            .token_code = -1,   /* auto-assign */
        },
    },
    /* ... DARROW, CONTAINS, WITHIN, QMARK follow the same pattern */
};
```

**Rules** -- Each operator gets a `MOD_ADD_RULE` entry. The RHS symbols
are declared as separate NULL-terminated arrays:

```c
static const char *rhs_arrow[] = { "a_expr", "ARROW", "a_expr", NULL };

/* Inside jsonb_mods[]: */
{
    .type = MOD_ADD_RULE,
    .description = "a_expr -> a_expr ARROW a_expr",
    .u.add_rule = {
        .lhs        = "a_expr",
        .rhs        = rhs_arrow,
        .nrhs       = 3,
        .code       = "/* jsonb_arrow(A, B) */",
        .precedence = -1,
    },
},
```

Note: The `?` (exists) operator uses `SCONST` on the right-hand side
instead of `a_expr`, because it checks for a string key:

```c
static const char *rhs_exists[] = { "a_expr", "QMARK", "SCONST", NULL };
```

**Precedence** -- ARROW and DARROW are set to left-associative at
precedence level 3 (matching PostgreSQL's comparison operators):

```c
{
    .type = MOD_MODIFY_PRECEDENCE,
    .description = "Set ARROW precedence (left, level 3)",
    .u.modify_prec = {
        .symbol         = "ARROW",
        .new_precedence = 3,
        .new_assoc      = 1,  /* 1 = left */
    },
},
```

The total modifications array contains 12 entries: 5 tokens + 5 rules +
2 precedence settings. The count is computed with a macro:

```c
#define JSONB_MOD_COUNT (sizeof(jsonb_mods) / sizeof(jsonb_mods[0]))
```

### Step 2: Implement the callbacks

**get_modifications** -- Returns the static array. The `base_snapshot`
parameter is ignored here but could be used to conditionally add
modifications:

```c
static bool jsonb_get_modifications(
    void *user_data,
    const struct ParserSnapshot *base_snapshot,
    GrammarModification **mods_out,
    uint32_t *nmods_out
) {
    (void)user_data;
    (void)base_snapshot;
    *mods_out  = jsonb_mods;
    *nmods_out = JSONB_MOD_COUNT;
    return true;
}
```

**on_conflict** -- Takes the conservative approach of keeping the
existing modification when conflicts arise:

```c
static ConflictResolution jsonb_on_conflict(
    void *user_data,
    const ConflictInfo *info
) {
    (void)user_data;
    (void)info;
    return CONFLICT_KEEP_EXISTING;
}
```

A more sophisticated implementation might inspect `info->existing_mod`
and `info->new_mod` to make per-conflict decisions.

**on_unload** -- A no-op since the modifications are statically
allocated. A dynamic extension would free its modifications array here:

```c
static void jsonb_on_unload(void *user_data) {
    (void)user_data;
}
```

### Step 3: Define the ExtensionInfo descriptor

This struct bundles everything together for `register_extension()`:

```c
const ExtensionInfo jsonb_extension_info = {
    .name               = "jsonb_operators",
    .version            = "1.0.0",
    .get_modifications  = jsonb_get_modifications,
    .on_conflict        = jsonb_on_conflict,
    .on_unload          = jsonb_on_unload,
    .user_data          = NULL,
};
```

### Step 4: Convenience registration function

The example provides a single-call function that handles both
registration and loading, with error reporting:

```c
ExtensionID jsonb_extension_register_and_load(
    ExtensionRegistry *reg,
    const struct ParserSnapshot *base_snapshot,
    char **error_out
) {
    ExtensionID id = 0;

    if (!register_extension(reg, &jsonb_extension_info, &id)) {
        if (error_out) {
            const char msg[] = "jsonb: failed to register extension";
            *error_out = malloc(sizeof(msg));
            if (*error_out) memcpy(*error_out, msg, sizeof(msg));
        }
        return 0;
    }

    char *load_error = NULL;
    if (!load_extension(reg, id, base_snapshot, &load_error)) {
        if (error_out) {
            *error_out = load_error;
        } else {
            free(load_error);
        }
        return 0;
    }

    if (error_out) *error_out = NULL;
    return id;
}
```

### Compiling the example

```
cc -std=c11 -I src -I include -c examples/jsonb_extension.c
```

## How Extensions Are Applied

When an extension is loaded, its modifications go through a pipeline
managed by the snapshot modification system:

```
load_extension()
  |
  v
get_modifications() callback
  |
  v
detect_conflicts()          -- scan for token collisions, duplicate rules
  |
  v
resolve_conflicts()         -- call on_conflict callbacks
  |
  v
clone_snapshot()            -- deep copy of the base snapshot
  |
  v
apply_modification() x N   -- apply each mod to the cloned snapshot
  |
  v
rebuild_automaton()         -- recompute LALR(1) states and tables
  |
  v
New ParserSnapshot (refcount == 1)
```

The base snapshot is never mutated. All modifications produce a new
snapshot that can be atomically swapped in. This copy-on-write design
means readers using the old snapshot are not affected until they
acquire the new one.

**Result codes** from `create_modified_snapshot()`:

| Code                    | Meaning                                    |
|------------------------|--------------------------------------------|
| `MODIFY_OK`            | Snapshot created successfully              |
| `MODIFY_ERR_ALLOC`     | Memory allocation failure                  |
| `MODIFY_ERR_INVALID_MOD` | Invalid modification (bad type or fields)  |
| `MODIFY_ERR_CONFLICT`  | Unresolved conflicts remain                |
| `MODIFY_ERR_BUILD`     | LALR(1) automaton rebuild failed           |

## Conflict Detection Types

The conflict detection system checks for several categories of conflicts
when multiple extensions contribute modifications:

| Conflict Type             | Trigger                                       |
|--------------------------|-----------------------------------------------|
| `CONFLICT_TOKEN_COLLISION` | Two extensions add a token with the same name |
| `CONFLICT_DUPLICATE_RULE`  | Two extensions add an identical production    |
| `CONFLICT_PRECEDENCE_CLASH`| Conflicting precedence for the same symbol    |
| `CONFLICT_SHIFT_REDUCE`    | Shift/reduce conflict in rebuilt automaton    |
| `CONFLICT_REDUCE_REDUCE`   | Reduce/reduce conflict in rebuilt automaton   |

The first three types are detected during the pre-application scan
(`detect_conflicts()`). The last two are detected during
`rebuild_automaton()` after modifications have been applied to the
cloned snapshot.

When a conflict is detected, the system calls the `on_conflict`
callback of the extension that proposed the newer modification. The
callback receives a `ConflictInfo` struct with:

- `existing_ext` -- ExtensionID that owns the existing modification
- `new_ext` -- ExtensionID proposing the conflicting modification
- `existing_mod` -- Pointer to the existing `GrammarModification`
- `new_mod` -- Pointer to the conflicting `GrammarModification`

If no `on_conflict` callback is provided (NULL), the system defaults
to `CONFLICT_KEEP_EXISTING`. Any `CONFLICT_UNRESOLVED` results cause
the load to fail with `MODIFY_ERR_CONFLICT`.

## Best Practices

### Keep modifications minimal

Only add what your extension needs. Every new token and rule increases
the parser's table size and can create conflicts with other extensions.

### Use auto-assigned token codes

Set `token_code = -1` to avoid collisions with other extensions. Manual
token codes should only be used when interoperating with an external
tokenizer that requires specific values.

### Inspect the base snapshot

Use the `base_snapshot` parameter in `get_modifications` to check for
existing symbols before adding them. This prevents duplicate-token
conflicts:

```c
/* Pseudocode: check if ARROW already exists */
if (snapshot_has_token(base_snapshot, "ARROW")) {
    /* Skip adding ARROW, just add the grammar rules */
}
```

### Handle conflicts gracefully

Always implement `on_conflict` if your extension might overlap with
others. The default behavior (keep existing) may silently drop your
modifications.

### Free dynamically allocated modifications

If you build your modifications array in `get_modifications`, always
provide an `on_unload` callback that frees it. Memory leaks from
unfree'd modifications will accumulate as extensions are loaded and
unloaded.

### Use user_data for state

Pass extension-specific context through the `user_data` field in
`ExtensionInfo`. This avoids global variables and allows multiple
instances of the same extension type with different configurations.

### Thread safety

The extension registry is protected by a `pthread_rwlock`. Multiple
threads can call `find_extension()` and `get_loaded_extension_count()`
concurrently. Registration, loading, and unloading serialize through
a write lock.

Your callbacks (`get_modifications`, `on_conflict`, `on_unload`) are
called while the registry write lock is held. Do not attempt to call
registry functions from within a callback -- this will deadlock.

If your extension allocates shared state in `get_modifications` that
is accessed from parser reduction actions, you are responsible for
synchronizing access to that state (e.g. with your own mutex).

### Extension ordering matters

Extensions are loaded in the order you call `load_extension()`. When
conflicts arise, the "existing" modification is from the
earlier-loaded extension and the "new" modification is from the
later-loaded one.

If your extension depends on tokens or non-terminals from another
extension, load the dependency first. There is no automatic dependency
resolution -- the loading order is your responsibility.

## Troubleshooting

### Extension fails to load

- Check the error message returned by `load_extension()`.
- Verify that `get_modifications` returns `true` and sets both
  output parameters.
- Ensure the modifications array remains valid (not stack-allocated
  in a function that has returned).

### Token conflicts

If two extensions add the same token name, the second one to load will
trigger a conflict. Solutions:
- Implement `on_conflict` to handle it explicitly.
- Use unique token names (e.g. prefix with extension name:
  `JSONB_ARROW` instead of `ARROW`).

### Grammar rule not taking effect

- Verify the LHS non-terminal exists in the base grammar.
- Check that all RHS symbols exist (either in the base grammar or
  added by earlier modifications in the same extension).
- Ensure modifications are ordered: tokens before rules that use them.

### Segmentation fault in on_unload

- Make sure you are not freeing the static modifications array.
  Only free dynamically allocated modifications.
- Check that `user_data` is still valid at unload time.

### Parser tables grow too large

Each extension adds entries to the action tables. If the combined
table size becomes a concern:
- Remove unused rules with `MOD_REMOVE_RULE`.
- Consider merging related extensions into one.
- Profile with the `-s` flag to `lime` to see table statistics.
