# Lime Lexer — PG Scanner Audit (M0 evidence base)

Empirical audit of the six PostgreSQL flex scanners that exercise
the `.l` design space.  Performed by the Lime maintainer to harden
`docs/LEXER_DESIGN.md` v0.1 against real-world idioms before
committing to the spec or any compiler code.  This is the
gate-1(A) deliverable I had asked PG for in Lime-Reply-7.txt,
done preemptively from our side rather than waiting on theirs.

**Method.**  Each scanner read end-to-end (small ones in full;
large ones via targeted skim of declarations, rules section, and
greps for high-friction idioms: `BEGIN`, `yyless`, `yymore`,
`REJECT`, `<<EOF>>`, `yyextra`, `yyterminate`).  Each idiom is
mapped to v0.1's expressivity; gaps roll into the v0.2 spec.

**Source tree.**  `~/ws/postgres/adaptive-buffer-mgmt` (PG branch
`master` near commit `e0feaaa4d16`, the same tree the migration
team works against).

**Status.**  Audit complete; v0.1 design has 10 gaps requiring
v0.2 revision.  None of the gaps invalidate the architecture —
they all extend the source language or runtime API in
backwards-compatible ways.

## The six scanners

| Scanner | LOC | Excl. states | Role |
|---------|-----|--------------|------|
| `bootscanner.l` | 165 | 0 | Bootstrap-mode parser (initdb).  Stateless; pure keyword-vs-ident dispatch + single-quoted strings. |
| `repl_scanner.l` | 350 | 2 (`xq`, `xd`) | Replication protocol.  SQL-style strings + delimited identifiers; everything else is keywords + punct. |
| `exprscan.l` | 445 | 1 (`EXPR`) | pgbench expression evaluator.  Caller-controlled state (psql streams between INITIAL and EXPR for different command parts). |
| `jsonpath_scan.l` | 741 | 4 (`xq`, `xnq`, `xvq`, `xc`) | JSON path syntax.  Quoted strings, non-quoted variable names, variable quoting, C-style comments. |
| `scan.l` | 1487 | 11 (`xb` `xc` `xd` `xh` `xq` `xqs` `xe` `xdolq` `xui` `xus` `xeu`) | Backend SQL.  Every string flavor SQL has, plus dollar-quoted strings with tag matching, plus Unicode-escape sequences with surrogate-pair support. |
| `pgc.l` | 1895 | 16 SQL + 6 ecpg overlay (`C` `SQL` `incl` `def` `def_ident` `undef`) | ecpg preprocessor.  Everything `scan.l` does, plus a separate ecpg-directive language layered on top via state mode-switching. |

Total: ~5,300 lines of hand-rolled flex, 6 scanners, ~50 distinct
exclusive states across the corpus (with significant overlap —
all the SQL scanners share the `xq`/`xd`/`xh`/`xb` quartet
roughly).

## Idioms inventory

For each idiom I observed, what it is, where I saw it, and how
v0.1 handles it (if at all).

### A. Pattern-definition shorthand (`{name}` interpolation)

Flex lets the file's definitions section bind names to regex
fragments:

```
digit       [0-9]
hexdigit    [0-9A-Fa-f]
ident_start [A-Za-z\200-\377_]
ident_cont  [A-Za-z\200-\377_0-9\$]
identifier  {ident_start}{ident_cont}*
```

Rules then write `{identifier}` to splice the fragment.  `scan.l`
has ~30 such definitions with three layers of nesting (e.g.
`identifier` references `ident_start` and `ident_cont`).

**v0.1 status.**  No equivalent.  Patterns are inline in rule
bodies.  Re-writing `scan.l` in v0.1 would inline the
definitions into every rule, bloating the file 5-10× and
losing the structural comments that group related patterns.

**Verdict.**  HARD GAP.  v0.2 must add `%pattern name /regex/.`
declarations and `{name}` interpolation in rule patterns.

