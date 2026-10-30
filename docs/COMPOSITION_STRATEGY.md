# Composition Strategy: A Tiered Approach to Multi-Dialect Grammars

This is a **proposal**, not an implementation plan with committed
dates, and not a description of shipped behavior beyond what each
section explicitly marks as "today." Nothing here should be read as
already built unless cited to a specific file/function/test. It
proposes routing grammar composition through three tiers of
increasing cost and increasing correctness guarantee, each of which
already has its load-bearing primitive in the tree — the proposal
is sequencing and gating logic on top, not new machinery.

See [RELATED_WORK.md](RELATED_WORK.md) for how this compares to
Marpa, Silver/ableC, SDF2/SGLR, Copper, and Elkhound. The short
version: none of the three tiers below claims Silver's "prove once,
compose freely" guarantee. Tier 1 proposes a narrow, weaker version
of that idea (FIRST/FOLLOW disjointness, not modular determinism
analysis) as a fast-path gate, not a full solution.

## Why tiers at all

The two correctness problems named in the task are real and
distinct:

1. LALR(1) is not closed under grammar union. Merging full dialects
   is not "a few conflicts," it can be conflicts in the hundreds,
   and picking a winner per conflict can silently delete an
   accepting path that the base grammar or another dialect relied
   on. `compose_snapshots()` (`src/parser_composition.c`) detects
   the conflicts it can see and returns `COMPOSE_ERR_CONFLICT`, but
   nothing in the pipeline replays known-good inputs to confirm the
   *resolution* didn't break something that used to work — see
   the two closing sections below.
