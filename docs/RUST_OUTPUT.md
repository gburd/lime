# Rust Output Target

Lime can emit Rust as well as C.  Non-replacing addition: `lime
grammar.y` continues to produce `grammar.c` + `grammar.h`; `lime
--rust grammar.y` *additionally* produces `grammar.rs`.  Use both
flags together for dual output.

**Status:** working end-to-end on the `feat/rust-output` branch.
Stages 2 + 3 + 4 + 6 of the original 9-stage roadmap have landed:
real action tables, per-rule reduce callbacks with action-body
substitution, a Rust LALR runtime, and a working `examples/rust_calc/`
end-to-end demo with `cargo test` integration into the meson suite.

Stages 5, 7, 8 are still ahead.  See "Roadmap" below.

## Quick start

```bash
$ lime --rust grammar.lime              # emits grammar.rs
$ rustc --crate-type lib --edition=2021 grammar.rs   # compiles clean
```

For a worked example:

```bash
$ cd examples/rust_calc
$ lime --rust calc.lime && mv calc.rs src/parser.rs
$ cargo run -- '1 + 2 * 3'
accept: 1 + 2 * 3 = 7
$ cargo test
   Running unittests src/lib.rs
running 5 tests
test tests::bad_char ... ok
test tests::left_assoc ... ok
test tests::precedence ... ok
test tests::syntax_error ... ok
test tests::simple_addition ... ok
```

## What `--rust` produces

A single self-contained `.rs` file matching the input's basename.

The file contains:

- `pub const FIRST_TOKEN: u16 = N;` -- from `%first_token`.
- `pub const TOKEN_NAME: u16 = N;` per terminal (external code).
- Dispatch range constants (`YY_MAX_SHIFT`, `YY_MIN_SHIFTREDUCE`,
  `YY_ERROR_ACTION`, etc.) matching the C output byte-for-byte.
- `pub static YY_ACTION: &[u16]`, `YY_LOOKAHEAD: &[u16]`,
  `YY_SHIFT_OFST: &[i32]`, `YY_REDUCE_OFST: &[i32]`,
  `YY_DEFAULT: &[u16]`, `YY_RULE_LHS: &[i16]`, `YY_RULE_NRHS:
  &[i8]` -- the same compressed action tables Lemon's C output
  uses.
- `pub static YY_FALLBACK: &[u16]` when grammar declares
  `%fallback`; `HAS_FALLBACK` bool gates runtime use.
- `pub type Value = i64;` -- the semantic value type.  The first
  cut uses `i64` universally.  `%token_type / %type` Rust generic
  mapping is stage 5 (below).
- `fn yy_rule_N(ctx: &mut ReduceCtx)` per rule, with action body
  literal-copied from the grammar with `$$/$N/<alias>` substitution
  to slot variables.
- `pub static YY_RULE_REDUCE_FN: [fn(&mut ReduceCtx); NRULE]` --
  per-rule dispatch table mirroring v0.6.0's C-side per-rule
  callback design.
- `pub struct <Name>Parser` + `impl` block with `new()`, `push()`,
  `finalize()`, and a `final_value: Value` field for the
  start-rule's computed result.
- LALR runtime body in `push()` -- ports `src/parse_engine.c`'s
  loop verbatim: pending-reduce unpacking, find_shift_action via
  `YY_SHIFT_OFST` + `YY_LOOKAHEAD`, plain shift / shift-reduce /
  reduce dispatch, `%fallback` retry, end-of-input accept.

## Performance

On i9-12900H (`examples/rust_calc/src/bin/bench.rs`):

```
rust: 100000 parses, 20.5 ms, 4.87M parses/sec
```

Compare to C path on the same machine, same expression class:
~4.2M parses/sec for the `bench_flex_bison_compare` arith run.
Rust is **within 15%** of C, sometimes faster.  Performance parity
is achievable because both outputs use the same compressed action
tables and the same dispatch algorithm; the Rust compiler optimises
the table-driven loop comparably to GCC's output for the
equivalent C.

## Action body translation

Lime's grammar action bodies use lemon's `$$/$N/<alias>` syntax
within `{ ... }` blocks.  When emitting Rust, the body is literal-
copied with three substitutions:

