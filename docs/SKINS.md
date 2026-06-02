# API-Compatibility Skins

`lime --target=<lang>:<skin>` emits an additional adapter alongside
the standard parser output.  The adapter exposes the API surface of
a different parser-generator's runtime while keeping Lime's tables
and reduce dispatch underneath.  Existing projects can switch the
engine without rewriting the calling code.

* `--target=c` (no skin): just the standard Lime C parser.
  Unchanged behaviour; identical bytes to pre-skin Lime.
* `--target=c:bison`: standard C parser **plus** a bison-style
  `<basename>_bison.c` / `<basename>_bison.h`.
* `--target=c:flex` (paired with `-X` and a `.lex` grammar):
  standard Lime lexer **plus** a flex-style
  `<basename>_flex.c` / `<basename>_flex.h`.
* `--target=c:bison,flex`: both skins active; each attaches to
  the appropriate grammar kind (`.y` for bison, `.lex` for flex).
* `--target=rust:logos`: standard Rust lexer **plus** a logos-style
  `<basename>_lex_logos.rs` adapter (requires `-X` since this is a
  lex-side skin; see *Logos skin* below).
* `--target=rust:nom` / `:pest` / `:lalrpop` / `:chumsky`:
  reserved for future work.  Recognised by the flag parser; rejected
  with a clear "not yet implemented" error.

Skin files are written **next to** the standard output, never
replacing it.  The `-d <output_dir>` flag affects both.

## Bison skin

### Files emitted

```
<basename>_bison.h
<basename>_bison.c
```

### API surface (matches `bison --header=...`)

```c
/* token codes -- named tokens start at 258 */
enum yytokentype {
    YYEMPTY = -2, YYEOF = 0, YYerror = 256, YYUNDEF = 257,
    /* user tokens at 258, 259, ... in declaration order */
};
typedef enum yytokentype yytoken_kind_t;

/* semantic value type -- typedef of %union body, %token_type, or void* */
typedef <token_type> YYSTYPE;
extern YYSTYPE yylval;

/* user must supply */
extern int  yylex(void);
extern void yyerror(const char *msg);

/* entry point */
int yyparse(void);

/* additional entry point when grammar declares %extra_argument */
int yyparse_extra(<extra_argument_decl>);

/* bison-style runtime trace flag (debug builds only) */
extern int yydebug;
```

### Return value contract

`yyparse()` returns:
* `0` -- successful parse
* `1` -- syntax error, with `yyerror()` invoked
* `2` -- memory exhaustion (Lime allocator returned NULL)

### Mapping from Lime to bison

| Lime concept | bison-skin surface |
|---|---|
| `<Name>(parser, code, value, ...)` | `yyparse()` driving `yylex()` |
| `<Name>Alloc()`/`<Name>Free()` | hidden inside `yyparse()` |
| `%token_type {T}` | `typedef T YYSTYPE;` |
| `%union { body }` | `typedef union { body } YYSTYPE;` |
| `%extra_argument {T x}` | `int yyparse_extra(T x);` |
| `%syntax_error { ... }` | runs verbatim; should call `yyerror()` |
| `%token NAME` | `enum yytokentype { NAME = 258, ... }` |
| `%location_type` / locations | `YYLTYPE` typedef + `extern yylloc` |
| `<Name>Trace(stderr, prefix)` | `extern int yydebug;` (set non-zero) |

### `%union` and the semantic-value type

Lime supports the bison `%union { ... }` directive.  When set, the
standard parser emits

```c
#ifndef YYSTYPE_IS_DECLARED
typedef union { /* user body */ } YYSTYPE;
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif
```

in the generated `<basename>.c`, and the bison skin's
`<basename>_bison.h` emits the same typedef under the same
`YYSTYPE_IS_DECLARED` guard so the two headers agree byte-for-byte
when compiled into the same translation unit.  Lime's parser stack
stores the full `YYSTYPE` per terminal, so a value pushed via
`yylval.<field>` arrives in the reduce action as `K.<field>`
(see worked example below).

Precedence when both `%union` and `%token_type` are set: the
`%token_type` value wins, mirroring bison's behaviour where an
explicit `%define api.value.type` overrides `%union`.

