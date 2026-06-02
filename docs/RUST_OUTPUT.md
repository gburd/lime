# Rust Output Target — feat/rust-output

Lime can emit Rust as well as C.  Non-replacing addition: `lime
grammar.y` continues to produce `grammar.c` + `grammar.h`; `lime
--target=rust grammar.y` *additionally* produces `grammar.rs`.
Use both flags together for dual output.

**Status:** five substantive commits on `feat/rust-output`.  The
parser side is feature-complete for arithmetic-style grammars
plus extensions covering user-arg threading, custom value types,
parse hooks, no_std, Cargo crate emission, and per-rule Rust body
overrides.  The lexer subsystem (`-X --target=rust`) is the one
piece deferred for a future commit.

## Quick start

```bash
$ lime --target=rust grammar.lime              # emits grammar.rs
$ rustc --crate-type lib --edition=2021 grammar.rs

$ lime --target=rust --enable=crate grammar.lime  # emits grammar_crate/
$ cd grammar_crate && cargo build                 # crate-style consumption
```

For a worked example, see `examples/rust_calc/`.

## CLI flags

v0.8.6 introduced a unified `--target` / `--enable` / `--disable`
flag scheme.  The old `--rust*` and `--per-token-dfa` flags continue
to work as deprecation aliases; see the *Deprecated flags* section
below.

| Flag | Effect |
|---|---|
| `-t c` / `--target=c` | Emit C output (default). |
| `-t rust` / `--target=rust` | Emit `grammar.rs` alongside C output. |
| `-t rust,unsafe` / `--target=rust,unsafe` | Emit Rust with `unsafe { ... }` wrappers + `get_unchecked` indexing in scalar DFA dispatch loops (perf-over-safety opt-out; see *Safe-mode emit* below). |
| `-e <list>` / `--enable=<list>` | Enable a comma-separated set of features. |
| `--disable=<list>` | Disable a comma-separated set of features. |

Feature names recognised by `--enable=` / `--disable=`:

| Feature | Default | Effect |
|---|---|---|
| `simd` | ON | SIMD-accelerated fast-path scans (Rust side; C side once `g_lime_lex_vectorize_flag` consumer lands). |
| `memchr` | OFF | memchr crate dispatch (Rust side, fast byte search). |
| `per-token-dfa` | OFF | Per-rule DFA dispatch (lifts both C and Rust output; default OFF until benched). |
| `vectorize` | ON | C-side SIMD/intrinsic emit (opt-out via `--disable=vectorize`). |
| `crate` | OFF | Emit Cargo crate skeleton (Rust target only). |
| `nostd` | OFF | Emit `#![no_std]` (Rust target only). |
| `safe` | **ON** (Rust target) | Drop `unsafe { ... }` wrappers around scalar DFA dispatch loops; replace `*x.get_unchecked(i)` with `x[i]`.  Categories 2 (SIMD intrinsics) and 3 (`#[target_feature]` callsites) are unaffected.  Opt OUT for ~<2% perf via `--target=rust,unsafe` or `--disable=safe`.  See *Safe-mode emit* below. |

`--enable=feat` and `--disable=feat` later on the command line
overrides earlier ones.  An unknown feature name is a hard error.
`--enable=<rust-only-feature>` without `--target=rust` prints a
warning and the flag has no effect.

Short-form flags accept both glued (`-trust`, `-esimd,memchr`) and
separate-arg (`-t rust`, `-e simd,memchr`) syntax.  No short form
for `--disable` exists because `-d` is already taken by the
existing `-d <output-dir>` flag.

## Deprecated flags

The old `--rust*` and `--per-token-dfa` flags are still recognised
for backward compatibility but each prints a one-line stderr
warning suggesting the canonical replacement.  Existing user
scripts continue to work; the warnings are harmless on stdout-
focused pipelines.

