# Architecture

This document describes the architecture of the extensible SQL parser system
built on top of the Lemon LALR(1) parser generator.

## Overview

The system extends a traditional Lemon-generated parser with runtime grammar
modification, SIMD-accelerated tokenization, optional LLVM JIT compilation,
and copy-on-write snapshots for safe concurrent access. Extensions can add
tokens, rules, and precedence changes without restarting the parser.

```
                     +-----------------+
                     |  Grammar File   |
                     +--------+--------+
                              |
                     +--------v--------+
                     |  Lime Parser   |
                     |   Generator     |
                     +--------+--------+
                              |
                     +--------v--------+
                     | Base Snapshot   |
                     | (action tables, |
                     |  symbols, rules)|
                     +--------+--------+
                              |
              +---------------+---------------+
              |                               |
     +--------v--------+            +--------v--------+
     | Extension System |            | Parse Context   |
     | (register, load, |            | (snapshot-pinned |
     |  modifications)  |            |  parser runtime) |
     +--------+--------+            +--------+--------+
              |                               |
     +--------v--------+            +--------v--------+
     | Modified Snapshot|            | SIMD Tokenizer  |
     | (clone + apply   |            | (AVX2/NEON/     |
     |  + rebuild)      |            |  scalar)        |
     +---------+--------+            +-----------------+
               |
      +--------v--------+
      | JIT Compilation  |
      | (optional LLVM)  |
      +-----------------+
```

## Component Overview

| Component | Files | Purpose |
|-----------|-------|---------|
| Lemon Generator | `lime.c`, `limpar.c` | Reads grammar, produces LALR(1) parser tables |
| Snapshot System | `src/snapshot.{h,c}` | Immutable, refcounted captures of parser state |
| Snapshot Modification | `src/snapshot_modify.{h,c}` | Clone-and-modify pipeline for grammar changes |
| Extension System | `src/extension.{h,c}` | Registration, loading, and lifecycle of extensions |
| Conflict Detection | `src/conflict.{h,c}` | Detects and resolves grammar modification conflicts |
| Token Table | `include/token_table.h`, `src/token_table.c` | Thread-safe, RCU-versioned keyword lookup |
| SIMD Tokenizer | `src/tokenize_simd.{h,c}`, `src/tokenize.c` | SIMD character classification for lexing |
| Parse Context | `include/parse_context.h`, `src/parse_context.c` | Snapshot-indirected parser runtime |
| JIT Context | `include/jit_context.h`, `src/jit_context.c` | LLVM-based compilation of action tables |
| JIT Policy | `include/jit_policy.h`, `src/jit_policy.c` | Metrics-driven decision for when to JIT |
| Public API | `include/parser.h` | Stable external interface to all subsystems |

## Copy-on-Write Snapshot Architecture

The snapshot system is the central abstraction that enables safe concurrent
access and runtime grammar modification.

### ParserSnapshot

A `ParserSnapshot` (defined in `src/snapshot.h`) captures the complete state
of a parser at a point in time:

- **Grammar data**: symbols, rules, states (deep copies)
- **Action tables**: `yy_action`, `yy_lookahead`, `yy_shift_ofst`,
  `yy_reduce_ofst`, `yy_default` (the compact arrays that drive parsing)
- **Bookkeeping**: monotonic version number, creation timestamp, JIT context
- **Refcount**: atomic `uint_fast32_t` for safe concurrent sharing

### Lifecycle

```
create_base_snapshot(grammar_file)
        |
        v
  [refcount = 1]  <--- snapshot_acquire() increments
        |                snapshot_release() decrements
        v
  [refcount = 0] ---> destroy_snapshot() frees all owned memory
```

Reference counting uses release-acquire semantics:
- `snapshot_acquire()` uses `memory_order_relaxed` (only needs atomicity)
- `snapshot_release()` uses `memory_order_release` on the decrement
- The final destroyer issues an `atomic_thread_fence(memory_order_acquire)`
  before reading snapshot data, ensuring all prior writes from other threads
  that released their references are visible

