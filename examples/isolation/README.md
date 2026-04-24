# PostgreSQL Isolation Test Spec Parser for Lime

This directory contains a conversion of the PostgreSQL isolation test
specification parser from its original Bison/Flex format into Lime parser
generator format.

## Background

PostgreSQL's isolation test framework (`isolationtester`) runs concurrent
sessions with specific step orderings to test transaction isolation behavior.
Test specifications are written in a `.spec` file format that this parser
handles.

## Spec File Format

```
# Optional global setup (zero or more)
setup { CREATE TABLE t (id int, val text); }

# Optional global teardown
teardown { DROP TABLE t; }

# Sessions (one or more)
session s1
  setup { BEGIN; }
  step s1_update { UPDATE t SET val = 'x' WHERE id = 1; }
  teardown { COMMIT; }

session s2
  setup { BEGIN; }
  step s2_update { UPDATE t SET val = 'y' WHERE id = 1; }
  teardown { COMMIT; }

# Optional explicit permutations
permutation s1_update s2_update(s1_update) s2_update
```

### Blocker Annotations

Permutation steps can have blocker annotations in parentheses:

- `step_name(other_step)` -- wait for `other_step` to block
- `step_name(other_step notices 3)` -- wait for 3 notices from `other_step`
- `step_name(*)` -- wait once (wildcard)
- `step_name(a, b, c)` -- multiple blockers

## Source Files

| File | Description |
|------|-------------|
| `isolation_gram.lime` | Grammar in Lime format (from `specparse.y`) |
| `isolation_defs.h` | Type definitions and function declarations |
| `isolation_tokenize.c` | Hand-written lexer (from `specscanner.l`) |
| `isolation_actions.c` | Semantic action helpers and AST constructors |
| `main.c` | Standalone driver program |
| `Makefile` | Build system |
| `tests/*.spec` | Test specification files |

## Original Sources

| PostgreSQL File | Lines | Description |
|-----------------|-------|-------------|
| `src/test/isolation/specparse.y` | 282 | Bison grammar |
| `src/test/isolation/specscanner.l` | 167 | Flex scanner |

## Building

```sh
make          # Build the parser
make test     # Run tests
make clean    # Remove object files
make distclean # Also remove generated files
```

## Usage

```sh
./isol_parser tests/full_scenario.spec    # parse and display
./isol_parser -q tests/minimal.spec       # validate only
```

## Conversion Notes

1. The `ptr_list` union member (struct with `void **elements` and `int nelements`)
   is replaced with an `IsolPtrList` type using per-nonterminal `%type` declarations.

2. SQL blocks (`{ ... }`) are scanned as single tokens by the hand-written
   lexer, with leading/trailing whitespace trimmed (matching the original Flex
   scanner behavior).

3. Keywords (`session`, `step`, `setup`, `teardown`, `permutation`, `notices`)
   are case-sensitive, matching the original.

4. Quoted identifiers (`"foo"`) with `""` escape for embedded quotes are
   supported.

5. The `*` wildcard in blocker lists produces a `PSB_ONCE` blocker type.