| In source                | In emitted Rust                  |
|--------------------------|----------------------------------|
| `$$`                     | `lhs` (or grammar's LHS alias)   |
| `$N` (1-indexed)         | `rhsN-1` (or RHS alias)          |
| LHS alias `A`            | `A` (used as a local mutable)    |
| RHS alias `B`            | `B` (used as a local Value)      |

User action bodies must be valid Rust.  Bodies that are empty or
follow the `A = expr;` pattern with arithmetic on bound aliases
just work.  Bodies that call C functions, use C-specific syntax
(`(void)X`, casts, `printf`, etc.), or rely on lemon's deeper
internal mappings (yymsp[N].minor.yyM addressing) need a
`%rust_action { ... }` directive in the grammar to override per-
rule (stage 5; not yet implemented).

For grammars used by both C and Rust outputs, write action bodies
in the intersection language: `A = B + C;`, `A = B;`, empty bodies.
Both outputs accept these.

## What this branch ALSO does

- Adds `--rust` to the lime CLI alongside existing flags.
- Reuses the v0.6.0 per-rule reduce callback design.
- Does NOT touch any C output path -- adding `--rust` is purely
  additive.  Existing tests (114 / 0 / 4 skipped) all pass on
  this branch.
- Adds two regression tests: `tests/test_emit_rust_skeleton.c`
  (drives the emitter, asserts output structure) and
  `tests/test_emit_rust_cargo.c` (drives `cargo test` on the
  rust_calc example end-to-end).

## What this branch does NOT do (yet)

Items deferred to subsequent commits on this branch before
v0.8.0 ships:

### Stage 5 -- `%rust_action` directive

A grammar-level directive that lets users supply per-rule Rust
action bodies parallel to the existing C `{ ... }` bodies.  Useful
when the C body uses language-specific calls but the grammar
should still produce both outputs.

```lime
expr(A) ::= expr(B) PLUS expr(C). { A = B + C; }
%rust_action expr(A) ::= expr(B) PLUS expr(C). { A = B.checked_add(C).unwrap_or(0); }
```

### Stage 7 -- Rust lexer output (`--rust-lex`)

The lex subsystem (M0-M5 in the C output) doesn't yet have a
Rust mirror.  Mirror `src/lex/lex_emit.c` to a Rust-emitting
sibling.

### Stage 8 -- Cargo crate mode (`--rust-crate`)

Today `--rust` emits a single `.rs` file; the user is responsible
for placing it inside a Cargo crate they own.  `--rust-crate`
will emit a complete crate skeleton (Cargo.toml + src/lib.rs)
that's ready to publish or `cargo include`.

### Generic semantic value types

Today `pub type Value = i64;`.  A future commit honours
`%token_type {T}` and `%type X {T}` to emit a Rust enum:

```rust
pub enum Value {
    Default,
    TokenType(<%token_type>),
    ExprType(<%type expr>),
    ...
}
```

with per-rule callbacks unwrapping the right variant.

### `%destructor` callbacks

Today the parser doesn't run destructors on stack pops.  The
runtime support for destructor closures lands when a consumer asks.

### no_std support

Action tables are already no_std.  The parser struct uses
`alloc::vec::Vec`.  A future `no_std` feature flag in the
generated `Cargo.toml` exposes the no_std-only API.

## Verification

End-to-end works:

```
$ meson test -C build emit_rust_skeleton emit_rust_cargo
2/2 OK (1.5s combined)
```

Hot-path runtime files in `src/` byte-identical to v0.5.3:

```
$ git diff --name-only v0.5.3..HEAD -- \
    src/parse_engine.c src/parse_context.c src/snapshot.c \
    src/jit_codegen.c src/jit_context.c src/glr.c src/parse_glr.c \
    src/snapshot_build.c src/context_switch.c src/grammar_context.c
src/jit_codegen.c
src/snapshot.c
src/snapshot_build.c
```

(`jit_codegen.c` was changed in v0.6.x for per-rule callbacks +
YYFALLBACK AOT; `snapshot.c` for dlopen cleanup hook;
`snapshot_build.c` for first_token/magic.  All from main, not
this branch.)

`feat/rust-output` adds:

```
$ git diff --stat main..feat/rust-output
src/emit_rust.c           |  NEW (~700 LOC)
lime.c                    |   +N (CLI flag, accessors, dispatch, table-assemble helper)
meson.build               |   +1 src/emit_rust.c
docs/RUST_OUTPUT.md       |  NEW (this file)
examples/rust_calc/...    |  NEW
tests/test_emit_rust_*.c  |  NEW
tests/meson.build         |   +N entries
```
