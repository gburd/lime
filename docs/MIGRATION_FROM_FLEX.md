# Migrating from Flex to Lime

This guide walks through the process of converting a flex scanner file
(`.l` or `.lex`) to a Lime lexer file (`.lex`).  It covers directive
mapping, action-body translation, state declarations, pattern syntax
differences, and common pitfalls, with worked examples drawn from
PostgreSQL's `bootscanner.l` and `jsonpath_scan.l`.

The companion document for porting parsers is
[MIGRATION_FROM_BISON.md](MIGRATION_FROM_BISON.md).  Read both if you are
porting a flex+bison pair.

## Overview

Lime's lexer subsystem is a from-scratch design, not a flex translator.
It does not consume `.l` files; you rewrite the scanner in `.lex` syntax.
Most flex idioms have direct equivalents.  A few (REJECT, yymore, trailing
context, PCRE features) have no equivalent and the design rejects them as
out-of-scope for v1; flex's `.l` corpus across the PostgreSQL tree (six
scanners, ~5,300 lines, audited in `LEXER_SCANNER_AUDIT.md`) shows zero
uses of any of them, so this is not a hardship.

The mechanical work per scanner is roughly:

1. Replace the flex `%option` block with Lime declarations
   (`%name_prefix`, `%pattern`, `%state`, `%exclusive_state`).
2. Translate each rule's action body.  `yytext`/`yyleng` become
   `matched`/`matched_len`; `BEGIN(STATE)` becomes
   `LEX_TRANSITION(STATE)`; `return TOK` becomes `LEX_EMIT(TOK)`;
   `yyless(n)` becomes `LEX_PUSHBACK(matched_len - n)`.
3. Replace the C prologue/epilogue (`yyalloc`, `yyfree`, `yyerror`)
   with the Lime runtime API surface (`LexAlloc(palloc)`,
   `LexFree(yyl, pfree)`, `LexErrorMessage`).
4. Rewrite the driver: replace the `while ((tok = yylex(...)))` loop
   with a single `LexFeedBytes(...)` call whose emit callback feeds
   the parser.

There is no automated translator and there will not be one.  The cost of
writing a translator that handles flex's full surface area is higher than
porting the ~17 PG scanners by hand, and the result would be an
unmaintainable mess.

## Directive Mapping

