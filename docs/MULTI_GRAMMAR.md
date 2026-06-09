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

## See also

- [EXTENSIONS.md](EXTENSIONS.md) — loading grammars at runtime.
- [CONTEXT_SWITCH.md](CONTEXT_SWITCH.md) — the *different* mechanism
  for *nesting* a sub-language (JSON in SQL) via boundary triggers.
  Use context-switch for embedded literals; use the admissibility
  oracle for keyword collisions between grammars that share a start
  context.
- `src/strategy_fork_resolve.c` — the fork engine for genuine
  ambiguity (the `delete` case above).
- `tests/test_admissibility.c` — the oracle's regression suite.