#### Tagged tokens (`%token<field> NAME`)

Since v0.9.3 Lime accepts bison's angle-bracket tag syntax to
document which `%union` arm carries each token's semantic value:

```
%union { int n; char *s; }
%token<n> NUMBER
%token<s> NAME
%token EQ.
```

The tag is recorded on the symbol (`struct symbol::union_field`)
and surfaces in the bison skin's emitted header as a per-token
comment beside each enum constant:

```c
enum yytokentype {
    YYEMPTY = -2,
    YYEOF = 0,
    YYerror = 256,
    YYUNDEF = 257,
    NUMBER = 258  /* yylval.n */,
    NAME = 259    /* yylval.s */,
    EQ = 260
};
```

At the point of use the user's `yylex()` writes the named arm:

```c
int yylex(void) {
    int c = next_char();
    if (isdigit(c)) { yylval.n = scan_number(c); return NUMBER; }
    if (isalpha(c)) { yylval.s = scan_ident(c);  return NAME;   }
    return c;
}
```

Reduce actions access the union arm by field name as before --
Lime does not synthesise a per-rule type-safe local for the tag
in v0.9.3 (planned for a follow-up; the `union_field` plumbing
is already in place).  The angle-bracket tag is therefore
documentation plus a hook for future codegen, not a runtime
change:

```
item ::= NAME(K) EQ NUMBER(V). { bind(K.s, V.n); }
```

Mixing tagged and untagged tokens in the same `%token` directive
is fine; the tag is reset on the directive's terminating `.`, so
`%token<n> A B. %token C.` produces `A->union_field = "n"`,
`B->union_field = "n"`, `C->union_field = NULL`.

Untagged grammars (`%token NUMBER NAME EQ.` with `%union`) keep
working byte-for-byte; the early-bison / Lemon idiom of writing
`yylval.<field>` from `yylex()` and reading `K.<field>` in reduce
actions is the supported alternative when you do not want to
annotate every `%token` with a tag.

#### Worked example: bison `%union` -> Lime

bison input:
```
%union { int n; char *s; }
%token <n> NUMBER
%token <s> NAME
%token EQ
%%
item: NAME EQ NUMBER  { bind($1, $3); }
```

Lime equivalent (one-to-one with the bison file):
```
%name KV
%union { int n; char *s; }
%extra_argument { struct kv_state *st }
%token<n> NUMBER.
%token<s> NAME.
%token EQ.
item ::= NAME(K) EQ NUMBER(V). { bind(K.s, V.n); }
```

The untagged variant (Lime <= v0.9.2 idiom) still works:
```
%name KV
%union { int n; char *s; }
%extra_argument { struct kv_state *st }
%token NUMBER NAME EQ.
item ::= NAME(K) EQ NUMBER(V). { bind(K.s, V.n); }
```

Generate and compile:
```
lime -T limpar.c --target=c:bison kv.lime
cc -o kv driver.c kv.c kv_bison.c
```

### Debug tracing

Lime's parser exposes `<Name>Trace(FILE *, char *)` for runtime
trace output -- the same surface bison-style consumers expect via
`yydebug`.  The bison skin bridges the two:

```c
extern int yydebug;          /* declared by <basename>_bison.h */

int main(void) {
    yydebug = 1;             /* enable trace before yyparse */
    yyparse();
    yydebug = 0;             /* silence again if desired */
}
```

The skin's `yyparse_extra()` (and the strict `yyparse(void)` shim
when there is no `%extra_argument`) checks `yydebug` on entry and
calls

```c
if (yydebug) <Name>Trace(stderr, ">> ");
else         <Name>Trace((FILE *)0, (char *)0);
```

The toggle runs every call so flipping `yydebug` between calls
works as in bison.  The default trace prefix is `">> "` -- to
change it, call `<Name>Trace(stderr, "<custom>")` directly
(after `yyparse_extra()` returns, or instead of setting
`yydebug`).  Tracing is wrapped in `#ifndef NDEBUG` because
Lime's standard parser strips `<Name>Trace` under `-DNDEBUG`; in
release builds the skin's wiring is silently a no-op and
setting `yydebug` has no effect.

