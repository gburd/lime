# Calculator Grammar

This module defines the production rules for a simple arithmetic expression
parser.  It depends on the token declarations from the `calc-tokens` module.

```yaml
name: calc-grammar
version: 1.0.0
description: Production rules for calculator expressions
provides: [rules]
depends: [tokens, precedence]
```

## Top-level Program

A program is a single expression.

```lime
program ::= expr(A). { printf("Result = %d\n", A); }
```

## Arithmetic Expressions

Binary operations follow the precedence declared in the tokens module.

```lime binary-operators
expr(A) ::= expr(B) PLUS expr(C).   { A = B + C; }
expr(A) ::= expr(B) MINUS expr(C).  { A = B - C; }
expr(A) ::= expr(B) TIMES expr(C).  { A = B * C; }
expr(A) ::= expr(B) DIVIDE expr(C). {
    if (C != 0) {
        A = B / C;
    } else {
        fprintf(stderr, "Division by zero\n");
        A = 0;
    }
}
```

## Unary and Grouping

Parenthesized expressions and unary minus.

```lime unary-and-grouping
expr(A) ::= MINUS expr(B). [UMINUS] { A = -B; }
expr(A) ::= LPAREN expr(B) RPAREN.  { A = B; }
```

## Literals

Integer literals are the leaf nodes of the expression tree.

```lime literals
expr(A) ::= INTEGER(B). { A = B; }
```
