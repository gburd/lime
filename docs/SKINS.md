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
* `--target=c:flex`: reserved.  Recognised by the flag parser but
  not yet implemented; emits a warning.
* `--target=c:bison,flex`: both skins (only `bison` actually emits
  files in this release).
* `--target=rust:nom` / `:pest` / `:lalrpop` / `:logos` / `:chumsky`:
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

/* semantic value type -- typedef of %token_type, or `void *` */
typedef <token_type> YYSTYPE;
extern YYSTYPE yylval;

/* user must supply */
extern int  yylex(void);
extern void yyerror(const char *msg);

/* entry point */
int yyparse(void);

/* additional entry point when grammar declares %extra_argument */
int yyparse_extra(<extra_argument_decl>);
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
| `%extra_argument {T x}` | `int yyparse_extra(T x);` |
| `%syntax_error { ... }` | runs verbatim; should call `yyerror()` |
| `%token NAME` | `enum yytokentype { NAME = 258, ... }` |
| `%location_type` / locations | `YYLTYPE` typedef + `extern yylloc` |

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

* `%union { ... }` in Lime grammars.  Lime does not accept the bison
  `%union` directive.  Workaround: define the union in a `%include`
  block, then point `%token_type` at the union typedef.  The
  generated `YYSTYPE` will be the union, and your `yylex()` writes
  `yylval.<field>` exactly as in bison.
* `%parse-param` declarations.  Use `%extra_argument` instead and
  call `yyparse_extra()`.
* `yydebug` / `YYDEBUG`.  Use Lime's `<Name>Trace(stderr, "...")`
  directly until a future commit wires bison's `yydebug` to it.
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

## Flex skin (planned)

Reserved for a future commit.  Will emit `<lexer>_flex.c` /
`<lexer>_flex.h` with `int yylex(void)` over Lime's lexer DFA,
`yytext` / `yyleng` / `yylineno` globals, `YY_BUFFER_STATE` typedef
and `yy_scan_string` / `yy_scan_buffer` / `yy_create_buffer`
wrappers, and `yywrap()` weak-link default.

`--target=c:flex` already parses; today it warns and emits no
files.  The flag-parser hook is in place so the future commit only
needs to add the emit module.

## Rust skins (planned)

Reserved.  See `.agent/notes/open-items.md` section 2:

* `--target=rust:nom` -- nom-style combinator surface
* `--target=rust:pest` -- pest-style `Pairs` iterator
* `--target=rust:lalrpop` -- lalrpop-style `<Grammar>Parser::parse(...)`
* `--target=rust:logos` -- logos-style `Logos` trait over Lime's lexer
* `--target=rust:chumsky` -- chumsky-style combinator surface

`--target=rust:<skin>` already parses and rejects with a clear
"not yet implemented" error.  Each skin will land in its own commit.

## Tests

`tests/test_skin_bison.c` round-trips a small calc grammar through
both the native Lime API and the bison skin and asserts result
equivalence over a handful of inputs (clean parses, invalid
character, missing operand).  See `tests/meson.build` for the
custom-target wiring.

## See also

* [docs/MIGRATION_FROM_BISON.md](MIGRATION_FROM_BISON.md) -- step-by-step
  bison-grammar conversion walkthrough.
* [docs/MIGRATION_FROM_FLEX.md](MIGRATION_FROM_FLEX.md) -- flex equivalent.
* [docs/CONCEPTS.md](CONCEPTS.md) -- Lime's parser model.
* `.agent/notes/open-items.md` -- design discussion for skins.