| Old flag | New canonical form |
|---|---|
| `--rust` | `--target=rust` |
| `--rustlex` | `-X --target=rust` |
| `--rust-crate` | `--target=rust --enable=crate` |
| `--rustcrate` (older spelling) | `--target=rust --enable=crate` |
| `--rust-nostd` | `--target=rust --enable=nostd` |
| `--rustnostd` (older spelling) | `--target=rust --enable=nostd` |
| `--rustlex-simd` | `--target=rust --enable=simd` (default since v0.8.6) |
| `--rustlex-memchr` | `--target=rust --enable=memchr` |
| `--per-token-dfa` | `--enable=per-token-dfa` |

Flag ordering matters in the lime CLI: `handleflags()` does
prefix-match.  Long deprecation aliases are listed long-to-short
in `s_options[]` so `--rustlex-simd` is checked before `--rustlex`,
and `--rust-crate` is checked before `--rust`.  We maintain that
invariant.

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

## Safe-mode emit (`--enable=safe`, default ON for `--target=rust`)

Since v0.9.3, `--target=rust` emits **safe Rust by default** for
the lexer's scalar DFA dispatch loops.  The `unsafe { ... }`
wrappers and `get_unchecked` indexing that the v0.9.2 emit used
for bounds-check elision are gone; the equivalent code is now:

```rust
// Old (v0.9.2, --target=rust default):
unsafe {
    while p < bytes.len() {
        let b = *bytes.get_unchecked(p);
        let next = dfa_step_for_state(self.state, state, b);
        if next < 0 { break; }
        state = next as u16;
        let accv = *accept.get_unchecked(state as usize);
        ...
    }
}

// New (v0.9.3, --target=rust default = --enable=safe):
while p < bytes.len() {
    let b = bytes[p];
    let next = dfa_step_for_state(self.state, state, b);
    if next < 0 { break; }
    state = next as u16;
    let accv = accept[state as usize];
    ...
}
```

### Why this is essentially free

- `bytes[p]` where `p < bytes.len()` is the loop guard: LLVM's
  IndVarSimplify + GVN passes hoist the redundant bounds check
  reliably from rustc 1.50+.  Identical machine code to the
  `get_unchecked` form on Godbolt for `-O2 -release`.
- `accept[state as usize]`: LLVM cannot prove `state < accept.len()`
  without help, so a single `cmp; jae .panic` survives.  Modern
  branch predictors learn this immediately (panic edge is cold);
  expected total perf cost <2% per the unsafe-audit (.agent/notes).
- `trans[off]` (per-token-dfa table read): same shape; same cost.

### What is NOT changed

Categories 2 and 3 unsafe (per the unsafe-audit taxonomy) are
untouched by this flag because they are **forced by Rust language
rules**, not by Lime's choice:

- **Category 2 — SIMD intrinsics.**  `_mm256_*`, `vld1q_u8`, etc.
  in `core::arch::x86_64` / `core::arch::aarch64` are declared
  `unsafe fn` because they have alignment / target-feature
  preconditions enforced by the CPU.  The `mod scan_avx2` and
  `mod scan_neon` helpers therefore stay `pub unsafe fn`.
- **Category 3 — `#[target_feature]` callsites.**  Functions
  marked `#[target_feature(enable = "avx2,bmi2")]` must be `unsafe
  fn` until the `target_feature_11` RFC stabilises.  The dispatch
  sites `unsafe { self.tokenize_avx2(bytes) }` and the
  `Scanner::scan_*` impls that wrap `scan_avx2::scan_until_*`
  therefore stay `unsafe { ... }`.

A typical `--target=rust` lexer emit thus shows ~18 `unsafe { ... }`
or `unsafe fn ...` occurrences, all in Cat 2/3 (SIMD plumbing).
The `--target=rust,unsafe` opt-out adds ~3 more (Cat 1) for the
scalar DFA loops; the audit's per-variant expectation of 17-26
unsafe blocks corresponds to v0.9.2 behaviour.

### Opting out

If the perf cost is unacceptable for your workload, opt out
explicitly:

```bash
lime --target=rust,unsafe -X grammar.lex     # preferred terse form
lime --target=rust --disable=safe -X grammar.lex   # equivalent alias
```