2. (Historical; fixed, see "Status" below.)  Composition was order-dependent by construction: the base
   snapshot is cloned and each loaded extension's modifications are
   applied one at a time in `load_extension()` call order
   (`create_modified_snapshot()` in `src/snapshot_modify.c`,
   `publish_modified_snapshot()`, then in `src/extension.c`). The default
   conflict policy when no `on_conflict` callback is registered is
   `CONFLICT_KEEP_EXISTING` (`src/extension.h`,
   `docs/EXTENSIONS.md`'s "Extension ordering matters" section) —
   the earlier-loaded extension silently wins, with zero diagnostic
   surfaced to the host. In PostgreSQL, `shared_preload_libraries`
   order is an operator-controlled config string; the same SQL text
   could parse differently across two replicas that loaded the same
   extensions in different orders. That is a correctness defect,
   not a style complaint.

Not every composition needs the same amount of machinery to be
correct. A tiered design lets "MySQL-compat adds a `LIMIT` variant
that the base grammar never touches" stay cheap (Tier 1), while
"Oracle-compat and MySQL-compat both want to grab `INTERVAL` syntax
in overlapping ways" pays for Tier 3's heavier fork/resolve
machinery only where it's actually needed.

## Tier 1 — Conflict-free LALR merge

### What exists today

This is the fast path already implemented. `compose_snapshots()`
(`src/parser_composition.c`) unifies symbols across N snapshots
(`unify_symbols()`), merges rule counts (`merge_rules()` — currently
metadata-only; see caveat below), concatenates/rebuilds action
tables, and calls `rebuild_automaton()`
(`src/snapshot_modify.c`). It then checks
`conflict_set_unresolved_count(cs) > 0` and fails the whole
composition with `COMPOSE_ERR_CONFLICT` if any unresolved conflict
survives (Step 8 of `compose_snapshots()`). The single-extension
variant, `create_modified_snapshot()` in `src/snapshot_modify.c`,
follows the same shape: clone → `detect_conflicts()` →
`resolve_conflicts()` → apply modifications → `rebuild_automaton()`.
Both paths are exercised by `tests/test_parser_composition.c`,
`tests/test_composition_e2e.c`, and `tests/test_random_composition.c`.

Two honest caveats from reading the code, both already documented
in [COMPOSITION.md](COMPOSITION.md)'s "Performance" section:
`rebuild_automaton()` today does validation and version-bumping,
not a true LALR(1) recompute from a merged grammar (that is
[ROADMAP.md](ROADMAP.md) item 1, "in-process LALR(1) automaton
rebuild"); a fully rebuilt automaton comes from
`publish_modified_snapshot()`, which rebuilds the merged grammar in-process
(there is no subprocess path any more). And `merge_rules()`
in `src/parser_composition.c` currently only sums rule counts — it
does not deep-copy rule content, because `struct rule` is opaque
outside `lime.c` (a comment in the function says so directly).
Tier 1 as proposed below assumes the ROADMAP item 1 rebuild lands;
without it, "prove zero new conflicts" has nothing precise to check
against beyond table sizes.

### What Tier 1 should be scoped to

Proposal: gate Tier 1 so it only runs when a pre-pass can *prove*
the extension set introduces zero new conflicts, rather than
running the LALR rebuild speculatively and finding out via
`COMPOSE_ERR_CONFLICT` after the fact. The value of proving instead
of discovering is that a failed proof can fall through to Tier 2 or
Tier 3 *before* spending the rebuild cost, and a successful proof
means the result is known-correct without needing the regression
gate described at the end of this document (though running it
anyway is still cheap insurance).

The pre-pass shape (sketch, not a full algorithm — this needs real
design work against `lime.c`'s `FindFirstSets` / `FindFollowSets`
output before being buildable):

- For each candidate extension's added productions, compute the
  FIRST and FOLLOW sets the extension introduces (or, more cheaply,
  reuse the base grammar's already-computed FIRST/FOLLOW tables and
  check whether the extension's new terminals/nonterminals appear
  in the viable-token set of any existing LALR state). The relevant
  existing data structures to walk are the snapshot's `yy_lookahead`
  / `yy_action` arrays (already exposed on `ParserSnapshot`, see
  `compute_snapshot_merkle()` in `src/parser_composition.c` for an
  example of walking `yy_action`) and, once ROADMAP item 1 lands,
  the in-memory FIRST/FOLLOW tables directly.
- If an extension's added terminals/productions never appear in any
  existing state's viable-token set, and no two *extensions in the
  same compose set* add overlapping terminals/productions (checked
  today, weakly, by `unify_symbols()`'s symbol-name collision
  detection — but that only catches identical names, not FIRST-set
  overlap under different names), the pre-pass can certify
  "conflict-free by construction" and Tier 1 proceeds.
- Otherwise, fall through to Tier 2 (if the overlap is scopeable to
  a sublanguage boundary) or Tier 3 (if it's genuine ambiguity).

This is deliberately the weak, cheap version of Silver's modular
determinism analysis (see [RELATED_WORK.md](RELATED_WORK.md)) — it
checks pairwise/set-wise disjointness empirically per compose call,
not a once-per-extension proof that holds for *any* future
combination. Building the stronger version would mean defining a
constrained extension-authoring interface (bounded production
shapes, declared attribute dependencies) that Lime's
`GrammarModification` API does not have today; that is a
substantially bigger project than this document is scoping.

### Land-before

Tier 1's gate is now real for `publish_modified_snapshot()`: it builds
the base alone and the composition in-process and fails on any conflict
key the base does not have (see "The conflict gate and the policy"
below).

## Tier 2 — Context-scoped sublanguage

### What exists today

`src/context_switch.c` (four entry points:
`context_switch_register_trigger()`, `context_switch_classify_lexeme()`,
`context_switch_needed()`, `context_switch_detect_exit()`) sits on
top of `src/grammar_context.c`'s `GrammarContextStack`
(`grammar_context_create()`, `grammar_context_push()` /
`grammar_context_pop()`, `grammar_context_register_mode()`). This is
already proven for SQL + embedded sublanguages — JSON literals, XML
via `XMLPARSE`/`XMLELEMENT`, JSONPath, EDN — per
[CONTEXT_SWITCH.md](CONTEXT_SWITCH.md) and exercised end-to-end by
`tests/test_context_switch_e2e.c`, with unit coverage in
`tests/test_context_switch_unit.c` and error-path coverage in
`tests/test_context_switch_error.c`. The mechanism: register a
trigger lexeme mapped to a fully separate `ParserSnapshot`; when the
host lexer emits a lexeme matching a registered trigger (prefix
match), the driver pushes a new stack entry, drives the embedded
grammar to completion as a sub-parse, and resumes the host parse
with a single composite token carrying the embedded AST.

### How this generalizes to dialect scoping

The task's example — "this MySQL construct is only reachable inside
a top-level `SELECT`" — is architecturally the same shape as "this
JSON literal is only reachable after the `json` keyword": both are
"compile the sublanguage as a separate automaton, dispatch into it
via a trigger, and only pay the multi-grammar cost inside the
scoped region." Concretely, generalizing today's mechanism to
dialect scoping means calling `context_switch_register_trigger()`
once per dialect-specific construct that can be lexically or
syntactically anchored — e.g. registering MySQL's `LIMIT offset,
count` comma-form as a trigger scoped to "inside a `SELECT`
statement," backed by a `ParserSnapshot` compiled from just that
construct's grammar fragment, rather than trying to fold the whole
MySQL grammar into the same LALR table as the whole Oracle grammar
via Tier 1.

What is genuinely missing for this to work at dialect scale (as
opposed to the single-sublanguage case it's proven for):

- **Trigger scoping is currently root-vs-embedded, not
  state-scoped.** `GrammarModeInfo`'s `trigger_lexeme` is matched by
  `context_switch_classify_lexeme()` as a flat prefix scan whenever
  `grammar_context_is_root_only()` is true — there is no notion of
  "this trigger is only active when the host parser is inside a
  `SELECT` clause specifically" versus "active at root generally."
  [CONTEXT_SWITCH.md](CONTEXT_SWITCH.md)'s own "Don't register
  triggers that overlap by prefix" and "greedy by registration
  order" caveats are direct symptoms: the registry has no
  parser-state awareness, so it cannot disambiguate two dialects
  that want the same keyword to mean different things in different
  syntactic positions. This is the Copper gap named in
  [RELATED_WORK.md](RELATED_WORK.md).
- **Single-trigger-per-lexeme is enforced by design**
  (`context_switch_register_trigger()` rejects duplicate trigger
  lexemes; see the loop in `src/context_switch.c` and the explicit
  design note in [CONTEXT_SWITCH.md](CONTEXT_SWITCH.md)'s "Design
  tradeoffs" section: "two grammars cannot both claim `json`...
  Mixing two SQL dialects would register distinct trigger sets
  against distinct stacks"). That's workable for genuinely distinct
  keywords (MySQL's backtick-quoted identifiers vs. Oracle's
  `CONNECT BY`) but doesn't help when two dialects want the *same*
  keyword to trigger different sublanguages — which is a real
  scenario for SQL dialect mixing (e.g., `INTERVAL` syntax differs
  between MySQL and Oracle but both use the same keyword).
- **No dialect-priority integration.** The trigger registry has no
  concept of `LimeContext.priority` (`include/conflict.h`) or of
  which extension registered which trigger; if this is generalized
  to "N dialects each register triggers," there's currently nothing
  that would resolve "MySQL and Oracle both try to register a
  trigger for the same lexeme" other than the existing duplicate
  rejection (silent failure to register, not a resolved priority
  decision).

### Land-before

Tier 2 generalization is independent of Tier 1's ROADMAP item 1
dependency — it doesn't need the in-process rebuild, since each
sublanguage compiles to its own separate `ParserSnapshot` already.
It does need the state-scoped trigger extension above before it's
usable for real dialect-vs-dialect keyword collisions; without that
it's usable today only for the case Tier 2 already covers well
(distinct, non-colliding trigger lexemes per sublanguage).

## Tier 3 — GLR fork-and-resolve

### What exists today

Three pieces, each real but not yet wired end-to-end for
multi-dialect use:

- `src/glr.c` + `include/glr.h`: a working GLR engine (GSS-based,
  forks on shift/reduce or reduce/reduce conflict, merges converging
  heads, disambiguates via a user callback). Opt-in via
  `lime_parse_glr()`, proven LALR-fast-path-zero-overhead-when-unused,
  documented 5-8x per-parse cost when active. Tested by
  `tests/test_glr_no_conflict.c` (LALR/GLR equivalence on
  deterministic grammars) and `tests/test_glr_ambiguous.c` (forking
  + disambiguation callback on a genuinely ambiguous grammar).
- `src/parser_fork.c` + `include/parser_fork.h`: deep-clones an
  opaque `yyParser` struct (stack, semantic values, pointer fixups)
  so multiple grammar interpretations can run against independent
  copies of live parser state. `fork_parser()` acquires a snapshot
  reference per fork; `ParseForkSet` manages a bounded collection
  with pruning (`parse_fork_set_prune()`) and best-fork selection
  (`parse_fork_set_best()`). Tested by `tests/test_parser_fork.c`.
- `src/strategy_fork_resolve.c`: a `DisambiguationStrategyVTable`
  implementation that forks a parser per candidate `LimeContext` in
  a `ConflictPoint`, feeds lookahead tokens, prunes failures, and
  picks a winner via a configurable tiebreak
  (`TIEBREAK_PRIORITY` / `TIEBREAK_LONGEST_MATCH` /
  `TIEBREAK_FIRST_COMPLETE`). Tested by `tests/test_fork_resolve.c`.

**Provenance check, since the task asked explicitly not to assume
it:** yes, `ParseFork` carries provenance in a limited sense — the
`priority` field on each `ParseFork` is set from the owning
`LimeContext.priority` scaled with `ext_id`
(`prio = ctx->priority * 10000 + ctx->ext_id` in
`fork_resolve_resolve()`), and the winner is mapped back to a
`LimeContext`/extension by matching that scaled priority. So a
fork's *origin extension* is recoverable after the fact via the
priority encoding, not via a first-class field. There is no direct
`ext_id` or `grammar_name` field on `ParseFork` itself
(`include/parser_fork.h`) — the mapping is reconstructed by the
caller (`strategy_fork_resolve.c`'s `lookup_grammar_name()` /
`lookup_priority()` helper functions scanning a side table), which
is fragile if two contexts happen to produce the same scaled
priority value (verified as a real, if narrow, risk: nothing
prevents `ctx->priority * 10000 + ctx->ext_id` colliding across two
distinct `(priority, ext_id)` pairs only if `ext_id` values or
priorities are chosen adversarially, but for typical small `ext_id`
ranges and default zero priority it's effectively a bijection in
practice).

### What's missing for production multi-dialect use

- **Token feeding is not wired to a real parser engine yet.**
  `feed_token_to_fork()` in `strategy_fork_resolve.c` has an
  explicit `TODO(ROADMAP item 1)` comment: today it just increments
  `tokens_consumed` and returns `true` unconditionally — it does not
  actually drive `parse_engine_step()` or consult the fork's
  `ParserSnapshot` action tables. Likewise `is_statement_end()` has
  a `TODO` noting it should inspect the parser stack for an accept
  state but currently just checks "has this fork consumed at least
  one token" as a placeholder. This means the fork-resolve strategy
  is currently exercised in tests as a *static/simulated* mode
  (`parse_ctx == NULL` path in `fork_resolve_resolve()`), not the
  live-parser mode. The live-parser entry points
  (`fork_resolve_create_forks()`, `fork_resolve_feed_tokens()`,
  `fork_resolve_evaluate()`, `fork_resolve_runtime()`) exist and
  compile against a real `yyParser*` and real token streams, but
  `feed_token_to_fork()` is the shared internal that both the
  "static" and "runtime" code paths call, and it is the one that's
  still a stub for the actual action-table consultation. Wiring
  that in is squarely the ROADMAP item 1 in-process rebuild
  dependency, since forking mid-parse against a snapshot that isn't
  fully rebuilt in-process is exactly the scenario that work is
  meant to unblock.
- **No cross-extension provenance beyond priority.** As noted above,
  a fork's origin extension is reconstructed via priority-matching,
  not carried as a direct field. For production multi-dialect
  debugging (a host wanting to log "MySQL-compat and Oracle-compat
  both proposed an interpretation for this input, MySQL won because
  X") the `StrategyResult::explanation` string
  (`fork_resolve_resolve()` builds one via `snprintf`) already
  carries a human-readable summary including the grammar name, which
  covers the observability need today better than the raw
  `ParseFork` struct does.
- **Fork count is bounded but not cost-modeled.**
  `FORK_RESOLVE_MAX_FORKS` (16) and `FORK_RESOLVE_MAX_LOOKAHEAD`
  (128) are fixed constants in `strategy_fork_resolve.c`. For a
  composition of many dialects where several overlap at the same
  conflict point, 16 forks × cloning a full `yyParser` + stack via
  `clone_parser_state()` (a `malloc` + `memcpy` per fork, per
  `src/parser_fork.c`) is a real per-conflict cost that has not been
  benchmarked at multi-dialect scale (GLR's own measured 5-8x
  overhead in [GLR.md](GLR.md) is the closest existing data point,
  and that's for GLR's GSS bookkeeping, not fork-resolve's clone-per-
  candidate model, which is architecturally heavier).

### Land-before

Tier 3's production-readiness for live multi-dialect parsing depends
on ROADMAP item 1 in the same way Tier 1 does — `feed_token_to_fork()`
needs a real action-table-driven single-step function, which is the
same in-process rebuild/step capability Tier 1's rebuild path needs.
Tier 3 can be exercised standalone (as today's tests do) without
that dependency for the *disambiguation-policy* logic (tiebreak
rules, fork set management), just not for driving real dialect
grammars against real input yet.

## Sequencing across tiers

Land-before ordering, given the above:

1. **ROADMAP.md item 1 (in-process LALR rebuild)** is the shared
   prerequisite for Tier 1's conflict-free-merge gate being
   meaningful *and* for Tier 3's fork-resolve to drive real token
   streams. It should land first regardless of which tier is
   prioritized next.
2. **Tier 2's state-scoped trigger extension** is the most
   independent of the three — it needs no ROADMAP dependency and
   the base mechanism is already proven for the single-sublanguage
   case. It's the cheapest next step for dialects whose overlap is
   syntactically scopeable (most SQL dialect additions are — new
   clause forms, new literal syntax gated by a keyword).
3. **Tier 1's proof-gated fast path** comes next once ROADMAP item 1
   lands, since it's the cheapest per-compose cost for the (common)
   case of genuinely non-overlapping extensions.
4. **Tier 3's live fork-resolve wiring** is the most expensive and
   should be last: it's needed only for the genuinely-overlapping
   construct case that Tiers 1 and 2 can't handle, and its
   `feed_token_to_fork()` gap is the deepest implementation hole of
   the three tiers.

## Closing the two sharpest correctness gaps

### (a) Order-dependence

**The problem, concretely.** `docs/EXTENSIONS.md`'s "Extension
ordering matters" section states it directly: "the 'existing'
modification is from the earlier-loaded extension and the 'new'
modification is from the later-loaded one... If no `on_conflict`
callback is provided (NULL), the system defaults to
`CONFLICT_KEEP_EXISTING`." The enum is `ConflictResolution` in
`src/extension.h`:

```c
typedef enum ConflictResolution {
    CONFLICT_UNRESOLVED = 0, /* No resolution provided            */
    CONFLICT_KEEP_EXISTING,  /* Keep the existing rule/token       */
    CONFLICT_USE_NEW,        /* Replace with the new modification  */
    CONFLICT_MERGE,          /* Extension provides merged result   */
} ConflictResolution;
```

and the default-when-NULL behavior is implemented in
`resolve_conflicts()` (`src/conflict.c`) and (formerly)
`publish_modified_snapshot()`: both walk each
unresolved `Conflict`, try `ext_a`'s `on_conflict` then `ext_b`'s,
and — critically — **when neither extension supplies a callback at
all, no `CONFLICT_KEEP_EXISTING` return value is even produced; the
conflict is simply left with `c->resolved == false` and, in
`create_modified_snapshot()`'s caller path
(`snapshot_modify.c::create_modified_snapshot`), an unresolved count
above zero correctly fails the load with `MODIFY_ERR_CONFLICT`.**
The task's framing ("default... is `CONFLICT_KEEP_EXISTING`,
meaning the earlier extension silently wins") is accurate for the
case where *one side does supply* an `on_conflict` callback that
itself defaults to keeping its own modification (the worked example
in `docs/EXTENSIONS.md`'s JSONB walkthrough does exactly this:
`jsonb_on_conflict()` unconditionally `return CONFLICT_KEEP_EXISTING;`)
— i.e., the danger is not that the *framework* silently resolves
conflicts by default, it's that the *documented convention* for
writing an `on_conflict` callback encourages exactly this
silent-keep-existing behavior, and an extension author who follows
the docs' own example gets silent, order-dependent, undiagnosed
data loss.

**Proposed minimal fix.** Two changes, both narrowly scoped:

1. Change the *documented default convention* in
   `docs/EXTENSIONS.md` (not touched by this task, but the target
   for a follow-up) away from "if `on_conflict` is NULL, keep
   existing" and toward: absence of a callback should be treated
   identically to a callback returning `CONFLICT_UNRESOLVED` — i.e.
   today's `resolve_conflicts()` / `publish_modified_snapshot()`
   behavior when *no* callback exists is already correct
   (unresolved → load fails loudly with `MODIFY_ERR_CONFLICT`); the
   gap is that the enum makes it easy for an extension author to
   *opt into* silence by writing `return CONFLICT_KEEP_EXISTING;`
   unconditionally, with no signal to the host that a silent
   pick-a-winner just happened.
2. The concrete code-level fix: add a diagnostic-carrying variant so
   a `CONFLICT_KEEP_EXISTING` or `CONFLICT_USE_NEW` resolution is
   *recorded* in the `ConflictSet`/`ConflictInfo` path with both
   extension names and both modification descriptions, surfaced to
   the host even on a "successful" resolution — not just on
   `CONFLICT_UNRESOLVED`. Today, `Conflict::resolved` is a bare
   `bool` (`include/conflict.h`); the loser's identity and the
   chosen resolution value are discarded the moment
   `c->resolved = true` is set in `resolve_conflicts()`
   (`src/conflict.c`) and `publish_modified_snapshot()`
   (`src/extension.c`). Extending `Conflict` with a
   `ConflictResolution resolution` field and an
   `ExtensionID winning_ext` field (both cheap, additive struct
   fields) would let a host log "extension B's modification lost to
   extension A's via CONFLICT_KEEP_EXISTING at load time" instead of
   the resolution being invisible after the fact. This is a smaller
   change than redefining the default policy and is where a
   `CONFLICT_UNRESOLVED`-by-default proposal (as the task suggests)
   would plug in: making *that* the literal default requires no enum
   change at all, since `CONFLICT_UNRESOLVED == 0` is already the
   zero-value / fail-safe member of the enum — the actual gap is
   purely in the *documented convention* for what extension authors
   should return, and in the missing "who won and how" diagnostic
   trail.

### (b) No detection of "conflict resolution silently deleted a base-grammar accepting path"

**The problem.** Even a policy change per (a) only prevents *silent*
order-dependence going forward — it does nothing to detect the case
where a conflict was *correctly* resolved (with a diagnostic, with
an explicit winner) but the resolution itself was semantically
wrong: picking extension A's rule over extension B's, or over the
base grammar's own rule, removed an accepting path that a real,
previously-valid SQL string depended on. Nothing in
`compose_snapshots()` or `create_modified_snapshot()` re-parses
anything after the automaton rebuild; both simply check
`conflict_set_unresolved_count()` and return success once that's
zero. A composition can report zero unresolved conflicts and still
have broken the base grammar.

**Proposed fix: a regression-replay gate.** After any successful
compose or extension-load (i.e. right before the new snapshot is
handed back to the caller as the "winning" result), re-parse a
corpus of known-good inputs against the *new* snapshot and fail the
compose/load if any input that the *base* snapshot (or the
previously-active composed snapshot) accepted is now rejected. This
is the same idea as a golden-file regression suite, but run
automatically as a gate inside the composition pipeline rather than
left to the host's own test suite to catch after the fact — the
whole point of Goal B is that a host like PostgreSQL composes
extensions it did not write, at startup, on an operator's schedule,
so the corpus check needs to run inline where the composition
happens, not depend on the host separately remembering to test.

Natural insertion points in the existing pipeline (both are the
last step before returning success, so the gate runs against the
*final* rebuilt automaton, not an intermediate state):

- `compose_snapshots()` in `src/parser_composition.c`, right after
  Step 8 ("Check for unresolved conflicts") and before Step 9
  (merkle computation) / the final `*out = composed; return
  COMPOSE_OK;`. The gate would take the composed snapshot plus a
  corpus (most naturally, the corpus associated with whichever
  snapshot occupies `snapshots[0]`, or an explicit corpus passed via
  a new field on `CompositionOptions`) and reject with a new result
  code (e.g. `COMPOSE_ERR_REGRESSION`) if the corpus doesn't fully
  re-accept.
- `create_modified_snapshot()` in `src/snapshot_modify.c`, right
  after Step 5 ("Rebuild the LALR(1) automaton") and before
  `*out = snap; return MODIFY_OK;`. Same shape: a corpus check
  against the newly-modified snapshot, gated on the *base*
  snapshot's corpus (the natural place to attach a corpus is
  per-`ParserSnapshot`, or per-extension as part of `ExtensionInfo`,
  since the base grammar's own regression corpus is a property of
  the base, not of any one extension).

What "corpus" means concretely is intentionally left open here — it
could be as simple as an array of `(input_string, expected_accept)`
pairs supplied by the host (PostgreSQL would supply a corpus of
representative SQL statements it needs to keep parsing), or, more
ambitiously, generated from the base grammar's own test suite. The
mechanical hook point — re-parse-and-compare right before the
"success" return in both `compose_snapshots()` and
`create_modified_snapshot()` — is the part that's concretely
actionable from reading the current code; sourcing and maintaining
the corpus itself is a host-level policy question, not something
this document is scoping.

## Status: the conflict gate and the policy (implemented)

Gap (a) and the "zero new conflicts" half of Tier 1 are now closed for
`publish_modified_snapshot()`:

- **Gate.**  The base is compiled alone once (its symbolic conflict keys
  are cached per base text), then base+extensions with a hook in
  `FindActions`.  Any key absent from the base must be resolved by the
  host policy, or the compose fails.  The base's `%expect` never absorbs
  it.  Measured with `tests/compose_fixtures/dialect_ext.lime`: +1 on
  `examples/pg/pg_grammar.lime`, +219 on `examples/pg/gram.lime`
  (`%expect 1665`); both fail by default (`tests/test_compose_conflict_
  gate.c`, `lime --explain-compose`).
- **Who wins** is the host's declarative policy (`include/lime_policy.h`,
  [EXTENSIONS.md](EXTENSIONS.md#conflict-policy-who-wins)).  Extensions'
  `on_conflict` answers are proposals, applied only under no policy or
  `trust-extension-proposals`.  Equal ranks on a real conflict are an
  error.  Composition is a function of (base, extension set, policy),
  not of load order (`tests/test_compose_order.c`, both parts).
- **Failure keeps the old snapshot**: there is no metadata-only
  fallback (`tests/test_compose_failure_keeps_snapshot.c`).

Gap (b), the regression-replay gate, is still open.

### Zero runtime cost, measured

`bench/bench_compose_zerocost` parses the PostgreSQL 18 regression
corpus (230 files, 49,201 statements, 644,545 tokens; crude keyword
tokenizer) through the public push parser.  It runs on a base snapshot
(examples/pg/pg_grammar.lime) and on the same base composed with the
30-rule dialect extension under the policy `conflict sr "upsert_cols
::=" on LPAREN prefer shift.`.  Correctness first: identical per-
statement accept/reject (47,327 accepted) in every configuration, an
identical hash of the reduced rule-identity sequence (1,910,293 reduces),
every base terminal keeps its code, and no extension callback fires on
the base corpus.  CPU time, Xeon 8488C, pinned to one core with its SMT
sibling idle, 21 rounds x 3 passes, configurations interleaved and
rotated, median of per-round ratios, three runs on cores 12/13/14:

| configuration | vs | ratio (3 runs) |
|---|---|---|
| base compiled twice (A/A noise floor) | base | 0.993, 0.978, 0.991 |
| composed with the policy (lime_compose_text) | base | 0.991, 0.978, 0.993 |
| composed with the policy (publish_modified_snapshot), no callbacks | base | 0.992, 0.982, 0.993 |
| + MOD_ADD_RULE reduce callbacks bound (31 rules), no host_reduce | the same tables unbound | 0.999, 0.996, 1.003 |
| + bound callbacks, with a pass-through host_reduce | base + the same host_reduce | 0.996, 0.982, 0.992 |

Base: 47.4-49.5 ns per token.  The policy leaves nothing in the snapshot
(the bound and unbound tables are hash-identical), and composing costs
nothing: all within the A/A floor.

Binding extension callbacks used to cost 3.9-5.5% in recognition mode.
Every reduce probed the 16-byte `ext_reduce[]` entry (3.6k rules x 16 B
does not fit L1), then copied `$1` through to `$$` and stored it on the
push.  Now `publish_modified_snapshot()` precomputes `ext_action[ruleno]`
(one byte: callback / passthrough / none, see REDUCE_DISPATCH.md).  The
engine has two step bodies, compiled from one source with a constant
`ext` flag: the plain one has no extension code at all, and the other's
no-host_reduce reduces go to a separate `ext_reduce()`.
`parse_engine_step()` picks one per token (`ext_action != NULL` and no
host_reduce), with one predicted branch.  A passthrough leaves `$1`'s slot
in place, since it is the slot the goto reuses.  The semantics are
unchanged; every value an extension or the host could observe before is
still observed (`tests/test_extension_value_passthrough.c`).

Two traps on the way, both measured.  A single body that tests for
extensions costs 3-5% on one path or another, depending on where the test
sits, because of codegen and not extra work: deleting the old (dead)
extension branches alone made recognition-only 1.5% slower.  And the stack
depth, (top - base) in 24-byte entries, is a divide (sar + imul) on every
reduce, in the chain the goto's loads wait on.  Comparing in bytes
(`stack_has`) removed it, and the base got 3% faster.

The same harness, before and after, as separate processes (one
configuration per process, 21 corpus passes, CPU ms, builds interleaved
and rotated, 7 iterations x 2 cores; "before" is the old engine, and a
second build of it is the cross-build A/A floor):

| configuration | before | old engine rebuilt (A/A) | after |
|---|---:|---:|---:|
| base, recognition | 1.000 | 0.998, 0.998 | 0.961, 0.959 |
| base + host_reduce | 1.000 | 1.000, 1.002 | 0.947, 0.948 |
| bound callbacks, recognition | 1.000 | 0.995, 1.001 | **0.916, 0.918** |
| bound callbacks + host_reduce | 1.000 | 0.996, 1.000 | 0.938, 0.945 |

`tests/test_pg_grammar` (`LIME_PG_BENCH=7`, 12 PG queries x 20000
passes, base snapshot, recognition only), 11 rounds interleaved: old
engine 107.8 ms, rebuilt 107.7 (A/A 0.999), after 105.8 (0.981).

Bound callbacks in recognition mode went from +3.9-4.5% over the base to
the base's own cost; every path is faster than the old engine.

## See also

- [RELATED_WORK.md](RELATED_WORK.md) — how this compares to Marpa,
  Silver/ableC, SDF2/SGLR, Copper, and Elkhound.
- [COMPOSITION.md](COMPOSITION.md), [EXTENSIONS.md](EXTENSIONS.md),
  [GLR.md](GLR.md), [CONTEXT_SWITCH.md](CONTEXT_SWITCH.md) — the
  existing mechanisms this proposal builds on.
- [ROADMAP.md](ROADMAP.md) item 1 — the in-process LALR(1) rebuild
  that both Tier 1 and Tier 3 depend on (done).
