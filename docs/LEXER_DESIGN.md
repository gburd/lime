# Lime Lexer Design (v0.1 draft)

**Status.**  This is a v0.1 design draft for review by the PostgreSQL
Lime migration team (P0-NEW-7) and the Lime maintainer.  Not yet
implementation-ready; expect two-to-three rounds of iteration before
the spec stabilizes and compiler work begins.

**Reading order.**  Skim Goals / Non-goals / Architecture first.  The
worked example near the bottom is what determines whether the design
is human-writable; the API and language sections explain how it
desugars.

---

## Goals

A scanner generator that applies Lime's parser philosophy to lexers:

1. **Push-driven.**  Caller feeds bytes; lexer emits tokens via a
   callback.  Mirror of `Parse(parser, token, value, loc)` at the
   byte level.
2. **Reentrant by construction.**  Per-instance state, no globals.
   No `yytext` / `yyleng` / `yylineno` side channels.
3. **Typed everything.**  Rules return typed token payloads.  States
   declare typed local data.  Locations are a typed payload, not a
   global.
4. **First-class state model.**  Exclusive states with optional
   typed local data.  State transitions are typed operations, not
   `BEGIN(STATE)` macro calls.
5. **Compile-time composable rule sets.**  Multi-grammar projects
   share rule sets between scanners (PG's backend SQL + ecpg + psql
   all share standard SQL tokens).
6. **First-class keyword tables.**  Declare a list of keywords; the
   generator picks switch / perfect-hash / trie based on size.
7. **Testable in isolation.**  Per-rule unit tests without spinning
   up the full parser.
8. **Inspectable runtime.**  `lime_lexer_to_text()` round-trips the
   compiled rule set for debugging and audit.

## Non-goals (v1)

These are deferred to v2 or rejected outright.  The list is
explicit so reviewers can argue about specific items without
re-litigating the whole scope.

- **Backwards compat with `.l` files.**  Don't translate; design
  fresh.  PG's hand-rolled scanners are the migration target.
- **Multi-language output.**  C only.  If Lime grows multi-language
  support someday, the lexer follows; but C-first is fine.
- **Runtime grammar modification.**  Mirror the parser's snapshot
  story for v2 when there's a real consumer asking for it.  v1
  ships with a static rule set per scanner.
- **Incremental / suspend-mid-token input.**  PG's existing
  scanners are static-input; ecpg's include directives use a
  buffer stack we'll replicate as `LexInclude`, not stream-feed.
  Network-fed and editor-highlighting use cases defer to v2.
- **REJECT replacement.**  Skip until a concrete grammar needs it.
- **PCRE / capture groups in patterns.**  POSIX-extended subset
  plus character classes.  No back-references, no named captures.
- **Performance parity with flex.**  Match order-of-magnitude;
  flex is faster than most workloads need.  Don't bend over
  backwards.
- **`.l` interop.**  `lime --lex foo.lex` produces `.c` + `.h`,
  not a flex-compatible scanner that some other build step
  consumes.

## Architecture

Mirrors Lime's parser side at every level.  This is not coincidence:
the design's defensibility is "we already proved this model works
for parsers; here it is for lexers."

```
  +---------------+         +---------------+
  |   foo.lex     |  --->   |    lime       |  --->  foo_lex.c
  |   (source)    |         | --lex driver  |        foo_lex.h
  +---------------+         +---------------+
                                    |
                                    |  reads template
                                    v
                            +---------------+
                            |  limpar_lex.c |
                            |  (runtime     |
                            |  template)    |
                            +---------------+

  Build time:                   Run time:

  +---------------+         +-----------------+
  |   foo_lex.c   |  --->   |    yyLexer      |
  +---------------+         |  (per-instance) |
                            |   - state       |
                            |   - buffers     |
                            |   - locations   |
                            +-----------------+
                                    |
                                    |  emit callback
                                    v
                            +-----------------+
                            |   user code     |
                            |  (typically     |
                            |   feeds parser) |
                            +-----------------+
```

The generator (`lime --lex foo.lex`) reads `.lex` source, compiles
patterns into a DFA, and emits a `.c` + `.h` pair via a runtime
template (`limpar_lex.c`, by analogy with `limpar.c` for the
parser).  At runtime the caller allocates a `yyLexer` instance,
feeds it bytes via `LexFeed*` calls, and receives tokens via a
callback whose signature matches the parser's `ParseLoc` exactly,
so the driver loop reduces to one line:

