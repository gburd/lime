# Multi-grammar composition & context-sensitive keyword disambiguation

Lime can load several grammars into one parser at runtime (the
[extension framework](EXTENSIONS.md)), rebuild a single unified
LALR(1) automaton, and resolve genuine parser-side ambiguity by
forking (`src/strategy_fork_resolve.c`).  This document covers the
remaining piece needed to make composition *seamless* when two
grammars share keyword spellings: deciding, at scan time, **which
token a colliding lexeme should become**.

## The problem: scanner shadowing

When a base grammar (e.g. SQL) and a loaded extension (e.g. a QUEL
dialect, or `pg_mysql_lang` / `pg_oracle_lang` / `pg_duckdb_lang`)
share keyword spellings, the scanner has to commit a lexeme to one
token *code* before the parser sees it.  A perfect-hash keyword table
that resolves `range` straight to the SQL `RANGE` token shadows the
extension: the QUEL `range of e is emp` production is unreachable
because `range` never arrives as the QUEL token.

The fix is **not** a new parser feature — Lime's parser already
handles rule/precedence/automaton conflicts.  The fix is to let the
scanner ask the parser *"in your current state, which of these
candidate token codes would you actually accept?"* and emit that one.

## Resolving an extension keyword's token code

Before the scanner can offer a candidate code to the oracle, it needs
the code.  For a base keyword the scanner already has it (baked into
the compiled-in keyword table).  For an *extension* keyword the code
is assigned by the runtime recompile that produced the composed
snapshot — it is not known at scanner build time and must not be
hard-coded.

```c
/* parser.h: external token code for a name in THIS snapshot, or -1. */
int lime_snapshot_token_code(const ParserSnapshot *snap, const char *name);
```

Resolve each extension keyword **once**, at scanner setup, against the
composed snapshot:

```c
int code_delete = lime_snapshot_token_code(composed, "K_QUEL_DELETE");
int code_range  = lime_snapshot_token_code(composed, "K_QUEL_RANGE");
/* -1 means the extension that defines this keyword is not loaded;
** fall back to the base token. */
```

The returned value is the **external** code (it already includes the
`%first_token` offset), i.e. exactly what `parse_token` expects and
what the admissibility oracle below takes.  This lets QUEL emit real
`delete` / `range` tokens instead of mangled `delete_quel` / `q_range`
workarounds: the scanner consults the extension first, resolves the
extension keyword's live code, and shadows the base keyword when the
oracle says the extension token is the admissible one.

The lookup is a linear scan over the snapshot's symbol table — do it
once per keyword at setup, not per token.  It works on any snapshot
that carries a name table (every `lime -n` snapshot, and any snapshot
recompiled in-process from grammar text); `lime_snapshot_first_token`
reports the offset if you need to cross-check against a compiled-in
header.

## The oracle

Two read-only entry points (declared in `include/parse_context.h`):

```c
/* Lookahead-correct: would the parser bound to `ctx`, in its current
** state, make progress on this token (shift/shift-reduce/reduce/
** accept) rather than syntax-error? */
LimeTokenAdmissibility
parse_context_token_admissible(const ParseContext *ctx,
                               int external_token_code);

/* Lower-level: classify a token against an explicit, already-settled
** LR state of a snapshot.  Most callers want the ctx form above. */
LimeTokenAdmissibility
lime_token_admissible_in_state(const ParserSnapshot *snap,
                               uint16_t stateno,
                               int external_token_code);
```

`LimeTokenAdmissibility` is `{ LIME_TOK_NONE, LIME_TOK_SHIFT,
LIME_TOK_SHIFTREDUCE, LIME_TOK_REDUCE, LIME_TOK_ACCEPT }`.  Anything
other than `LIME_TOK_NONE` means the token is admissible — the parser
would make progress on it in that state.

`parse_context_token_admissible` is the one to use.  It replays the
engine's shift/reduce/goto loop **read-only** on a scratch copy of the
state stack: it resolves any pending shift-reduce sitting on the
stack top, follows lookahead-gated default reduces through their
gotos, and re-probes — exactly what `parse_engine_step` would do with
that lookahead, but without mutating the live parse or running user
actions.  This matters: between tokens the raw stack top is often a
*pending shift-reduce encoding*, not a settled LR state, so a naive
single-table probe would be wrong.  (`parse_context_current_state`
exposes that raw value for introspection only; do not interpret it
yourself for admissibility.)

### Cost

Zero on the hot path for single-grammar parsers — the oracle is only
*called* when a registered-extension keyword collides with a base
keyword, an empty set when no extension is loaded.  Each call is a
short bounded replay over the action tables already resident for the
parse; no allocation, no locking.

## The collision-resolution pattern

A scanner that has a lexeme matching both a base token (`base_code`)
and an extension token (`ext_code`):