### Token-code translation

The skin includes a static table that maps bison codes (258 + i)
back to Lime's internal codes (i + 1, with `%first_token` offset
applied).  ASCII char-literal tokens (1..255), `YYerror` (256), and
`YYUNDEF` (257) are not currently routed to Lime; they cause the
skin to call `yyerror("syntax error: invalid token")` and return 1.
A future commit will let users register ASCII literals via
`%token '+'` -- pending grammar-side work.

### `yyerror()` integration

Lime's `%syntax_error { body }` directive places `body` verbatim
inside the generated parser's error path.  The bison skin does not
inject `yyerror()` into that body.  When you port a bison grammar
to Lime, write the directive like this:

```
%syntax_error {
    yyerror("syntax error");
    /* optional: set %extra_argument fields, etc. */
}
```

The `void yyerror(const char *)` declaration in `<basename>_bison.h`
makes the symbol available without you having to forward-declare it
yourself.

### `%extra_argument` and bison's `%parse-param`

Bison uses `%parse-param { T x }` to add a parameter to `yyparse()`.
Lime's equivalent is `%extra_argument { T x }`, which threads a
parameter through every reduce action.  When the grammar declares
`%extra_argument`, the skin emits **two** entry points:

```c
int yyparse(void);          /* strict bison API; passes a zero-init x */
int yyparse_extra(T x);     /* preferred when you need a non-default x */
```

The strict `yyparse(void)` constructs a zero-initialised `T` via a
`union` shim and forwards to `yyparse_extra`.  That works for any
type the user named in `%extra_argument` -- pointer types, structs,
arrays -- because zero-init is well-defined for every C type.  Most
callers should use `yyparse_extra()` directly to thread a real
value through the reduce chain.

### What is **not** supported

* `%parse-param` declarations.  Use `%extra_argument` instead and
  call `yyparse_extra()`.
* `yychar` / `yynerrs` global lookahead/error counters.  Lime's
  parser does not expose these via globals.
* GLR mode (`%glr-parser`).  The bison skin always uses Lime's LALR
  driver.  GLR support is a separate project (see
  [docs/GLR.md](GLR.md)).

### Example: porting a bison calc

bison input:
```
%token PLUS MINUS TIMES DIVIDE INTEGER
%left PLUS MINUS
%left TIMES DIVIDE
%%
program : expr     { *result = $1; }
expr    : expr PLUS  expr  { $$ = $1 + $3; }
        | expr MINUS expr  { $$ = $1 - $3; }
        | expr TIMES expr  { $$ = $1 * $3; }
        | expr DIVIDE expr { $$ = $1 / $3; }
        | INTEGER          { $$ = $1; }
```

Lime equivalent (`calc.lime`):
```
%name Calc
%token_type {int}
%type expr {int}
%extra_argument {int *result}
%token PLUS MINUS TIMES DIVIDE INTEGER.
%left PLUS MINUS.
%left TIMES DIVIDE.
%start_symbol program
%syntax_error { yyerror("syntax error"); }
program ::= expr(A). { *result = A; }
expr(A) ::= expr(B) PLUS  expr(C). { A = B + C; }
expr(A) ::= expr(B) MINUS expr(C). { A = B - C; }
expr(A) ::= expr(B) TIMES expr(C). { A = B * C; }
expr(A) ::= expr(B) DIVIDE expr(C). { A = B / C; }
expr(A) ::= INTEGER(B). { A = B; }
```

Generate:
```
lime -T limpar.c --target=c:bison calc.lime
```

Drop-in driver:
```c
#include "calc_bison.h"

int yylex(void) { /* return bison enum code, set yylval */ }
void yyerror(const char *msg) { fprintf(stderr, "%s\n", msg); }

int main(void) {
    int result = 0;
    if (yyparse_extra(&result) == 0) printf("%d\n", result);
    return 0;
}
```

Compile both the standard Lime output and the skin:
```
cc -o calc driver.c calc.c calc_bison.c
```

## Flex skin