### B. State-qualified rule blocks (`<STATE>{ ... }`)

Flex allows grouping multiple rules under a single state
qualifier:

```
<EXPR>{
    "+"     { return '+'; }
    "-"     { return '-'; }
    "*"     { return '*'; }
    /* ... 30 more rules ... */
}
```

`exprscan.l` uses one ~80-rule block.  `scan.l` uses several
state blocks of varying size.  The shorthand makes it easy to
see at a glance which rules belong to which state.

**v0.1 status.**  Per-rule `<STATE>` prefix only.  Rewriting in
v0.1 means repeating `<EXPR>` 80 times.

**Verdict.**  HARD GAP.  v0.2 must add `<STATE> { rule ... }`
block syntax.

### C. Multi-state rule qualifier

`pgc.l` has rules active in several states simultaneously:

```
<xq,xqc,xe,xn,xus><<EOF>>   { mmfatal(...); }
```

Eleven such multi-state qualifiers in pgc.l, mostly grouping
"all the string-flavor states" under one EOF or one error
fallthrough.

**v0.1 status.**  Single-state qualifier per rule.

**Verdict.**  HARD GAP.  v0.2 must accept comma-separated
state lists in `<...>`.

### D. `<<EOF>>` per-state actions

Flex's `<<EOF>>` rule fires when end-of-input arrives in a
specific state.  PG uses this extensively to detect "unterminated
X at EOF".  Counts:

| Scanner | `<<EOF>>` rules |
|---------|-----------------|
| jsonpath_scan.l | 4 |
| scan.l | 9 |
| pgc.l | 13 |

Typical pattern:

```
<xq,xqc,xe,xn,xus><<EOF>>   { mmfatal(PARSE_ERROR, "unterminated quoted string"); }
<xh><<EOF>>                 { mmfatal(PARSE_ERROR, "unterminated hexadecimal string literal"); }
<xdolq><<EOF>>              { mmfatal(PARSE_ERROR, "unterminated dollar-quoted string"); }
```

**v0.1 status.**  `LexFeedEOF` returns `LEX_OK`/`LEX_ERROR` but
has no per-state action hook.  No way to say "if EOF arrives in
xq, run THIS code."

**Verdict.**  HARD GAP.  v0.2 must add `<STATE><<EOF>> { ... }`
rule form.  Default behavior in INITIAL state is `LEX_OK`;
default in any exclusive state without an EOF rule is
`LEX_ERROR` (current behavior).

### E. `yyless(n)` — pushback into the input stream

Flex's `yyless(n)` rewinds the input cursor so that the trailing
`yyleng - n` bytes of the just-matched text are re-presented to
the next lex call.  Counts:

| Scanner | `yyless` calls |
|---------|----------------|
| repl_scanner.l | 2 |
| scan.l | ~25 |
| jsonpath_scan.l | 2 |
| pgc.l | ~40 |

Typical pattern: a rule matches a prefix-greedy token, then
inspects content and calls `yyless(K)` to "un-match" some
trailing bytes.  Used heavily in scan.l for things like
`scientific notation that turned out not to be one — back up
to the digit boundary`.

**v0.1 status.**  No equivalent.  `LEX_PUSHBACK(n)` is mentioned
nowhere in the spec.

**Verdict.**  HARD GAP.  v0.2 must add `LEX_PUSHBACK(n)` action
primitive.  Cursor semantics: "un-consume" the last `matched_len -
n` bytes of `matched`; the next lex iteration sees them again
from the top.

### F. Caller-controlled starting state

`exprscan.l` has this idiom at the top of `yylex()`:

```c
%{
    PsqlScanState cur_state = yyextra;
    BEGIN(cur_state->start_state);   /* caller chose: INITIAL or EXPR */
    last_was_newline = false;
%}
```

The pattern is "the caller embeds a starting-state choice in the
extra-data struct; the lexer entry block reads it and switches".
This is how psql streams a single input through the same scanner
in two different "modes" (backslash-command-arg lexing vs SQL
expression lexing).

