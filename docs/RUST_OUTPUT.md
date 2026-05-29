# Rust Output Target — feat/rust-output

Lime can emit Rust as well as C.  Non-replacing addition: `lime
grammar.y` continues to produce `grammar.c` + `grammar.h`; `lime
--rust grammar.y` *additionally* produces `grammar.rs`.  Use
both flags together for dual output.

**Status:** five substantive commits on `feat/rust-output`.  The
parser side is feature-complete for arithmetic-style grammars
plus extensions covering user-arg threading, custom value types,
parse hooks, no_std, Cargo crate emission, and per-rule Rust body
overrides.  The lexer subsystem (`--rustlex`) is the one piece
deferred for a future commit.

## Quick start

```bash
$ lime --rust grammar.lime              # emits grammar.rs
$ rustc --crate-type lib --edition=2021 grammar.rs

$ lime --rust --rustcrate grammar.lime  # emits grammar_crate/
$ cd grammar_crate && cargo build       # crate-style consumption
```

For a worked example, see `examples/rust_calc/`.

## CLI flags

| Flag | Effect |
|---|---|
| `--rust` | Emit `grammar.rs` alongside C output. |
| `--rustcrate` | + Cargo.toml + src/lib.rs around the .rs (ready-to-build crate). |
| `--rustnostd` | Use `alloc::vec::Vec` instead of std's prelude Vec.  When combined with `--rustcrate`, lib.rs gets `#![no_std]` + `extern crate alloc;` at the crate root. |
| `--rustlex` | Lexer Rust output.  **DEFERRED in v0.8.0** — prints a notice. |

Flag ordering matters in the lime CLI: `handleflags()` does
prefix-match.  `--rustlex` must precede `--rust` in the options
table; we maintain that invariant.

## Grammar directives

| Directive | Effect on Rust output |
|---|---|
| `%name X` | Parser type is `XParser`. |
| `%token A B C.` | `pub const A: u16`, `B: u16`, `C: u16`. |
| `%first_token N` | `pub const FIRST_TOKEN: u16 = N;` external code = internal + N. |
| `%left` / `%right` / `%nonassoc` | Folded into action tables (transparent). |
| `%fallback PRIMARY ALT.` | `YY_FALLBACK: &[u16]` slice; runtime retries with substitute. |
| `%wildcard` | Implicit in action tables (transparent). |
| `%start_symbol s` | Used to drive the LALR construction; transparent at output. |
| `%rust_value_type {T}` | `pub type Value = T;` (default `i64`). |
| `%rust_extra_argument {T}` | `pub type UserArg = T;` and threaded into ReduceCtx as `&mut UserArg`. |
| `%rust_action { ... }` | Per-rule Rust body override (verbatim emit). |
| `%rust_syntax_error { ... }` | Hook on parse error. `_token: u16, _state: u16`. |
| `%rust_parse_accept { ... }` | Hook on accept.  `self.final_value` available. |
| `%rust_parse_failure { ... }` | Hook on unrecoverable failure. |
| `%rust_stack_overflow { ... }` | Hook on stack overflow.  In Rust the Vec stack grows; mostly informational. |

C-side directives that don't affect Rust output (silently ignored):
`%token_type` / `%type` / `%token_destructor` / `%default_destructor`
/ `%extra_argument` / `%extra_context` / `%include` / `%syntax_error`
/ `%parse_accept` / `%parse_failure` / `%stack_overflow` / `%code`.
For Rust equivalents, use the matching `%rust_*` directive.

## What `--rust` produces

A self-contained `.rs` file matching the input's basename:

