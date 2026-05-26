# Runtime Parser Composition

`compose_snapshots()` merges N independently produced parser
snapshots into a single runtime-callable parser.  It is the
foundation for the multi-extension grammar ecosystem story:
PostgreSQL loads any subset of installed extensions
(DuckDB-compat, EDB Oracle-compat, pg_infer, pg_mentat, MongoDB
syntax, XPath/XQuery, ...) and composes their grammars into one
merged LALR(1) machine that every backend uses.

This page explains when composition applies, how it works, what
the merkle-tree content hash buys you, and how it fits with the
other grammar-evolution mechanisms Lime ships.

## When to use composition vs other mechanisms

Lime offers five distinct ways to evolve a grammar.  They sit
along two axes -- *when the change happens* (build time vs runtime)
and *how grammars combine* (replace, extend, embed, compose):

| Mechanism                              | Time      | Scope                                        | Doc                                |
|----------------------------------------|-----------|----------------------------------------------|------------------------------------|
| `%dialect NAME { ... }`                | Build     | Single binary, dialect chosen by `lime -D`   | [DIALECT.md](DIALECT.md)           |
| `%extends "base.lime"`                 | Build     | Single binary, derived grammar               | [EXTENDS.md](EXTENDS.md)           |
| `%embed lang TRIGGER 'lex' ENTRY TOK.` | Build     | Single binary, sub-grammar mode-switching    | [EMBED.md](EMBED.md)               |
| `register_extension` + `publish_modified_snapshot` | Runtime | One extension's additive rules into one base | [EXTENSIONS.md](EXTENSIONS.md)     |
| **`compose_snapshots`**                | **Runtime** | **N independent plugin grammars into one**| **(this page)**                    |

Decision tree:

```
need to vary the grammar?
├─ yes, at build time:
│    ├─ pick one of several presets         -> %dialect
│    ├─ inherit from a base file           -> %extends
│    └─ embed a foreign mini-grammar       -> %embed
└─ yes, at runtime:
     ├─ one extension adds a few rules    -> register_extension
     └─ N independent plugins merge       -> compose_snapshots
```

Concretely: if you ship two `.so` plugins that each carry their
own grammar and the host process must accept inputs from either
without preferring one, you want composition.  Anything else --
single-source dialects, file-level inheritance, build-time
specialisation -- is cheaper.

## The PostgreSQL extension-distribution model

The motivating use case.  PostgreSQL extension authors publish
shared libraries that Just Work when an operator drops them into
`shared_preload_libraries`:

- `pg_oracle.so` adds Oracle-compatible syntax (CONNECT BY,
  hierarchical queries, custom INTERVAL forms)
- `pg_duckdb.so` adds DuckDB-style aggregations and bulk-load
  pragmas
- `pg_infer.so` adds inference verbs (`INFER ... USING ...`)
- `pg_mentat.so` adds EDN literals and datalog rules
- ...and an open-ended set of others

Versions of PostgreSQL and these extensions will not move in
lockstep.  An operator might run PG 18 with pg_oracle 1.4,
pg_duckdb 0.9, and pg_infer 2.1 simultaneously.  Composition is
the contract that makes this work.

The startup sequence:

```c
/* PostgreSQL postmaster init, simplified. */

ParserManager *mgr = parser_manager_create(NULL);

/* Each shared_preload_libraries entry registers itself by
** dlopen()ing into the postmaster.  Lime extensions hand back
** their snapshot via the LimeParserPlugin contract. */
LimePluginHandle plugin_handles[N];
ParserSnapshot *snapshots[N];
for (int i = 0; i < N; i++) {
    parser_manager_load(mgr, ext_paths[i], NULL, &plugin_handles[i]);
    parser_manager_set_active(mgr, plugin_handles[i], ext_grammar_files[i]);
    snapshots[i] = parser_manager_get_snapshot(mgr);
}

/* One composition call.  All exported tokens, all rules, all
** state machines fold into one LALR(1) machine. */
CompositionOptions opts = {
    .flags = COMPOSE_FLAG_COMPUTE_MERKLE | COMPOSE_FLAG_DEDUP_RULES,
};
ParserSnapshot *composed = NULL;
CompositionDiagnostics diag = {0};
CompositionResult cr = compose_snapshots(snapshots, N, &opts, &composed, &diag);
if (cr != COMPOSE_OK) {
    /* diag.error names which extension caused the conflict;
    ** diag.conflicts enumerates every shift/reduce or
    ** reduce/reduce ambiguity that survived merging. */
    elog(FATAL, "extension composition failed: %s", diag.error);
}

/* Hand the composed snapshot to every backend that forks off the
** postmaster.  Snapshots are reference-counted, so each backend's
** parse_begin() pins its own reference; an operator-issued
** `pg_reload_conf()` can swap the composed snapshot atomically
** for a new one without disturbing in-flight queries. */
SetActiveParserSnapshot(composed);
```