```c
LexFeedBytes(lex, source, len, (LexEmitFn)ParseLoc, parser);
```

## Lexer source language (`.lex`)

The source language uses Lime's existing directive syntax (terminating
periods, `%directive` keywords, `{C code}` blocks).  Authoring a
`.lex` should feel like authoring a `.lime`.

### Directives

| Directive | Meaning |
|-----------|---------|
| `%name_prefix prefix.` | Symbol prefix for emitted functions (mirrors parser). |
| `%token_prefix PREFIX.` | Token-name prefix in emitted header. |
| `%token_type {C type}` | Default type of a token's emitted value. |
| `%location_type {C type}` | Type of source locations (default: `LimeLocation`). |
| `%lexer_extra_argument {C type *name}` | Per-instance opaque user pointer (mirrors `%extra_argument`). |
| `%state NAME.` | Declare an inclusive state. |
| `%state NAME { typed body }.` | Declare a state with typed local data. |
| `%exclusive_state NAME.` | Declare an exclusive state (rules in other states do not match). |
| `%keyword_table NAME (case_insensitive, prefix=K_) { "select", "from", ... }.` | Declare a keyword set. |
| `%ruleset name { ... }.` | Group rules into a named, composable set. |
| `%lexer_include name1, name2, ....` | Include rule sets into the current lexer. |
| `%include { C code }` | C code emitted at file top (mirrors parser). |

### Rules

Each rule has the shape:

```
[<STATE>]  rule_name  matches  /pattern/  { action body }
```

- Optional state qualifier — when present, the rule fires only in
  that state.  When absent, the rule fires in INITIAL.
- `rule_name` is a stable identifier used in trace output and
  introspection (`lime_lexer_to_text`).  Required.
- `/pattern/` is a POSIX-extended regex.  Patterns are delimited by
  `/.../` (no embedded `/` allowed; use `\/`).
- Action body is C code with typed locals bound (see below).

The action body executes in a scope where these locals are live:

| Local | Type | Meaning |
|-------|------|---------|
| `matched` | `const char *` | Pointer into caller's buffer; matched text. |
| `matched_len` | `size_t` | Length of matched text. |
| `loc` | `YYLOCATIONTYPE` | Location of the matched span. |
| `lex` | `yyLexer *` | The lexer instance handle. |
| `extra` | as declared by `%lexer_extra_argument` | User data pointer. |
| `state` | `yyLexState` | Current state code (an enum). |

Action bodies emit tokens by calling `LEX_EMIT(token, value)` (or
`LEX_EMIT_NOVAL(token)`).  State transitions use
`LEX_TRANSITION(NEW_STATE)` — no `BEGIN(...)` macro.

### State-local data access

When a state declares typed local data:

```
%exclusive_state DOLLAR_QUOTED {
    const char *opening_tag;
    size_t      opening_tag_len;
}.
```

