# Extensible SQL Parser -- API Reference

This document covers the public C API for the extensible SQL parser library.
All public symbols are declared in headers under `include/`.

## Table of Contents

- [Library Version](#library-version)
- [Snapshot API](#snapshot-api)
- [Parse Context API](#parse-context-api)
- [Tokenizer API](#tokenizer-api)
- [Token Table API](#token-table-api)
- [SIMD Character Classification API](#simd-character-classification-api)
- [Extension API](#extension-api)
- [In-Process Compiler API](#in-process-compiler-api)
- [JIT Compilation API](#jit-compilation-api)
- [JIT Policy API](#jit-policy-api)
- [Data Structures Reference](#data-structures-reference)
- [Token Type Codes](#token-type-codes)
- [Error Handling Conventions](#error-handling-conventions)
- [Thread Safety](#thread-safety)
- [Linking and pkg-config](#linking-and-pkg-config)
- [Usage Examples](#usage-examples)

---

## Library Version

**Header:** `include/parser.h`

```c
const char *lime_parser_version(void);
```

Returns the library version as a NUL-terminated string (e.g. `"0.12.0"`).
The returned pointer is to static storage and must not be freed.

---

## Snapshot API

**Header:** `include/parser.h`
**Internal details:** `src/snapshot.h`

A `ParserSnapshot` captures the complete state of a parser's tables at a
point in time. Snapshots are reference-counted and immutable after creation.
Multiple threads can share a snapshot safely by acquiring references.

### Types

```c
typedef struct ParserSnapshot ParserSnapshot;  /* Opaque handle */
```

### Functions

#### lime_snapshot_create

```c
ParserSnapshot *lime_snapshot_create(const char *grammar_file, char **error);
```

Create a base snapshot by parsing a Lemon grammar file. On success, returns
a snapshot with reference count 1 and sets `*error` to `NULL`. On failure,
returns `NULL` and sets `*error` to a `malloc`'d error message that the
caller must `free()`.

**Parameters:**
- `grammar_file` -- Path to a `.y` grammar file.
- `error` -- Output pointer for error message on failure.

#### lime_snapshot_acquire

```c
ParserSnapshot *lime_snapshot_acquire(ParserSnapshot *snap);
```

Increment the reference count on a snapshot. Returns the same pointer for
convenience. The caller must eventually call `lime_snapshot_release()`.
Passing `NULL` is safe and returns `NULL`.

#### lime_snapshot_release

```c
void lime_snapshot_release(ParserSnapshot *snap);
```

Decrement the reference count. When it reaches zero, the snapshot and all
memory it owns (grammar data, action tables, JIT context) are freed.
Passing `NULL` is safe.

---

## Parse Context API

**Header:** `include/parse_context.h`

A `ParseContext` wraps a Lemon-generated parser with a pinned snapshot
reference. Table lookups are indirected through the snapshot rather than
compiled-in static arrays, enabling hot-swapping of parser tables when
extensions modify the grammar.

### Types

```c
typedef struct ParseContext ParseContext;  /* Opaque handle */
```

### Functions

#### parse_begin

```c
ParseContext *parse_begin(ParserSnapshot *snap);
```

Begin a new parse session pinned to `snap`. Acquires a reference to the
snapshot that is held until `parse_end()` is called. Returns `NULL` on
allocation failure. `snap` must not be `NULL`.

#### parse_begin_borrowed

*Available since v0.10.0.*

```c
ParseContext *parse_begin_borrowed(ParserSnapshot *snap);
```

Begin a new parse session pinned to `snap` **without** touching the
snapshot's atomic refcount. The caller MUST guarantee that `snap`
lives at least until `parse_end()` returns on the resulting
`ParseContext`. `parse_end()` detects the borrowed flag and skips
the matching `lime_snapshot_release()`.

**Why it exists.** `parse_begin` / `parse_end` issue an
`atomic_fetch_add` / `atomic_fetch_sub` on `snap->refcount`. At high
concurrency these LOCK-prefixed RMW ops serialise through the L3
cache-coherence directory; on `bench/bench_parse_fanout` the
refcount-acquiring API tops out at ~22% scaling efficiency at 8
threads. Skipping the atomics entirely closes the gap. Measured
3.4x throughput uplift at 8 threads (4M parses/sec → 13M parses/sec)
for identical work.

**When to use.** A typical parser server holds a snapshot for hours
while worker threads borrow it for milliseconds. Anything where the
snapshot's lifetime statically dominates the parse session's is a
candidate.

**Safety.** If the caller releases the snapshot while a borrowed
parse is in flight, `parse_token` will read freed memory. When in
doubt, use `parse_begin()` and accept the refcount cost.

```c
/* Server pattern: snapshot owned by the manager, parse threads borrow */
ParserSnapshot *snap = lime_snapshot_create("sql.y", &err);   /* refcount = 1 */

for (int t = 0; t < num_threads; ++t) {
    pthread_create(&threads[t], NULL, worker, snap);
}

/* Inside worker: */
ParseContext *ctx = parse_begin_borrowed(snap);   /* no atomic op */
parse_token(ctx, ...);
parse_end(ctx);                                   /* no atomic op */
```

#### parse_token

```c
int parse_token(ParseContext *ctx,
                int token_code,
                void *token_value,
                int location);
```

Feed one token to the parser.

**Parameters:**
- `ctx` -- Active parse context.
- `token_code` -- Integer token type (terminal symbol code). Pass `0` to
  signal end-of-input.
- `token_value` -- Opaque pointer to the semantic value. The layout is
  determined by the parser template's `TOKENTYPE`.
- `location` -- Byte offset of the token in the original source, or
  `LIME_LOC_UNKNOWN` (-1) if the grammar does not declare `%locations`
  or the caller does not track positions (e.g. the synthetic
  end-of-input token).  Threaded through `parse_token` into the runtime
  engine and into the snapshot's `yyloc` slot for each stack entry
  when `%locations` is active in the grammar.  Pass `LIME_LOC_UNKNOWN`
  if location data is unavailable for this token.

**Returns:** `0` on success, non-zero on error (syntax error or OOM).

**See also:** `LIME_LOC_UNKNOWN`.

#### parse_end

```c
void parse_end(ParseContext *ctx);
```

End the parse session. Releases the pinned snapshot reference and frees all
internal state. Passing `NULL` is safe.

#### parse_get_snapshot

```c
ParserSnapshot *parse_get_snapshot(ParseContext *ctx);
```

Return the snapshot pinned by this context. Valid as long as the context
is alive.

#### LIME_LOC_UNKNOWN

```c
#define LIME_LOC_UNKNOWN (-1)
```

Sentinel value for the `location` argument of `parse_token()`.  Pass
this when the grammar does not declare `%locations`, or when no
meaningful byte offset can be attributed to the token (the synthetic
end-of-input marker, runtime-injected tokens, etc.).  Guaranteed to be
`-1` so that integer byte offsets (always `>= 0`) never collide with
it.

### Snapshot-Indirected Table Access

These lower-level functions replace direct static array access in the
generated parser. They are primarily used internally by the parse engine.

```c
uint16_t snap_find_shift_action(const ParserSnapshot *snap,
                                uint16_t stateno, uint16_t iLookAhead);
uint16_t snap_find_reduce_action(const ParserSnapshot *snap,
                                 uint16_t stateno, uint16_t iLookAhead);
uint16_t snap_default_action(const ParserSnapshot *snap, uint16_t stateno);
```

---

## Tokenizer API

**Header:** `include/tokenize.h`

The tokenizer converts SQL text into a stream of tokens using
SIMD-accelerated character classification. It automatically selects the
fastest available implementation (AVX2 on x86_64, NEON on ARM, or scalar
fallback) at runtime.

### Types

```c
typedef struct Tokenizer Tokenizer;  /* Opaque handle */

typedef struct Token {
    int type;            /* Token type code (TK_* constant or keyword code) */
    const char *start;   /* Pointer into the source buffer */
    size_t length;       /* Length in bytes */
    uint32_t line;       /* 1-based line number */
    uint32_t column;     /* 1-based column number */
} Token;
```

### Functions

#### tokenizer_create

```c
Tokenizer *tokenizer_create(TokenTable *table, const char *input, size_t length);
```

Create a new tokenizer for the given input buffer.

**Parameters:**
- `table` -- Keyword lookup table for recognizing SQL keywords. Pass `NULL`
  for identifier-only mode (all identifiers return `TK_IDENTIFIER`).
- `input` -- NUL-terminated SQL input string. Must remain valid for the
  lifetime of the tokenizer.
- `length` -- Length of `input` in bytes, not including the NUL terminator.
  **Important:** The buffer must have at least 32 bytes of readable memory
  past the end (e.g., zero-padded) for SIMD safety.

**Returns:** A new tokenizer, or `NULL` on allocation failure.

#### tokenizer_destroy

```c
void tokenizer_destroy(Tokenizer *tok);
```

Destroy the tokenizer and free its memory. Passing `NULL` is safe.

#### tokenizer_next

```c
bool tokenizer_next(Tokenizer *tok, Token *out);
```

Extract the next token from the input. Returns `true` if a token was
produced, `false` at end-of-input. On `false` return, `out->type` is
`TK_EOF`.

Comments (both `--` single-line and `/* */` block) are skipped
automatically and never returned as tokens.

#### tokenizer_peek

```c
bool tokenizer_peek(Tokenizer *tok, Token *out);
```

Peek at the next token without consuming it. Subsequent calls to
`tokenizer_peek()` return the same token. The next call to
`tokenizer_next()` consumes the peeked token.

#### tokenizer_position

```c
size_t tokenizer_position(const Tokenizer *tok);
```

Return the current byte offset in the input.

#### tokenizer_line

```c
uint32_t tokenizer_line(const Tokenizer *tok);
```

Return the current 1-based line number.

#### tokenizer_column

```c
uint32_t tokenizer_column(const Tokenizer *tok);
```

Return the current 1-based column number.

### SIMD Acceleration

The tokenizer uses SIMD instructions to accelerate three hot paths:

1. **Whitespace skipping** -- Classifies 32 characters at a time to find
   the first non-whitespace character.
2. **Identifier scanning** -- Uses the alpha+digit bitmask to find
   identifier boundaries in 32-byte chunks.
3. **Number scanning** -- Uses the digit bitmask for bulk digit scanning in
   integer and float literals.

The SIMD implementation is selected automatically at runtime via
`get_classify_func()` and requires no user configuration.

---

## Token Table API

**Header:** `include/token_table.h`

The token table provides thread-safe keyword lookup using a hash table with
RCU-style versioning. Readers are lock-free; writers acquire an internal
write lock.

### Types

```c
typedef uint32_t ExtensionID;

typedef struct TokenDefinition {
    const char *lexeme;        /* Token string (e.g. "SELECT") */
    size_t lexeme_len;
    int token_code;            /* Numeric token ID */
    ExtensionID extension_id;  /* Which extension added it (0 = base) */
    uint32_t next_in_chain;    /* Internal: hash collision chain */
} TokenDefinition;

typedef struct TokenTable TokenTable;
```

### Functions

#### create_token_table

```c
TokenTable *create_token_table(uint32_t initial_capacity);
```

Create a new token table. `initial_capacity` is the initial number of
slots in the hash table. Returns `NULL` on allocation failure.

#### destroy_token_table

```c
void destroy_token_table(TokenTable *table);
```

Destroy the token table and free all memory.

#### lookup_token

```c
int lookup_token(TokenTable *table, const char *str, size_t len);
```

Look up a token by its string value. This is **lock-free** for concurrent
readers. Returns the `token_code` if found, or `-1` if not found.

#### add_token

```c
bool add_token(TokenTable *table, const char *lexeme, int token_code,
               ExtensionID ext_id);
```

Add a token to the table. Acquires the write lock internally. Returns
`true` on success, `false` on failure (allocation error or duplicate).

#### remove_tokens_by_extension

```c
bool remove_tokens_by_extension(TokenTable *table, ExtensionID ext_id);
```

Remove all tokens belonging to a given extension. Acquires the write lock
and rebuilds hash chains. Returns `true` on success.

---

## SIMD Character Classification API

**Header:** `src/tokenize_simd.h`

Low-level parallel character classification. Most users should use the
`Tokenizer` API instead; this interface is for advanced users building
custom scanners.

### Types

```c
typedef struct CharClassVector {
    uint32_t is_alpha_mask;  /* Bit i set if char i is [A-Za-z_] */
    uint32_t is_digit_mask;  /* Bit i set if char i is [0-9] */
    uint32_t is_space_mask;  /* Bit i set if char i is [ \t\n\r] */
} CharClassVector;

typedef CharClassVector (*ClassifyFunc)(const char *input, size_t offset);
```

### Functions

#### get_classify_func

```c
ClassifyFunc get_classify_func(void);
```

Return the best available classification function for the current CPU.
Performs runtime CPU feature detection (CPUID on x86, compile-time on ARM).

| Platform | CPU Feature | Function returned |
|---|---|---|
| x86_64 | AVX2 present | `classify_simd_avx2` (32 chars) |
| ARM | NEON (baseline on AArch64) | `classify_simd_neon` (16 chars) |
| Any | Fallback | `classify_scalar` (32 chars) |

#### classify_scalar

```c
CharClassVector classify_scalar(const char *input, size_t offset);
```

Classify 32 characters starting at `input + offset`. Always available on
every platform. The caller must ensure 32 bytes are readable from
`input + offset`.

#### classify_simd_avx2 (x86_64 only)

```c
CharClassVector classify_simd_avx2(const char *input, size_t offset);
```

AVX2 implementation. Classifies 32 characters in parallel using 256-bit
SIMD registers. Only callable on CPUs with AVX2 support -- use
`get_classify_func()` for safe dispatch.

#### classify_simd_neon (ARM only)

```c
CharClassVector classify_simd_neon(const char *input, size_t offset);
```

NEON implementation. Classifies 16 characters in parallel. Only the lower
16 bits of each mask field are meaningful.

---

## Extension API

**Headers:** `include/parser.h` (public entry points), `src/extension.h` (internal)

Extensions add grammar modifications (new tokens, rules, precedence
changes) to the parser at runtime. Each extension is managed through a
thread-safe registry.

### High-Level API (parser.h)

```c
bool lime_extension_registry_init(void);
void lime_extension_registry_destroy(void);
```

Initialize and destroy the global extension registry. Must be called before
and after any extension operations, respectively.

### Internal Registry API (src/extension.h)

#### Types

```c
typedef uint32_t ExtensionID;

typedef enum ExtensionState {
    EXT_REGISTERED,  /* Registered but not loaded */
    EXT_LOADED,      /* Active, modifications applied */
    EXT_UNLOADED,    /* Was loaded, now removed */
    EXT_ERROR,       /* Failed to load */
} ExtensionState;
```

**ExtensionInfo** -- Input to `register_extension()`:

```c
typedef struct ExtensionInfo {
    const char *name;
    const char *version;
    ExtGetModificationsFn get_modifications;  /* Required */
    ExtOnConflictFn on_conflict;              /* Optional */
    ExtOnUnloadFn on_unload;                  /* Optional */
    void *user_data;
} ExtensionInfo;
```

#### Extension Callbacks

```c
/* Called on load to get grammar modifications */
typedef bool (*ExtGetModificationsFn)(
    void *user_data,
    const ParserSnapshot *base_snapshot,
    GrammarModification **mods_out,
    uint32_t *nmods_out);

/* Called when two extensions conflict */
typedef ConflictResolution (*ExtOnConflictFn)(
    void *user_data,
    const ConflictInfo *info);

/* Called on unload for cleanup */
typedef void (*ExtOnUnloadFn)(void *user_data);
```

#### Registry Functions

```c
ExtensionRegistry *create_extension_registry(void);
void destroy_extension_registry(ExtensionRegistry *reg);

bool register_extension(ExtensionRegistry *reg,
                        const ExtensionInfo *info,
                        ExtensionID *id_out);

bool load_extension(ExtensionRegistry *reg,
                    ExtensionID id,
                    const ParserSnapshot *base_snapshot,
                    char **error);

bool unload_extension(ExtensionRegistry *reg, ExtensionID id);

const Extension *find_extension(ExtensionRegistry *reg, ExtensionID id);

uint32_t get_loaded_extension_count(ExtensionRegistry *reg);
```

### Grammar Modification Types

```c
typedef enum GrammarModType {
    MOD_ADD_RULE,
    MOD_ADD_TOKEN,
    MOD_MODIFY_PRECEDENCE,
    MOD_ADD_TYPE,
    MOD_REMOVE_RULE,
} GrammarModType;
```

Each modification is a `GrammarModification` struct with a `type` field and
a tagged union `u` containing the type-specific payload.

| Type | Union Field | Purpose |
|---|---|---|
| `MOD_ADD_RULE` | `u.add_rule` | Add a new production rule |
| `MOD_ADD_TOKEN` | `u.add_token` | Add a new terminal token |
| `MOD_MODIFY_PRECEDENCE` | `u.modify_prec` | Change symbol precedence |
| `MOD_ADD_TYPE` | `u.add_type` | Add a non-terminal type |
| `MOD_REMOVE_RULE` | `u.remove_rule` | Remove an existing rule |

#### MOD_ADD_RULE reduce actions

`u.add_rule` carries two fields for the rule's reduction action:

```c
typedef void (*LimeReduceFn)(
    void       *user_data,    /* opaque, from .reduce_user           */
    void       *extra_arg,    /* grammar's %extra_argument, or NULL  */
    int         nrhs,         /* count of RHS symbols in this rule    */
    const void *rhs_values,   /* array of nrhs %token_type payloads   */
    const int  *rhs_locs,     /* array of nrhs byte offsets, or NULL  */
    void       *lhs_out       /* writeback slot for the LHS value     */
);

struct {
    /* ... lhs / rhs / nrhs / precedence fields ... */
    LimeReduceFn  reduce;      /* runtime-dispatched action, or NULL */
    void         *reduce_user; /* opaque pointer passed to reduce()   */
    const char   *code;        /* generator-time C code, or NULL      */
} add_rule;
```

Precedence of the two action-source fields:

| `reduce` | `code`  | Behaviour |
|----------|---------|-----------|
| non-NULL | any     | Parser invokes `reduce(reduce_user, ...)` at reduce time. |
| NULL     | non-NULL| `code` is compiled into the parser's generated `reduce()` switch at generator time.  Applicable to grammars fed through `lime`; not usable from extensions loaded into a pre-compiled parser. |
| NULL     | NULL    | Rule reduces with no action. |

Reduce-based dispatch is wired through to the push-parser stack
in `src/parse_engine.c::reduce` (state machine is fully driven by
the snapshot's `yy_rule_info_lhs` and `yy_rule_info_nrhs` tables).

### Conflict Resolution

When two extensions modify the same grammar element, the `on_conflict`
callback is invoked:



```c
typedef enum ConflictResolution {
    CONFLICT_UNRESOLVED,    /* No resolution provided */
    CONFLICT_KEEP_EXISTING, /* Keep the existing item */
    CONFLICT_USE_NEW,       /* Replace with new item */
    CONFLICT_MERGE,         /* Extension provides merged result */
} ConflictResolution;
```

---

## In-Process Compiler API

**Header:** `include/lime_compiler.h`
**Library:** `liblime_compiler.a` (separate from `liblime_parser`).
**pkg-config:** `pkg-config --cflags --libs lime-compiler`
**Available since:** v0.5.4 (in-process compile), v0.10.0 (in-process lint).

These entry points compile or lint a grammar in the calling process,
without `fork`, `exec`, or a temp file. Eliminates the ~200 ms
latency of the subprocess pipeline used by `lime_compile_grammar_text`
and removes the runtime dependency on a C compiler. Required by
`lime-lsp`'s diagnostic refresh path and by daemon-startup grammar
composition.

### Functions

#### lime_compile_grammar_in_process

```c
int lime_compile_grammar_in_process(const char *grammar_text,
                                    size_t len,
                                    struct ParserSnapshot **out_snapshot,
                                    char **error);
```

Compile grammar text into a `ParserSnapshot` in-process. On success
returns `0` and sets `*out_snapshot` to a heap-allocated snapshot
(caller releases with `lime_snapshot_release()`). On failure returns
non-zero, sets `*out_snapshot` to `NULL`, and sets `*error` to a
`malloc`'d error message that the caller must `free()`.

**Thread safety.** Each call uses a fresh `LimeCompilerContext`
internally. The active-context pointer is currently process-global,
not `_Thread_local`, so two concurrent calls in different threads
race. Sequential calls in the same thread are fully isolated.
Thread-local storage is tracked under ROADMAP item 1, phase 5.

#### lime_lint_grammar_in_process

*Available since v0.10.0.*

```c
int lime_lint_grammar_in_process(const char *grammar_text,
                                 size_t len,
                                 char **out_diags);
```

Run the same parse + `FindActions` + `lint_grammar` pipeline that
`lime -L` runs, in-process, with no fork / exec / temp file. Used by
`lime-lsp`'s diagnostic refresh path.

Unlike `lime_compile_grammar_in_process()`, this entry point does
not build a `ParserSnapshot` — its purpose is purely diagnostics. It
runs through `FindActions` (so conflicts are visible to the linter)
and then `lint_grammar` (which emits `W`-class warnings the compile-
only path skips).

Captures everything the lint pipeline writes to stderr (parse errors,
conflict messages, lint diagnostics) into a heap-allocated buffer at
`*out_diags`. Output format matches `lime -L`'s default human format
— `lime-lsp`'s `parse_output()` in `lsp_diagnostics.c` consumes it
directly. Caller `free()`s `*out_diags`.

**Returns:**

| Value | Meaning |
|---|---|
| `0`         | Clean lint (zero errors, zero warnings). |
| non-zero    | Errors or warnings emitted. Parse `*out_diags` to know what. |
| `-1`        | In-process compiler context could not be set up (NULL grammar text, OOM). |

Measured ~10% / 200 ms saved per LSP request on PostgreSQL's 21 k-line
`gram.lime` versus the `fork`+`exec` pipeline. `lime-lsp` enables this
path by default; the meson option `-Dlime_lsp_in_process=enabled`
controls it explicitly.

#### Subprocess fallback warning

When `lime_compile_grammar_text` (the subprocess pipeline in
`src/snapshot_create.c`) is called and `lime_compile_grammar_in_process`
is not linked in, Lime emits a one-shot stderr warning naming the
missing link contract:

```text
lime: warning: subprocess pipeline (link -llime-compiler for in-process)
```

The warning fires once per process. Set `LIME_FORCE_SUBPROCESS=1` to
opt in to the subprocess path explicitly and suppress the warning.

### Modification Serializer

**Header:** `src/mod_serialize.h`

```c
char *lime_modifications_to_grammar_text(
    const GrammarModification *mods,
    uint32_t                   nmods,
    uint32_t                  *skipped_out,  /* may be NULL */
    char                     **error         /* may be NULL */
);
```

Render an array of `GrammarModification`s as `.lime`-syntax text that,
when concatenated after a base grammar and re-parsed by the `lime`
generator, produces a parser equivalent to applying the modifications.
This is the mechanism behind `publish_modified_snapshot`'s subprocess
rebuild path: the merged grammar is fed back through the `lime + cc +
dlopen` pipeline (`src/snapshot_create.c`).  The pure in-process LALR
rebuild remains the open item in `docs/ROADMAP.md` (item 1 -- structural
decoupling of `lime.c`'s `Find*` pipeline from file I/O and global
state).

**Returns:** malloc'd NUL-terminated buffer; `NULL` on allocation
failure or bad arguments.  Caller owns the buffer.

**Round-trip fidelity** -- not every modification serializes cleanly:

| Case | Behaviour |
|---|---|
| `MOD_ADD_RULE` with `.reduce != NULL` and `.code == NULL` | Skipped; counted in `*skipped_out`.  A function pointer has no text form. |
| `MOD_REMOVE_RULE` | Always skipped; concat cannot express removal.  Filter the base grammar text if removals must take effect. |
| `MOD_MODIFY_PRECEDENCE` with `new_assoc == 0` | Emitted as a comment (no single `.lime` directive expresses "no associativity"). |
| Integer `.precedence` on `MOD_ADD_RULE` | Emitted as a `/* NOTE */` comment; `.lime` uses `[SYMBOL]` markers, not numbers. |

Typical subprocess-fallback usage:

```c
uint32_t skipped = 0;
char *err = NULL;
char *fragment = lime_modifications_to_grammar_text(
    mods, nmods, &skipped, &err);
if (fragment == NULL) {
    fprintf(stderr, "serialization failed: %s\n", err);
    free(err);
    return -1;
}

/* concat base grammar text + fragment, write to tempfile,
** spawn `lime`, compile the output, dlopen the result */
FILE *tmp = fopen(tmpfile, "w");
fputs(base_grammar_text, tmp);
fputs(fragment, tmp);
fclose(tmp);
free(fragment);
```

---

## JIT Compilation API

**Header:** `include/jit_context.h`

Optional LLVM-based JIT compilation of parser action tables. When LLVM is
available, the JIT compiles specialized lookup functions for each parser
state, replacing table-driven lookups with direct branch sequences.

When compiled without LLVM (`LIME_NO_JIT`), all JIT functions degrade
to no-ops.

### Types

```c
typedef struct JITContext JITContext;  /* Opaque handle */

typedef uint16_t (*JITShiftActionFn)(uint16_t iLookAhead);

typedef enum JITStatus {
    JIT_OK = 0,
    JIT_ERR_NO_LLVM,
    JIT_ERR_INIT_FAILED,
    JIT_ERR_CODEGEN_FAILED,
    JIT_ERR_COMPILE_FAILED,
    JIT_ERR_LOOKUP_FAILED,
    JIT_ERR_INVALID_ARG,
    JIT_ERR_ALREADY_COMPILED,
} JITStatus;

typedef struct JITStats {
    uint32_t states_compiled;
    uint32_t states_total;
    uint64_t compile_time_ns;
    uint64_t code_size_bytes;
    bool     available;
} JITStats;
```

### Functions

#### High-Level (parser.h)

```c
bool lime_jit_available(void);
int  lime_jit_compile(ParserSnapshot *snap);
```

`lime_jit_available()` returns `true` if LLVM was linked and
initialization succeeds.

`lime_jit_compile()` compiles and attaches JIT code to a snapshot.
Returns `0` on success, non-zero on failure. No-op if already compiled
or LLVM is unavailable.

#### Low-Level (jit_context.h)

```c
JITStatus jit_create(JITContext **ctx_out);
void      jit_destroy(JITContext *ctx);

JITStatus jit_compile_snapshot(JITContext *ctx, const ParserSnapshot *snap);

JITShiftActionFn jit_get_shift_action(const JITContext *ctx, uint32_t state_id);

JITStats    jit_get_stats(const JITContext *ctx);
const char *jit_status_string(JITStatus status);
bool        jit_is_available(void);
```

#### Snapshot Integration

```c
JITStatus jit_attach_to_snapshot(ParserSnapshot *snap);
void      jit_detach_from_snapshot(ParserSnapshot *snap);

uint16_t jit_find_shift_action(const ParserSnapshot *snap,
                                uint16_t stateno,
                                uint16_t iLookAhead);
```

`jit_find_shift_action()` is the primary runtime dispatch function. If
the snapshot has JIT code for the given state, it uses the compiled path;
otherwise it falls back to the table-driven lookup.

---

## JIT Policy API

**Header:** `include/jit_policy.h`

Adaptive JIT compilation policy that decides when to compile based on
runtime metrics. Tracks per-snapshot usage and triggers compilation when
the expected benefit exceeds the cost.

### Types

```c
typedef struct JITMetrics {
    atomic_uint_fast64_t parse_count;
    atomic_uint_fast64_t total_parse_time_ns;
    atomic_uint_fast64_t action_lookup_count;
    atomic_int           is_jitted;
    atomic_int           jit_in_progress;
} JITMetrics;

typedef struct JITPolicyConfig {
    uint64_t min_parse_count;             /* Default: 50 */
    uint64_t min_total_parse_time_ns;     /* Default: 10,000,000 (10 ms) */
    uint64_t min_avg_lookups_per_parse;   /* Default: 100 */
    bool     background_compile;          /* Default: true */
} JITPolicyConfig;
```

### Functions

```c
JITPolicyConfig jit_policy_default_config(void);

void jit_metrics_init(JITMetrics *m);

void jit_metrics_record_parse(JITMetrics *m,
                              uint64_t parse_time_ns,
                              uint64_t action_lookups);

bool jit_should_compile(const JITMetrics *m, const JITPolicyConfig *config);

int jit_maybe_compile(ParserSnapshot *snap,
                      JITMetrics *m,
                      const JITPolicyConfig *config);

void jit_policy_shutdown(void);
```

`jit_maybe_compile()` returns `0` if compilation was triggered, `1` if
metrics do not yet warrant compilation, or `-1` on error. When
`background_compile` is true, compilation happens on a detached thread.

---

## Data Structures Reference

### ParserSnapshot (src/snapshot.h)

| Field | Type | Description |
|---|---|---|
| `version` | `uint64_t` | Monotonically increasing version number |
| `refcount` | `atomic_uint_fast32_t` | Reference count (starts at 1) |
| `symbols` | `struct symbol **` | Array of symbol structs |
| `nsymbol` | `uint32_t` | Total symbol count |
| `nterminal` | `uint32_t` | Terminal symbol count |
| `rules` | `struct rule *` | Linked list of production rules |
| `nrule` | `uint32_t` | Rule count |
| `states` | `struct state **` | Array of parser states |
| `nstate` | `uint32_t` | State count |
| `yy_action` | `uint16_t *` | Combined shift+reduce action array |
| `yy_lookahead` | `uint16_t *` | Parallel lookahead values |
| `yy_shift_ofst` | `int16_t *` | Per-state shift offset |
| `yy_reduce_ofst` | `int16_t *` | Per-state reduce offset |
| `yy_default` | `uint16_t *` | Default action per state |
| `create_time_ns` | `uint64_t` | Creation timestamp (nanoseconds) |
| `jit_ctx` | `void *` | Attached JIT context (or NULL) |

---

## Token Type Codes

Defined in `include/tokenize.h`. Keyword tokens use positive codes
assigned via the `TokenTable`. Built-in token types use non-positive
values:

| Constant | Value | Description |
|---|---|---|
| `TK_EOF` | 0 | End of input |
| `TK_IDENTIFIER` | -1 | Unrecognized identifier |
| `TK_INTEGER` | -2 | Integer literal (decimal or hex) |
| `TK_FLOAT` | -3 | Floating point literal |
| `TK_STRING` | -4 | Single-quoted string literal |
| `TK_BLOB` | -5 | Blob literal (`X'...'`) |
| `TK_LPAREN` | -6 | `(` |
| `TK_RPAREN` | -7 | `)` |
| `TK_SEMICOLON` | -8 | `;` |
| `TK_COMMA` | -9 | `,` |
| `TK_DOT` | -10 | `.` |
| `TK_STAR` | -11 | `*` |
| `TK_PLUS` | -12 | `+` |
| `TK_MINUS` | -13 | `-` |
| `TK_SLASH` | -14 | `/` |
| `TK_PERCENT` | -15 | `%` |
| `TK_EQ` | -16 | `=` or `==` |
| `TK_NE` | -17 | `!=` or `<>` |
| `TK_LT` | -18 | `<` |
| `TK_GT` | -19 | `>` |
| `TK_LE` | -20 | `<=` |
| `TK_GE` | -21 | `>=` |
| `TK_BITAND` | -22 | `&` |
| `TK_BITOR` | -23 | `\|` |
| `TK_BITNOT` | -24 | `~` |
| `TK_LSHIFT` | -25 | `<<` |
| `TK_RSHIFT` | -26 | `>>` |
| `TK_CONCAT` | -27 | `\|\|` |
| `TK_DQUOTE_ID` | -28 | `"quoted identifier"` |
| `TK_BACKTICK_ID` | -29 | `` `backtick identifier` `` |
| `TK_BRACKET_ID` | -30 | `[bracket identifier]` |
| `TK_ILLEGAL` | -31 | Unrecognized character |

---

## Error Handling Conventions

The library uses the following conventions for error reporting:

- **NULL return** -- Functions that create objects (`tokenizer_create`,
  `parse_begin`, `create_token_table`, `lime_snapshot_create`) return
  `NULL` on failure.

- **Boolean return** -- Functions that perform operations (`add_token`,
  `register_extension`, `load_extension`) return `false` on failure.

- **Error string** -- Functions with a `char **error` parameter set it to
  a `malloc`'d string on failure. The caller must `free()` this string.

- **Status codes** -- JIT functions return `JITStatus` enum values. Use
  `jit_status_string()` to convert to a human-readable message.

- **NULL-safe** -- Destroy/release functions (`tokenizer_destroy`,
  `parse_end`, `lime_snapshot_release`) accept `NULL` safely.

---

## Allocator Contract

Lime's generated parsers accept a caller-supplied allocator via
`XxxAlloc(void *(*mallocProc)(size_t))` (where `Xxx` is the
parser-name prefix set by `%name` or `-P`).  The matching
`XxxFree(void *, void (*freeProc)(void*))` uses the caller's `free`.
This is strictly better than Bison's `YYMALLOC`/`YYFREE` macro hack:
the allocator is passed as a first-class argument rather than baked
in at compile time.

The contract the generator relies on:

1. **Error semantics are caller-chosen.**  `mallocProc` may return
   `NULL` on failure, or it may never return (longjmp / throw).
   If `mallocProc` returns `NULL`, the parser enters a failure
   path and subsequent `Parse()` calls are no-ops.  If it longjmps
   out, the parser's internal state is left in whatever condition
   the jump leaves it; the caller must not reuse that parser
   instance without calling `XxxFree` first.

2. **Pairing is symmetric.**  `freeProc` is called exactly as many
   times as `mallocProc` succeeded -- one call per successful
   allocation -- and always on the pointers `mallocProc` returned.

3. **No assumptions about alignment beyond `max_align_t`.**
   Pointers returned by `mallocProc` must satisfy the alignment
   requirements of any C type up to `max_align_t` (the guarantee
   `malloc(3)` gives).  Lime never allocates over-aligned objects.

4. **Allocation sites are stack growth, token buffer growth, and
   the parser handle itself.**  Typical parsers allocate once at
   `XxxAlloc` time and then occasionally as the shift stack grows
   past `%stack_size`.  Callers embedding Lime in memory-constrained
   contexts can set `%stack_size` to a static upper bound to avoid
   runtime growth.

This contract lets a Lime-driven parser hosted inside a language
runtime (e.g. one with a memory-context-aware allocator and
longjmp-based error handling) delegate allocation to that runtime
without macro gymnastics.

---

## Thread Safety

| Component | Read | Write |
|---|---|---|
| `ParserSnapshot` | Thread-safe (immutable after creation) | N/A (immutable) |
| `snapshot_acquire/release` | Thread-safe (atomic refcount) | N/A |
| `ParseContext` | Single-thread only | Single-thread only |
| `Tokenizer` | Single-thread only | Single-thread only |
| `TokenTable` lookup | Lock-free (concurrent readers) | Write-locked |
| `TokenTable` add/remove | N/A | Acquires internal lock |
| `ExtensionRegistry` | Read-locked (concurrent) | Write-locked |
| `JITMetrics` | Atomic reads | Atomic updates |

**Key points:**

- Snapshots are safe to share across threads. Acquire a reference per
  thread.
- Each `ParseContext` and `Tokenizer` is single-threaded. Create one per
  thread/task.
- The `TokenTable` supports concurrent readers with lock-free lookups.
  Writes (adding/removing tokens) serialize internally.
- JIT metrics use atomic operations for contention-free updates from
  multiple parser threads.

---

## Linking and pkg-config

*pkg-config files since v0.5.4 (`lime`); `lime-compiler` since v0.10.0.*

`meson install` ships two pkg-config files:

| File | Library | Use it for |
|---|---|---|
| `lime.pc`          | `liblime_parser` | The runtime push parser, snapshots, tokenizer, extension registry, JIT. |
| `lime-compiler.pc` | `liblime_compiler` (depends on `liblime_parser`) | The in-process compiler / linter API (`lime_compile_grammar_in_process`, `lime_lint_grammar_in_process`). |

Most consumers want only `lime`:

```sh
cc $(pkg-config --cflags lime) main.c $(pkg-config --libs lime)
```

Consumers that need the in-process compile or lint API link both via
the `Requires.private` chain:

```sh
cc $(pkg-config --cflags lime-compiler) main.c $(pkg-config --libs lime-compiler)
```

The split exists because `liblime_compiler.a` is ~1.2 MB of
LALR-construction code that most parser-runtime consumers never
need. Splitting keeps the `liblime_parser` link footprint small for
the common embed-a-parser case.

---

## Usage Examples

### Basic Tokenization

```c
#include "tokenize.h"
#include "token_table.h"

/* Prepare a padded input buffer (32 extra bytes for SIMD) */
const char *sql = "SELECT id, name FROM users WHERE active = 1;";
size_t len = strlen(sql);
char *buf = calloc(1, len + 64);
memcpy(buf, sql, len);

/* Optional: set up keyword table */
TokenTable *keywords = create_token_table(64);
add_token(keywords, "SELECT", 100, 0);
add_token(keywords, "FROM",   101, 0);
add_token(keywords, "WHERE",  102, 0);

/* Tokenize */
Tokenizer *tok = tokenizer_create(keywords, buf, len);
Token t;
while (tokenizer_next(tok, &t)) {
    printf("line %u col %u: type=%d '%.*s'\n",
           t.line, t.column, t.type, (int)t.length, t.start);
}

tokenizer_destroy(tok);
destroy_token_table(keywords);
free(buf);
```

### Snapshot Lifecycle

```c
#include "parser.h"

char *error = NULL;
ParserSnapshot *snap = lime_snapshot_create("sql.y", &error);
if (!snap) {
    fprintf(stderr, "Error: %s\n", error);
    free(error);
    return 1;
}

/* Share across threads */
ParserSnapshot *ref = lime_snapshot_acquire(snap);

/* ... use ref in another thread ... */

lime_snapshot_release(ref);   /* Thread done */
lime_snapshot_release(snap);  /* Original reference */
```

### Parse Session

```c
#include "parser.h"
#include "parse_context.h"

ParseContext *ctx = parse_begin(snap);

/* Feed tokens from the tokenizer */
Token t;
while (tokenizer_next(tok, &t)) {
    if (t.type == TK_EOF) break;
    int rc = parse_token(ctx, t.type, (void *)&t, (int)t.offset);
    if (rc != 0) {
        fprintf(stderr, "Parse error at line %u col %u\n", t.line, t.column);
        break;
    }
}
parse_token(ctx, 0, NULL, LIME_LOC_UNKNOWN);  /* Signal end-of-input */

parse_end(ctx);
```

### Borrowed-Snapshot Parse Session (Server Pattern)

```c
#include "parser.h"
#include "parse_context.h"
#include <pthread.h>

/* Manager owns the snapshot.  Worker threads borrow it. */
static ParserSnapshot *g_snap;

static void *worker(void *arg) {
    /* No atomic refcount op on entry or exit -- 3.4x throughput
    ** uplift at 8 threads versus parse_begin/parse_end. */
    ParseContext *ctx = parse_begin_borrowed(g_snap);
    /* ... feed tokens ... */
    parse_end(ctx);
    return NULL;
}

int main(void) {
    char *err = NULL;
    g_snap = lime_snapshot_create("sql.y", &err);   /* refcount = 1 */

    pthread_t threads[8];
    for (int i = 0; i < 8; ++i)
        pthread_create(&threads[i], NULL, worker, NULL);
    for (int i = 0; i < 8; ++i)
        pthread_join(threads[i], NULL);

    /* Workers all exited.  Now safe to release the only reference. */
    lime_snapshot_release(g_snap);
}
```

### In-Process Lint (LSP Diagnostics Pattern)

```c
#include "lime_compiler.h"     /* lime_compile_grammar_in_process */
#include "parser.h"

extern int lime_lint_grammar_in_process(const char *, size_t, char **);

void lsp_publish_diagnostics(const char *grammar_text, size_t len) {
    char *diags = NULL;
    int rc = lime_lint_grammar_in_process(grammar_text, len, &diags);
    if (rc == 0) {
        /* clean lint */
        free(diags);
        return;
    }
    /* Parse the captured stderr into LSP Diagnostic[] -- format
    ** matches `lime -L`'s default human format.  See lime-lsp's
    ** lsp_diagnostics.c::parse_output for a worked parser. */
    publish_to_client(diags);
    free(diags);
}
```

### Extension Registration

```c
#include "parser.h"
#include "extension.h"

/* Extension callback: provide modifications */
static bool my_get_mods(void *user_data,
                        const ParserSnapshot *base,
                        GrammarModification **mods_out,
                        uint32_t *nmods_out) {
    static GrammarModification mods[1];
    mods[0].type = MOD_ADD_TOKEN;
    mods[0].description = "Add JSONB arrow operator token";
    mods[0].u.add_token.name = "TK_JSONB_ARROW";
    mods[0].u.add_token.lexeme = "->";
    mods[0].u.add_token.token_code = -1;  /* auto-assign */
    *mods_out = mods;
    *nmods_out = 1;
    return true;
}

/* Register and load */
ExtensionRegistry *reg = create_extension_registry();
ExtensionInfo info = {
    .name = "jsonb_ops",
    .version = "1.0.0",
    .get_modifications = my_get_mods,
};
ExtensionID id;
register_extension(reg, &info, &id);

char *error = NULL;
load_extension(reg, id, snap, &error);
```

### JIT Compilation with Policy

```c
#include "parser.h"
#include "jit_policy.h"

/* Initialize metrics and policy */
JITMetrics metrics;
jit_metrics_init(&metrics);
JITPolicyConfig policy = jit_policy_default_config();
policy.min_parse_count = 100;

/* After each parse session, record metrics */
jit_metrics_record_parse(&metrics, elapsed_ns, lookup_count);

/* Check if JIT compilation should trigger */
int rc = jit_maybe_compile(snap, &metrics, &policy);
/* rc == 0: compilation triggered
** rc == 1: not yet warranted
** rc == -1: error */

/* At shutdown */
jit_policy_shutdown();
```