- Provenance header with regen instructions
- `#![allow(...)]` crate attrs + optional `extern crate alloc; use alloc::vec::Vec;` (with `--rustnostd`)
- `pub const FIRST_TOKEN`, `NSTATE`, `NRULE`, `NTERMINAL`, `NSYMBOL`, `NTOKEN`
- Dispatch range constants (`YY_MAX_SHIFT`, `YY_MIN_SHIFTREDUCE`, `YY_ERROR_ACTION`, etc.) byte-for-byte equivalent to C
- `pub static YY_ACTION: &[u16]`, `YY_LOOKAHEAD: &[u16]`, `YY_SHIFT_OFST: &[i32]`, `YY_REDUCE_OFST: &[i32]`, `YY_DEFAULT: &[u16]`, `YY_RULE_LHS: &[i16]`, `YY_RULE_NRHS: &[i8]`, optional `YY_FALLBACK: &[u16]`
- `pub type Value = ...;` (from `%rust_value_type` or `i64` default)
- `pub type UserArg = ...;` (from `%rust_extra_argument` or `()`)
- `pub struct ReduceCtx<'a>` with `lhs: &mut Value`, `rhs: &mut [Value]`, `user: &mut UserArg`
- `fn yy_rule_N(ctx: &mut ReduceCtx)` per rule, action body literal-copied with `$$/$N/<alias>` substitution OR `%rust_action` verbatim
- `pub static YY_RULE_REDUCE_FN: [fn(&mut ReduceCtx); NRULE]`
- `pub enum ParseError` with `SyntaxError`, `OutOfRange`, `BadReduce`, `StackUnderflow`
- `pub struct <Name>Parser` with `stack: Vec<Frame>`, `accepted`, `errored`, `final_value: Value`, `user: UserArg`
- `impl <Name>Parser`:
  - `new() -> Self where UserArg: Default`
  - `new_with_user(user: UserArg) -> Self`
  - `push(&mut self, token_code: u16, value: Value) -> Result<bool, ParseError>`
  - `finalize(&mut self) -> Result<bool, ParseError>` (= `push(0, default)`)
  - 4 hook methods: `on_syntax_error`, `on_parse_accept`, `on_parse_failure`, `on_stack_overflow`

## Performance

On i9-12900H, `examples/rust_calc/src/bin/bench.rs`:

```
rust:  ~21 ms / 100K parses = 4.87M parses/sec
c:     ~24 ms / 100K parses = 4.17M parses/sec    (bench_flex_bison_compare)
```

Rust within 15%, sometimes faster.  Performance parity is real
because both outputs use the same compressed action tables and
the same dispatch algorithm; rustc/LLVM optimises the table-driven
loop comparably to GCC's output for the equivalent C.

## Action body translation

Default behaviour (no `%rust_action`):

| Syntax in source | Substituted to |
|---|---|
| `$$` | `lhs` (or LHS alias) |
| `$N` (1-indexed) | `rhs(N-1)` (or RHS alias) |
| LHS alias `A` | `A` (mutable local Value) |
| RHS alias `B` | `B` (Value, cloned) |

User-written body must be valid Rust.  The intersection language
(arithmetic, simple assignments) compiles for both targets:

```lime
expr(A) ::= expr(B) PLUS expr(C). { A = B + C; }   /* both C and Rust */
```

For bodies that need different semantics or non-portable syntax,
`%rust_action` overrides:

```lime
expr(A) ::= expr(B) PLUS expr(C). { A = B + C; }
%rust_action { let v = B.checked_add(C).expect("overflow"); A = v; }
```

The `%rust_action` body is emitted **verbatim**: no
`$$/$N/<alias>` substitution.  The user has full Rust syntax
including pattern matching, `?` operator, method calls, etc.

## Working example

`examples/rust_calc/`:

```
$ cd examples/rust_calc
$ lime --rust calc.lime && mv calc.rs src/parser.rs
$ cargo run -- '1 + 2 * 3'
accept: 1 + 2 * 3 = 7
$ cargo test --lib
test tests::bad_char ... ok
test tests::left_assoc ... ok        # 10 - 3 - 2 = 5
test tests::precedence ... ok        # 1 + 2 * 3 = 7
test tests::syntax_error ... ok      # "1 +" rejects
test tests::simple_addition ... ok   # 1 + 2 = 3
```

## Stages on this branch

Five commits beyond v0.7.0:

1. `850ad26` — skeleton CLI flag + dispatch + token consts (Stage 1).
2. `a88b7e9` — real action tables + reduce callbacks + LALR runtime + working example (Stages 2 + 3 + 4 + 6).
3. `cffad0f` — `--rustcrate` Cargo crate emission (Stage 8).
4. `19d8c41` — `%rust_action` per-rule body override (Stage 5).
5. `1753879` — `%rust_extra_argument` user-arg threading.
6. `c59fb25` — four parse hooks (`%rust_syntax_error` / `accept` / `failure` / `overflow`).
7. `5418b7c` — `%rust_value_type` override + `--rustnostd` no_std support.
8. THIS COMMIT — `--rustlex` stub + comprehensive docs.

