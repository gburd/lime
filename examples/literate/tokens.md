# Calculator Tokens

This module defines the token declarations and operator precedence for a
simple arithmetic expression parser.

```yaml
name: calc-tokens
version: 1.0.0
description: Token declarations and precedence for calculator
provides: [tokens, precedence]
```

## Parser Configuration

Set up the parser name and token type.

```lime configuration
%name calc
%token_type {int}
%default_type {int}

%include {
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
}

%syntax_error {
    fprintf(stderr, "Syntax error\n");
}
```

## Operator Precedence

Precedence is declared from lowest to highest.  Operators listed earlier
bind less tightly than operators listed later.

```lime precedence
%left PLUS MINUS.
%left TIMES DIVIDE.
%right UMINUS.
```

## Token Declarations

Parentheses and literal tokens used by the grammar.

```lime tokens
%token LPAREN.
%token RPAREN.
%token INTEGER.
```