Both produce the v0.9.2 unsafe-bearing Rust.  The C target is
unaffected -- `safe` is a Rust-only feature.

### Expected perf delta

Measurement on the JSON fixture (~226 KB, x86_64-linux, no CPU
isolation), `--enable=simd` lexer, 100-iter median of 5 runs each:

```
safe   (default):  0.799 - 0.864 ms / iter   (median ~0.814)
unsafe (opt-out):  0.832 - 1.034 ms / iter   (median ~0.881)
```

Safe mode is **within noise of, and sometimes faster than**, the
unsafe-bearing emit -- the bounds checks LLVM keeps cost <= 0
because they let the optimiser prove non-aliasing for the
subsequent `accept[state]` table read.  The audit's <2% projection
was conservative.

Static analysis (.agent/notes/unsafe-audit.md section 2.2) projects:

| Variant | Cat-1 sites removed | Expected delta vs `--target=rust,unsafe` |
|---|---:|---|
| `--target=rust` | 3 | 0-1% (LLVM elides byte-index check; only accept-table check survives) |
| `--enable=memchr` | 2 | 0-1% (memchr crate replaces scalar self-loop) |
| `--enable=simd` | 5 | -1 to +2% (measured: noise) |
| `--enable=per-token-dfa` | 8 | 1-3% (more table-indexed sites in per-rule DFAs) |

If you measure >5% on your workload, file an issue and we'll revisit
defaults.


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

## Per-rule reduce inlining

The Rust output classifies every rule's action body and emits
`#[inline(always)]` on the corresponding `fn yy_rule_N(...)`
reducer when the body is small and pure enough that rustc can
safely inline it into the parse loop's match arm.

The classifier (`jit_can_inline_rule_text` in `src/jit_inline.c`)
is the same one Lime's runtime LLVM JIT uses to decide whether to
inline an action into a trace.  For the Rust output -- which has
no runtime JIT, only ahead-of-time `rustc` -- the equivalent gain
is the inline hint, which lets LLVM collapse the indirect dispatch
through `YY_RULE_REDUCE_FN[ruleno]` into the action body itself for
the common case of trivial reducers.

### What gets inlined

The classifier returns true (and Lime emits `#[inline(always)]`) for:

- **Empty action bodies** (`s ::= a b c.` with no `{...}`).  The
  generator emits the default `lhs = rhs0` assignment.
- **Single-symbol passthrough** (`A = B;`).  Single short identifier
  on each side, optional whitespace.
- **Single small expressions** with no calls and no control flow
  (`A = B + C;`, `A = B << 4 | C;`, `A = -B;`).

It returns false (no annotation, rustc decides) for:

- **Function calls** -- any `identifier(...)` pattern, including
  the lemon-generated `Parse_*` callbacks.
- **Control flow** -- the keywords `if`, `while`, `for`, `switch`,
  `goto`, `setjmp`, `longjmp`, `return`.
- **Allocations** -- `malloc`, `free`, `realloc`, `calloc`.
- **Multi-statement bodies** -- any non-trailing `;`.
- **Block statements** -- any `{` or `}`.
- **Bodies longer than 200 chars** -- caps the inlined code size.
- **`%rust_action` overrides** -- the user's body is emitted
  verbatim, the classifier doesn't see it, so we default to no
  annotation and let rustc decide.

The classifier targets C action bodies (Lime's primary output) but
the vocabulary it inspects -- function-call parens, block braces,
the keyword set -- overlaps cleanly with Rust, so the same heuristic
works for both targets.

### Impact in real grammars

Measured on Lime's own example grammars (`lime --rust grammar.lime`
then `grep -B1 '^fn yy_rule_'` on the output):