## What remains DEFERRED (post v0.8.0)

### Stage 7 — `--rustlex` (Rust lexer output)

The lex subsystem (M0-M5) emits a flex-equivalent C tokenizer.
Mirroring it to Rust requires:

- Mirroring `src/lex/lex_emit.c`'s DFA-table emission to a Rust
  emit_lex_rust.c.  ~400-600 LOC.
- Emitting Rust DFA tables (`pub static LEX_DFA_TRANSITIONS:
  &[u16]` etc.).  Straightforward translation of the C arrays.
- Porting the lex runtime (FeedBytes / Pushback / SetState etc.)
  to Rust.  ~300-500 LOC.
- Action body translation (analogous to the parser's, but with
  the lex action conventions: `LEX_EMIT(tok)`, `LEX_SKIP()`,
  `LEX_TRANSITION(state)`).

Estimated 1000-1500 LOC across emit + runtime + tests.  Defer
until a real consumer needs it; the parser-only `--rust` covers
grammars that supply their own tokenizer (PG-team's pattern,
which uses a hand-written scan.c).

`--rustlex` is wired up as a CLI flag that prints a notice and
exits 0; consumers can `lime --rust --rustlex grammar.lex` today
and get a useful error message rather than a silent wrong output.

### Generic Value enum from `%token_type` / `%type`

`%rust_value_type {T}` covers the use case where one Value type
suits the whole grammar.  The C-side approach of per-symbol type
declarations (`%token_type {int}`, `%type expr {Box<Expr>}`,
`%type ident {String}`) requires emitting a Rust enum:

```rust
pub enum Value {
    Default,
    Token(int),
    Expr(Box<Expr>),
    Ident(String),
}
```

with per-rule callbacks unwrapping the right variant via
pattern match.  This is a non-trivial codegen — each rule's
action body needs to know which variant its slots are in.  The
existing `%rust_value_type` override is the v0.8.0 escape hatch
for grammars with non-trivial semantic types.

### `%destructor` callbacks on stack pop

The C runtime fires `%destructor` bodies when popping unconsumed
RHS slots during error recovery.  In Rust, the equivalent is
`Drop` — Vec frames are auto-dropped when truncated.  The Rust
parser today doesn't fire user-supplied `%destructor` bodies at
custom callback sites.

For grammars whose semantic values need explicit destruction
beyond Drop, `%rust_destructor { ... }` would parallel the
existing C side.  Defer until asked.

### `%rust_include`

The C-side `%include { ... }` injects a code fragment at the top
of the generated `.c`.  The Rust analog `%rust_include` would
emit `use ...;` statements.  Today users add use-statements via
the consuming Cargo crate's lib.rs (not the generated parser.rs),
which is the more idiomatic Rust pattern anyway.

## Verification

```
$ meson test -C build    # 114 / 0 ok stock + ASan/UBSan
$ ninja -C build lime
$ ./build/lime --help | grep -i rust    # 4 flags advertised

$ # End-to-end smoke
$ ./build/lime --rust /tmp/grammar.lime && rustc --crate-type lib /tmp/grammar.rs
$ ./build/lime --rust --rustcrate /tmp/g.lime
$ cd /tmp/g_crate && cargo build
$ ./build/lime --rust --rustnostd --rustcrate /tmp/g.lime
$ cd /tmp/g_crate && cargo build      # no_std build clean
```

## Branch state

```
feat/rust-output:
  HEAD ── --rustlex stub + docs polish        (this commit)
   │
   ├── --rustnostd + %rust_value_type
   ├── parse hooks (%rust_syntax_error/etc.)
   ├── %rust_extra_argument
   ├── %rust_action
   ├── --rustcrate
   ├── action tables + LALR runtime + rust_calc
   └── skeleton emitter
       │
       e3961a6 release: v0.7.0  ← branch point
```

Ready for review and merge to main as v0.8.0 when the maintainer
decides the parser-only Rust output story is sufficient for the
first cut.  The deferred items above are real but each is its own
chunk of work; v0.8.x patch releases can land them as consumers
ask.
