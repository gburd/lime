# Migrating from Bison to Lime

This guide walks through the process of converting a Bison grammar file
(`.y`) to a Lime grammar file (`.lime`). It covers directive mapping,
syntax differences, and common pitfalls, with a worked example based on the
PostgreSQL bootstrap parser in `examples/bootstrap/`.

## Directive Mapping

| Bison Directive | Lime Equivalent | Notes |
|----------------|-----------------|-------|
| `%name-prefix "foo"` | `%name_prefix foo` or `%name foo` | Both accepted.  Lime does not parse the dashed form `%name-prefix` (its directive tokenizer rejects the dash); the underscore form is a direct alias for `%name`. |
| `%union { ... }` | Per-symbol `%type` | Lime has no union; each symbol declares its own C type |
| `%token <type> TOK` | `%token TOK.` | Lime tokens have no inline type annotation; use `%token_type` for the default |
| `%token_type` | (none) | Bison has no equivalent; uses `%union` instead |
| `%type <type> sym` | `%type sym {CType}` | Curly braces instead of angle brackets |
| `%start sym` | `%start sym` or `%start_symbol sym` | Both accepted. |
| `%parse-param { T *p }` | `%extra_argument {T *p}` | Passed as parameter to all parse calls |
| `%lex-param { T *p }` | (none) | Lime uses push parsing; caller manages the lexer |
| `%pure-parser` | (default) | Lime parsers are always reentrant |
| `%expect N` | `%expect N.` | Supported.  Lime treats the count as an **exact-match assertion** (unlike Bison's loose "at most N"): if the actual conflict count differs, `lime` exits non-zero.  Currently reports a combined shift/reduce + reduce/reduce total; distinct `%expect_shift_reduce` / `%expect_reduce_reduce` counters are not yet separated. |
| `%destructor { ... } sym` | `%destructor sym { ... }` | Order is reversed |
| `%left TOK1 TOK2` | `%left TOK1 TOK2.` | Terminating period required |
| `%right TOK1 TOK2` | `%right TOK1 TOK2.` | Terminating period required |
| `%nonassoc TOK1` | `%nonassoc TOK1.` | Terminating period required |
| `%prec TOKEN` | `[TOKEN]` | Square brackets at end of rule |
| `%code { ... }` | `%include { ... }` | Code included at top of generated file |
| `%defines "file.h"` | Automatic | Lime always generates a `.h` file |
| `%output "file.c"` | `-d dir` flag | Lime uses output directory, not output filename |
| `%verbose` | Default | Lime always generates `.out` report (suppress with `-q`) |
| `%define api.pure full` | (default) | Lime is always pure/reentrant |
| `%define parse.error verbose` | `%syntax_error { ... }` | Custom error callback |

## Grammar Syntax Differences

### Rule Syntax

Bison:
```
result : symbol1 symbol2 { $$ = f($1, $2); }
       | symbol3         { $$ = g($1); }
       ;
```

Lime (two supported forms):

**Expanded** -- each alternative as a separate rule with its own action:
```
result(A) ::= symbol1(B) symbol2(C). { A = f(B, C); }
result(A) ::= symbol3(B).            { A = g(B); }
```

**`|`-alternated** -- alternatives share one trailing action:
```
result(A) ::= symbol1(B) symbol2(C)
            | symbol3(B)              .
            { A = B; /* same action for both */ }
```

Key differences from Bison:
- `::=` instead of `:`
- Every rule ends with a period (`.`) before the action
- Semantic values use letter labels (A, B, C...) instead of `$$`, `$1`, `$2`
- Labels are declared in parentheses after the symbol name
- `|` is accepted in RHS for bison-compat: the trailing action,
  precedence marker, and `{NEVER-REDUCE}` flag all propagate to every
  alternative in the group.  **Per-alternative actions are not
  supported** -- actions are always after the rule-terminating `.`, not
  inline per alternative.  If the original Bison grammar's alternatives
  each had different actions, expand them to separate rules (the
  expanded form above).
- Epsilon alternatives (`s ::= A | | B .`) are accepted; the empty
  position between `|`s becomes a rule with zero RHS symbols.

### Token Declarations

Bison:
```
%token <str_val> IDENTIFIER STRING_LITERAL
%token <int_val> INTEGER
%token PLUS MINUS TIMES
```

Lime:
```
%token_type {Token}
%token IDENTIFIER.
%token STRING_LITERAL.
%token INTEGER.
%token PLUS.
%token MINUS.
%token TIMES.
```

Lime uses a single `%token_type` for all tokens. If you need different
types per token, use a union or tagged struct as your `%token_type`.

### Type Declarations

Bison:
```
%union {
    int    int_val;
    char  *str_val;
    Node  *node;
}
%type <node> expression statement
%type <int_val> count
```

Lime:
```
%type expression {Node *}
%type statement {Node *}
%type count {int}
```

Each non-terminal gets its own type declaration. There is no shared union.

### Precedence with %prec

Bison:
```
expr : '-' expr %prec UMINUS { $$ = -$2; }
     ;
```

Lime:
```
expr(A) ::= MINUS expr(B). [UMINUS] { A = -B; }
```

The `[UMINUS]` in square brackets replaces `%prec UMINUS`.

### Error Handling

Bison:
```
%{
void yyerror(const char *msg) { fprintf(stderr, "%s\n", msg); }
%}
```

Lime:
```
%syntax_error {
    fprintf(stderr, "Syntax error near token %s\n",
            yyTokenName[yymajor]);
}
%parse_failure {
    fprintf(stderr, "Parse failure -- giving up\n");
}
```

Lime provides two separate callbacks: `%syntax_error` fires on each error
token, while `%parse_failure` fires when recovery is impossible. Inside
these blocks, `yymajor` is the offending token type, `yyminor` is its
semantic value, and `yyTokenName[]` maps token codes to strings.

### Mid-Rule Actions

Bison supports actions between grammar symbols:
```
stmt : KW_BEGIN { start_transaction(); }
       block
       KW_END   { end_transaction(); }
     ;
```

Lime does not support mid-rule actions. Restructure by moving all logic
to the final action, or split the rule:

```
stmt(A) ::= KW_BEGIN block(B) KW_END. {
    start_transaction();
    /* process B */
    end_transaction();
    A = 0;
}
```

If the mid-rule action produces a value consumed by later symbols, you
must introduce a helper non-terminal:

```
begin_marker(A) ::= KW_BEGIN. { start_transaction(); A = get_txn_id(); }
stmt(A) ::= begin_marker(B) block(C) KW_END. {
    end_transaction(B);
    A = C;
}
```

## Build System Changes

### Bison Build

```makefile
parser.c parser.h: grammar.y
	bison -d -o parser.c grammar.y
```

### Lime Build

```makefile
LIME ?= /path/to/lime

parser.c parser.h: grammar.lime $(LIME)
	$(LIME) grammar.lime
```

Or with a custom template:
```makefile
parser.c parser.h: grammar.lime $(LIME) limpar.c
	$(LIME) -Tlimpar.c grammar.lime
```

Lime generates the `.c` and `.h` files in the same directory as the input
grammar by default. Use `-d <dir>` to redirect output.

### Parser Interface

Bison (pull parser):
```c
extern int yyparse(void);
extern FILE *yyin;

yyin = fopen("input.txt", "r");
yyparse();
```

Lime (push parser):
```c
void *parser = ParseAlloc(malloc);

/* Feed tokens one at a time */
Parse(parser, TOKEN_TYPE, token_value, extra_arg);
Parse(parser, TOKEN_TYPE2, token_value2, extra_arg);
/* ... */
Parse(parser, 0, zero_value, extra_arg);  /* Signal end of input */

ParseFree(parser, free);
```

The push model means you control the tokenization loop. There is no
`yylex()` callback.

## Worked Example: PostgreSQL Bootstrap Parser

The `examples/bootstrap/` directory contains a complete conversion of the
PostgreSQL BKI bootstrap parser from Bison to Lime. Here are the key
transformations:

### Original Bison (bootparse.y excerpt)

```
%name-prefix="boot_yy"
%parse-param {BootParseState *pstate}

%union {
    char    *str;
    int      ival;
    Oid      oid;
}

%type <ival> boot_query boot_openStmt
%type <str>  boot_ident
%type <oid>  oidspec

%token <str>  ID
%token OPEN XCLOSE XCREATE

%%

boot_openStmt:
    OPEN boot_ident
        {
            boot_do_start(pstate);
            boot_openrel(pstate, $2);
            boot_do_end(pstate);
            $$ = 0;
        }
    ;
```

### Converted Lime (boot_grammar.lime excerpt)

```
%name boot
%extra_argument {BootParseState *pstate}
%token_type {BootToken}

%type boot_query {int}
%type boot_openStmt {int}
%type boot_ident {char *}
%type oidspec {Oid}

%token ID.
%token OPEN.
%token XCLOSE.
%token XCREATE.

boot_openStmt(A) ::= OPEN boot_ident(B). {
    boot_do_start(pstate);
    boot_openrel(pstate, B);
    boot_do_end(pstate);
    A = 0;
}
```

### What Changed

1. `%name-prefix "boot_yy"` became `%name boot`
2. `%parse-param` became `%extra_argument`
3. `%union` was removed; each symbol gets its own `%type`
4. `$2` became `B` (named parameter)
5. `$$` became `A` (LHS result)
6. The `:` rule separator became `::=`
7. The `;` rule terminator became `.` (before the action block)
8. Each `|` alternative became a separate rule

## Common Gotchas

1. **Forgetting the period**: Every rule in Lime ends with `.` before the
   action. Missing it causes cryptic parse errors in the grammar file.

2. **Token declarations need periods too**: Write `%token FOO.` not
   `%token FOO`.

3. **No `|` alternatives**: Each production is a separate `LHS ::= RHS.`
   rule. There is no shorthand for alternatives.

4. **No `%union`**: If your Bison grammar uses multiple semantic value
   types via `%union`, you must either use a tagged union struct as your
   `%token_type` or restructure to use per-symbol `%type` declarations.

5. **Push vs pull**: Lime does not call a `yylex()` function. You must
   write the token-feeding loop yourself. This is actually an advantage
   for integration but requires rethinking the control flow.

6. **`%expect` is an exact-match assertion**: Unlike Bison's loose
   "at most N" semantics, Lime's `%expect N.` fails the build when the
   actual conflict count differs from N (in either direction).  Use
   `lime -p` to see which conflicts were resolved by precedence rules.
   Lime currently reports a single combined count rather than separate
   shift/reduce and reduce/reduce totals.

7. **Extra argument scope**: The `%extra_argument` value is available in
   all action blocks as the variable name you declared. In Bison,
   `%parse-param` values require explicit access patterns.

8. **Destructor syntax**: Lime's `%destructor` puts the symbol name before
   the code block: `%destructor sym { free($$); }`. Bison puts the code
   first: `%destructor { free($$); } sym`.

## Quick Reference Card

| Task | Bison | Lime |
|------|-------|------|
| Define a rule | `lhs: rhs { action };` | `lhs(A) ::= rhs(B). { action }` |
| LHS value | `$$` | `A` |
| RHS value N | `$N` | `B`, `C`, `D`, ... |
| Alternative | `lhs: alt1 \| alt2;` | Two separate `lhs ::=` rules |
| Precedence override | `%prec TOKEN` | `[TOKEN]` |
| Token type | `%union` + `%token <member>` | `%token_type {Type}` |
| Non-terminal type | `%type <member> sym` | `%type sym {Type}` |
| Parser param | `%parse-param {T *p}` | `%extra_argument {T *p}` |
| Error callback | `yyerror()` function | `%syntax_error { ... }` |
| Start symbol | `%start sym` | `%start sym` or `%start_symbol sym` |
| Generate parser | `bison -d gram.y` | `lime gram.lime` |
| Invoke parser | `yyparse()` | `Parse(p, token, val, arg)` |