| Grammar                                | Rules | Inlined | %   |
|----------------------------------------|------:|--------:|----:|
| `examples/rust_calc/calc.lime`         |     5 |       5 | 100 |
| `examples/calc/calc.lime`              |     8 |       7 |  88 |
| `examples/cobol/cobol_grammar.lime`    |   216 |     191 |  88 |
| `examples/mongodb/mongodb.lime`        |    98 |      62 |  63 |
| `examples/jsonpath/jsonpath_gram.lime` |   135 |      73 |  54 |
| `examples/xpath/xpath.lime`            |    79 |      37 |  47 |
| `examples/datalog/datalog.lime`        |    65 |      28 |  43 |
| `examples/xquery/xquery.lime`          |   160 |      62 |  39 |
| `examples/replication/repl_gram.lime`  |    81 |      29 |  36 |
| `examples/json/json_grammar.lime`      |    17 |       5 |  29 |
| `examples/syncrep/syncrep_gram.lime`   |     9 |       2 |  22 |
| `examples/isolation/isolation_gram.lime`|    28 |       6 |  21 |
| `examples/pgbench/pgbench_expr.lime`   |    46 |       4 |   9 |

The distribution tracks grammar style.  Tiny calculator grammars
(rust_calc, calc, cobol with its many passthrough productions) come
out 88-100 % inlinable -- nearly every rule is trivial.  AST-heavy
grammars (json, syncrep, isolation, pgbench) sit at 9-29 % --
most rules call into a node-builder allocator, which the classifier
correctly rejects.  The conservative middle band (cobol, mongodb,
jsonpath, xpath, datalog, xquery, replication) lands 36-63 %.

A grammar where 100 % of rules came out inlinable would be
suspicious -- the classifier would have to be ignoring some
blacklist signal -- but in practice the worst we see is 100 % on a
five-rule grammar where every rule really is a one-line
passthrough.  Pattern-matching against mid-sized grammars
(cobol-216 at 88 %, jsonpath-135 at 54 %, xpath-79 at 47 %) shows
the classifier discriminates rather than rubber-stamps.

### Performance

The expected gain is rustc inlining the per-rule reducer into the
parse loop's reduce step, eliminating the indirect call through
`YY_RULE_REDUCE_FN[ruleno]`.  Whether that translates to measurable
throughput depends on:

- The fraction of inlinable rules in the grammar.
- How hot the reduce path is relative to shift / table lookup
  (token-bound workloads see less benefit; reduce-bound ones see
  more).
- Whether rustc/LLVM was already devirtualising the dispatch with
  link-time optimisation (LTO collapses many indirect calls
  whether or not the hint is present).

The `#[inline(always)]` hint is a request, not a guarantee --
rustc still applies its own size and recursion checks before
actually inlining.  For trivial passthrough/arithmetic bodies it
always inlines; for borderline-size bodies the hint tips the
decision.

Running the JSON parse benchmark in `bench/rust_compare/` (3 MB
fixture, ~70k tokens, parser-only path) on this branch with and
without the `#[inline(always)]` markers shows results within run-
to-run noise (parse throughput swings ~7 % across consecutive runs
on a busy laptop in either configuration; the inlined-vs-baseline
delta sits inside that band).  This is consistent with the
benchmark grammar: `bench/rust_compare/json.lime` is all empty-body
productions (`value ::= STRING.` etc.), which rustc / LLVM already
inline aggressively without the hint -- the function bodies are
literally `*ctx.lhs = rhs0.clone()` plus a stack write.  Where the
hint pays for itself is grammars with non-trivial inlinable bodies
(short arithmetic, branchy conditionals that the classifier still
accepts) on a tight reduce-bound loop; that needs a real
benchmark grammar that exercises the reduce path more heavily than
the shift path.  v0.9 has a richer benchmark planned -- numbers
there will tell the story honestly.

The change is no-cost when the classifier returns false (no
annotation), so there's no downside to leaving it on by default.

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
$ meson test -C build    # 117 / 0 ok stock + ASan/UBSan
$ ninja -C build lime
$ ./build/lime --help | grep -E '(target|enable|disable|rust)'

$ # End-to-end smoke
$ ./build/lime --target=rust /tmp/grammar.lime && rustc --crate-type lib /tmp/grammar.rs
$ ./build/lime --target=rust --enable=crate /tmp/g.lime
$ cd /tmp/g_crate && cargo build
$ ./build/lime --target=rust --enable=crate,nostd /tmp/g.lime
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
