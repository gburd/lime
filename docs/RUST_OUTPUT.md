# Rust Output Target

Lime can emit Rust as well as C.  This is a non-replacing addition:
`lime grammar.y` continues to produce `grammar.c` + `grammar.h`;
`lime --rust grammar.y` *additionally* produces `grammar.rs`.  Use
both flags together when you want C and Rust outputs from the same
source grammar (composable downstream).

**Status:** SKELETON in v0.7.x.  The `feat/rust-output` branch is
the development line; v0.8.0 ships the first usable Rust output.
This document is the design surface; pull commits on the branch for
the implementation as it lands.

## Why Rust output

PostgreSQL extensions and the broader plugin ecosystem are still
overwhelmingly C, but new tools targeting database parsers
increasingly want Rust:

- Memory-safe extension authoring (e.g. pgrx-style crates).
- Standalone CLI tools that consume a database dialect for
  formatting, linting, or migration tooling.
- WASM targets where C's runtime is awkward but Rust's is native.

A Rust output target lets one grammar definition produce both
languages' parsers, eliminating the dual-maintain burden every
existing dual-language tool incurs.

## What `--rust` produces

A single `.rs` file matching the input's basename:

```
grammar.lime  ->  grammar.rs
gram.y        ->  gram.rs
```

The file is a self-contained Rust module:

- `pub const FIRST_TOKEN: u16 = N;` -- the `%first_token` value.
- `pub const TOKEN_NAME: u16 = N;` -- one per terminal, external code.
- Action-table statics: `pub static YY_ACTION: &[u16] = &[...];` etc.
- A `<Name>Parser` struct with `new() / push() / finalize()`.
- A `ParseError` enum for the runtime contract.

The output is `no_std`-compatible for the const-data tables; the
parser struct uses `Vec` from `alloc::vec::Vec` (`std` by default;
opt out with a `no_std` feature flag in subsequent commits).  No
dependency on any Lime C runtime symbol -- the .rs is standalone.

## Design surface

### CLI

```
lime --rust grammar.y        # additive: also emits grammar.rs
lime grammar.y               # C only (existing)
lime --rust -d build/ g.y    # respects -d output directory
```

`--rust` composes with every other CLI flag (`-d`, `-l`, `-q`, etc.).

### File layout

The Rust file is a single flat module by default; subsequent commits
add a `--rust-crate` mode that emits a `Cargo.toml` + `src/lib.rs`
pair next to the .rs for ergonomic crate-style consumption.

### Action tables

`yy_action`, `yy_lookahead`, `yy_shift_ofst`, `yy_reduce_ofst`,
`yy_default`, `yy_rule_info_lhs`, `yy_rule_info_nrhs` from the C
generator are emitted as `&[u16]` / `&[i32]` / `&[i16]` / `&[i8]`
statics.  Indexing semantics match the C emit byte-for-byte:
`yy_action[yy_shift_ofst[state] + token]` returns the action.

`%first_token` semantics also match C: external code = internal +
FIRST_TOKEN.  The Rust `push` method does the same subtraction +
range check `parse_engine.c::parse_token` does.

### Reduce callbacks

v0.6.0's per-rule reduce dispatch maps cleanly onto Rust:

```rust
fn yy_rule_0(ctx: &mut ReduceCtx) { /* user action body */ }
fn yy_rule_1(ctx: &mut ReduceCtx) { /* user action body */ }
...

const YY_RULE_REDUCE_FN: &[fn(&mut ReduceCtx); NRULE as usize] = &[
    yy_rule_0, yy_rule_1, ..., yy_rule_N
];
```

Action bodies (the `{ ... }` blocks in `.y` source) are translated
verbatim from C to Rust where possible.  Subsequent commits define
the translation table:

| C action body            | Rust action body         |
|--------------------------|--------------------------|
| `$$ = $1 + $3`           | `*A = *B + *C`           |
| `$$ = strdup($1)`        | (must be Rust source)    |

For action bodies that don't translate (raw C calls, malloc, etc.),
the user supplies the Rust body via a parallel directive
`%rust_action { ... }` that overrides the C body when emitting Rust.

### Snapshots

The Rust parser does NOT integrate with the C `ParserSnapshot`
runtime.  It's a standalone parser.  Composition / hot-swap /
extension grammar features live on the C side; the Rust target is
"give me a parser library I can `use` in my crate".  When a
consumer needs both, run lime twice (once `--rust`, once without)
and link both outputs.

### Tests

`examples/rust_calc/` is the canonical test fixture: a Cargo crate
that depends on the lime-generated parser via a `build.rs` that
shells out to `lime --rust`.  The example's tests parse a few
arithmetic expressions and assert the output values.

## Roadmap on this branch

The branch ships in stages, each landing a self-contained
sub-feature so the diff stays reviewable:

1. **Skeleton** (this commit, `e3961a6+1`): CLI flag, dispatch,
   header + token constants + parser-struct stub.  Emits
   compileable but not-yet-functional Rust.
2. **Action-table emit**: populate the `&[u16]` / `&[i32]` etc.
   statics from `lemp->yy_*`.  Tables are byte-for-byte equivalent
   to what `ReportTable` produces in C.
3. **Reduce callback emit**: per-rule `fn yy_rule_N` + dispatch
   table.  Mirrors v0.6.0's C-side per-rule callbacks.
4. **`push() / finalize()` body**: copy the LALR loop from
   `parse_engine.c` to the Rust impl block.
5. **Action body translation**: simple cases (passthrough,
   arithmetic).  Document `%rust_action` directive for explicit
   Rust bodies.
6. **`examples/rust_calc/`**: end-to-end example with Cargo + tests.
7. **Lexer output (`--rust-lex`)**: the lex subsystem (M0-M5)
   already emits a tokenizer in C; mirror that to Rust.
8. **`--rust-crate` mode**: emit `Cargo.toml` + `src/lib.rs`.
9. **Merge to main, tag v0.8.0**.

## What v0.8.0 will NOT include

- `no_std` support for the parser struct (only the const-data
  tables are no_std today).  Defer to v0.8.x.
- Generic-typed `%token_type` / `%type` mapping.  The first cut
  uses `i64` / `String` / `Box<dyn Any>` placeholders; a real
  type-table translation lands in v0.8.x.
- A pure-Rust LALR generator (Lime in Rust).  Out of scope; this
  is a Rust *output*, the generator stays C.
- Re-implementing JIT in Rust.  No.

## Source

- `src/emit_rust.c` -- the emitter.
- `lime.c` -- CLI flag wiring + accessors that bridge `struct lime`
  internals to the emitter.
- `examples/rust_calc/` -- end-to-end example (lands with stage 6).