**v0.1 status.**  Lexer state is per-instance and persists across
calls; no "caller picks a start state for THIS feed call" API.

**Verdict.**  MEDIUM GAP.  v0.2 must add `LexSetState(yyl, state)`
runtime API.  Action bodies can already use `LEX_TRANSITION`;
this is the same primitive exposed for caller use.

### G. Token integer literals (char codes as token codes)

`exprscan.l`, `scan.l`, others return character literals as
token codes:

```c
"+"     { return '+'; }
"("     { return '('; }
","     { return ','; }
```

i.e., the parser's `%token` declaration is `'+' '(' ','`.

**v0.1 status.**  `LEX_EMIT_NOVAL(NAMED_TOKEN)`.  Nothing forbids
`LEX_EMIT_NOVAL('+')`, but it isn't documented.

**Verdict.**  SOFT GAP.  v0.2 documents that token codes are
`int` and char literals are valid arguments to the emit macros.

### H. `<STATE>` introspection from action bodies

Several scanners read the current state inside an action:

```c
yyextra->state_before_str_stop = YYSTATE;
BEGIN(xqs);
...
BEGIN(yyextra->state_before_str_stop);   /* restore */
```

`scan.l` does this for the "string-quote continuation" optimization
(detecting `'foo'   'bar'` as one string with the space treated
as a comment).

**v0.1 status.**  Action bodies have `state` (the current state
code) as a typed local.  ✓

**Verdict.**  COVERED.  Confirm in v0.2 that `state` is an
lvalue-equivalent for save/restore.  Saved-state restoration
uses `LEX_TRANSITION(saved)` where `saved` is a regular C
variable; no special API needed.

### I. Literal-buffer accumulation pattern

Four of the six scanners (every one with quoted strings) define
this set of helpers in C:

```c
#define startlit()  ( yyextra->literallen = 0 )
static void  addlit(char *ytext, int yleng, yyscan_t yyscanner);
static void  addlitchar(unsigned char ychar, yyscan_t yyscanner);
static char *litbufdup(yyscan_t yyscanner);
```

Plus the underlying allocation logic (grow on demand, palloc
context, etc.).  This is ~80-150 lines of boilerplate per
scanner, every one of which implements approximately the same
thing.

**v0.1 status.**  Sketched as state-local data fields (the
jsonpath worked example's `buffer`/`buffer_len`/`buffer_cap`).
Authors still write the resize logic.

**Verdict.**  MEDIUM GAP.  v0.2 should add a first-class
`%literal_buffer NAME { type, palloc/realloc/free hooks }`
directive that emits the equivalent of startlit/addlit/
addlitchar/litbufdup operations.  Big ergonomic win across all
SQL-shaped grammars.

### J. `yyterminate()` — abort lexing without emit

`pgc.l` uses `yyterminate()` to stop lexing entirely (different
from emitting an EOF token):

```
<undef>{other}|\n {
    yyterminate();
}
```

**v0.1 status.**  No primitive.

**Verdict.**  SOFT GAP.  v0.2 adds `LEX_TERMINATE()` macro that
causes the current `LexFeedBytes` call to return `LEX_OK` with
no further emits.  Distinct from emitting EOF (which would
cause the parser to see the EOF token).

### K. Comment nesting via state-local depth counter

`scan.l`'s `xc` state uses `yyextra->xcdepth` to count nested
`/* ... */`:

```c
yyextra->xcdepth = 0;
BEGIN(xc);
...
<xc>"/*"   { (yyextra->xcdepth)++; ... }
<xc>"*/"   { if (yyextra->xcdepth <= 0) BEGIN(INITIAL); else (yyextra->xcdepth)--; }
```

**v0.1 status.**  Sketched in v0.1's "Open question 4" with a
typed state-local `int depth` field.  ✓

**Verdict.**  COVERED.  Confirm in v0.2 by including the
worked example.

### L. Dollar-quoted-string tag matching

`scan.l` and `pgc.l` implement PG's `$tag$ ... $tag$` syntax
where the closing `$tag$` must literally match the opening tag:

```c
<xdolq>{dolqdelim}   {
    if (strcmp(yytext, yyextra->dolqstart) == 0) {
        pfree(yyextra->dolqstart);
        yyextra->dolqstart = NULL;
        BEGIN(INITIAL);
        yylval->str = litbufdup(yyscanner);
        return SCONST;
    } else {
        addlit(yytext, yyleng, yyscanner);
    }
}
```

**v0.1 status.**  Sketched in v0.1's "Open question 5" with
typed state-local `char *tag` + `size_t tag_len`.  ✓

**Verdict.**  COVERED.  Confirm in v0.2 by including the worked
example with the state-local-data syntax.

### M. ecpg-overlay state mode-switching

`pgc.l` has 16 SQL exclusive states PLUS 6 ecpg-overlay states
(`C` `SQL` `incl` `def` `def_ident` `undef`).  The ecpg
preprocessor switches between "lexing C source" and "lexing SQL
embedded in C" by toggling the C/SQL outer state, with the
SQL-quoted-string states layered underneath.  Switching states
is done via `BEGIN()` calls keyed off seeing `EXEC SQL` etc.

**v0.1 status.**  Sketched as composable `%ruleset` (sketch 4
in PG's letter), but compile-time only.  pgc.l's pattern is
"two distinct rule-sets coexist, the active one is selected
dynamically".

**Verdict.**  MEDIUM GAP, but addressable.  Two options for v0.2:

  - **Option α**: pgc.l-shape is "one big lexer with 22 exclusive
    states"; the ecpg-overlay states are just additional states
    in a single rule set.  No new feature needed; the runtime
    handles the 22-state DFA.

  - **Option β**: introduce "active rule set" as a runtime
    concept; switching changes which subset of rules is live.
    This is sketch 9b (runtime composition) in disguise and
    explicitly v2.

I prefer **option α** for v1.  It works, fits the existing model,
and the semantics are simple.  v2 can revisit when there's a
real need to swap rule sets atomically.

### N. Include directive / buffer stack

`pgc.l` implements `EXEC SQL INCLUDE filename;` via:

```
<incl>\<[^\>]+\>{space}*";"?    { parse_include(); }
<incl>{dquote}{xdinside}{dquote}{space}*";"?  { parse_include(); }
<incl>[^;\<\>\"]+";"             { parse_include(); }
```

`parse_include()` is a C function that opens the file, calls
`yy_create_buffer`, and pushes onto flex's buffer stack.  Flex's
`yywrap` returns 0 to pop back when the included file ends.

`psqlscan.l` uses `yy_scan_buffer` similarly for re-lexing
expanded variable references.

**v0.1 status.**  `LexInclude(yyl, bytes, n)` API sketched.
Replaces flex's buffer stack.

**Verdict.**  COVERED at the API level, but the stack management
needs detail in v0.2: how does the lexer know when to pop?  My
proposal: `LexInclude` pushes; the runtime tracks include
depth; on EOF in an included buffer the runtime pops
automatically and resumes the parent.  No `yywrap`-equivalent
needed at the user level.

## Idioms NOT seen (rejecting in v1 with confidence)

Things flex supports that I expected to find used and didn't:

- **`yymore()`** — accumulating successive matches into one
  yytext.  Zero uses across all 6 scanners.  Skip.
- **`REJECT`** — alternative-pattern fallthrough.  Zero uses.
  Skip.
- **`yy_push_state` / `yy_pop_state`** — flex's own state stack
  primitive (vs hand-rolled save_state in yyextra).  Zero uses.
  Skip.  Hand-rolled save-and-restore is the documented PG
  pattern.
- **Trailing context (`pattern/lookahead`)** — the `pattern1/pattern2`
  flex syntax that asserts pattern2 follows pattern1 without
  consuming it.  Zero uses across the audit corpus.  Multi-rule
  state machines or `yyless` cover the same ground.
- **PCRE features** — back-references, named captures, lookahead
  assertions, inline flags.  Zero uses.

These were already in the v0.1 non-goals list; the audit
confirms.

## Gap summary (v0.1 → v0.2 deltas)

In priority order for the v0.2 design draft.

| # | Gap | Severity | Where to add |
|---|-----|----------|--------------|
| A | `%pattern` definitions + `{name}` interpolation | HARD | New directive; pattern parser |
| B | `<STATE> { rule ... }` block syntax | HARD | Rule parser |
| C | Comma-separated multi-state qualifier | HARD | Rule parser |
| D | `<STATE><<EOF>>` per-state EOF rule | HARD | Rule parser + runtime |
| E | `LEX_PUSHBACK(n)` action primitive | HARD | Action body locals + runtime cursor mgmt |
| F | `LexSetState(yyl, state)` API | MEDIUM | Runtime |
| G | Token codes are `int`; char literals OK | SOFT | Documentation |
| H | `state` lvalue + save/restore pattern | COVERED | Documentation |
| I | `%literal_buffer NAME` directive | MEDIUM | New directive; emits start/append/take ops |
| J | `LEX_TERMINATE()` action primitive | SOFT | Action body API |
| K | Comment nesting via state-local depth | COVERED | Worked example in v0.2 |
| L | Dollar-quoted tag matching | COVERED | Worked example in v0.2 |
| M | ecpg-overlay states | COVERED (option α) | Architecture note in v0.2 |
| N | `LexInclude` stack management detail | partial | API docs in v0.2 |

Five HARD gaps (A, B, C, D, E) require source-language additions
that affect the M1 milestone (frontend + AST shape).  The
remainder are MEDIUM/SOFT and can land alongside or after M1.

## Effect on the implementation plan

The v0.1 plan had M1 at 3-4 weeks for "tokenizer + AST + pattern
parser, no DFA construction yet".  Adding gaps A through E:

- A (`%pattern` definitions): +3-5 days.  Requires resolution
  pass (substitute fragments before regex compilation) and
  cycle-detection.
- B (block syntax): +1-2 days.  Pure parser change.
- C (multi-state qualifier): +1 day.  Pure parser change.
- D (`<<EOF>>` rules): +2-3 days.  Parser + AST flag for
  EOF-rule disposition + runtime hook.
- E (`LEX_PUSHBACK`): +3-5 days.  Runtime cursor semantics
  need to be re-examined; pushback interacts with longest-match
  bookkeeping and the M2 DFA construction.

Adjusted M1 budget: **5-6 weeks** (was 3-4).  M2 budget grows
by ~1 week to handle pushback's interaction with the DFA.

Total v1 budget revision: **15-22 weeks** focused (was 13-20).
Calendar: **6-8 months realistic, 9-10 months pessimistic**.

## Conclusion

v0.1's architecture survives the audit unchanged.  No sketch
needs to be reconsidered or dropped.  But the source language
needs five additions (A-E) before it can express the existing
PG scanners faithfully.  All five are routine extensions, none
introduce design risk.

v0.2 of `docs/LEXER_DESIGN.md` will fold in gaps A-E with their
syntax sketches and worked examples; gaps F, I, J get one-line
spec additions; the COVERED gaps (G, H, K, L, M, N) get
documentation upgrades.

After v0.2 is committed, the next gate is PG's own A/B/C
deliverable (their gate-1 ask).  Their A overlaps with this
audit; mostly they need to confirm they agree with the gap
list and provide their own worked example (B) in the v0.2
syntax.

---

*Audit performed 2026-05-15 by Greg Burd against PG branch
master (~e0feaaa4d16).  6 scanners, ~5,300 LOC, all states
catalogued, all hard idioms classified.*
