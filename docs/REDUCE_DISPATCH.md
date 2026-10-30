# Reduce dispatch: which reduce paths execute today

A composed Lime snapshot (a base grammar plus runtime extension rules)
runs reduce actions at parse time through three cooperating pieces.
This page states which paths are shipped and gives one worked example.

## The three paths

| Path | Callback ABI | Which rules | Wired by |
|------|--------------|-------------|----------|
| **Snapshot-bound extension reduce** | `LimeReduceFn` (via `LimeExtReduceBinding`, `src/snapshot.h`) | each rule a `MOD_ADD_RULE` gave a `reduce` | automatic: `publish_modified_snapshot()` (liblime_compiler) binds by rule identity; runs only when no `host_reduce` is bound (see "Recognition mode with bound extension callbacks" below) |
| **Base host-reduce** | `LimeHostReduceFn` (`src/snapshot.h`) | ruleno `< base_nrule` | `lime -n --host-reduce` emits `<Name>HostReduce`; bound as `snap->host_reduce` |
| **Extension reduce** | `LimeReduceFn` (`src/extension.h`) | ruleno `>= base_nrule` | embedder binds one per rule in `LimeExtRuleBinding.reduce` |
| **Router** | `LimeHostReduceFn` (it *is* one) | all rules | `lime_host_reduce_router` (`include/multi_grammar.h`) |

`LimeHostReduceFn` and `LimeReduceFn` are **separate ABIs** with
different signatures (the host-reduce fn carries `ruleno` and
`lhs_loc_out`; the extension fn carries an `extra_arg`). They are not
one ABI. The router is the bridge: you install it once with
`parse_set_host_reduce`, and it dispatches each reduce to the right
callback by rule number so no embedder hand-writes the base-vs-extension
switch.

The router maps a `LimeReduceFn`'s `void` return to host-reduce success
(`0`); a `NULL` extension binding passes `$1` through to `$$` unchanged;
an out-of-range ruleno or a `NULL` router returns non-zero (abort).

## Recognition mode with bound extension callbacks

When a composed snapshot carries extension callbacks and the session has
no `host_reduce`, `publish_modified_snapshot()` has precomputed one byte
per rule, `snap->ext_action[ruleno]`:

| value | rules | at reduce |
|---|---|---|
| 1 | the rule has a bound `LimeReduceFn` | gather the RHS slots, call it; `$$` is what it writes, `@$` is `@1` |
| 2 | every other rule with nrhs > 0 | `$$ = $1`, `@$ = @1` (the passthrough) |
| 0 | epsilon rules | `$$ = NULL` |

So an extension's value climbs through base unit rules (`program ::=
expr`, `expr ::= term`, ...) to the next callback that consumes it, or to
`parse_result()`.  The leftmost token's value and location climb the same
way, so a callback on `a_expr ::= a_expr JSON_ARROW a_expr` sees its left
operand's token value and position in `$1`.  These are the semantics the
engine had before the table existed (`tests/test_extension_value_
passthrough.c`, `tests/test_extension_reduce_e2e.c`); the table only made
them cheaper.  A snapshot with no bindings (`ext_action == NULL`) does
none of this: recognition only.  With a `host_reduce` bound, the host owns
every value and the table is not consulted.

The passthrough costs no memory traffic.  After the pop, `$1`'s stack slot
is the slot the goto pushes `$$` into, so passthrough leaves that slot's
value and location in place.  Measured on the PostgreSQL regression
corpus (COMPOSITION_STRATEGY.md, "Zero runtime cost, measured"),
recognition mode with 31 bound callbacks runs in the same time as the same
tables without callbacks (0.996-1.003x).  It used to cost 3.9-4.5%.

## Worked example

A three-rule composed grammar: `list ::= attr` (unit), `list ::= list
COMMA attr`, and the extension rule `attr ::= ATTR`. Base rules
`[0, 3)` route to the generated `<Name>HostReduce`; rule `3` routes to
an extension `LimeReduceFn`.

```c
ParserSnapshot *s = NULL; char *e = NULL; int nc = 0;
lime_compile_grammar_in_process_ex(grammar_text, len, &s, &e, &nc);

LimeExtRuleBinding binds[1] = {
    { .reduce = ext_attr_reduce, .user_data = NULL },  /* rule 3 */
};
LimeHostReduceRouter router = {
    .base        = base_host_reduce,   /* generated <Name>HostReduce */
    .base_user   = NULL,
    .base_nrule  = 3,                  /* rulenos [0,3) route to base */
    .ext_rules   = binds,
    .n_ext_rules = 1,                  /* index i == ruleno base_nrule + i */
    .extra_arg   = NULL,               /* e.g. a scanner handle */
};

ParseContext *ctx = parse_begin(s);
parse_set_host_reduce(ctx, lime_host_reduce_router, &router);

parse_token(ctx, TOK_ATTR, (void *)(intptr_t)5, 0);
parse_token(ctx, 0, NULL, -1);          /* EOF */
intptr_t result = (intptr_t)parse_result(ctx);
parse_end(ctx);
snapshot_release(s);
```

If the extension rule tags its value `+7` and the base `list` rule adds
`+1000`, `result == 1012` proves both the extension `LimeReduceFn` and
the base host-reduce fired in one parse. This is the exact assertion in
`tests/test_multi_grammar.c` (the router case).

## See also

- `docs/PERFORMANCE_COMPOSED.md` — why a composed snapshot is slower than
  the AOT parser, where the time goes, and why no existing knob (nor the
  since-removed LLVM JIT) closes the gap.
- `docs/HOST_REDUCE.md` — the base host-reduce contract, value
  representation, and unit-rule handling.
- `docs/EXTENSIONS.md` — the extension `LimeReduceFn` contract.
- `include/multi_grammar.h` — `LimeHostReduceRouter`,
  `LimeExtRuleBinding`, `lime_host_reduce_router`.
- `tests/test_multi_grammar.c` — the end-to-end routing regression.
