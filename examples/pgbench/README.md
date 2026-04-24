# pgbench Expression Parser for Lime

This directory contains a conversion of the PostgreSQL pgbench expression
parser from its original Bison/Flex format into Lime parser generator format.

## Source

Converted from:
- `src/bin/pgbench/exprparse.y` -- Bison grammar (pgbench expressions)
- `src/bin/pgbench/exprscan.l` -- Flex lexer (EXPR state)

## Expression Language

pgbench uses a simple expression language in `\set` commands for
benchmarking scripts. The language supports:

- **Integer and double constants**: `42`, `3.14`, `.5e2`, `1e-3`
- **Boolean constants**: `true`, `false`
- **NULL constant**: `null`
- **Variables**: `:varname`
- **Arithmetic operators**: `+`, `-`, `*`, `/`, `%`
- **Comparison operators**: `<`, `<=`, `>`, `>=`, `=`, `<>`, `!=`
- **Bitwise operators**: `&`, `|`, `#` (xor), `<<`, `>>`, `~`
- **Logical operators**: `and`, `or`, `not`
- **IS predicates**: `is null`, `is not null`, `is true`, `is not false`, etc.
- **ISNULL/NOTNULL postfix**: `isnull`, `notnull`
- **Function calls**: `funcname(arg1, arg2, ...)`
- **CASE expressions**: `case when ... then ... else ... end`

## Directory Structure

```
examples/pgbench/
  pgbench_expr.lime    - Expression grammar in Lime format
  tokenize.h           - Tokenizer public interface
  tokenize.c           - Hand-written expression tokenizer
  tests/               - Test expression files
  Makefile             - Build system
  README.md            - This file
```

## Prerequisites

- A C11-capable compiler (gcc, clang, etc.)
- The Lime parser generator built from the repository root
- GNU Make

## Building

From this directory:

```sh
# Build the parser (also builds lime if needed)
make

# Build standalone tokenizer test tool
make tokenize
```

This will:
1. Build the `lime` tool (if not already built)
2. Generate `pgbench_expr.c` and `pgbench_expr.h` from `pgbench_expr.lime`
3. Compile and link the `pgbench_parse` executable

## Usage

Parse an expression from stdin:

```sh
echo "1 + 2 * 3" | ./pgbench_parse
```

Tokenize an expression (tokenizer-only mode):

```sh
echo ":scale * 100 + random(1, 1000)" | ./pgbench_tokenize
```

Enable parser trace output:

```sh
echo "case when :x > 0 then :x else -:x end" | ./pgbench_parse -t
```

## Running Tests

```sh
make test
```

Place `.expr` files in the `tests/` subdirectory. Each file should contain
a single pgbench expression. The test runner feeds each file to the parser
and reports pass/fail status.

## Cleaning

```sh
make clean      # Remove executables
make distclean  # Also remove generated parser source
```

## Conversion Notes

### Token Mapping

Bison's single-character literal tokens (`'+'`, `'-'`, etc.) become named
tokens in Lime:

| Bison  | Lime    |
|--------|---------|
| `'+'`  | PLUS    |
| `'-'`  | MINUS   |
| `'*'`  | STAR    |
| `'/'`  | SLASH   |
| `'%'`  | PERCENT |
| `'<'`  | LT      |
| `'>'`  | GT      |
| `'='`  | EQ      |
| `'&'`  | AMP     |
| `'\|'` | PIPE    |
| `'#'`  | HASH    |
| `'~'`  | TILDE   |
| `'('`  | LPAREN  |
| `')'`  | RPAREN  |
| `','`  | COMMA   |

### Semantic Actions

- Bison `$$` maps to the named LHS result: `result(R)`, `expr(R)`, etc.
- Bison `$1`, `$2`, etc. map to named RHS parameters: `A`, `B`, `C`, etc.
- Bison `%prec TOKEN` maps to Lime `[TOKEN]` at end of rule.

### Key Differences from Bison

- Lime uses `%include { ... }` instead of `%{ ... %}` for prologue code.
- Lime uses `%token_type` with a union type instead of `%union`.
- Lime uses `%extra_argument` instead of `%parse-param`.
- Lime has explicit `%syntax_error` and `%parse_failure` handlers.
- Lime does not support `%expect` (no shift/reduce conflict counting).
- Empty alternatives use `nt ::= .` (explicit empty RHS with period).

### Standalone AST

This example includes a self-contained AST implementation (in the grammar's
`%include` block) that mirrors the structure from pgbench.h but does not
depend on any PostgreSQL headers. In a production integration, the AST types
and constructor functions would come from the pgbench codebase.
