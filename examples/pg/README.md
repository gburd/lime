# PostgreSQL Grammar for Lime

This directory contains a conversion of the PostgreSQL SQL grammar from
its original yacc/bison format (`gram.y`) into Lime parser generator format.

## Purpose

PostgreSQL uses a large, complex yacc grammar to parse SQL. This project
converts that grammar to work with the Lime LALR(1) parser generator,
producing a standalone parser that can parse PostgreSQL-compatible SQL
statements.

## Directory Structure

```
examples/pg/
  gram.lime        - PostgreSQL grammar in Lime format
  gram_helpers.c   - Helper functions ported from gram.y actions
  tokenize.c       - SQL tokenizer compatible with the Lime-generated parser
  tests/           - SQL test files for validation
  Makefile         - Build system
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

# Or specify a custom compiler
make CC=clang
```

This will:
1. Build the `lime` tool (if not already built)
2. Generate `gram.c` and `gram.h` from `gram.lime`
3. Compile all source files
4. Link the `pg_parser` executable

## Running Tests

```sh
make test
```

Place `.sql` files in the `tests/` subdirectory. Each file should contain
a single SQL statement or a series of statements. The test runner feeds each
file to the parser and reports pass/fail status.

## Cleaning

```sh
make clean      # Remove object files and executable
make distclean  # Also remove generated parser source
```

## Current Status

**Work in Progress** -- This conversion is being developed incrementally:

- [ ] Token definitions converted from gram.y
- [ ] Type system (%type declarations) converted
- [ ] Production rules converted
- [ ] Helper functions ported
- [ ] Tokenizer adapted for Lime interface
- [ ] Basic SELECT statements parsing
- [ ] Full DML (INSERT, UPDATE, DELETE) support
- [ ] DDL statement support
- [ ] Expression parsing
- [ ] PL/pgSQL support

## Architecture

The PostgreSQL grammar conversion follows this approach:

1. **Token definitions** -- PostgreSQL keyword and operator tokens are mapped
   to Lime `%token` declarations with appropriate precedence and associativity.

2. **Type system** -- The `%union` from gram.y is replaced with Lime's
   `%token_type` and `%type` directives pointing to AST node structures.

3. **Production rules** -- Bison rules are translated to Lime syntax. Bison's
   `$1`, `$2`, etc. become named references in Lime reduction actions.

4. **Semantic actions** -- The C code in reduction actions is ported to
   `gram_helpers.c` where possible, keeping the grammar file focused on
   structure.

5. **Tokenizer** -- A standalone tokenizer (`tokenize.c`) feeds tokens to the
   Lime-generated parser, replacing the flex-based scanner from PostgreSQL.
