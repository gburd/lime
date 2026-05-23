# Integrating Lime into a project that has its own scanner and actions

This document answers the integration question raised in the
PG team's Letter 14: how do you adopt Lime in a project that
already has

  - a hand-written / Flex-generated scanner that returns tokens
    with a YYSTYPE-like value and YYLTYPE-like location;
  - reduce actions written into the grammar's `{ ... }` blocks
    that read those values directly out of file-static or
    function-local variables (PG's `base_yylval` / `base_yylloc`
    pattern); and
  - an existing `int yyparse(scanner *)` entry point that the
    rest of the codebase calls.

Two integration shapes work today.  Both produce a working
parser; the trade-offs are described below.

## Shape A: keep the static parser

The path of least resistance.  Run `lime grammar.lime` exactly
the way you'd run `bison grammar.y`: it emits `grammar.c` plus
`grammar.h` and you compile and link them with the rest of your
backend like any other generated source.

The generated entry points match the Lemon convention -- if your
grammar declared `%name_prefix Foo` you get:

```c
void *FooAlloc(void *(*malloc_proc)(size_t));
void  FooFree (void  *p, void (*free_proc)(void*));
void  Foo     (void  *p, int yymajor, void *yyminor /* maybe more */);
```

One token per `Foo()` call.  The scanner / driver loop is

```c
void *parser = FooAlloc(palloc);
int t;
YYSTYPE v;
YYLTYPE loc;
while ((t = base_yylex(&v, &loc, scanner)) > 0) {
    Foo(parser, t, v /*, &loc */);
}
Foo(parser, 0, NULL);   /* EOF */
FooFree(parser, pfree);
```

Reduce actions in `grammar.lime` see your YYSTYPE union member
the same way Bison would.  No ParserSnapshot, no parse_token,
no runtime engine.  PG's existing `base_yyparse` model is a
straight-line port.

What you get on this path:

  - All the parser-generation improvements (the `lime` tool
    itself: substantially faster than Bison, modern error
    diagnostics, single-file generator, the lexer compiler,
    the formatter / linter flags).
  - The runtime extension framework -- but only as a future
    upgrade.  Without ParserSnapshot you can't load extensions
    at runtime (the static `yy_action` table is fixed at
    compile time).
  - JIT acceleration -- not on this path.  See Shape B.

## Shape B: ParserSnapshot + parse_token (runtime extensible, JITable)

This is the path PG's roadmap lands on for the dialect /
extension story.  It uses

```c
ParserSnapshot *snap = lime_snapshot_create("grammar.lime", &err);
ParseContext   *ctx  = parse_begin(snap);
while ((t = base_yylex(&v, &loc, scanner)) > 0) {
    parse_token(ctx, t, &v, loc.first_column);
}
parse_token(ctx, 0, NULL, -1);  /* EOF */
parse_end(ctx);
lime_snapshot_release(snap);
```

`lime_snapshot_create("grammar.lime", &err)` does the equivalent
of `lime grammar.lime + cc -shared` internally and returns a
ParserSnapshot the runtime engine can drive.  Subsequent
`parse_token` calls walk the same LALR(1) action tables a static
`Parse()` would, but they go through the runtime engine in
`src/parse_engine.c` rather than the generated dispatch.

**Reduce-action callbacks are not yet wired in this path.**
The runtime engine performs the LR reductions on its stack
(correctly tracking `nrhs` pop + LHS push) but does not invoke
extension-supplied reduce callbacks.  In other words, on this
path the runtime engine accepts/rejects but does not run your
`{ ... }` action bodies.  Roadmap item.  See
`docs/EXTENSIONS.md` for the current status.

What you get on this path:

  - **Runtime grammar extension.**  Load and unload `.so`
    extensions that add new tokens / rules / precedence at
    runtime via the ExtensionRegistry.  See
    `docs/EXTENSIONS.md`.
  - **JIT-armed dispatch.**  After `lime_jit_compile(snap)`,
    `parse_token` resolves shift actions through the JIT'd
    `find_shift_action` instead of the table walk.  See
    `docs/JIT_ANALYSIS.md` for the cost-benefit story.
  - **Snapshot acquire / release** is reference-counted.  Two
    threads can hold references to the same snapshot
    concurrently and call `parse_token` against it; copy-on-
    write means modifications produce a new snapshot rather
    than mutating the shared one.

## Hybrid path (PG's likely deployment shape)