Within rules qualified `<DOLLAR_QUOTED>`, the data is reachable as
`state_data` (a pointer to the state's struct).  Setting fields at
state-entry uses an extended transition:

```c
LEX_TRANSITION(DOLLAR_QUOTED, .opening_tag = matched, .opening_tag_len = matched_len);
```

When the state is exited via `LEX_TRANSITION` to a different state,
the struct is zeroed.  No manual cleanup required for plain data;
states with C-pointer-owning fields use a `%state_destructor NAME { ... }`
block (mirrors parser's `%destructor`).

## Runtime C API

Symmetric with the parser API.  `Lex` is renamed by the same
`%name_prefix` machinery as `Parse` (so `%name_prefix Foo` produces
`FooLexAlloc`, `FooLexFree`, etc.).

```c
/* Allocate / free.  Match malloc/free shape from the parser. */
void *LexAlloc(void *(*mallocProc)(size_t));
void  LexFree (void *yyl, void (*freeProc)(void *));

/* Feed input bytes.  emit is called zero-or-more times during the
** call as tokens are recognized.  Returns LEX_OK, LEX_NEED_MORE
** (input ended in mid-token; caller can call LexFeedEOF), or
** LEX_ERROR (unmatched input; the err_state field describes
** what went wrong). */
typedef enum { LEX_OK, LEX_NEED_MORE, LEX_ERROR } LexResult;

typedef void (*LexEmitFn)(void *user_data,
                          int token,
                          const void *token_value,
                          YYLOCATIONTYPE loc);

LexResult LexFeedBytes(void *yyl,
                       const char *bytes, size_t n,
                       LexEmitFn emit, void *user_data);

/* Signal end-of-input.  Forces emit of any in-progress token
** (longest-match completion).  Returns LEX_OK or LEX_ERROR. */
LexResult LexFeedEOF(void *yyl, LexEmitFn emit, void *user_data);

/* State introspection.  Useful for diagnostics. */
int  LexCurrentState(void *yyl);
const char *LexStateName(int state);

/* Buffer stack for include directives.  LexInclude pushes a new
** input source onto the stack; the lexer continues from the new
** source until it hits EOF, then resumes the prior buffer.  This
** replaces flex's yywrap + multi-buffer machinery. */
LexResult LexInclude(void *yyl, const char *bytes, size_t n);

/* Trace output (mirrors ParseTrace). */
void LexTrace(void *yyl, FILE *trace, const char *prompt);
```

### Composition with the parser

```c
/* Parser-side glue: a thin shim that converts the lexer's
** emit-callback signature into ParseLoc(). */
static void emit_to_parser(void *p, int tok,
                           const void *v, YYLOCATIONTYPE loc) {
    ParseLoc(p, tok, *(ParseTOKENTYPE *)v, loc);
}

/* Driver: */
void *lex = FooLexAlloc(malloc);
void *par = FooParseAlloc(malloc);
LexResult r = FooLexFeedBytes(lex, source, len, emit_to_parser, par);
if (r == LEX_OK)
    r = FooLexFeedEOF(lex, emit_to_parser, par);
ParseLoc(par, 0, eof_value, eof_loc);   /* push EOF */
FooLexFree(lex, free);
FooParseFree(par, free);
```

This driver loop replaces the entire `while ((tok = yylex(&val,
&loc, scanner))) yyparse(...)` pattern that every Lime user
hand-writes today.

## Pattern language

POSIX-extended regex subset.  No PCRE-isms.

| Construct | Meaning |
|-----------|---------|
| `abc` | Literal characters |
| `[a-z0-9_]` | Character class |
| `[^...]` | Negated class |
| `.` | Any byte except newline |
| `*`, `+`, `?` | Zero-or-more, one-or-more, optional |
| `{n,m}` | Bounded repetition |
| `()` | Grouping (no capture) |
| `\|` | Alternation |
| `\\` | Escape |
| `\n`, `\t`, `\r`, `\f`, `\v`, `\0` | C-style escapes |
| `\x{hex}` | Hex byte escape |
| `^`, `$` | Beginning / end of input (NOT line — lexers don't track lines specially) |

Notably absent (rejected for v1):

- Back-references (`\1`)
- Named captures (`(?P<name>...)`)
- Lookahead / lookbehind assertions (`(?=...)`, `(?<!...)`)
- Inline flag modifiers (`(?i)...`)

If a real PG scanner needs lookahead (e.g., dollar-quote tag
matching that has to peek ahead to find the closing tag), it is
expressed as a multi-rule state machine, not as a regex assertion.
This matches what hand-rolled scanners do today and keeps the DFA
construction tractable.

### Match disambiguation

Three rules, in priority order:

1. **Longest match wins.**  Standard lex/flex semantics.
2. **Within a length tie, declaration order wins.**  Earlier rules
   in the source file beat later rules.
3. **Across composed rule sets, `%lexer_include` order wins.**
   The first-included set's rules take priority on length ties.

The `lime --lex` tool emits an audit trail in `foo_lex.out` showing
which rule wins each conflict, mirroring the parser's `.out` audit.

## Keyword tables

```
%keyword_table sql_keywords (case_insensitive, prefix=K_) {
    "select", "from", "where", "as", "and", "or", "not",
    "true", "false", "null"
}.

rule sql_word matches /[A-Za-z_][A-Za-z0-9_]*/ {
    int kw = sql_keywords_lookup(matched, matched_len);
    if (kw >= 0)
        LEX_EMIT_NOVAL(kw);
    else
        LEX_EMIT(IDENT, intern(matched, matched_len));
}
```

The generator picks the lookup implementation:

- ≤ 8 entries: linear scan (fastest cache-wise for tiny sets)
- 9 - 64 entries: switch on first char + linear within bucket
- 65 - 512 entries: perfect hash (gperf-style, generated inline)
- > 512 entries: trie

For PG's `kwlist.h` (~440 SQL keywords): perfect hash.  Replaces
the entire `gen_keywordlist.pl` machinery.

## Locations

```
%location_type { struct { int line; int col; size_t byte; } }.
```

Default: `LimeLocation` (a `{start, end}` byte-offset struct
matching the parser's default).  Override is exactly the parser's
`%location_type` mechanism.

Locations are computed by the runtime from the input byte cursor
and are passed to the emit callback alongside the token value.
There is no `yylineno` global.  Line/column tracking, when wanted,
is done by overriding `%location_type` and threading the column
update through a `%location_advance` block (called once per
matched token):

```
%location_advance {
    /* loc, matched, matched_len in scope */
    for (size_t i = 0; i < matched_len; i++) {
        if (matched[i] == '\n') { loc.line++; loc.col = 1; }
        else                     { loc.col++; }
    }
    loc.byte += matched_len;
}
```

This replaces flex's `yylineno` magic with a typed, opt-in,
inspectable computation.

## Composition (compile-time only in v1)

```
%ruleset whitespace_and_comments {
    rule ws         matches /[ \t\r\n]+/        { /* skip */ }
    rule line_comment matches /--[^\n]*/        { /* skip */ }
    rule block_comment matches "..."            { /* nested handling */ }
}.

%ruleset standard_sql_punct {
    rule comma  matches /,/  { LEX_EMIT_NOVAL(COMMA); }
    rule lparen matches /\(/ { LEX_EMIT_NOVAL(LPAREN); }
    /* ... */
}.

%lexer ecpg_pgc {
    %lexer_include whitespace_and_comments,
                   standard_sql_punct,
                   ecpg_directives;
}.
```

Composition is **compile-time only in v1.**  `lime --lex` walks the
included rule sets, merges their rule lists in declaration order,
and builds one DFA from the union.

Conflict policy on composition:

- Patterns in different rule sets that match the same input fire
  in `%lexer_include` order on length ties.
- Two patterns in the *same* rule set that match the same input
  with the same length is an error (the generator refuses to
  build).
- The `.out` audit lists every cross-set conflict and which rule
  wins.

Runtime composition (loading a rule set after lexer creation,
mirroring the parser's snapshot story) is **deferred to v2**.

## Testability

Each rule has a stable name (`rule_name`), and the generator
emits a per-rule test entry point:

```c
/* For each rule emitted by the generator, a callable: */
LexResult Foo_test_rule_sql_word(const char *input, size_t n,
                                 LexEmitFn emit, void *user_data);
```

The test entry point allocates a fresh lexer in INITIAL, feeds the
input, invokes the emit callback once if the named rule fires,
and tears down.  This lets `tests/test_lex_dollar_quote.c` exercise
exactly one rule without spinning up the full grammar.

The Lime tree gains a `tests/test_lex_*.c` family on the same
shape as `tests/test_locations.c` etc.

## Introspection

```c
char *lime_lexer_to_text(const yyLexer *lex);
```

Round-trippable serializer for the lexer's compiled rule set.
Mirrors `lime_modifications_to_grammar_text` for the parser.
Output is a `.lex`-syntax text dump suitable for diff-ing two
lexer builds or feeding to a third-party tool.

## Build integration (meson)

```meson
foo_lex_gen = custom_target('foo_lexer',
  input   : ['foo.lex'],
  output  : ['foo_lex.c', 'foo_lex.h'],
  command : [
    lime_exe,
    '--lex',
    '-T' + meson.project_source_root() / 'limpar_lex.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)
```

When a target uses both a `.y` parser and a `.lex` lexer, the
generator emits the parser-side glue automatically (the
`emit_to_parser` shim shown above is generated, not hand-written).
A combined target `lime --combined foo.y foo.lex` produces a
single coordinated parser+lexer pair plus a default driver.

## Worked example: bootscanner.l (165-line PG scanner) in Lime

PG's bootscanner.l is the simplest of the 17 hand-rolled scanners:
no exclusive states, ~30 keywords, identifiers, single-quoted
strings, and comments.  Here is what it looks like as a `.lex`:

```
%name_prefix     boot
%token_prefix    BOOT_
%token_type      { union BootValue }
%location_type   { LimeLocation }

%include {
    #include "postgres.h"
    #include "bootstrap/bootstrap.h"
    #include "bootparse.h"

    union BootValue {
        const char *kw;
        char       *str;
    };
}

%keyword_table boot_kw (case_sensitive, prefix=K_) {
    "open", "close", "create", "OID", "bootstrap",
    "shared_relation", "rowtype_oid", "insert", "declare",
    "build", "indices", "unique", "index", "on", "using",
    "toast", "FORCE", "NOT", "NULL"
}.

rule whitespace  matches /[ \t\r]+/   { /* skip */ }
rule newline     matches /\n/         { /* skip; line bookkeeping via
                                         %location_advance */ }
rule line_comment matches /^#[^\n]*/  { /* skip */ }

rule comma   matches /,/   { LEX_EMIT_NOVAL(BOOT_COMMA); }
rule equals  matches /=/   { LEX_EMIT_NOVAL(BOOT_EQUALS); }
rule lparen  matches /\(/  { LEX_EMIT_NOVAL(BOOT_LPAREN); }
rule rparen  matches /\)/  { LEX_EMIT_NOVAL(BOOT_RPAREN); }

rule null_literal matches /_null_/ {
    LEX_EMIT_NOVAL(BOOT_NULLVAL);
}

rule ident matches /[-A-Za-z0-9_]+/ {
    int kw = boot_kw_lookup(matched, matched_len);
    if (kw >= 0) {
        union BootValue v;
        v.kw = boot_kw_name(kw);   /* interned */
        LEX_EMIT(kw, &v);
    } else {
        union BootValue v;
        v.str = pstrdup_n(matched, matched_len);
        LEX_EMIT(BOOT_ID, &v);
    }
}

rule sqstring matches /'([^']|'')*'/ {
    union BootValue v;
    v.str = boot_deescape_quoted(matched, matched_len);
    LEX_EMIT(BOOT_ID, &v);
}

rule unexpected matches /./ {
    boot_lex_error(loc, matched, matched_len);
}
```

**Bookkeeping check**:  the original `bootscanner.l` is 165 lines
(rules section ~80 lines, plus options block, %top, yyalloc/yyfree
shims, and prologue/epilogue).  The Lime equivalent above is ~50
lines and has zero of the flex options-block ceremony, no
yyalloc/yyfree shims, no `%option reentrant`/`bison-bridge`/
`never-interactive` declarations.  The keyword set is a real
data structure rather than 19 individual rules.  Identifier
disambiguation against keywords is one rule with a table lookup,
not 19 rules in declaration order.

## Worked example: a stateful scanner (jsonpath_scan.l shape)

PG's jsonpath_scan.l has 4 exclusive states (`xq`, `xnq`, `xvq`,
`xc`) for quoted strings, non-quoted variable names, variable
quoting, and C-style comments.  Sketched form:

```
%name_prefix     jsonpath
%location_type   { LimeLocation }

%exclusive_state QUOTED {
    char *buffer;       /* accumulating string contents */
    size_t buffer_len;
    size_t buffer_cap;
}.

%exclusive_state COMMENT.

rule quote_open matches /"/ {
    LEX_TRANSITION(QUOTED, .buffer = palloc(64),
                           .buffer_len = 0,
                           .buffer_cap = 64);
}

<QUOTED> rule quote_char matches /[^"\\]+/ {
    /* state_data->buffer / buffer_len / buffer_cap accessible */
    jsonpath_buffer_append(state_data, matched, matched_len);
}

<QUOTED> rule quote_escape matches /\\./ {
    char c = jsonpath_unescape(matched[1]);
    jsonpath_buffer_append(state_data, &c, 1);
}

<QUOTED> rule quote_close matches /"/ {
    LEX_EMIT(JP_STRING, state_data->buffer);
    LEX_TRANSITION(INITIAL);
}

%state_destructor QUOTED {
    /* called if the lexer is freed mid-state */
    if (state_data->buffer) pfree(state_data->buffer);
}

rule comment_open matches "/\\*" {
    LEX_TRANSITION(COMMENT);
}

<COMMENT> rule comment_close matches "\\*/" {
    LEX_TRANSITION(INITIAL);
}

<COMMENT> rule comment_body matches /([^*]|\*[^\/])+/ {
    /* skip */
}
```

**Wins over flex**:

- State-local `buffer`/`buffer_len`/`buffer_cap` are typed struct
  fields, not `yyextra` slots smuggled through a thread-unsafe
  global.
- `LEX_TRANSITION(QUOTED, .buffer = ..., ...)` is a single typed
  initialiser, replacing the flex `BEGIN(xq); state.buffer = ...`
  two-step where the second step's name and side effects are
  invisible to the generator.
- `%state_destructor` is invoked automatically on premature lexer
  free, so a half-quoted string doesn't leak.  Flex has no
  equivalent.

## Implementation plan

Five milestones.  Each is independently testable and can land as
a separate commit series on `main`.

### M1 — Source language frontend (3-4 weeks)

- Tokenizer for `.lex` syntax (fork from `tokenize.c`).
- AST types for rules, states, keyword tables, ruleset blocks.
- Pattern parser (POSIX-extended regex → AST).
- No DFA construction yet; just parse-and-print round-trip.
- **Test gate**: `lime --lex --syntax-only foo.lex` round-trips
  all 6 PG scanners' converted forms with no semantic loss.

### M2 — DFA compiler (4-6 weeks)

- NFA construction from regex AST (Thompson).
- Subset construction NFA → DFA.
- Hopcroft minimization.
- Action attachment to accepting states.
- Per-state DFAs for exclusive states (avoid combined-state
  blowup; cap each DFA at ~10k states).
- **Test gate**: `tests/test_lex_dfa_*` exercise each construction
  step on small grammars; total DFA states across all 6 PG
  scanners stay within 10× the rule count.

### M3 — Runtime template + emit pipeline (2-3 weeks)

- `limpar_lex.c` template (analog of `limpar.c`).
- `LexAlloc`/`LexFree`/`LexFeedBytes`/`LexFeedEOF`/`LexInclude`.
- Tokenize-and-emit hot path.
- Trace machinery (`LexTrace`).
- **Test gate**: bootscanner.l-equivalent passes its own
  regression test suite via Lime; ASan/UBSan/valgrind clean.

### M4 — Composition + introspection + testability (2-4 weeks)

- `%ruleset` definition, `%lexer_include` resolution.
- Conflict detection on cross-set length ties.
- `.out` audit emission.
- `lime_lexer_to_text` serializer.
- Per-rule test entry points.
- **Test gate**: `whitespace_and_comments` ruleset + `boot_punct`
  ruleset compose into a working bootscanner-equivalent;
  `lime_lexer_to_text` round-trips.

### M5 — PG integration + docs (2-3 weeks)

- One PG scanner ported end-to-end (target: bootscanner.l, the
  simplest).
- `docs/MIGRATION_FROM_FLEX.md`.
- Man page expansion (`man/lime_lex.5`).
- meson custom-target recipe + worked example.
- **Ship gate**: PG's bootscanner port replaces the flex
  bootscanner.l in their tree, all PG meson tests still pass.

**Cumulative effort**: 13-20 weeks focused work.  Calendar with
other commitments and design iteration: 5-7 months realistic, 9
months pessimistic.

## What's deferred to v2

Explicitly out of scope, with rationale, so reviewers can argue
about specific items rather than the whole boundary.

| Feature | Why deferred |
|---------|--------------|
| Runtime rule-set composition | Mirror parser snapshot story; need a second consumer beyond PG before paying for the CoW machinery. |
| Incremental / suspend-mid-token input | Forces every component's design; no PG scanner needs it for the migration; psql streaming and editor highlighting can drive v2. |
| REJECT replacement | Flex feature most grammars never use; defer until a concrete scanner needs it. |
| PCRE-style assertions, captures | Locks in a specific regex flavor; multi-rule state machines cover the same use cases without DFA construction risk. |
| Multi-language output | C-first.  Whole project is C-first today. |
| `.l` file interop | Don't translate; design fresh.  Migration team converts grammar by grammar. |
| Performance benchmarks vs flex | Match order-of-magnitude.  Detailed perf work after correctness. |

## Open questions for review

These are the design choices I am least confident about.  PG team
input solicited.

1. **State-local data initialization syntax.**  `LEX_TRANSITION(STATE,
   .field = value, ...)` uses C99 designated initialisers in a
   macro arg, which means the macro has to do something clever
   (e.g., `do { yyLexState_##STATE_data tmp = { __VA_ARGS__ };
   /* assign */ } while (0)`).  Workable but ugly.  Alternative:
   require an explicit `state_data->field = value` in the action
   body before `LEX_TRANSITION`.  Less elegant, less footgun.

2. **`%location_advance` granularity.**  Once per matched token
   is what flex does (yytext-then-update).  An alternative is
   once per byte; that's slower but lets line/column tracking
   handle embedded newlines in multi-line tokens cleanly.  Need
   PG's input on whether the once-per-token model is sufficient
   for ecpg's source-text accumulation.

3. **`%lexer_include` order semantics.**  Currently: first include
   wins on length ties.  Alternative: explicit priority hint per
   include (`%lexer_include set1 priority=10, set2 priority=5`).
   PG's input: do any of the existing scanners need priority
   hints, or is declaration order enough?

4. **Comment nesting.**  PG's scan.l supports nested SQL
   comments (`/* /* nested */ still in comment */`).  This is a
   counter to a context-free language; the standard answer is
   "use a counter in state-local data."  Sketch:

   ```
   %exclusive_state COMMENT { int depth; }.

   rule comment_open  matches "/\\*"  { LEX_TRANSITION(COMMENT, .depth = 1); }
   <COMMENT> rule nest_open  matches "/\\*"  { state_data->depth++; }
   <COMMENT> rule nest_close matches "\\*/"  {
       if (--state_data->depth == 0) LEX_TRANSITION(INITIAL);
   }
   <COMMENT> rule body matches /[^/*]+/ { /* skip */ }
   ```

   Confirm this shape works for PG before finalizing.

5. **Dollar-quote tag matching (pgc.l's hardest pattern).**
   `$tag$ ... $tag$` requires capturing the opening tag and
   matching it against later candidate-close patterns.  Can be
   done with state-local data:

   ```
   %exclusive_state DOLLAR_QUOTED { char *tag; size_t tag_len; }.

   rule dq_open matches /\$([A-Za-z_][A-Za-z0-9_]*)?\$/ {
       LEX_TRANSITION(DOLLAR_QUOTED,
                      .tag = pnstrdup(matched + 1, matched_len - 2),
                      .tag_len = matched_len - 2);
   }

   <DOLLAR_QUOTED> rule dq_close_candidate
     matches /\$([A-Za-z_][A-Za-z0-9_]*)?\$/ {
       if (matched_len - 2 == state_data->tag_len &&
           memcmp(matched + 1, state_data->tag, state_data->tag_len) == 0) {
           LEX_EMIT(SCONST, current_buffer());
           LEX_TRANSITION(INITIAL);
       } else {
           buffer_append(matched, matched_len);
       }
   }

   <DOLLAR_QUOTED> rule dq_body matches /[^$]+/ {
       buffer_append(matched, matched_len);
   }
   ```

   This is more code than flex's pgc.l version (which uses
   inline C in the action body to track the tag).  Trade-off:
   the Lime version puts the tag in a typed, inspectable
   field; the flex version smuggles it through `xcdepth` and
   friends.  Confirm the trade-off with PG before finalizing.

## What I need from PG to harden this draft

This is the (A)/(B)/(C) ask from Lime-Reply-7.txt restated against
this draft:

- (A) **Permalinks to the 17 hand-rolled scanners**, with
  one-paragraph notes per scanner on what made each one hard.
  I'll diff each against the draft above and flag every place
  the draft cannot express what the scanner needs.

- (B) **Confirm or revise the worked examples** (bootscanner.l
  and jsonpath-shape).  If either reads as worse than flex,
  we iterate; if either reads as cleaner, the design holds.
  Pick one or two of YOUR scanners and rewrite them in this
  draft's syntax.

- (C) **meson integration shape**.  Where would the custom_target
  recipe slot in?  Does PG's tree need `lime --combined` or are
  separate `.y` and `.lex` targets fine?  Single output file
  per scanner or pair?

When (A)/(B)/(C) come back I'll fold the feedback into a v0.2
draft and we move to compiler implementation (M1).

---

*v0.1 draft authored 2026-05-15 by Greg Burd.  Iteration tracking:
none yet; bump version on substantive changes.*