### Copy-on-Write Modification

When extensions modify the grammar, the base snapshot is never mutated.
Instead, `create_modified_snapshot()` in `src/snapshot_modify.c` follows
this pipeline:

1. **Detect conflicts** among the proposed modifications
2. **Resolve conflicts** via extension `on_conflict` callbacks
3. **Clone** the base snapshot (deep copy of all action tables and arrays)
4. **Apply** each modification to the mutable clone
5. **Rebuild** the LALR(1) automaton (recompute states and action tables)
6. **Return** the new snapshot with `refcount = 1`

Active parsers continue using the old snapshot via their pinned references.
New parse sessions pick up the new snapshot. This enables zero-downtime
grammar updates.

## LALR(1) Algorithm

The Lemon parser generator implements a standard LALR(1) construction:

### Processing Pipeline (in `lime.c`)

1. **Parse** - Read grammar file, build symbol and rule tables
2. **FindRulePrecedences** - Determine precedence for each production
3. **FindFirstSets** - Compute FIRST sets for all non-terminals
4. **FindStates** - Build LR(0) automaton (state machine of configurations)
5. **FindLinks** - Establish follow-set propagation links between states
6. **FindFollowSets** - Compute FOLLOW sets via fixed-point iteration
7. **FindActions** - Determine shift/reduce/accept actions per state
8. **CompressTables** - Pack action tables into compact arrays
9. **ResortStates** - Reorder states to minimize table size
10. **ReportTable** - Generate C source code with embedded tables

### Action Table Layout

The generated parser uses five parallel arrays:

| Array | Type | Purpose |
|-------|------|---------|
| `yy_action` | `uint16_t[]` | Combined shift and reduce actions |
| `yy_lookahead` | `uint16_t[]` | Expected lookahead for each action entry |
| `yy_shift_ofst` | `int16_t[]` | Per-state offset into yy_action for shifts |
| `yy_reduce_ofst` | `int16_t[]` | Per-state offset into yy_action for reduces |
| `yy_default` | `uint16_t[]` | Default action when no lookahead matches |

Action lookup: for state `S` and lookahead `T`, compute
`i = yy_shift_ofst[S] + T`. If `yy_lookahead[i] == T`, the action is
`yy_action[i]`. Otherwise, fall back to `yy_default[S]`.

## Extension System

### Design

Extensions are managed through a thread-safe registry (`ExtensionRegistry`)
protected by a `pthread_rwlock`. The registry maps `ExtensionID` values
(1-based `uint32_t`, with 0 reserved for "base grammar") to `Extension`
structs.

### Extension Lifecycle

```
                register_extension()
                       |
                       v
               [EXT_REGISTERED]
                       |
                load_extension()
              calls get_modifications()
                       |
                       v
                 [EXT_LOADED]
             modifications cached
                       |
              unload_extension()
             calls on_unload()
                       |
                       v
                [EXT_UNLOADED]
                 (can reload)
```

### Extension Callbacks

Each extension provides up to three callbacks:

| Callback | Required | Purpose |
|----------|----------|---------|
| `get_modifications` | Yes | Returns array of `GrammarModification` structs |
| `on_conflict` | No | Resolves conflicts with other extensions |
| `on_unload` | No | Frees extension-owned resources |

### Grammar Modifications

Extensions express grammar changes through `GrammarModification` structs
with a tagged union:

| Type | Fields | Effect |
|------|--------|--------|
| `MOD_ADD_RULE` | lhs, rhs[], code, precedence | Add a production rule |
| `MOD_ADD_TOKEN` | name, lexeme, token_code | Add a terminal symbol |
| `MOD_REMOVE_RULE` | lhs, rule_index | Remove a production |
| `MOD_MODIFY_PRECEDENCE` | symbol, precedence, assoc | Change precedence |
| `MOD_ADD_TYPE` | name, datatype | Set non-terminal C type |

### Conflict Detection

