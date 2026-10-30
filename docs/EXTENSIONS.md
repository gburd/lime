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
    .on_conflict        = NULL,           /* deprecated: proposals only */
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
- `fallback_to` -- Optional.  Name an existing terminal (usually the
  base grammar's identifier token) to make this a **soft keyword**: where
  the keyword cannot be shifted, the parser treats it as that terminal.
  This is Lemon's `%fallback`, the mechanism SQLite uses to keep most of
  its keywords non-reserved.  Leave it NULL (the default) for a reserved
  keyword.

**Use `fallback_to` for any keyword spelled like a plausible identifier.**
Without it, adding `CONNECT` makes every existing `SELECT connect FROM t`
(lexed as `SELECT CONNECT FROM ID`) a syntax error the moment the
extension loads.  With `.fallback_to = "ID"`, that query still parses
and the new `CONNECT t` statement works too.
`tests/test_ext_soft_keyword.c` checks both.  This covers keywords that
replace an identifier; it does not do context-aware scanning, so a
scanner still has to emit the keyword's code, and a keyword can only
fall back to one terminal.

### Adding Grammar Rules (`MOD_ADD_RULE`)

Adds a new production rule.

```c
static const char *rhs[] = { "a_expr", "ARROW", "a_expr", NULL };

GrammarModification mod = {
    .type = MOD_ADD_RULE,
    .description = "a_expr -> a_expr ARROW a_expr",
    .u.add_rule = {
        .lhs         = "a_expr",     /* Left-hand side non-terminal */
        .rhs         = rhs,          /* NULL-terminated RHS symbols */
        .nrhs        = 3,            /* Number of RHS symbols */
        .code        = "{ A = jsonb_arrow(B, C); }",  /* Reduction action */
        .reduce      = NULL,         /* Or a LimeReduceFn -- see below */
        .reduce_user = NULL,         /* Opaque pointer for .reduce    */
        .precedence  = -1,           /* -1 = inherit from symbols     */
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
- `code` -- Generator-time C code string.  Used when the modification is
  consumed by a static grammar compile (fed through `lime` as text).
  Inside the code block, `A` refers to the LHS's value slot and `B`,
  `C`, ... refer to the RHS symbols in order.
- `reduce` -- Optional runtime reduce callback of type `LimeReduceFn`
  (see below).  When set, the parser invokes this function at reduction
  time instead of dispatching through the generated `reduce()` switch.
  This is the mechanism for extensions loaded into an already-compiled
  parser (where the switch is fixed).
- `reduce_user` -- Opaque pointer passed through to `reduce` as its
  first argument.  Useful for carrying extension-private context.
- `precedence` -- Override precedence for the rule. Set to -1 to use
  the default behavior (inherit from the rightmost terminal).

**Action-source precedence**: if `reduce` is non-NULL, the parser uses
it (and `code` is treated as documentation / generator-time fallback).
If both are NULL, the rule reduces with no action.

#### LimeReduceFn -- runtime reduce callback

```c
typedef void (*LimeReduceFn)(
    void       *user_data,    /* = .reduce_user                       */
    void       *extra_arg,    /* grammar's %extra_argument, or NULL   */
    int         nrhs,         /* count of RHS symbols                 */
    const void *rhs_values,   /* array of nrhs %token_type payloads   */
    const int  *rhs_locs,     /* array of nrhs byte offsets or NULL   */
    void       *lhs_out       /* writeback slot for the LHS value     */
);
```

The callback must write the LHS value into `*lhs_out` before returning.
The extension owns any memory referenced from that value; Lime treats
slot payloads as opaque.  See `docs/API.md` for the full contract.

**Current status: extension reduces run at parse time.** There are two
ways to get an extension rule's action to execute.

**1. Directly, from `MOD_ADD_RULE.reduce` (no router).** When
`publish_modified_snapshot()` rebuilds the grammar (the recompile path,
taken whenever the base snapshot carries its grammar source, i.e. it was
built with `lime -n`), it binds every `MOD_ADD_RULE` that supplied a
`reduce` callback onto the new snapshot, keyed by the rule's *identity*
(`lhs ::= rhs1 rhs2 ...`, see `lime_snapshot_rule_by_id`) rather than
by a positional rule number.  A plain `parse_begin` / `parse_token` on
that snapshot then calls the callback on every reduce of that rule.
Rules with no action pass `$1` through to `$$`, so the extension's value
reaches `parse_result()` through the base grammar's unit rules.
`tests/test_extension_reduce_e2e.c` checks this end to end: the callback
fires once with `nrhs=2` for `expr ::= expr BANG`, and its sentinel value
is what `parse_result()` returns.

Limits of the direct path:

- It runs only when **no** `host_reduce` is bound on the session or
  snapshot.  A bound `host_reduce` owns dispatch for every rule, so the
  snapshot binding steps aside instead of running the action twice --
  use path 2 if you also need base-grammar actions.
- Base-grammar rules run no action on this path (there is no base
  reduce function to call); they only pass values through.
- `extra_arg` is passed as NULL.
- The metadata-only fallback (base snapshot without grammar source) does
  not rebuild tables and does not bind callbacks.

**2. Through `lime_host_reduce_router`**, when you also want the base
grammar's own actions.  A composed snapshot dispatches reduces through
two paths, unified at runtime by `lime_host_reduce_router`
(`include/multi_grammar.h`):

- **Base-grammar rules** (ruleno `< base_nrule`) run through the
  generated, exported `<Name>HostReduce` wrapper (a `LimeHostReduceFn`,
  wired by `lime -n --host-reduce`).
- **Extension rules** (ruleno `>= base_nrule`) run through the per-rule
  `LimeReduceFn` callback bound in the router's `LimeExtRuleBinding`
  table.

The embedder builds a `LimeHostReduceRouter`, points it at the base
wrapper and the extension bindings, and installs it with
`parse_set_host_reduce(ctx, lime_host_reduce_router, &router)`.  The
router then dispatches each reduce to the correct callback by rule
number.  This is proven end-to-end in `tests/test_multi_grammar.c`,
where a real `LimeReduceFn` bound into an extension rule and the base
rules both fire in one composed parse.

`LimeHostReduceFn` and `LimeReduceFn` are *separate* ABIs with
different signatures; the router bridges them so you do not hand-write
the base-vs-extension dispatcher.  See `docs/HOST_REDUCE.md` for the
host-reduce contract and `docs/REDUCE_DISPATCH.md` for a worked routing
example.

There is no subprocess `cc` pipeline: extension reduces are host-linked
function pointers, and composition rebuilds tables in-process.

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

### on_conflict (deprecated: a proposal only)

An extension can no longer decide a conflict by itself.  The *host* owns
the compose policy (see [Conflict policy](#conflict-policy-who-wins)).
`on_conflict` still gets called for declaration-level conflicts, but its
answer is only a proposal:

| answer | meaning as a proposal |
|---|---|
| `CONFLICT_KEEP_EXISTING` | keep the earlier modification (`prefer-base`) |
| `CONFLICT_USE_NEW` | keep the later one (`prefer-extension`) |
| `CONFLICT_MERGE` | unsupported |
| `CONFLICT_UNRESOLVED` | no proposal |

A proposal applies only if the host installed **no** policy (the legacy
behaviour, kept for one release), or if the host trusts that extension
with `trust-extension-proposals NAME.`  Otherwise it is ignored, and the
compose error lists it.  `on_conflict` will be removed in the next
release, so write policy lines instead.

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

**on_conflict** (deprecated; a proposal the host may ignore, see
[on_conflict](#on_conflict-deprecated-a-proposal-only)) -- proposes
keeping the existing modification:

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

New extensions should leave `on_conflict` NULL and document the policy
lines they need.

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

`publish_modified_snapshot(reg, base, &out, &conflicts, &err)` (in
liblime_compiler) composes every loaded extension onto `base`:

```
modifications of each loaded extension, in extension-NAME order
  |  `reject` lines drop items                       (host policy)
  v
declaration conflicts: keyword / conflict rule / token / rank (host policy)
  |  then the legacy check + on_conflict proposals   (deprecated)
  v
base grammar text + each extension's serialised text
  |  in-process LALR(1) rebuild; FindActions offers every conflict the
  |  base alone does not have to the policy          (host policy)
  v
any unresolved -> FAIL: *out = NULL, *err = the report, old snapshot stays
  v
bind MOD_ADD_RULE reduce callbacks by rule identity -> new ParserSnapshot
```

The base snapshot is never mutated, and readers holding the old
snapshot are unaffected.  There is no fallback: when the compose cannot
be built correctly it fails, and nothing is published.

## Conflict policy: who wins

Composing extensions creates conflicts.  The **host** decides who wins,
in a declarative policy.  The default is to fail: every LALR conflict an
extension introduces (a symbolic key present in base+extensions and
absent from the base compiled alone) must be resolved explicitly, or the
compose fails.  The base's `%expect` never absorbs it.  For example, the
30-rule `tests/compose_fixtures/dialect_ext.lime` adds 1 conflict to the
precedence-complete PostgreSQL grammar and 219 to the precedence-less
`examples/pg/gram.lime` (whose own 1665 are covered by its `%expect
1665`).  Both composes fail until the policy says what to do.

```c
#include "lime_policy.h"
LimeComposePolicy *pol = lime_policy_parse(text, len, &err);  /* or build the struct */
extension_registry_set_policy(reg, pol);   /* host only; NULL = default */
```

Policy text, either a file or a `%policy { ... }` block in the host
grammar:

```
rank timescale 50.   rank pgvector 40.          /* base = rank 0 */
default reject.                                 /* | prefer-base | prefer-extension | prefer-rank */
token JSON_ARROW prec-like OP assoc left.       /* like a %left line, relative to OP */
conflict sr "upsert_cols ::=" on LPAREN prefer shift.
conflict rr "atom ::= NUM" prefer reduce.       /* keep this reduce */
conflict sr "a_expr ::= MINUS a_expr" prefer pgvector.  /* keep pgvector's side */
conflict rule "a_expr ::= a_expr DPIPE a_expr" prefer pgvector.  /* two actions: keep pgvector's */
keyword "connect" owner oracle_compat soft.     /* lexeme owner; soft = %fallback to ID */
reject "stmt ::= showExtStmt" from pgvector.
reject token SHOW_EXT_K from pgvector.
trust-extension-proposals mysql_compat.         /* accept its on_conflict answers */
```

For one conflict, a `conflict` line beats rank, and rank beats
`default`.  Rank decides extension-vs-extension conflicts.  It decides
base-vs-extension conflicts (base = 0) only under `default prefer-rank`.
**Equal ranks on a real conflict are an error**, even under
`prefer-rank`; a `conflict` line can still settle it.  A rule that loses
every conflict it takes part in is reported as DEAD (never reduced)
rather than failing the compose.

Every resolution compiles to something Lemon already has: a
precedence, a chosen shift or reduce, a dropped rule, a dropped token or
lexeme.  The snapshot carries no trace of the policy, and parsing pays
nothing for it (measured: [COMPOSITION_STRATEGY.md](COMPOSITION_STRATEGY.md#zero-runtime-cost-measured)).

Declaration-level conflicts are settled before generation:

| kind | example | settled by |
|---|---|---|
| shared | two extensions add the same token (name and lexeme), or the same rule with the same action | merged; not a conflict |
| TOKEN_LEXEME | CONNECT and CONNECT_G both claim `connect` | `keyword "connect" owner E soft\|reserved.`, or rank; the loser keeps its terminal but no lexeme |
| DUP_RULE_ACTION | two `a_expr ::= a_expr DPIPE a_expr` with different callbacks | `conflict rule "..." prefer E.`, or rank |
| PREC_CLASH | two different precedences for DPIPE | the host's `token DPIPE ...` line replaces both, or rank |

To see what a set of extensions does to a base before shipping it:

```
lime --explain-compose base.lime ext1.lime ext2.lime [host.policy]
```

This prints every new conflict, with the owners of both sides, file and
line, a shortest example input, and a suggested policy line.  It then
groups them by root cause.  Exit status: 0 nothing unresolved, 1
unresolved, 2 error.  It is designed as a CI gate.

`lime_mg_resolve` (runtime multi-grammar fork-resolve, a different
feature) can take its tie-break from the same ranks:
`lime_mg_candidates_rank_from_policy(cands, n, pol)`.

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

### Expect the host to decide conflicts

Your extension cannot win a conflict by itself.  Ship a suggested policy
snippet with it (the `fix` lines `lime --explain-compose` prints), and
test it against the base you target, so hosts can adopt it.

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

### Compose fails with "extension-introduced LALR conflict(s)"

That is the default policy doing its job.  Run `lime --explain-compose
base.lime your_ext.lime`, then either change the extension's rules or
give the host the policy lines it prints.  Two extensions declaring the
same token name *and* lexeme share it; the same name with different
lexemes is a conflict the host must settle (`rank`).

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
