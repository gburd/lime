# Migrating from Yacc to Lime

This guide covers converting a Yacc grammar (`.y`) to Lime format (`.lime`).
Yacc and Bison share most syntax, so this guide focuses on Yacc-specific
considerations. For the full directive mapping and syntax translation rules,
see [MIGRATION_FROM_BISON.md](MIGRATION_FROM_BISON.md) -- nearly everything
there applies to Yacc as well.

## Key Differences from Yacc

### Parser Model

Yacc generates a **pull parser** that calls `yylex()` internally. Lime
generates a **push parser** where you feed tokens one at a time:

Yacc:
```c
/* Yacc calls yylex() for you */
int yylex(void) {
    /* return next token */
}

int main(void) {
    return yyparse();
}
```

Lime:
```c
/* You call Parse() with each token */
int main(void) {
    void *parser = ParseAlloc(malloc);
    int token;
    TokenValue value;

    while ((token = get_next_token(&value)) != 0) {
        Parse(parser, token, value, extra_arg);
    }
    Parse(parser, 0, value, extra_arg);  /* end of input */

    ParseFree(parser, free);
    return 0;
}
```

### Reentrancy

Yacc parsers use global variables (`yylval`, `yychar`, `yydebug`, etc.)
by default. Lime parsers are always reentrant -- the parser state is an
opaque pointer returned by `ParseAlloc()`. Multiple Lime parsers can run
concurrently with no shared state.

### No yylex Integration

Yacc tightly couples the parser to a `yylex()` function (typically generated
by Lex/Flex). Lime has no built-in lexer integration. Write a hand-coded
tokenizer or adapt your Lex output to feed tokens via `Parse()`.

## Directive Mapping (Yacc-Specific)

| Yacc | Lime | Notes |
|------|------|-------|
| `%{ ... %}` (prologue) | `%include { ... }` | Code inserted at file top |
| `%token TOK` | `%token TOK.` | Period required |
| `%left TOK` | `%left TOK.` | Period required |
| `%right TOK` | `%right TOK.` | Period required |
| `%nonassoc TOK` | `%nonassoc TOK.` | Period required |
| `%type <tag> sym` | `%type sym {CType}` | Direct C type, no union tag |
| `%union { ... }` | `%token_type {T}` + per-symbol `%type` | No shared union |
| `%start sym` | `%start_symbol sym` | Different keyword |
| `%prec TOK` | `[TOK]` | Square brackets after rule RHS |
| `$$` | `A` | LHS semantic value |
| `$1`, `$2`, ... | `B`, `C`, ... | RHS semantic values |
| `yyerror(msg)` | `%syntax_error { ... }` | Inline callback block |

## Rule Syntax Translation

Yacc:
```yacc
%%
expr : expr '+' expr    { $$ = $1 + $3; }
     | expr '*' expr    { $$ = $1 * $3; }
     | '(' expr ')'     { $$ = $2; }
     | NUMBER            { $$ = $1; }
     ;
%%
```

Lime:
```
expr(A) ::= expr(B) PLUS expr(C).    { A = B + C; }
expr(A) ::= expr(B) TIMES expr(C).   { A = B * C; }
expr(A) ::= LPAREN expr(B) RPAREN.   { A = B; }
expr(A) ::= NUMBER(B).               { A = B; }
```

Note: Yacc allows single-character literals (`'+'`); Lime requires named
tokens (`PLUS`). You must define named tokens for all punctuation and map
them in your tokenizer.

## Handling `%union`

Most Yacc grammars use `%union` to define the semantic value type:

```yacc
%union {
    int    ival;
    double dval;
    char  *sval;
}
%token <ival> INTEGER
%token <sval> IDENTIFIER
%type <dval> expression
```

Lime approach: define a struct or union as your `%token_type`, and use
per-symbol `%type` for non-terminals:

```
%token_type {YYValue}

%type expression {double}

%include {
    typedef union {
        int    ival;
        double dval;
        char  *sval;
    } YYValue;
}
```

Alternatively, if all tokens share a single type, use that directly:

```
%token_type {int}
```

## Compatibility Notes

- **No `$<tag>N` casts**: Yacc allows `$<ival>3` to cast a semantic value
  through a union member. Lime does not support this. Use explicit casts
  in your action code if needed.

- **No `@N` position tracking**: Yacc's `@1`, `@2` location tracking is
  not built into Lime. Pass position information through your token type
  or extra argument.

- **No `YYACCEPT` / `YYABORT`**: Yacc macros for early accept or abort
  have no direct equivalent. Lime parsers terminate naturally when they
  receive token `0` (end of input) or enter an unrecoverable error state.

- **No `YYERROR`**: The Yacc macro to trigger error recovery from within
  an action is not available. Use your `%extra_argument` to set an error
  flag and check it after parsing completes.

- **No `%expect`**: Yacc allows `%expect N` to suppress conflict warnings.
  Lime reports all conflicts. Resolve them with precedence rules or grammar
  restructuring.

## Quick Migration Checklist

1. Rename `.y` to `.lime`
2. Replace `%{ ... %}` prologue with `%include { ... }`
3. Replace `%union` with `%token_type` and per-symbol `%type`
4. Replace `%start` with `%start_symbol`
5. Add periods to all `%token`, `%left`, `%right`, `%nonassoc` lines
6. Convert rules: `:` to `::=`, `|` to separate rules, `;` to `.`
7. Replace `$$` with `A`, `$1` with `B`, `$2` with `C`, etc.
8. Replace `%prec TOKEN` with `[TOKEN]`
9. Replace character literals (`'+'`) with named tokens (`PLUS`)
10. Replace `yyerror()` with `%syntax_error { ... }` block
11. Rewrite the main loop from `yyparse()` to `Parse()` push model
12. Remove Lex/Flex dependency or adapt scanner to push tokens