### Files emitted

```
<basename>_flex.h
<basename>_flex.c
```

Generated by `lime -X --target=c:flex <basename>.lex` alongside
the standard `<basename>_lex.{c,h}`.  Pair with
`--target=c:bison,flex` to get both skins in one run; the bison
skin attaches to a `.y` grammar (parser side) and the flex skin
attaches to a `.lex` grammar (lexer side), so they are
independent files.

### API surface (matches `flex --header-file=...`)

```c
/* flex globals.  yytext / yyleng track the current match;        */
/* yylineno is incremented as '\n' bytes are consumed.            */
extern char *yytext;
extern int   yyleng;
extern int   yylineno;
extern FILE *yyin;
extern FILE *yyout;

/* Opaque buffer-state handle. */
typedef struct yy_buffer_state *YY_BUFFER_STATE;

/* Rule-code constants returned by yylex().  Per-grammar; one      */
/* enum constant per rule, in declaration order, starting at 1.    */
enum {
    YY_FLEX_EOF     = 0,
    YY_FLEX_INVALID = -1,
    <PREFIX>_FLEX_<RULE_NAME> = 1,
    /* ... */
};

/* Pull-driven entry point.  Returns 0 at EOF, -1 on a no-match   */
/* scan (one byte was skipped), or rule_id+1 on a successful      */
/* match.                                                          */
int yylex(void);

/* End-of-buffer hook (default returns 1).                         */
int yywrap(void);

/* Buffer-state operations. */
YY_BUFFER_STATE yy_scan_string(const char *str);
YY_BUFFER_STATE yy_scan_buffer(char *base, size_t size);
YY_BUFFER_STATE yy_create_buffer(FILE *file, int size);
void            yy_delete_buffer(YY_BUFFER_STATE b);
void            yy_switch_to_buffer(YY_BUFFER_STATE b);

/* Start-condition state setter.  Use the standard               */
/* <PREFIX>_STATE_* constants from <basename>_lex.h.             */
void yy_set_state(int state);
#define BEGIN(state) yy_set_state(state)
```

### Return-value contract

`yylex()` returns:
* `0` (`YY_FLEX_EOF`) -- end of buffer; `yywrap()` returned 1.
* `>0` -- the matched rule id plus one.  The skin-emitted enum
  `<PREFIX>_FLEX_<NAME>` resolves to the same value, in
  declaration order.
* `-1` (`YY_FLEX_INVALID`) -- no rule matched at the cursor.
  The skin advanced one byte (so the next call resumes after the
  unmatched character) and set `yytext` / `yyleng` to point at
  the skipped byte.  The caller decides whether to abort or
  recover.

The `+1` shift on rule ids keeps `0` reserved for EOF, matching
flex's contract.  Consumers that pair `--target=c:bison,flex`
and use identical token names in both grammars can wire
`yylex()` directly into bison's parser via a small offset; the
skin does not auto-bridge the two enum spaces because parser and
lexer grammars have separate symbol tables.

### Mapping from Lime to flex

| Lime concept | flex-skin surface |
|---|---|
| `<Prefix>_match()` | hidden inside `yylex()` |
| `<Prefix>LexAlloc / Free / FeedBytes / FeedEOF` | bypassed |
| rule id `<PREFIX>_RULE_<NAME>` | `<PREFIX>_FLEX_<NAME>` (`= RULE + 1`) |
| `<PREFIX>_STATE_<NAME>` | passed through; `BEGIN(state)` writes it |
| `%name_prefix` | drives `<PREFIX>_FLEX_*` enum names |
| `%lexer_extra_argument` | **rejected at emit time** -- see below |

### What is **not** supported

* **Action-body macros** (`ECHO`, `REJECT`, `yymore`, `yyless`).
  These are flex idioms; the skin does not honour Lime's
  `LEX_SKIP` / `LEX_EMIT` / `LEX_TRANSITION` / `LEX_TERMINATE` /
  `LEX_ERROR_AT` action-body primitives either, because
  `yylex()` calls Lime's lower-level `<Prefix>_match()` directly
  rather than driving the standard `LexFeedBytes()` runtime.
  Consumers that need action-body semantics should use the
  standard Lime runtime instead.  Specifically, this means a
  whitespace-skipping grammar must skip whitespace on the caller
  side:

  ```c
  int tok;
  while ((tok = yylex()) == <PREFIX>_FLEX_WS) { /* skip */ }
  ```

