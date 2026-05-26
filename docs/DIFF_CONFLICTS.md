# Diffing LALR Conflicts (`lime --diff-conflicts`)

`lime --diff-conflicts base.lime ext.lime` compares the LALR(1)
conflict sets of two grammars and reports which conflicts are new,
which are resolved, and which are unchanged. It is designed for the
**dialect-overlay workflow**: a base grammar (e.g. ANSI SQL, PG core)
plus an extension file (Oracle additions, vendor-specific overrides)
that the maintainer needs to vet before merging.

This is the v0.4.3 mechanism. It complements:

| Mechanism | When | Granularity |
|---|---|---|
| [`%dialect`](DIALECT.md) (v0.4.0) | parser-generation time | conditional rule blocks inside one file |
| [`%extends`](EXTENDS.md) (v0.4.1) | parser-generation time | one file extends another at the source level |
| `--diff-conflicts` (v0.4.3) | tooling | post-merge LALR-conflict diff for review / CI |

The workflow:

```bash
# Author dialect overlay
$EDITOR pg_oracle.lime

# Verify it does not introduce new LALR conflicts over base
lime --diff-conflicts pg_core.lime pg_oracle.lime

# CI-friendly variant -- exit 1 on any NEW conflict
lime --diff-conflicts pg_core.lime pg_oracle.lime || \
    echo "extension introduces conflicts; reject patch" >&2
```

## Why this exists

PG's `gram.lime` carries 1,682 expected LALR conflicts that are
resolved by precedence directives (`%left`, `%right`, `%nonassoc`).
v0.4.2 caught the regression in which Lime was silently dropping
those directives — the patch went out and the conflict count
exploded from 0 to 1,682. A diff tool would have caught the change
instantly: every dropped directive shows up as a NEW conflict.

`--diff-conflicts` is also the dialect-overlay author's pre-flight
check. Adding new operators or new productions can introduce
shift/reduce or reduce/reduce conflicts that are invisible at the
source level. Running the diff before merging tells you exactly which
new pair of rules is ambiguous and which lookahead exposes it.

## Symbolic conflict identity

Conflicts are matched across grammars by a **symbolic key**:

- **Shift/reduce**: `(reduce_lhs, reduce_rhs, lookahead, "SR")`
- **Reduce/reduce**: `(rule_a_shape, rule_b_shape, lookahead, "RR")`,
  with the two rule shapes sorted by `strcmp` so order does not
  matter.
- **Shift/shift**: `(lookahead, "SS")` (rare; included for completeness).

Raw state IDs are **deliberately excluded** from the key. Adding any
rule renumbers states, so a key built from `state.statenum` would
change after every grammar edit. The symbolic key uses only data
that survives source-level edits: rule LHS names, RHS symbol
sequences, and lookahead terminal names.

This means two grammars that produce *the same conflict at
different state IDs* are still recognised as carrying the same
conflict. That is exactly the property the dialect-overlay reviewer
needs.

## Output formats

### Human-readable (default)

```
Adding pg_oracle.lime to pg_core.lime:

== NEW conflicts (1) ==
  + shift/reduce  expr ::= expr STAR expr  | lookahead OUTER_JOIN_PLUS
      shift rule:  expr ::= expr OUTER_JOIN_PLUS expr   (pg_oracle.lime:18)
      reduce rule: expr ::= expr STAR expr             (pg_core.lime:1843)
      Recommendation: declare precedence for 'OUTER_JOIN_PLUS' with
        %left/%right/%nonassoc, or fork-resolve at runtime.

== RESOLVED conflicts (0) ==
  (none)

== UNCHANGED conflicts: 1682 ==
  See `lime -L pg_core.lime` for the full pre-existing conflict set.

Summary: +1 new  -0 resolved  =1682 unchanged   net change: +1
```

The UNCHANGED bucket is summarised as a count rather than dumped
verbatim; a 1682-line block of pre-existing conflicts in a review
output is unhelpful.

