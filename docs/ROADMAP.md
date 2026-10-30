# Lime Roadmap

This file tracks features and limitations that the codebase currently
references as **under construction**.  Each entry describes what works
today, what is missing, what would be required to finish the feature,
and where the placeholder lives in the source tree.

Items are listed in rough order of impact.

---

## Releases shipped

- **v1.0.0** (June 2026) -- API stability commitment for v1.x line.
  Background diagnostics, did-you-mean linter suggestions, GDB +
  LLDB pretty-printers.
- **v1.1.0** (June 2026) -- Rust-target syntax-error introspection
  (`YY_TOKEN_NAMES`, `token_name`, `expected_tokens_in_state`).
- **v1.2.0** (June 2026) -- LSP fast-lint mode, formatter
  idempotence, LLDB pretty-printer test, Win32 thread shim.
- **v1.3.0 (LTS)** (June 2026) -- lalrpop-API Rust skin, formal
  24-month support commitment, customer-reported `%rust_action`
  alias-binding fix.
- **v1.3.1 (LTS patch)** (June 2026) -- alt-group `%rust_action`
  propagation fix (state-machine bug; see CHANGELOG.md), Rust-
  target API-stability documentation, CHANGELOG backfill, README
  LTS callout.

The LTS line is **v1.3.x** through June 2028.  See
[`docs/SUPPORT.md`](SUPPORT.md) for the backport policy.

---

## 1. In-process LALR(1) automaton rebuild library

**Status as of v0.10.0: COMPLETE.**

**What works today.**  The full pipeline is end-to-end:

- `lime -n` emits a `<Prefix>BuildSnapshot()` builder AND embeds the
  original grammar source as a `static const unsigned char[]` so the
  runtime can rebuild against a merged grammar.
- `lime_snapshot_create("foo.y", &err)` reads a grammar file and
  builds a populated snapshot in-process.  See `src/snapshot_create.c`
  and `tests/test_snapshot_create.c` (runs with PATH emptied).
- `lime_compile_grammar_text(text, len, &err)` does the same for a
  grammar held in memory.
