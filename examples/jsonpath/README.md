# JSONPath Parser for Lime

This directory contains a conversion of PostgreSQL's JSONPath grammar from
its original Bison/Flex format (`jsonpath_gram.y` / `jsonpath_scan.l`) into
a standalone parser using the Lime parser generator.

## Source

- **Original**: PostgreSQL `src/backend/utils/adt/jsonpath_gram.y` (686 lines, Bison)
  and `src/backend/utils/adt/jsonpath_scan.l` (741 lines, Flex)
- **Converted**: `jsonpath_gram.lime` (Lime grammar, ~430 rules)
  with hand-written `tokenize.c` replacing the Flex scanner

## Directory Structure

```
examples/jsonpath/
  jsonpath_gram.lime     - JSONPath grammar in Lime format
  jsonpath_internal.h    - Standalone data structures (replaces PG headers)
  jsonpath_helpers.h     - Helper function declarations
  jsonpath_helpers.c     - AST construction helpers (from gram.y epilogue)
  tokenize.h             - Tokenizer public API
  tokenize.c             - Hand-written tokenizer (replaces Flex scanner)
  main.c                 - Driver program for testing
  Makefile               - Build system
  tests/                 - Test files (.txt, one expression per line)
```

## Building

From this directory:

```sh
make
```

This will:
1. Build the `lime` tool (if not already built)
2. Generate `jsonpath_gram.c` and `jsonpath_gram.h` from the Lime grammar
3. Compile all source files
4. Link the `jsonpath_parser` executable

## Usage

```sh
# Parse expressions from command line
./jsonpath_parser '$.store.book[*].author'

# Verbose mode (prints AST)
./jsonpath_parser -v '$.store.book[0].title'

# Read from stdin
echo '$.x == 1 && $.y > 5' | ./jsonpath_parser -v

# Parse expressions starting with -
./jsonpath_parser -- '-$.x'
```

## Running Tests

```sh
make test
```

Test files in `tests/` contain one JSONPath expression per line.
Lines starting with `#` are comments. Empty lines are skipped.

## Conversion Notes

### Grammar Mapping (Bison to Lime)

| Bison                    | Lime                              |
|--------------------------|-----------------------------------|
| `%union { ... }`         | `%token_type {JsonPathToken}`     |
| `%parse-param {X *r}`   | `%extra_argument {State *pstate}` |
| `%name-prefix="foo"`    | `%name fooParse`                  |
| `$$ = expr`             | `A = expr`                        |
| `$1`, `$2`, ...         | `B`, `C`, `D`, ...                |
| `%prec TOKEN`           | `[TOKEN]`                         |
| `'$'`, `'@'`, `'.'`     | `DOLLAR`, `AT`, `DOT`             |
| empty alternative        | `rule(A) ::= .`                   |

### Tokenizer

The original Flex scanner (`jsonpath_scan.l`) has been replaced with a
hand-written C tokenizer that implements the same lexical rules:

- Quoted strings with escape sequences (`\n`, `\t`, `\uXXXX`, `\u{XXXXXX}`, `\xHH`)
- Unquoted identifiers with keyword recognition (binary search)
- Numeric literals: decimal, hex (`0x`), octal (`0o`), binary (`0b`), real
- Variables: `$name` and `$"quoted name"`
- Multi-character operators: `&&`, `||`, `**`, `==`, `!=`, `<>`, `<=`, `>=`
- C-style comments: `/* ... */`

### Standalone Operation

This parser operates without PostgreSQL dependencies:

- `JsonPathParseItem` uses `char *` for numeric storage instead of PG's `Numeric` type
- Memory is managed with standard `malloc`/`free` instead of `palloc`/`pfree`
- Regex validation in `like_regex` is deferred (pattern stored as-is)
- A simple linked list replaces PostgreSQL's `List` API

## Cleaning

```sh
make clean      # Remove object files and executable
make distclean  # Also remove generated parser source
```