For a backend like PG that wants per-process throughput on the
common case but the option to load dialect extensions at runtime,
the hybrid shape is:

  1. Build the production parser with Shape A (static `gram.c`
     emitted by `lime`, action callbacks compiled in,
     `base_yyparse` invoked from `raw_parser`).  This is the
     hot path; no runtime engine overhead.
  2. Provide a parallel ParserSnapshot built via
     `lime_snapshot_create` that **shares the same grammar
     source**.  When an extension is loaded, the dispatch
     routes through the snapshot path; when no extensions are
     loaded, the static path is used.

The headline performance number on the static path is the
arithmetic / JSON Bison-vs-Lime comparison in
`docs/BENCHMARKS_VS_BISON.md`: 1.04-1.81× faster than Bison
across architectures.  The headline on the snapshot path is
the JIT speedup in `docs/BENCHMARK_RESULTS.md` (typically
2-3× over the C interpreter on grammars > 100 states, on
aarch64; on x86 the JIT is auto-skipped for small grammars
because the unrolled-switch interpreter is already optimal).

## Concrete answers to Letter 14

### Q1.1 -- Calling static reduce actions from `parse_token`

Today's runtime engine does not invoke reduce-action callbacks
at parse time.  The action tables in the snapshot encode the
LR machine; running the user's `{ ... }` body is reserved for
a future engine extension.  PG's existing static-parser path
(Shape A above) is the right home for action callbacks today.

When the runtime engine grows reduce-callback dispatch (tracked
in `docs/ROADMAP.md`), the integration shape will be: a single
opaque user-data pointer threaded through `parse_begin`, with
the engine calling a per-rule callback at each reduction.  The
PG team can prototype against the existing snapshot machinery
today and the callback wiring will light up transparently.

### Q1.2 -- `parse_token`'s `int location` parameter

`parse_token(ctx, token, value, location)`:

  - `location` is treated as the byte offset (or column) of
    the token's first character.  Pass `-1` if you don't have
    one available.
  - It maps to the `yyloc` slot in the engine's stack entry
    for that token.  When `%locations` is set in the grammar,
    user actions can read it via the same `@N` syntax Bison
    uses.
  - For PG's `YYLTYPE { int first_column; int last_column; }`
    convention, pass `loc.first_column`.  The `last_column`
    half is currently unused by Lime (PG's own usage of it
    has trailed off since 11 anyway).

### Q1.3 -- ParseContext lifecycle vs Bison's per-parse

Yes, `parse_begin` / `parse_end` are cheap -- they are pool
allocations from the snapshot's arena, not full process-level
state.  A single backend that creates one MemoryContext per
parse can call `parse_begin` inside that context and
`parse_end` at MemoryContext reset.  We expect to land a
`parse_reset(ctx)` helper in v0.3.0 that recycles the ParseEngine
without releasing the snapshot reference; until then,
`parse_end` followed by a new `parse_begin` is the right shape.

### Q2.1 -- JIT and the static action-table dispatch

The JIT operates on a ParserSnapshot.  It does not directly
accelerate the static `yy_find_shift_action` in your generated
`gram.c`.  If you want JIT acceleration, you need the snapshot
path (Shape B or hybrid).

### Q2.2 -- AOT (-j flag) as the static-codegen equivalent

Yes.  `lime -j grammar.lime` emits a `grammar_aot.c` containing

```c
YYACTIONTYPE_AOT yy_find_shift_action_aot(stateno, lookahead);
```

implemented as a switch-of-switches that the C compiler
optimises into a jump table at -O2.  That's the static-time
equivalent of the JIT: no runtime LLVM, no per-grammar compile
delay, but the same constant-folded dispatch shape the JIT
gives you on aarch64 grammars.

To use: compile the AOT file alongside your `gram.c`, link
both, define `-DYYAOT` so the generator's `yy_find_shift_action`
forwards to the `_aot` version.

The bug from Letter 14 (`free(): invalid size`) is fixed in
v0.2.5 (commit `b566478`) -- `lime -j` now produces a valid
`*_aot.c` on every grammar tested.

## Reference points

  - `examples/calc/main.c`             -- Shape A driver
  - `examples/json/main.c`             -- Shape B driver with
                                          full AST
  - `tests/test_extension_e2e.c`       -- Shape B with runtime
                                          extension loading
  - `tests/test_jit_parse_equivalence.c` -- Shape B with JIT
  - `bench/bench_jit_real_parser.c`    -- Shape B perf
                                          measurement

Each is a self-contained, < 200-line C file.  All examples
build via the top-level `make` and each subdirectory's
own `Makefile`; the meson build system also builds them all
under `meson setup builddir && ninja -C builddir`.