- `lime_compile_grammar_in_process(text, len, &snap, &err)` is the
  in-process LALR construction both of them call, in
  `liblime_compiler.a`.  The old subprocess pipeline (fork + `lime` +
  `cc` + dlopen) is deleted: at runtime Lime cannot invoke a compiler
  or load a shared object.  Available since v0.5.4
  (`include/lime_compiler.h`).  Linked via `pkg-config --libs
  lime-compiler` (pkg-config file shipped since v0.10.0).
  Documented in [docs/API.md](API.md#in-process-compiler-api).
- `lime_lint_grammar_in_process(text, len, &diags)` runs the same
  parse + `FindActions` + `lint_grammar` pipeline that `lime -L`
  runs, in-process, with no fork / exec / temp file.  Available
  since v0.10.0; used by `lime-lsp`'s diagnostic refresh path.
  ~10% / 200 ms saved per LSP request on PG's 21k-line `gram.lime`.
- `publish_modified_snapshot(reg, base, ...)` (liblime_compiler)
  rebuilds from the base snapshot's `grammar_source`.  It calls
  `lime_modifications_to_grammar_text()` to serialise each loaded
  extension, concatenates them with the base text, and rebuilds
  in-process; any extension-introduced LALR conflict the host policy
  does not resolve fails the compose (`docs/EXTENSIONS.md`).  The resulting
  snapshot has fully recomputed action tables and the new tokens /
  rules are reachable through `parse_token()`.  Verified by
  `tests/test_extension_rebuild.c` (11/11 PASS): a postfix-factorial
  extension adds a token + rule, the merged grammar is rebuilt, and
  parses through the new rule succeed.
Requirements: link `liblime_compiler` (`pkg-config lime-compiler`).
No `lime` binary, C compiler or `LIME_*` environment is consulted.

**Why this was the prerequisite.**  This is the prerequisite for
composition (`compose_snapshots`,
[docs/COMPOSITION.md](COMPOSITION.md)) at production speed.
Without it, composition had to either run lime+cc in a subprocess to re-derive
LALR (~200ms per merge) or settle for snapshot-table merging only
(which doesn't recompute conflict resolutions across composed
grammars).  The in-process path collapses both paths to sub-
millisecond work, which is the threshold for PG-style daemon-startup
extension loading where `shared_preload_libraries` can ship
arbitrary mixes of grammar plugins (DuckDB-compat, EDB Oracle-compat,
pg_infer, pg_mentat, ... composed at backend startup).

Composition is the primary consumer of this work.

**Open follow-ups.**  None blocking.  The previously listed
"thread-local active-context storage" item is done: the active
`LimeCompilerContext` pointer is declared
`static LIME_THREAD_LOCAL LimeCompilerContext *lime_active_ctx`
in `lime.c` (`_Thread_local` under C11), so concurrent
`lime_compile_grammar_in_process` calls on different threads are
isolated.

**Source-tree references.**

- `src/snapshot_create.c::create_base_snapshot,
  lime_compile_grammar_text` -- file/text front ends.
- `lime.c::lime_compile_grammar_in_process[_ex2]` (in-process
  implementation, gated on `LIME_HAVE_SNAPSHOT_BUILD`).
- `lime.c::lime_lint_grammar_in_process` (v0.10.0; LSP-grade
  in-process linter).
- `src/compose.c::publish_modified_snapshot` -- rebuilds from the
  base's `grammar_source`; fails (no fallback) when it cannot.
- `lime.c::ReportSnapshotInit` -- emits `s_grammar_source[]` and
  the `grammar_source` field on `LimeParserTables`.

---

## 2. Bayesian disambiguation strategy (LLM: not planned)

**What works today.**  Priority-based, fork-resolve, and Bayesian
(Beta-Bernoulli) disambiguation strategies are fully implemented
and exercised by tests.  See `src/strategy_bayesian.c` and
`tests/test_strategy_bayesian.c` (4 tests / 20 assertions covering
cold-start, biased feedback, independence across conflict points,
and posterior-mean closed-form match).

**What is missing.**  Bayesian uses greedy posterior-mean selection;
Thompson sampling would be a natural exploration upgrade.

**Not planned: `STRAT_LLM`.**  Deliberately unimplemented.  A parser
must produce the same tree for the same input every time, so a
non-deterministic disambiguator is a disqualifying design choice, not
missing work.  `disambiguation_create(STRAT_LLM, ...)` now returns
NULL instead of binding a silent stub.  `examples/llm_oracle/` remains
available as a research demo through the `STRAT_CUSTOM`
user-supplied-vtable path.

**What it would take.**  Thompson sampling for Bayesian is ~50 lines
of additional code in `strategy_bayesian.c`.

**Source-tree references.**

- `src/strategy_bayesian.c` -- working Beta-Bernoulli
  implementation.
- `src/disambiguation.c::get_builtin_vtable` -- the STRAT_LLM case
  returning NULL.

---

## 3. `examples/calc/` running on the runtime extension API

**What works today.**  `examples/calc/main.c` demonstrates loading a
`.so` plugin and calling its parser via a function pointer.

**What is missing.**  The example uses a homegrown plugin scheme
rather than the `register_extension` / `load_extension` /
`create_modified_snapshot` flow.  Until item 1 lands the example
cannot demonstrate runtime grammar mutation through the framework.

**What it would take.**  Once item 1 is finished, rewrite the calc
example as a registered extension that adds the `^` operator at
runtime and have the host re-parse the same input through the
modified snapshot.

---

## 4. Flex / Bison comparison benchmark

**Status: COMPLETE.**

**What works today.**  `bench/bench_flex_bison_compare/` ships a
runnable harness that compares Lime against Flex+Bison on an
identical arithmetic grammar.  When `flex` and `bison` are not on
`$PATH`, the benchmark prints a diagnostic and exits cleanly so
unrelated builds are not blocked.  Numbers reproduced in
[BENCHMARKS_VS_BISON.md](BENCHMARKS_VS_BISON.md): Lime 1.27x faster
than Bison + default flex, 0.83x vs Bison + `flex -CF` (the earlier
1.81x was against flex's slowest table mode only), on the lex+parse
end-to-end JSON benchmark.

Retained in this list as a reference entry; nothing remains to do.

---

## Removed

- *JIT-accelerated `parse_token` (LLVM).*  Removed in the v1.11.3
  review.  On the PostgreSQL grammar it was 4-6% slower than the table
  interpreter on x86_64 and 7-9% slower on aarch64, so it never broke
  even; its background compile thread also cannot run inside a
  PostgreSQL backend.  See `docs/JIT_ANALYSIS.md`.
- *Stale "Task #3 dynamic-table support" references.*  The "Task #3"
  identifier referred to an internal tracker that has been folded
  into item 1 above.  All source comments now point at this file.