```c
LimeTokenAdmissibility base = parse_context_token_admissible(ctx, base_code);
LimeTokenAdmissibility ext  = parse_context_token_admissible(ctx, ext_code);

if (ext != LIME_TOK_NONE && base == LIME_TOK_NONE) {
    emit(ext_code);            /* only the extension fits here */
} else if (base != LIME_TOK_NONE && ext == LIME_TOK_NONE) {
    emit(base_code);           /* only the base fits here */
} else if (base != LIME_TOK_NONE && ext != LIME_TOK_NONE) {
    /* BOTH admissible -> genuine ambiguity.  Hand to the
    ** disambiguation strategy (fork-resolve), which forks the parse,
    ** feeds a few tokens of lookahead down each branch, and picks the
    ** winner by extension priority / longest match.  Do NOT guess
    ** here. */
    emit_via_fork_resolve(ctx, base_code, ext_code);
} else {
    emit(base_code);           /* neither fits: let the base error */
}
```

### Worked example: QUEL leading verbs

Every Berkeley QUEL statement leads with its verb
(`retrieve`/`append`/`replace`/`delete`/`range`).  In a unified
SQL+QUEL automaton the QUEL grammar adds `stmt ::= quel_stmt` and
`quel_stmt ::= K_QUEL_RANGE ...` etc., so the QUEL verb is shiftable
in exactly the states where a statement can begin (the closure of the
top-level statement loop).  `parse_context_token_admissible` returns
admissible there and inadmissible elsewhere — **the oracle is the
statement-start test**; no `%statement_start` annotation is required.

Of QUEL's verbs, five (`range`, `of`, `is`, `to`, `replace`,
`retrieve`, `append`) do **not** lead a statement in base SQL, so at
statement start the SQL meaning is inadmissible and the oracle picks
the QUEL token with no fork.

`delete` is the exception.  `DELETE FROM ...` is a real SQL statement,
so at statement start **both** the SQL `DeleteStmt` and the QUEL
`quel_delete_stmt` are admissible — a genuine ambiguity the oracle
cannot settle alone.  The two diverge at the second token
(`delete e where ...` vs `DELETE FROM ...`), so the
`emit_via_fork_resolve` branch with one to two tokens of lookahead
resolves it.  Plan for `delete` (and any verb that legitimately leads
statements in both grammars) to go through fork-resolve, not pure
leading-keyword promotion.

## Where the parser state comes from

`parse_context_token_admissible` needs a *live* parse context — the
parser must be mid-parse, with the previous token already consumed,
when the scanner asks about the next lexeme.  A scanner architecture
that pre-scans the entire input into a token FIFO *before* parsing
starts has no live parser state at scan time and cannot use the
oracle; such a scanner must be converted to interleaved lex+parse
(emit a token, let the parser consume it, then scan the next) so the
parser state is current when each colliding lexeme is classified.

## Beyond one token: the three disambiguation tiers