* **`<<EOF>>` rules**.  Lime supports `%eof` rules in the lexer
  grammar but the flex syntax (`<<EOF>>`) differs and the skin
  does not translate.  Workaround: the consumer's `yywrap()`
  returns non-zero to signal end-of-input, OR the caller
  observes `YY_FLEX_EOF` and runs its own end-of-buffer logic.

* **Multiple buffers / `yy_push_state` / `yy_pop_state`**.  v0.9.3
  ships a single-buffer model (`yy_switch_to_buffer` swaps; no
  stack).  The buffer machinery is API-compatible enough that
  most existing flex driver code compiles unchanged, but a
  start-condition stack is not provided.  Consumers that need
  one can layer it on top of `yy_set_state`.

* **`%lexer_extra_argument` grammars**.  The flex skin refuses
  to emit when the grammar declares a threaded extra argument
  (the flex API has no equivalent for it).  Drop the directive
  or use the standard Lime runtime.  Error message:

  ```
  lime --target=c:flex: grammar uses %lexer_extra_argument; the
  flex skin has no equivalent.  Drop the directive or use the
  standard Lime LexFeedBytes runtime instead.
  ```

### Worked example

Grammar (`tokens.lex`):

```
%name_prefix Tk.
rule plus  matches /\+/      { /* */ }
rule num   matches /[0-9]+/  { /* */ }
rule ident matches /[a-z]+/  { /* */ }
rule ws    matches /[ \t]+/  { /* */ }
rule nl    matches /\n/      { /* */ }
```

Generate:

```
lime -X --target=c:flex tokens.lex
#  -> tokens_lex.c, tokens_lex.h, tokens_flex.c, tokens_flex.h
```

Driver (`driver.c`):

```c
#include <stdio.h>
#include <stdlib.h>
#include "tokens_flex.h"

int main(void) {
    YY_BUFFER_STATE b = yy_scan_string("hello 123 + world\n");
    if (b == NULL) { perror("yy_scan_string"); return 1; }
    int tok;
    while ((tok = yylex()) != YY_FLEX_EOF) {
        if (tok == YY_FLEX_INVALID) {
            fprintf(stderr, "invalid byte at line %d\n", yylineno);
            continue;
        }
        if (tok == TK_FLEX_WS || tok == TK_FLEX_NL) continue;
        printf("line %d: rule %d -> \"%.*s\"\n",
               yylineno, tok, yyleng, yytext);
    }
    yy_delete_buffer(b);
    return 0;
}
```

Compile both the standard Lime lexer and the skin:

```
cc -o driver driver.c tokens_lex.c tokens_flex.c
```

### Pairing with the bison skin

`lime --target=c:bison,flex` emits both skins, but they attach
to different grammars (`.y` for the parser, `.lex` for the
lexer).  The two are wired together driver-side -- exactly like
a hand-rolled flex+bison project.  The wiring is driver-side
because the parser's terminal-symbol enum (bison: 258+) and the
lexer's rule enum (flex: 1+) live in separate ABI spaces.  The
simplest bridge is a name-based switch:

```c
/* In the user's main(), or a thin shim. */
int yylex_for_bison(void) {
    int tok = yylex();
    if (tok <= 0) return tok;            /* EOF or YY_FLEX_INVALID */
    /* The flex-skin enum and the bison-skin enum share rule names
    ** by convention; offset is constant when the lex grammar's
    ** rule names align one-for-one with the parser's %token list. */
    return tok + 257;                    /* 1 -> 258, 2 -> 259, ... */
}
```

When the lex grammar's rule names match the parser grammar's
`%token` names one-for-one (in declaration order), the offset
shim above is sufficient.  When they diverge, an explicit switch
statement translating each `<PREFIX>_FLEX_<NAME>` to the
corresponding `<NAME>` in the bison enum is the safer form.

