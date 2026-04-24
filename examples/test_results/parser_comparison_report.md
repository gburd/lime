# Parser Conversion Test Report

**Date**: 2026-04-23
**Tool**: Lime Parser Generator

## Summary

All 6 PostgreSQL sub-parsers have been successfully converted from
Bison/Flex to Lime format. Each parser compiles cleanly and passes
its full test suite.

## Test Results

| Parser       | Source Grammar     | Source Scanner     | Tests | Status |
|--------------|-------------------|--------------------|-------|--------|
| bootstrap    | bootparse.y       | bootscanner.l      | 9/9   | PASS   |
| isolation    | specparse.y       | specscanner.l      | 9/9   | PASS   |
| jsonpath     | jsonpath_gram.y   | jsonpath_scan.l    | 101/101| PASS  |
| pgbench      | exprparse.y       | exprscan.l         | 10/10 | PASS   |
| replication  | repl_gram.y       | repl_scanner.l     | 20/20 | PASS   |
| syncrep      | syncrep_gram.y    | syncrep_scanner.l  | 9/9   | PASS   |

**Total: 159/159 tests pass (100%)**

## Build Status

All parsers compile with `cc -std=c11 -Wall -Wextra -g -O2` and link
without errors. Generated parser code produces expected warnings from
Lime boilerplate (unused variables in destructor/error functions).

## Conversion Approach

Each parser was converted following a consistent pattern:

1. **Grammar (.lime)**: Bison rules converted to Lime syntax
   - `%union` replaced with `%token_type` and `%type` declarations
   - `%parse-param` replaced with `%extra_argument`
   - `$$/$1/$2/...` replaced with `A/B/C/...` named params
   - Single-char tokens (`'(' ')' ','`) replaced with named tokens
   - `%prec TOKEN` replaced with `[TOKEN]`

2. **Tokenizer (.c)**: Flex scanner replaced with hand-written C
   - Keyword tables with binary search
   - State-machine based string/comment handling
   - Token codes from Lime-generated `.h` header

3. **Helpers (.c)**: Grammar action code extracted to separate file
   - AST construction functions
   - PostgreSQL dependencies replaced with standalone equivalents

4. **Standalone operation**: No PostgreSQL headers required
   - Custom list/memory management
   - Simplified type representations

## Feature Coverage by Parser

### jsonpath (101 tests)
- Root/current/last path primaries
- Key access, wildcard, any-level
- Array indexing (single, range, wildcard)
- All 13 method calls (abs, size, type, floor, double, ceiling,
  keyvalue, bigint, boolean, date, integer, number, string)
- datetime/time/timestamp with precision
- Decimal with precision/scale
- Filter predicates with comparisons
- AND/OR/NOT logical operators
- starts with, like_regex with flags
- Strict/lax mode prefixes
- Variables (bare and quoted)
- Arithmetic operators (+, -, *, /, %)
- Numeric formats (decimal, hex, octal, binary, real)
- String escapes (unicode, hex, standard)
- C-style comments

### pgbench (10 tests)
- Arithmetic expressions
- Function calls (random, sqrt, pi, etc.)
- Parenthesized expressions
- Variable references
- NULL handling

### bootstrap (9 tests)
- CREATE/INSERT/OPEN/CLOSE/DECLARE commands
- Type specifications (OID, name, int2, etc.)
- Quoted strings with special characters

### replication (20 tests)
- IDENTIFY_SYSTEM, BASE_BACKUP
- START_REPLICATION with slots and LSN
- CREATE/DROP/READ_REPLICATION_SLOT
- TIMELINE_HISTORY
- UPLOAD_MANIFEST

### syncrep (9 tests)
- ANY/FIRST synchronous modes
- Standby name lists
- Wildcard patterns
- Quoted identifiers

### isolation (9 tests)
- Setup/teardown blocks
- Session definitions with steps
- Permutation specifications
- Multi-blocker scenarios