The admissibility oracle above resolves *lexical* collisions and the
bounded-lookahead cases (QUEL vs SQL, the `delete` one-token peek).
For dialects that overlap more deeply ("load MySQL + Oracle + DuckDB
at once"), Lime offers three layered mechanisms, each **explicit about
its guarantee level**.  This is deliberately not a promise that any
union of dialects "always works": LR theory forbids that when two
dialects share a production with a different meaning.  (`multi_grammar.h`)

### Tier 1 — full-statement fork-resolve (CORRECT when forks diverge)

When a collision is admissible in two or more candidate grammars,
simulate each candidate over the **real upcoming token stream** and
prefer the one that reaches accept (or gets furthest with fewest
errors):

```c
LimeForkCandidate cands[] = { {mysql_snap, 0, 1}, {oracle_snap, 0, 2} };
int w = lime_mg_resolve(cands, 2, lookahead, nlook,
                        /*bayes*/NULL, 0, 0, /*ranks*/NULL);
/* w == 0 -> MySQL; the simulated MySQL parse reached accept on this
** stream and the Oracle one errored (e.g. at LIMIT vs ROWNUM). */
```

`lime_simulate_parse` (the primitive) replays the engine's
shift/reduce/goto loop on a private state stack — read-only, no
actions, no mutation — so this is *full-statement matching*, not a
one-token peek.  It is **correct** for the common "90% overlap" case
where the dialects diverge somewhere inside the statement (LIMIT vs
ROWNUM, hint syntax, `(+)` joins).  It does **not** resolve two
truly-identical productions (same RHS, same LHS, different action) —
those simulate identically and fall to Tier 2/3.

### Tier 2 — Bayesian tie-break (HEURISTIC)

When Tier 1 ties, a Beta-Bernoulli store keyed by `(state, token,
ext_id)` picks the historically-likelier dialect and learns from
confirmed parses:

```c
LimeBayesStore *bs = lime_bayes_create();
... lime_mg_resolve(cands, n, look, nlook, bs, state, token, NULL) ...
lime_bayes_observe(bs, state, token, winning_ext, parse_succeeded);
/* persist across sessions: */
size_t k = lime_bayes_serialize(bs, NULL, 0); void *blob = malloc(k);
lime_bayes_serialize(bs, blob, k);   /* store blob; lime_bayes_deserialize later */
```

This does **not** make an ambiguous parse correct — it makes it
resolve consistently and improve with feedback.  It is consulted only
on a Tier-1 tie, and only overrides the deterministic fallback once it
has real evidence (posterior moved off the 0.5 prior), so behaviour is
reproducible until the host actually trains it.

### Tier 3 — dialect mode selection (EXACT, but not disambiguation)

For genuinely mutually-ambiguous full dialects that no parse-time
method can separate, select one dialect per session or per statement:

```c
LimeDialectRegistry *reg = lime_dialect_registry_create();
lime_dialect_register(reg, "mysql",  mysql_snap);
lime_dialect_register(reg, "oracle", oracle_snap);

/* per session: */
ParserSnapshot *snap = lime_dialect_select(reg, guc_grammar_dialect);

/* per statement, via a leading @dialect sigil: */
ParserSnapshot *snap; size_t body;
if (lime_dialect_parse_sigil(reg, "@mysql SELECT ...", &snap, &body)) {
    /* parse input + body with snap */
}
```

This is mode selection, not disambiguation — but it is the only thing
that is *exact* for fully-overlapping dialects, and Lime enables it as
a first-class, named capability.

### Cost

All three tiers cost **zero on the parse hot path**.  Tier 1
simulation runs only on a registered-extension collision with two or
more admissible candidates; single-dialect parsing never forks.  Tier
2 tables allocate lazily on the first collision.  Tier 3 is a
host-side snapshot-pointer swap.  `parse_engine_step` and
`lime_simulate_parse` share no state; the bench binaries are byte-
identical with and without the tiers compiled in.

## Compose-time conflict reporting

When composing fragments introduces an LALR conflict, the contract has
two cases:

- A **shift/reduce** conflict (and a reduce/reduce that lemon resolves
  keep-first while leaving both rules reducible) is **resolved
  silently** -- `lime_compile_grammar_in_process` returns 0 and builds
  a snapshot, with the winner chosen by table-build order, not author
  intent.  This is the dangerous case: a fragment that shadows an
  already-loaded dialect's rule mis-parses silently.
- A **reduce/reduce** conflict that renders a rule unreducible is a
  **hard error** (non-zero return, non-NULL error string).

To detect the silent case, use the reporting variant and refuse / warn
when the conflict count is non-zero:

```c
int nconf = 0;
int rc = lime_compile_grammar_in_process_ex(text, len, &snap, &err, &nconf);
if (rc != 0)        { /* hard error: err explains */ }
else if (nconf > 0) { /* built, but a rule was shadowed -- reject the
                       ** extension or warn the author, do not install */ }
else                { /* clean compose */ }
```

## Per-backend snapshot safety

A `ParserSnapshot` is **read-only during a parse**: the engine only
reads its tables, and the sole field written on the parse path is the
atomic refcount.  Composed snapshots have the same layout, so the
per-dialect-selection model is safe:

- Hold several composed snapshots; each backend pins the active one
  with `snapshot_acquire` and releases with `snapshot_release` (atomic,
  lock-free).  Swap dialects by pointer.
- Concurrent parses of the **same** snapshot from many backends are
  race-free (verified under ThreadSanitizer:
  `tests/test_multi_grammar.c` runs 8 threads parsing one shared
  composed snapshot).
- A `ParseContext` is **not** shareable -- it holds the live parse
  stack; use one per backend/parse.  The snapshot is the shared object;
  the context is per-thread.

## See also

- [EXTENSIONS.md](EXTENSIONS.md) — loading grammars at runtime.
- [CONTEXT_SWITCH.md](CONTEXT_SWITCH.md) — the *different* mechanism
  for *nesting* a sub-language (JSON in SQL) via boundary triggers.
  Use context-switch for embedded literals; use the admissibility
  oracle for keyword collisions between grammars that share a start
  context.
- `src/strategy_fork_resolve.c` — the fork engine for genuine
  ambiguity (the `delete` case above).
- `multi_grammar.h` / `src/multi_grammar.c` — the three disambiguation
  tiers (full-statement fork-resolve, Bayesian tie-break, dialect
  selection).
- `tests/test_admissibility.c` — the oracle's regression suite.
- `tests/test_multi_grammar.c` — the three-tier regression suite.