`detect_conflicts()` in `src/conflict.c` performs pairwise comparison of
modifications to find:

- **Token collisions**: two extensions add the same token name
- **Duplicate rules**: identical production rules from different extensions
- **Precedence clashes**: conflicting precedence/associativity on the same symbol
- **Shift/reduce and reduce/reduce**: detected during automaton rebuild

Conflicts are collected in a `ConflictSet` and offered to each extension's
`on_conflict` callback for resolution. Unresolved conflicts cause the
modification to fail with `MODIFY_ERR_CONFLICT`.

## SIMD Tokenization

### Character Classification

The SIMD tokenizer (`src/tokenize_simd.{h,c}`) classifies characters in
parallel using vector instructions. The `CharClassVector` struct holds
bitmasks for alphabetic, digit, and whitespace characters:

```c
typedef struct CharClassVector {
    uint32_t is_alpha_mask;   // Bit i set if char i is alphabetic
    uint32_t is_digit_mask;   // Bit i set if char i is a digit
    uint32_t is_space_mask;   // Bit i set if char i is whitespace
} CharClassVector;
```

### Implementation Tiers

| Tier | Width | Platform | Selection |
|------|-------|----------|-----------|
| AVX2 | 32 chars | x86_64 | `__attribute__((target("avx2")))` + runtime CPUID |
| NEON | 16 chars | ARM | Compile-time `__ARM_NEON` |
| Scalar | 32 chars | Any | Always available fallback |

`get_classify_func()` selects the best implementation at runtime. The AVX2
path uses the `target` attribute so it compiles on any x86_64 toolchain
without requiring `-mavx2` globally.

### Token Table

The `TokenTable` (`include/token_table.h`) provides thread-safe keyword
lookup using:

- **Hash table** with chained collision resolution
- **RCU-style versioning**: an `atomic_uint_fast32_t` version counter that
  readers check before and after lookup. If the version changed, a write
  occurred and the reader retries.
- **Write lock**: `pthread_rwlock_t` protects mutations (add/remove tokens)
- **Extension tracking**: each `TokenDefinition` records which `ExtensionID`
  added it, enabling `remove_tokens_by_extension()` for clean unloading

## LLVM JIT Integration

### Architecture

The JIT subsystem compiles parser action table lookups into native machine
code using LLVM's C API. It has three layers:

1. **JIT Context** (`src/jit_context.c`): Manages LLVM module, execution
   engine, and compiled function pointers. Creates specialized
   `find_shift_action` functions for each parser state where the action
   table logic is baked into the instruction stream rather than driven
   by table lookups.

2. **JIT Code Generation** (`src/jit_codegen.c`): Generates LLVM IR for
   each parser state. Translates the `yy_action`/`yy_lookahead` table
   structure into a series of comparisons and direct returns, which LLVM
   then optimizes into efficient branch sequences.

3. **JIT Policy** (`src/jit_policy.c`): Decides when compilation is
   worthwhile based on runtime metrics (parse count, cumulative parse
   time, action lookup frequency).

### Graceful Degradation

When LLVM is not available (`LIME_NO_JIT` defined at compile time),
all JIT functions compile as stubs:
- `jit_is_available()` returns `false`
- `jit_create()` returns `JIT_ERR_NO_LLVM`
- `jit_find_shift_action()` falls through to the table-driven path

### Compilation Trigger

The policy uses three thresholds (configurable via `JITPolicyConfig`):

| Threshold | Default | Purpose |
|-----------|---------|---------|
| `min_parse_count` | 50 | Avoid compiling rarely-used grammars |
| `min_total_parse_time_ns` | 10 ms | Ensure enough time spent parsing |
| `min_avg_lookups_per_parse` | 100 | Ensure complex enough grammars |

When all thresholds are met, `jit_maybe_compile()` spawns a detached
background thread that compiles and atomically publishes the JIT context
to `snap->jit_ctx`. Active parsers transparently pick up the JIT path
on their next action lookup.

