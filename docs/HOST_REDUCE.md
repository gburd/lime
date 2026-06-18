# Host-reduce: running base-grammar actions over a runtime snapshot

Lime's runtime push parser (`parse_begin` / `parse_token` / `parse_end`)
drives a `ParserSnapshot`'s LALR automaton to accept or reject input. By
default it is **recognition-only** — it runs no reduce actions and builds no
tree. The *host-reduce* hook (Lime-Letter-30) lets it additionally run a
statically-generated grammar's **base** reduce actions in-process, with no C
compiler, so a composed snapshot (base grammar + runtime extensions) can build
a real parse result.

This closes the gap that previously forced the subprocess `cc` pipeline: the
base grammar's reduce actions are compile-time `code` blocks dispatched through
the **static** `yy_rule_reduce_fn[]` inside the generated parser `.c`, which
the grammar-agnostic `liblime_parser` cannot reach. Host-reduce bridges them.

## Enabling it

The hook is **opt-in**. A plain `lime -n` snapshot stays recognition-only
(`host_reduce == NULL`), so existing consumers are unaffected. Pass
`--host-reduce` alongside `-n` to wire it:

```
lime -n --host-reduce -T limpar.c gram.lime
```

This emits, in the parser `.c`, an exported wrapper:

```c
int <Name>HostReduce(void *user, int ruleno,
                     const void *rhs_values, const int *rhs_locs,
                     int nrhs, void *lhs_out, int *lhs_loc_out);
```

and binds it into the snapshot from the generated `<Name>BuildSnapshot`
(`snap->host_reduce = <Name>HostReduce`). The push parser then calls it on
every base reduce.

### Opt-in, not automatic — and why

The wrapper invokes the generated per-rule action functions, which run in a
reduced environment: the parser stack/lookahead are not live during a
host-reduce.  Actions that read `$1..$N` (via `yymsp`), compute `$$`, and use
the grammar's `%extra_argument` are fully supported.  Actions that require a
*live* parser stack or lookahead mid-reduce are not.  Making the hook opt-in
keeps every existing `-n` consumer working unchanged.

### %extra_argument (scanners, allocators, error sinks)

Most non-toy grammars thread something through their actions with bison/Lemon's
`%extra_argument` — a scanner, an arena, an error sink, a symbol table.
PostgreSQL's grammar declares `%extra_argument {core_yyscan_t yyscanner}` and 95
actions read `yyscanner`.

The `user` pointer you pass to `parse_set_host_reduce` (or store in
`snap->host_reduce_user`) is delivered into the generated parser's
extra-argument slot before each action runs, so an action that fetches
`yypParser->yyscanner` reads exactly that pointer:

```c
snap = GramBuildSnapshot();                       /* host_reduce set */
ctx  = parse_begin(snap);
parse_set_host_reduce(ctx, snap->host_reduce, yyscanner);  /* user = scanner */
... parse_token(ctx, code, boxed_value, loc) ...
tree = parse_result(ctx);
parse_end(ctx);
```

The wrapper builds a zeroed stack `yyParser`, stores `user` into the
extra-argument field (`yyp.<argname> = (<argtype>)user;`), and points the
reduce context at it.  The extra-argument value must be **pointer-
representable** (a scanner handle, an arena pointer — all fine).  Grammars
with no `%extra_argument` ignore `user` (the wrapper still passes a valid
zeroed `yyParser`).

## Driving it

```c
ParserSnapshot *snap = GramBuildSnapshot();   /* host_reduce wired */
ParseContext *ctx = parse_begin(snap);
parse_token(ctx, TOK_NUM, boxed_value, loc);  /* value flows to $N */
...
parse_token(ctx, 0, NULL, -1);                /* EOF */
void *tree = parse_result(ctx);               /* the start symbol's $$ */
parse_end(ctx);
```

`parse_token`'s `token_value` now lands on the engine's value stack and is
handed to the base action as the matching `$N` slot; the `$$` value the action
computes propagates back onto the stack, and the start-rule value is returned
by `parse_result()`.

### Session override

```c
void parse_set_host_reduce(ParseContext *ctx, LimeHostReduceFn fn, void *user);
```

binds or replaces the reducer for one session, overriding the snapshot's hook.
This keeps a snapshot immutable and shareable across threads while letting each
session route base reduces differently (e.g. a composed snapshot driven by a
host that dispatches base rules to `<Name>HostReduce` and extension rules to
their `LimeReduceFn`s). Passing `fn == NULL` clears the override and falls back
to the snapshot's hook.

## ABI

The two questions raised in Letter 30:

1. **The push parser does not expose the generated `yyStackEntry` /
   `YYMINORTYPE` layout.** It keeps a generic value stack of `void *` slots.
   The generated `<Name>HostReduce` wrapper is the documented adapter — it owns
   the grammar-specific layout knowledge and bridges the engine's `void *`
   slot array to the static `yy_rule_reduce_fn[]` dispatch. That layout never
   crosses the `liblime_parser` boundary, so there is no cross-module ABI on
   `yyStackEntry` to freeze. The stable contract is `LimeHostReduceFn`
   (`include/parser.h` / `src/snapshot.h`), which mirrors the existing
   extension `LimeReduceFn` so base and extension reduce paths share one shape.

2. **Token values are propagated.** A value passed to
   `parse_token(ctx, code, value, loc)` is stored on the value stack and
   delivered to the base action as `$1..$N` (rule order, `rhs_values[0]` is
   `$1`); the LHS value the action writes is propagated back. The
   "stored but not yet propagated" caveat on `parse_token` is resolved.

### Value representation

Each `rhs_values[i]` **is the symbol's value, by value** -- a pointer-width
payload -- not a pointer to a slot holding the value.  There is no extra
indirection: read it directly as `(Type)rhs_values[i]`, never
`*(Type *)rhs_values[i]`.  For `%type {char *}` the element is the `char*`;
for `%type {Node *}` it is the `Node*`; for `%type {intptr_t}` it is the
scalar.  Write the LHS the same way: `*(Type *)lhs_out = v`.

When the grammar's `%token_type` is a **union** (e.g. PostgreSQL's
`core_YYSTYPE { int ival; char *str; const char *keyword; }`) the element is
the union's pointer-width *content* -- the active member's bits -- delivered
directly.  A terminal carrying `core_YYSTYPE.str` arrives as that `char*`:
read it as `(char *)rhs_values[i]`; do **not** reconstruct a
`core_YYSTYPE *` and dereference it.

Mechanically, the wrapper bridges the payload through the union's `.yy0`
member (the slot the generated parser uses for terminal values), copying a
pointer-width payload in and out.  Any **pointer-representable** semantic
value works -- including a pointer-width union and any `intptr_t`-wide scalar.
Grammars whose value type is wider than a pointer must box it behind a pointer
to use this path.

## Cost

Zero on the recognition-only path: `host_reduce == NULL` skips the entire
block, and the per-entry value/location stores are cheap. Benchmarks
(`parser_bench`, `bench_jit_real_parser`) show no measurable change versus the
recognition-only engine. The hook is consulted only inside a reduce, never on
the shift hot path.

## See also

- `src/snapshot.h` — `LimeHostReduceFn`, `ParserSnapshot.host_reduce`.
- `include/parse_context.h` — `parse_set_host_reduce`, `parse_result`.
- `tests/test_host_reduce.c` — end-to-end value-propagation regression.
- `src/extension.h` — `LimeReduceFn`, the extension-reduce sibling contract.