### JSON (`--json`)

```json
{
  "schema_version": 1,
  "base":     "pg_core.lime",
  "ext":      "pg_oracle.lime",
  "summary":  { "new": 1, "resolved": 0, "unchanged": 1682, "net_change": 1 },
  "new": [
    {
      "kind":      "shift_reduce",
      "lhs":       "expr",
      "rhs":       ["expr", "STAR", "expr"],
      "lookahead": "OUTER_JOIN_PLUS",
      "base_rule": { "lhs": "expr", "rhs": ["expr", "OUTER_JOIN_PLUS", "expr"], "file": "pg_oracle.lime", "line": 18 },
      "ext_rule":  { "lhs": "expr", "rhs": ["expr", "STAR", "expr"], "file": "pg_core.lime", "line": 1843 }
    }
  ],
  "resolved": [],
  "unchanged_count": 1682
}
```

JSON design choices:

- `unchanged` is a **count**, not an array. Dumping 1682 entries per
  CI run would dominate log volume and add no review signal.
- `base_rule` is the SHIFT-side rule for SR, and the first reduce
  rule for RR. `ext_rule` is the REDUCE-side rule for SR, second for
  RR. Both carry their `origin_file` and `origin_line` so reviewers
  can navigate straight to the source.
- `schema_version: 1` is stamped at the top. Future format changes
  will bump it.

## Exit code contract

Tailored for CI integration:

| Exit | Meaning |
|------|---------|
| `0`  | No NEW conflicts. The extension is safe to merge. |
| `1`  | NEW conflicts present. Review or reject. |
| `2`  | Argument or file error (missing file, child crashed, etc.). |

Use the `||` shell idiom in CI:

```bash
lime --diff-conflicts base.lime ext.lime --json > diff.json \
    || (cat diff.json && exit 1)
```

## Implementation notes

- `--diff-conflicts` runs the full Lime compile pipeline through
  `FindActions()` for both grammars, twice, **in forked child
  processes**. Each child gets pristine global state for the
  Strsafe/Symbol/State tables, so cross-grammar contamination is
  impossible.
- The base side is parsed straight from `base.lime`. The merged side
  is parsed from a temp file built by `cat base.lime '\n' ext.lime`.
  This is the same shape `lime_compile_grammar_text` uses for
  runtime extension snapshots.
- Conflicts are collected by `FindActions()` into
  `lemp->conflict_list`, a singly-linked list of `ConflictRecord`
  structs. The list is built in declaration order. Costs one
  small allocation per conflict; even on PG-scale grammars
  (~1700 conflicts) the overhead is well under 1 ms.
- `resolve_conflict()` is **unchanged**. The list is populated in
  `FindActions()` immediately after `resolve_conflict()` returns
  non-zero — pure side channel.

## Limitations

- Windows builds: `--diff-conflicts` returns exit 2 with a
  diagnostic. The implementation uses `fork()`+`pipe()`; a
  CreateProcess-based Windows path is tracked for v0.5.
- Same-file diff (`lime --diff-conflicts a.lime a.lime`) fails
  during the merged-file compile because concatenating a grammar
  with itself produces duplicate-symbol errors. This is by design;
  use `lime -L a.lime` to inspect a single grammar's conflict set.
- Extensions that use `%extends "base.lime"` cannot be diffed
  directly via `--diff-conflicts` against the base — the merged
  temp file is in `/tmp` and the relative `%extends` path will not
  resolve. Run the diff against the *flat* (already-merged) grammar
  instead.

## See also

- [DIALECT.md](DIALECT.md) — `%dialect NAME { ... }` (v0.4.0).
- [EXTENDS.md](EXTENDS.md) — `%extends "base.lime"` + `%override` /
  `%remove` / `%override_type` (v0.4.1).
- [DIAGNOSTICS.md](DIAGNOSTICS.md) — interpreting conflict
  diagnostics in the report file.