### Runtime Dispatch

`jit_find_shift_action()` checks if a compiled function exists for the
given state. If yes, it calls the compiled function directly. If no, it
falls back to the standard table-driven `snap_find_shift_action()` from
the parse context.

## Thread Safety

### Synchronization Mechanisms

| Component | Mechanism | Readers | Writers |
|-----------|-----------|---------|---------|
| Snapshots | Atomic refcount | Lock-free acquire/release | N/A (immutable) |
| Extension Registry | `pthread_rwlock` | Concurrent read lock | Exclusive write lock |
| Token Table | RCU versioning + `pthread_rwlock` | Lock-free with retry | Write lock |
| JIT Metrics | `atomic_uint_fast64_t` counters | Lock-free | Lock-free |
| JIT Compilation | `atomic_int` flag + detached thread | Lock-free | Single background thread |

### Key Invariants

1. **Snapshots are immutable after publication.** Once a snapshot is shared
   (refcount > 1), no thread may modify its contents. All changes produce
   a new snapshot via copy-on-write.

2. **Parse contexts pin a snapshot.** Each `ParseContext` holds a reference
   to one snapshot for its entire lifetime, ensuring table pointers remain
   valid throughout a parse session.

3. **Extension callbacks run under the registry write lock.** The
   `get_modifications` and `on_unload` callbacks execute while the registry
   write lock is held, so they must not attempt to acquire the registry
   lock themselves (deadlock).

4. **Token table reads are wait-free under no contention.** The RCU-style
   version check only retries when a concurrent write is detected, which
   is expected to be rare.

## Memory Management

### Ownership Model

- **Snapshots** own their grammar data arrays (`symbols`, `rules`, `states`)
  and action table arrays. All are freed when the refcount reaches zero.

- **Extensions** own their `modifications` array. The array is populated by
  `get_modifications()` and freed by `on_unload()`. The registry owns the
  `name` and `version` strings (copies made during registration).

- **Token table** owns all `TokenDefinition` entries and the hash table
  array. Lexeme strings are copied during `add_token()`.

- **JIT context** owns the LLVM module, execution engine, and compiled
  function pointer array. Freed by `jit_destroy()` or automatically when
  the owning snapshot is released via `jit_detach_from_snapshot()`.

- **Conflict sets** own all `Conflict` structs and their description
  strings. Freed by `conflict_set_destroy()`.

### Allocation Strategy

The project uses standard `malloc`/`calloc`/`realloc`/`free` throughout
the library code. The Lemon parser generator itself (`lime.c`) uses
custom wrappers (`lime_malloc`, `lime_calloc`, `lime_realloc`) that
track allocations in a linked list for bulk cleanup via `lime_free_all()`.

## Build System

The project uses Meson as its build system with a Nix flake for
reproducible development environments.

### Build Targets

| Target | Type | Contents |
|--------|------|----------|
| `lime` | Executable | Parser generator (`lime.c`) |
| `liblime_parser.a` | Static library | Core library (snapshot, extension, tokenizer, etc.) |
| `libtokenize_simd.a` | Static library | SIMD character classification |
| `liblime_jit.a` | Static library | JIT compilation (LLVM or stubs) |

### Conditional Compilation

- **`_GNU_SOURCE`**: Defined project-wide for POSIX extensions
  (`pthread_rwlock_t`, `clock_gettime`, etc.)
- **`LIME_NO_JIT`**: Defined when LLVM is not available; activates stub
  implementations in the JIT library
- **AVX2**: Uses `__attribute__((target("avx2")))` per-function, not
  global compiler flags

### Directory Layout

```
lime/
  lime.c              Lemon parser generator (standalone)
  limpar.c             Parser template (runtime code with %% placeholders)
  include/             Public headers (parser.h, token_table.h, etc.)
  src/                 Library implementation
  tests/               Test executables
  bench/               Benchmark executables
  test_harness/        Python test framework
  examples/            Example extensions
  docs/                Documentation (this file)
```