## The merkle-tree content-hash architecture

Every `ParserSnapshot` carries a 32-byte `merkle_root` field.
`compose_snapshots()` with `COMPOSE_FLAG_COMPUTE_MERKLE` populates
it by:

1. Hashing each input snapshot's grammar data (symbols, rules,
   action table, default-reduce table) with SHA-256 to produce
   leaf nodes.
2. Building a balanced merkle tree over those leaves.
3. The root hash is the content hash of the *composition*: any
   change to any input snapshot, in any position, propagates a
   new root.  Identity composition (same inputs in same order
   with same options) is byte-identical reproducible.

This determinism property is what makes the cache architecture
possible.  The cache key for a composition is:

```
key = SHA256( sorted_set( merkle_root(input[0]), ...,
                          merkle_root(input[N-1]) )
              || serialize(CompositionOptions) )
```

The cache value is the composed LALR tables themselves
(serialised via the existing snapshot serialisation surface).
Daemons like PostgreSQL then look for a hit on the cache key at
startup *before* invoking `compose_snapshots()`; on a hit they
deserialise the cached tables in microseconds and skip the merge.

The properties this depends on:

- **Input determinism.**  Two identical inputs must produce
  identical merkle leaves.  Lime's table-layout passes are
  deterministic on the same host architecture today; cross-host
  cache portability requires the LALR rebuild to also be
  endianness/word-size canonical (already true for tables, since
  they live as `int16_t` / `uint16_t`).
- **Composition determinism.**  `compose_snapshots()` is order-
  sensitive when `COMPOSE_FLAG_LAST_WINS` is set, otherwise
  commutative across rule additions.  Use sorted inputs (by
  module name, version, or merkle hash itself) for cross-host
  cache stability.

Verified by `tests/test_composition_e2e.c`: the same inputs
produce identical roots across two `compose_snapshots()` calls;
swapping one input changes the root.

## Versioning model

Each plugin declares a SemVer version via `LimePluginVersion`
(returned by `get_version()`).  When a plugin also ships a
`ParserModule` description, it can declare dependencies and
version constraints:

```c
ParserModule mod = {
    .name = "pg_oracle",
    .version = { .major = 1, .minor = 4, .patch = 0 },
    .nrequires = 1,
    .requires = (ModuleDependency[]) {
        { .name = "pg_sql", .min = { 14, 0, 0 }, .max = { 99, 0, 0 } },
    },
    .nexports = 4,
    .exports = (const char *[]) { "TK_CONNECT", "TK_BY", "TK_PRIOR", "TK_LEVEL" },
};
```

`compose_snapshots()` (or `validate_composition_inputs()` in
isolation) calls into `dependency_resolver` to:

1. Build the dependency graph.
2. Topologically sort modules so deps load before dependents.
3. Verify every `requires` has a satisfying `version` in the
   compose set.
4. Verify every imported symbol is exported by some module.

On failure, `CompositionDiagnostics::error` names the specific
incompatibility (e.g. `"pg_oracle 1.4 requires pg_sql >= 14.0
but pg_sql 13.5 was provided"`) and the resolver returns a
fail-fast `COMPOSE_ERR_DEPENDENCY` without doing the expensive
LALR work.

## Plugin verification (supply-chain integrity)

Each `.so` can embed its grammar's merkle hash at build time, as
a const byte array exported alongside `lime_plugin_entry`.  The
host then verifies the hash matches the snapshot the plugin
hands back:

```c
/* In the plugin: */
LIME_PLUGIN_EXPORT const uint8_t lime_plugin_grammar_hash[32] = {
    0xef, 0x6c, 0xab, ...  /* sha256(grammar source + lime version) */
};

/* In the host: */
uint8_t *expected = dlsym(handle, "lime_plugin_grammar_hash");
ParserSnapshot *snap = plugin->create_snapshot(grammar_file, &err);
if (memcmp(snap->merkle_root, expected, 32) != 0) {
    /* Plugin grammar drifted from what the .so was signed for. */
    elog(WARNING, "rejecting plugin: grammar hash mismatch");
    parser_manager_unload(mgr, handle);
}
```

This gives signed-release ecosystems a path to refuse plugins
that have been tampered with post-build, without re-running the
full grammar pipeline at load time.

## Hot-swap and atomic replacement

`parser_manager_hot_swap()` swaps the active snapshot atomically
from the perspective of `parser_manager_get_snapshot()`.  In-flight
parse sessions keep their pinned references; only new sessions
see the new snapshot.

For composition, the typical sequence on a config-reload event is:

```c
/* New extension was added or removed via a SIGHUP. */
ParserSnapshot *composed = NULL;
compose_snapshots(updated_snapshots, n, &opts, &composed, &diag);
parser_manager_set_snapshot(mgr, composed);
/* Backends that begin parsing AFTER this point see the composed
** snapshot.  Backends parsing right now finish on the old one
** and the old snapshot is freed when the last reference drops. */
```

