# Reduce dispatch: which reduce paths execute today

A composed Lime snapshot (a base grammar plus runtime extension rules)
runs reduce actions at parse time through three cooperating pieces.
This page states which paths are shipped and gives one worked example.

## The three paths

| Path | Callback ABI | Which rules | Wired by |
|------|--------------|-------------|----------|
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

- `docs/HOST_REDUCE.md` — the base host-reduce contract, value
  representation, and unit-rule handling.
- `docs/EXTENSIONS.md` — the extension `LimeReduceFn` contract.
- `include/multi_grammar.h` — `LimeHostReduceRouter`,
  `LimeExtRuleBinding`, `lime_host_reduce_router`.
- `tests/test_multi_grammar.c` — the end-to-end routing regression.