| Flex | Lime | Notes |
|------|------|-------|
| `%option reentrant` | (default) | Lime lexers are always reentrant. |
| `%option bison-bridge` | (default) | Lime emits a typed callback, not yylval/yyloc globals. |
| `%option 8bit` | (default) | Lime treats input as 8-bit bytes. |
| `%option never-interactive` | (default) | Lime is push-driven; "interactive" doesn't apply. |
| `%option nodefault` | (default) | Unmatched input always returns `LEX_ERROR`. |
| `%option noinput` / `nounput` | (default) | No `input()`/`unput()` macros exist. |
| `%option noyywrap` | (default) | No yywrap; EOF auto-pops include frames. |
| `%option noyyalloc` / `noyyrealloc` / `noyyfree` | (default) | Caller passes `mallocProc`/`freeProc` to `LexAlloc`/`LexFree`. |
| `%option warn` | (default) | Compile-time conflict reports are always emitted. |
| `%option prefix="foo"` | `%name_prefix Foo.` | Both controls the symbol prefix.  Note the period and case convention: Lime canonicalises to `Foo` -> `FooLexAlloc`, etc. |
| `name regex` (definitions section) | `%pattern name /regex/.` | Reference as `{name}` in patterns. |
| `%x STATE` | `%exclusive_state STATE.` | Period required.  Optional `{ typed body }` for state-local data. |
| `%s STATE` | `%state STATE.` | Inclusive state. |
| `%top { ... }` | `%include { ... }` | C code emitted at the top of the generated file. |
| `%{ ... %}` | `%include { ... }` | Same as above; flex distinguishes the two only by where flex emits them.  Lime has one slot. |
| (no flex equivalent) | `%keyword_table NAME (...) { ... }.` | First-class keyword set; generator picks switch / perfect-hash / trie based on size.  See "Keyword tables" below. |
| (no flex equivalent) | `%literal_buffer NAME { ... }.` | Managed character-accumulation buffer for quoted strings.  Replaces the per-scanner startlit/addlit/litbufdup boilerplate. |
| (no flex equivalent) | `%lexer_extra_argument {T *p}` | Per-instance opaque user pointer.  Read inside actions as `extra`. |
| (no flex equivalent) | `%location_type {T}` | Override the default location struct. |
| (no flex equivalent) | `%location_advance { ... }` | Per-token line/column update hook (replaces flex's `yylineno` magic). |

The "(default)" entries reflect the design choice that Lime hardwires the
options every modern PG scanner sets.  If you see one of these `%option`
lines in a flex source you are porting, just delete it.

## Action Body Translation

The single most invasive part of any port.  Flex actions use thread-unsafe
globals (`yytext`, `yyleng`, `yylineno`, `yyextra`) and mutating macros
(`BEGIN`, `yyless`, `yyterminate`).  Lime actions use named locals and
explicit primitives.

### Local variables

| Flex global | Lime local | Type | Notes |
|-------------|------------|------|-------|
| `yytext` | `matched` | `const char *` | Pointer into the caller's input buffer.  NOT null-terminated; use `matched_len`. |
| `yyleng` | `matched_len` | `size_t` | Length of the matched span. |
| `yylineno` | (none) | -- | Line tracking is opt-in via `%location_advance` plus a `%location_type` that contains a line field.  See "Locations" below. |
| `yyextra` | `extra` | as declared by `%lexer_extra_argument` | User-data pointer set at `LexAlloc` time. |
| `yyloc`, `*yylloc` | `loc` | as declared by `%location_type` | Location of the matched span. |
| `YYSTATE`, `yy_top_state()` | `state` | `int` | Current state code.  Read or write; assigning is equivalent to `LEX_TRANSITION(state)` without state-local-data init. |
| `yyscanner` | `lex` | `Foo_Lexer *` | The lexer instance.  Pass to runtime API calls (`FooLexInclude(lex, ...)` etc.). |

The lexer instance is named after the prefix: `%name_prefix Foo.` produces
`Foo_Lexer *lex` as the action-body local.

### Action primitives

| Flex | Lime | Notes |
|------|------|-------|
| `return TOK;` | `LEX_EMIT(TOK);` | Emits the token to the user's callback.  `TOK` may be a `<PREFIX>_RULE_*` constant or any `int` (char literals like `'+'` work). |
| (empty action body) | (empty action body) | Both flex and Lime fall through; in Lime, an empty body auto-emits the matched rule's id. |
| `;` (single semicolon, "skip") | `LEX_SKIP();` | Suppresses the auto-emit; consumes the bytes silently.  Equivalent to a flex `;` action body for whitespace/comment skipping. |
| `BEGIN(STATE);` | `LEX_TRANSITION(STATE);` | Switch state.  Clears any prior state-local data.  Inside an action body, `state = STATE;` is equivalent (and is what flex's `yy_set_bol`-style fiddling looks like at the AST level). |
| `BEGIN(INITIAL);` | `LEX_TRANSITION(<PREFIX>_STATE_INITIAL);` | The default state's constant is generated as `<PREFIX>_STATE_INITIAL`. |
| `yyless(n);` | `LEX_PUSHBACK(matched_len - n);` | Beware: flex's `yyless(N)` keeps the first N bytes; Lime's `LEX_PUSHBACK(K)` un-consumes the trailing K bytes.  The arithmetic flips.  See "Common gotchas" below. |
| `yymore();` | (no equivalent in v1) | Audit shows zero uses across PG.  If you need it, file an issue with a concrete grammar. |
| `REJECT;` | (no equivalent in v1) | Audit shows zero uses across PG.  Multi-rule state machines cover the same ground. |
| `yyterminate();` | `LEX_TERMINATE();` | Stops the current `LexFeedBytes` call cleanly with `LEX_OK`.  Distinct from emitting an EOF token. |
| `yyerror("msg")` / `elog(ERROR, "msg")` | `LEX_ERROR_AT("msg");` | Causes the call to return `LEX_ERROR`; the message is retrievable via `LexErrorMessage(yyl)` until the next `LexFeed*` call. |

For state-local data, action bodies see `state_data` as a typed pointer
to the state's struct (declared in the `%state {...}` /
`%exclusive_state {...}` body).  See the jsonpath worked example below.

### Side-by-side rule translation

A single flex rule and its Lime equivalent:

```
   /* flex */                                     /* lime */
   bootstrap   { yylval->kw = "bootstrap";        rule kw_bootstrap matches /bootstrap/ {
                 return XBOOTSTRAP; }                 LEX_EMIT(BOOT_RULE_KW_BOOTSTRAP);
                                                  }
```

If the action body's only effect is the `return`, the Lime form simplifies
to an empty body that auto-emits:

```
   rule kw_bootstrap matches /bootstrap/ { /* auto-emit */ }
```

Whether to use the explicit `LEX_EMIT` form or the empty-body form is a
style choice; the former is clearer when an action body has multiple
emit paths or remaps the rule code (a keyword-vs-ident lookup is the
canonical example).

## State Declarations

Flex separates state declarations from rules:

```
%x QUOTED COMMENT
%s NORMAL

%%

<QUOTED>"..."  { ... }
<COMMENT>"..." { ... }
```

Lime puts state declarations in the directive section:

```
%exclusive_state QUOTED.
%exclusive_state COMMENT.
%state NORMAL.

<QUOTED> rule q_close matches /"/  { ... }
<COMMENT> rule c_body matches /[^*]+/ { ... }
```

Period required on each declaration.  States with typed local data
declare them inline:

```
%exclusive_state DOLLAR_QUOTED {
    char    *opening_tag;
    size_t   opening_tag_len;
}.
```

Inside any rule qualified `<DOLLAR_QUOTED>`, the action body reads
`state_data->opening_tag` and `state_data->opening_tag_len` as fields of
a typed struct.  Setting them at state entry uses an extended transition:

```c
LEX_TRANSITION(DOLLAR_QUOTED, .opening_tag = matched + 1,
                              .opening_tag_len = matched_len - 2);
```

Compare to the flex equivalent, which stuffs the same state through
`yyextra->dolqstart` -- a pointer in a thread-unsafe global, with no type
checking that the field is the right one for the current state.

### Multi-state rules

Flex's `<a,b,c>pattern` syntax for "this rule fires in any of these
states" is preserved in Lime:

```
<xq, xqc, xe, xn, xus> rule string_eof matches <<EOF>> {
    LEX_ERROR_AT("unterminated quoted string at end of input");
}
```

Comma-separated state list inside the angle brackets.  Whitespace inside
the list is permitted.  Lime also accepts a state-block shorthand for
many rules sharing one state qualifier:

```
<EXPR> {
    rule plus  matches /\+/  { LEX_EMIT('+'); }
    rule minus matches /-/   { LEX_EMIT('-'); }
    rule times matches /\*/  { LEX_EMIT('*'); }
    /* ... 30 more rules ... */
}
```

Desugars to per-rule `<EXPR>` qualifiers.  Block-form and per-rule forms
can be interleaved freely; the state qualifier is per-rule semantically.

## End-of-Input Rules

Flex's `<<EOF>>` rule fires when the input ends in a specific state.
Lime preserves the syntax:

```
<COMMENT> rule comment_eof matches <<EOF>> {
    LEX_ERROR_AT("unterminated comment");
}
```

Default behaviour when no `<<EOF>>` rule is declared for the current
state: INITIAL returns `LEX_OK` (clean end-of-input); any exclusive
state returns `LEX_ERROR` (a non-INITIAL state at EOF means an open
construct, e.g. an unterminated string).

> **Status note.**  In Lime v0.2 (current), `<<EOF>>` rule *syntax*
> parses and the rule is recognised.  Per-state action *dispatch* on
> EOF is wired up by milestone M3.6.  Until M3.6 lands, declaring an
> EOF rule has no runtime effect beyond the spec'd `LEX_ERROR` default
> for non-INITIAL states.  Existing PG-shape ports that rely on EOF
> rules for "unterminated string at EOF" diagnostics should write the
> rule and assume it will fire once M3.6 ships.

## Pattern Syntax Differences

Lime supports a POSIX-extended subset of regex.  Flex supports the same
core plus a few extensions; the differences are:

| Construct | Flex | Lime | Notes |
|-----------|------|------|-------|
| Literals, classes, alternation | yes | yes | Identical. |
| `*`, `+`, `?` | yes | yes | Identical. |
| `{n,m}` repetition | yes | yes | Identical. |
| `()` grouping | yes (capturing) | yes (non-capturing) | Lime has no capture groups; the grouping is purely structural. |
| `\n`, `\t`, `\r`, `\f`, `\v`, `\0` | yes | yes | Identical. |
| `\xNN` hex escape | yes | yes | Identical. |
| `[abc]`, `[^abc]` char class | yes | yes | Identical. |
| `^` / `$` anchors | start/end of *line* | start/end of *input* | **Different.** See below. |
| `.` (any-but-newline) | yes | yes | Identical. |
| `"..."` quoted literals | yes | no | Use bare characters or escapes; there is no special quoting syntax inside Lime patterns. |
| `\\` escape | yes | yes | Identical. |
| Trailing context `pattern1/pattern2` | yes | no | Use a state machine.  Audit shows zero uses across PG. |
| Backreferences `\1` | no (flex doesn't have them either) | no | Use a state machine. |
| Named captures `(?P<name>...)` | no | no | Not part of POSIX-extended. |
| Lookahead/lookbehind `(?=...)` | no | no | Not part of POSIX-extended. |

### Anchors

Flex's `^` matches at the start of a line (after a `\n` or at start of
input); `$` matches at end of line.  Lime's `^` matches at start of
*input* only; `$` matches at end of *input*.

This is a deliberate design choice: lexers as a class don't track lines
specially -- every byte is a byte -- and start-of-line anchors are
expressible as a multi-rule state machine with a NEWLINE state entered
on `\n`.  In practice, the four PG scanners audited use `^` patterns
only for line-start comments, and the simplest port replaces
`^#[^\n]*` with the unanchored `#[^\n]*` if the input language doesn't
allow `#` outside comments (PG's BKI does not).

### Pattern fragments

Flex's definitions section (the part above `%%`):

```
digit       [0-9]
hexdigit    [0-9A-Fa-f]
ident_start [A-Za-z_]
ident_cont  [A-Za-z0-9_]
identifier  {ident_start}{ident_cont}*
```

Translates to Lime `%pattern` directives:

```
%pattern digit       /[0-9]/.
%pattern hexdigit    /[0-9A-Fa-f]/.
%pattern ident_start /[A-Za-z_]/.
%pattern ident_cont  /[A-Za-z0-9_]/.
%pattern identifier  /{ident_start}{ident_cont}*/.
```

Reference fragments inside a rule's pattern using `{name}` interpolation,
exactly as in flex.  Cycles are rejected at parse time; forward
references are allowed.

## Keyword Tables

Flex doesn't have first-class keyword tables; PG hand-rolls them with a
gen_keywordlist.pl script and a perfect-hash header.  Lime makes them a
declaration:

```
%keyword_table sql_keywords (case_insensitive, prefix=K_) {
    "select", "from", "where", "as", "and", "or", "not"
}.

rule sql_word matches /[A-Za-z_][A-Za-z0-9_]*/ {
    int kw = sql_keywords_lookup(matched, matched_len);
    if (kw >= 0) {
        LEX_EMIT(kw);
    } else {
        LEX_EMIT(<PREFIX>_RULE_IDENT);
    }
}
```

The generator picks the lookup implementation based on table size:
linear scan for ≤8 entries, switch+linear for 9-64, perfect hash for
65-512, trie for >512.  Drop in the table; let the compiler pick.

> **Status note.**  Keyword tables parse and round-trip in v0.2 but the
> compile/emit path is not wired yet.  For an immediate port today,
> express each keyword as its own rule; declaration-order ties keep
> them ahead of the catch-all ident rule.  See
> `examples/pg_bootscanner/bootscanner.lex` for the worked example.

## Literal Buffers

Four of the six audited PG scanners hand-roll the same
`startlit`/`addlit`/`addlitchar`/`litbufdup` quartet for accumulating
quoted-string contents.  Lime offers a first-class declaration:

```
%literal_buffer scanstr {
    type      char
    initial   64
    grow      "*2"
    alloc     palloc
    realloc   repalloc
    free      pfree
}.
```

Inside any action body, the macros `LEX_BUF_START(scanstr)`,
`LEX_BUF_APPEND(scanstr, ptr, n)`, `LEX_BUF_APPEND_CH(scanstr, ch)`, and
`LEX_BUF_TAKE(scanstr)` collapse the per-scanner C boilerplate to a
single declaration plus call sites.

> **Status note.**  Literal-buffer syntax parses in v0.2; runtime
> emission is in M3.7.  Until then, hand-roll the buffer in
> state-local data fields (the v0.2 design's jsonpath worked example
> shows the shape).

## Build System

### Flex Build

```makefile
parser_lex.c parser_lex.h: parser.l
	flex -o parser_lex.c --header-file=parser_lex.h parser.l
```

### Lime Build

```makefile
LIME ?= /path/to/lime

parser_lex.c parser_lex.h: parser.lex $(LIME)
	$(LIME) -X -d. parser.lex
```

Lime generates the `.c` and `.h` files in the directory specified by
`-d` (default: current directory) named `<basename>_lex.c` and
`<basename>_lex.h`.

### meson

```meson
parser_lex_gen = custom_target('parser_lex',
  input   : ['parser.lex'],
  output  : ['parser_lex.c', 'parser_lex.h'],
  command : [lime_exe, '-X', '-d@OUTDIR@', '@INPUT@'],
)

parser = executable('parser',
  ['driver.c', 'parser_grammar.c'],
  parser_lex_gen,
  include_directories : ['.'],
)
```

The `lime_exe` reference assumes the build also produces the lime tool;
in a downstream consumer, pass the host's `lime` via a `find_program` or
`dependency('lime')` lookup.

## Driver Loop

The largest behavioural change.  Flex is pull-driven: the parser calls
`yylex()` to fetch each token.  Lime is push-driven: the caller feeds
bytes, the lexer calls back for each token.

### Flex driver (pull)

```c
yyin = fopen("input.txt", "r");
while ((tok = yylex(&yylval, &yylloc, scanner)) != 0) {
    yyparse(tok, &yylval, &yylloc, parser_arg);
}
```

### Lime driver (push)

```c
static void emit_to_parser(void *p, int tok,
                           const char *text, size_t len) {
    /* convert text/len to a parser-side payload, then: */
    Parse(p, tok, build_value(text, len));
}

void *lex = FooLexAlloc(malloc);
void *par = FooParseAlloc(malloc);

LexResult r = FooLexFeedBytes(lex, source, source_len,
                              emit_to_parser, par);
if (r == FOO_LEX_OK) {
    r = FooLexFeedEOF(lex, emit_to_parser, par);
}
Parse(par, 0, eof_value);   /* push EOF to the parser */

FooLexFree(lex, free);
FooParseFree(par, free);
```

The driver is shorter, threading is easy, and there is no `yyin` /
`fread` ceremony.  The trade-off is that the *caller* now owns the
input buffer for its lifetime -- the lexer does not copy bytes.

## Worked Example: PostgreSQL bootscanner.l

The simplest of the six audited PG scanners.  No exclusive states, ~22
keywords, identifiers, single-quoted strings, comments.  The full Lime
port is in `examples/pg_bootscanner/bootscanner.lex`; this section
shows the diff in shape.

### Flex original (excerpts)

```flex
%top{
#include "postgres.h"
#include "bootstrap/bootstrap.h"
#include "bootparse.h"
}

%option reentrant bison-bridge 8bit never-interactive
%option nodefault noinput nounput noyywrap noyyalloc noyyrealloc noyyfree
%option warn prefix="boot_yy"

id    [-A-Za-z0-9_]+
sid   \'([^']|\'\')*\'

%%

open       { yylval->kw = "open";       return OPEN; }
bootstrap  { yylval->kw = "bootstrap";  return XBOOTSTRAP; }
/* ... 20 more keywords ... */

","        { return COMMA; }
"="        { return EQUALS; }

[\n]       { yylineno++; }
[\r\t ]    ;
^\#[^\n]*  ;

{id}       { yylval->str = pstrdup(yytext); return ID; }
{sid}      { yylval->str = DeescapeQuotedString(yytext); return ID; }

.          { elog(ERROR, "syntax error at line %d ...", yylineno, yytext); }

%%
```

### Lime port (matching excerpts)

```
%name_prefix Boot.

%pattern id    /[-A-Za-z0-9_]+/.
%pattern sid   /'([^']|'')*'/.

rule whitespace matches /[\r\t ]+/ { LEX_SKIP(); }
rule newline    matches /\n/       { LEX_SKIP(); }
rule comment    matches /#[^\n]*/  { LEX_SKIP(); }

rule comma   matches /,/  { /* auto-emit */ }
rule equals  matches /=/  { /* auto-emit */ }

rule kw_open      matches /open/      { /* auto-emit */ }
rule kw_bootstrap matches /bootstrap/ { /* auto-emit */ }
/* ... 20 more keywords ... */

rule ident    matches /{id}/  { /* auto-emit */ }
rule sqstring matches /{sid}/ { /* auto-emit */ }

rule unexpected matches /./ {
    LEX_ERROR_AT("syntax error: unexpected character");
}
```

The flex `%top{}` block goes into a Lime `%include { ... }` block (omitted
above for brevity).  The `%option` clutter is gone -- every option this
scanner sets is the Lime default.  Line bookkeeping is dropped in the
port; if PG cares, the test harness restores it via `%location_advance`.

## Worked Example: jsonpath_scan.l (stateful)

A snippet showing a stateful scanner with state-local data.  Flex
buffers the quoted-string contents through `yyextra` slots; Lime puts
them in a typed struct.

### Flex (yyextra-mediated)

```flex
%x xq

%%

<INITIAL>\"  {
    yyextra->buf_start();
    BEGIN(xq);
}

<xq>[^"\\]+  { yyextra->buf_append(yytext, yyleng); }
<xq>\\.      { yyextra->buf_append_unescaped(yytext); }

<xq>\"  {
    BEGIN(INITIAL);
    yylval->str = yyextra->buf_take();
    return JP_STRING;
}

<xq><<EOF>>  {
    yyerror("unterminated quoted string");
    yyterminate();
}
```

### Lime (typed state-local data)

```
%exclusive_state QUOTED {
    char   *buffer;
    size_t  buffer_len;
    size_t  buffer_cap;
}.

rule q_open matches /"/ {
    LEX_TRANSITION(QUOTED, .buffer = palloc(64),
                           .buffer_len = 0,
                           .buffer_cap = 64);
}

<QUOTED> rule q_chunk matches /[^"\\]+/ {
    jp_buf_append(state_data, matched, matched_len);
}

<QUOTED> rule q_escape matches /\\./ {
    char ch = jp_unescape(matched[1]);
    jp_buf_append(state_data, &ch, 1);
}

<QUOTED> rule q_close matches /"/ {
    LEX_EMIT(JP_RULE_STRING);   /* emit callback sees state_data->buffer */
    LEX_TRANSITION(INITIAL);
}

<QUOTED> rule q_eof matches <<EOF>> {
    LEX_ERROR_AT("unterminated quoted string");
}

%state_destructor QUOTED {
    if (state_data->buffer) pfree(state_data->buffer);
}
```

The wins:

- `state_data->buffer` is a typed field, not a `yyextra` slot reached
  via a thread-unsafe global.
- `LEX_TRANSITION(QUOTED, .buffer = ..., ...)` initialises the
  state-local struct atomically with the transition.
- `%state_destructor QUOTED` runs automatically if the lexer is freed
  mid-state (e.g. on a parse abort), so a half-quoted string doesn't
  leak.  Flex has no equivalent.

## Common Gotchas

1. **Pushback arithmetic flips.** Flex's `yyless(n)` keeps the first `n`
   bytes of the match, putting back the trailing `yyleng - n`.  Lime's
   `LEX_PUSHBACK(k)` un-consumes the trailing `k` bytes.  Translation:
   `yyless(n)` → `LEX_PUSHBACK(matched_len - n)`.  Mechanical, but
   easy to get wrong.

2. **Empty action body auto-emits.** A flex `pattern { /* nothing */ }`
   action falls through to the default rule (which echoes by default,
   suppressed by `%option nodefault`).  In Lime, an empty action body
   *auto-emits the matched rule* -- the design treats "I matched and
   the user code did nothing" as "emit me as my own token kind".  If
   you want flex's "consume and skip" behaviour, write `LEX_SKIP();`
   explicitly.  This is the most common porting mistake.

3. **Anchors mean different things.** Flex's `^foo` matches `foo` at
   the start of any line; Lime's `^foo` matches at start of input only.
   See "Pattern syntax differences" above.

4. **No `yytext` mutation.** Flex lets action bodies mutate `yytext`
   in place; Lime's `matched` is `const char *`.  Copy first
   (`pstrdup` style) if you need to mutate.

5. **No bounded scan inside action bodies for character literals.**
   Flex action bodies sometimes hand-loop through `yytext` looking for
   a closing delimiter that flex's regex already matched.  In Lime,
   the regex already pinned the bounds; loop from `matched` to
   `matched + matched_len` and don't read past.  No null terminator.

6. **`yywrap` doesn't exist.** Flex's include mechanism uses
   `yywrap` to signal "I exhausted this buffer, push the next".  Lime
   pushes a new buffer via `LexInclude(yyl, bytes, n)` and pops
   automatically on EOF in the included frame -- no callback.  The
   caller is responsible for keeping included buffers alive until the
   pop, observable via `LexIncludeDepth(yyl)`.

7. **State name conflicts with C reserved words.** Flex permits
   `INITIAL`, `xq`, etc.  Lime exposes states as
   `<PREFIX>_STATE_<NAME>` constants.  Avoid state names that collide
   with system header macros (`COMMENT` is fine; `INITIAL` is fine;
   `BUFSIZ` is not).

8. **`yyless(0)` is legal in flex and means "redo this match".**  In
   Lime, `LEX_PUSHBACK(matched_len)` does the same thing.  Watch out
   for accidental infinite loops -- Lime's runtime has no built-in
   "no progress" guard.

9. **`unput()` has no equivalent.** Flex's `unput(c)` injects a
   synthetic byte into the input.  Lime has no such primitive; the
   correct shape is to use state-local data plus a multi-rule state
   machine.  Audit shows zero uses across PG.

10. **Reentrancy was non-default in flex; it's the only mode in Lime.**
    A scanner that uses globals (no `%option reentrant`) has to be
    refactored to take its state through `%lexer_extra_argument`
    before the port will work.  This is usually trivial -- the globals
    become fields of the user-data struct.

## Rejecting features by design

The features below are explicitly out of scope in v1, with rationale:

- **REJECT.** Flex's alternative-pattern fallthrough.  Audit shows zero
  uses; multi-rule state machines cover the same ground.
- **yymore().** Accumulating successive matches into one yytext.  Audit
  shows zero uses; literal buffers and concatenating actions cover the
  same ground.
- **Trailing context (`p1/p2`).** Audit shows zero uses; either
  `LEX_PUSHBACK` or a state machine covers the cases.
- **PCRE features.** Back-references, named captures, lookahead /
  lookbehind, inline flag modifiers.  All locked to a specific regex
  flavor that the design explicitly rejects.

If a real grammar surfaces a need for any of these, file an issue with
the concrete pattern and the use case; the v2 design will reconsider.

## Quick Reference Card

| Task | Flex | Lime |
|------|------|------|
| Declare a rule | `pattern { action; }` | `rule name matches /pattern/ { action; }` |
| Auto-emit token | `return TOK;` | `{ /* empty */ }` or `LEX_EMIT(TOK);` |
| Skip whitespace | `[ \t]+ ;` | `rule ws matches /[ \t]+/ { LEX_SKIP(); }` |
| Match text | `yytext` | `matched` |
| Match length | `yyleng` | `matched_len` |
| Switch state | `BEGIN(STATE);` | `LEX_TRANSITION(STATE);` |
| Pushback | `yyless(n);` | `LEX_PUSHBACK(matched_len - n);` |
| Stop lexing | `yyterminate();` | `LEX_TERMINATE();` |
| Raise error | `elog(ERROR, ...);` | `LEX_ERROR_AT("msg");` |
| Pattern fragment | `name regex` | `%pattern name /regex/.` |
| Exclusive state | `%x STATE` | `%exclusive_state STATE.` |
| Inclusive state | `%s STATE` | `%state STATE.` |
| Multi-state rule | `<a,b,c>pattern { ... }` | `<a, b, c> rule name matches /p/ { ... }` |
| EOF rule | `<STATE><<EOF>> { ... }` | `<STATE> rule name matches <<EOF>> { ... }` |
| Driver shape | pull (`yylex` callback) | push (`LexFeedBytes` + emit callback) |
| User-data slot | `yyextra` | `%lexer_extra_argument` + action-body `extra` |
| Build | `flex -o foo_lex.c foo.l` | `lime -X -d. foo.lex` |