## Rust skins

### Logos skin (`--target=rust:logos`)

Lex-side skin.  Requires `-X`; emits **next to** the standard
`<stem>_lex.rs`:

```
<stem>_lex_logos.rs
```

#### API surface (matches a `#[derive(Logos)]` enum)

```rust
pub enum Token { /* one unit variant per lex rule */ Lbrace, Rbrace, /* ... */ Error }
impl Token {
    pub fn lexer<'source>(input: &'source str) -> Lexer<'source>;
}
pub struct Lexer<'source> { /* ... */ }
impl<'source> Lexer<'source> {
    pub fn span(&self) -> core::ops::Range<usize>;
    pub fn slice(&self) -> &'source str;
    pub fn source(&self) -> &'source str;
}
impl<'source> Iterator for Lexer<'source> {
    type Item = Result<Token, ()>;
}
```

The wrapper imports the sibling lexer module via
`use super::<stem>_lex as lime_lex;`.  The consumer's `lib.rs` (or
parent module) is expected to declare both files as siblings:

```rust
pub mod foo_lex;
pub mod foo_lex_logos;
```

#### Limitations (v0.9.3)

* Token variants carry no semantic payload (unit variants only).
  `logos`'s `Token::Number(i64)`-style payloads are deferred.
* Single-buffer input only; spans index into `input.as_bytes()`.
* On lex error, yields `Some(Err(()))` once then `None`.  Logos
  itself resyncs and continues; we don't yet.
* The wrapper drives `Lexer::tokenize()` eagerly to materialise
  the token stream up-front.  Avoids self-referential lifetimes
  at the cost of a per-call allocation.  Streaming will follow
  once the inner `TokenIter`'s lifetime story is reworked.

### Reserved Rust skins (planned)

See `.agent/notes/open-items.md` section 2:

* `--target=rust:nom` -- nom-style combinator surface
* `--target=rust:pest` -- pest-style `Pairs` iterator
* `--target=rust:lalrpop` -- lalrpop-style `<Grammar>Parser::parse(...)`
* `--target=rust:chumsky` -- chumsky-style combinator surface

`--target=rust:<skin>` already parses and rejects with a clear
"not yet implemented" error.  Each skin will land in its own commit.

## Tests

`tests/test_skin_bison.c` round-trips a small calc grammar through
both the native Lime API and the bison skin and asserts result
equivalence over a handful of inputs (clean parses, invalid
character, missing operand).  See `tests/meson.build` for the
custom-target wiring.

`tests/test_skin_bison_union.c` exercises the `%union` /
`yylval.<field>` workflow plus the `yydebug` runtime trace flag.
Its grammar (`tests/test_skin_bison_union_grammar.y`) declares
`%union { int n; char *s; }` with no `%token_type`, so YYSTYPE is
the full union and Lime's parser stack stores it per terminal.
The driver runs both the native and skin paths, exercises both
union arms, and verifies that flipping `yydebug` enables/disables
the `<Name>Trace` output without crashing.

`tests/test_skin_flex.c` and
`tests/test_skin_flex_grammar.lex` round-trip a small auto-emit
grammar through both Lime's native push-driven `LexFeedBytes()`
runtime and the flex skin's pull-driven `yylex()`.  The driver
asserts the two surfaces emit identical (rule_id, text)
sequences over five inputs (clean token streams, leading
whitespace, single-token, empty buffer) and exercises the
`yylineno` counter and the `YY_FLEX_INVALID` recovery branch on
an unmatched byte.  It also locks in the `<PREFIX>_FLEX_*` enum
values at 1, 2, ... in declaration order.

## See also

* [docs/MIGRATION_FROM_BISON.md](MIGRATION_FROM_BISON.md) -- step-by-step
  bison-grammar conversion walkthrough.
* [docs/MIGRATION_FROM_FLEX.md](MIGRATION_FROM_FLEX.md) -- flex equivalent.
* [docs/CONCEPTS.md](CONCEPTS.md) -- Lime's parser model.
* `.agent/notes/open-items.md` -- design discussion for skins.