## Performance: where composition stands today

`compose_snapshots()` does not currently rebuild the LALR(1)
automaton in-process.  The merging passes (symbol unification,
rule deduplication, action-table concatenation) all run in the
process, but a full LALR rebuild from the merged grammar
requires either:

- forking `lime` + a C compiler subprocess (~200ms per merge),
  same path as `lime_snapshot_create()`, or
- accepting the snapshot-table-merging-only result, which
  *combines* the action tables but does not recompute conflict
  resolutions across newly-juxtaposed rules from different
  inputs.

Both are correct; both are not what production PG-startup
extension loading wants.  Sub-millisecond composition is the
threshold for daemon-startup viability.

The roadmap item that fixes this is **in-process LALR(1)
automaton rebuild** (see [ROADMAP.md](ROADMAP.md) item 1).  That
work exposes lime's `FindRulePrecedences` / `FindFirstSets` /
`FindStates` / `FindLinks` / `FindFollowSets` / `FindActions` /
`CompressTables` phases as a callable library, removing both the
fork+exec latency and the runtime C-compiler dependency.
Composition is the primary consumer of that work.

Until then, the supported production pattern is:

1. Compose plugins at PG startup with `COMPOSE_FLAG_COMPUTE_MERKLE`.
2. Look the resulting merkle root up in a persistent cache (on
   disk, keyed by the sorted set of input hashes).
3. On hit (the common case for stable extension sets across
   restarts), deserialise the cached LALR tables.  On miss,
   pay the rebuild cost once and write the result back to the
   cache.

This is the architecture every operator running a stable set of
extensions converges on: cold start is slow once, every restart
after that is sub-millisecond.

## Failure modes

| Failure                                | `CompositionResult`            | What `diag.error` reports                    |
|----------------------------------------|--------------------------------|----------------------------------------------|
| Two snapshots define the same terminal | `COMPOSE_ERR_SYMBOL_COLLISION` | The colliding symbol name and source indices |
| Dependency graph is unsatisfiable      | `COMPOSE_ERR_DEPENDENCY`       | The unsatisfied requirement                  |
| Merged grammar has unresolved S/R conflicts | `COMPOSE_ERR_CONFLICT`    | `diag.conflicts` enumerates the conflicts   |
| Allocation failure                     | `COMPOSE_ERR_ALLOC`            | "out of memory"                              |
| NULL or zero-length input              | `COMPOSE_ERR_INVALID_INPUT`    | "snapshots is NULL" / "nsnapshots is zero"   |

The host should always call `composition_diagnostics_destroy(&diag)`
even on success -- it owns the merkle tree (when computed),
symbol mapping array, and conflict set.

## API surface

See `src/parser_composition.h` for the full surface:

```c
CompositionResult compose_snapshots(
    ParserSnapshot **snapshots, uint32_t nsnapshots,
    const CompositionOptions *opts,
    ParserSnapshot **out,
    CompositionDiagnostics *diag);

CompositionResult merge_snapshots(
    const ParserSnapshot *base, const ParserSnapshot *extension,
    const CompositionOptions *opts,
    ParserSnapshot **out,
    CompositionDiagnostics *diag);

MerkleTree *compute_snapshot_merkle(const ParserSnapshot *snap);

CompositionResult validate_composition_inputs(
    ParserSnapshot **snapshots, uint32_t nsnapshots,
    const CompositionOptions *opts,
    CompositionDiagnostics *diag);
```

And `include/parser_manager.h` for the runtime plugin contract
that produces the input snapshots.

## Worked example

`tests/test_composition_e2e.c` is the live worked example.  It
builds three small `.so` plugins under
`tests/composition_e2e_fixtures/` (each with distinct grammar
counts), dlopens them, runs them through composition, and
asserts on:

- summed dimensions of the composed snapshot
- determinism of the merkle root across two compose calls
- the merkle root *changing* when one input is swapped

Run it:

```bash
meson test -C build composition_e2e -v
```

`examples/plugin_template/` is the matching producer-side
example: a complete sql_plugin shared library plus a host
application demonstrating both static-linking and dynamic-loading
patterns through `ParserManager`.

## See also

- [DIALECT.md](DIALECT.md), [EXTENDS.md](EXTENDS.md),
  [EMBED.md](EMBED.md), [EXTENSIONS.md](EXTENSIONS.md) -- the
  other grammar-evolution mechanisms
- [ROADMAP.md](ROADMAP.md) item 1 -- in-process LALR(1) rebuild,
  the prerequisite for sub-millisecond composition
- [PARSER_PLUGIN_DESIGN.md](PARSER_PLUGIN_DESIGN.md) -- the
  plugin ABI that produces input snapshots
- [MODULE_FORMAT.md](MODULE_FORMAT.md) -- the module metadata
  format consumed by the dependency resolver
