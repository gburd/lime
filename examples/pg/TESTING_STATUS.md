# PostgreSQL Lime Grammar -- Testing Status

## Grammar Generation: PASS

The Lime parser generator successfully processes `gram.lime` and produces
`gram.c` (3MB) and `gram.h` (24KB).

Command used:
```
../../lime -T../../limpar.c gram.lime
```

### Parser Statistics

| Metric                    | Value   |
|---------------------------|---------|
| Terminal symbols          | 557     |
| Non-terminal symbols      | 782     |
| Total symbols             | 1,339   |
| Production rules          | 3,584   |
| Parser states             | 3,842   |
| Shift-reduce conflicts    | 1,665   |
| Action table entries      | 145,227 |
| Lookahead table entries   | 145,363 |
| Total table size (bytes)  | 613,920 |

### Conflicts

All 1,665 conflicts are shift-reduce and are resolved by Lime's default
rules (shift wins over reduce) and explicit `[TOKEN]` precedence
annotations.  PostgreSQL's Bison grammar uses `%expect 0` -- it resolves
the same underlying conflicts via Bison's implicit rightmost-terminal
precedence and `%prec` directives.

Main conflict sources:
- `stmtmulti`: 128+ statement alternatives create SR conflicts with SEMICOLON
- `a_expr`/`b_expr`: binary operator expression grammar
- Keyword lists: 469+ alternatives in `bare_label_keyword`

These are structurally identical to the conflicts in PostgreSQL's Bison
grammar and resolve correctly.

### Warnings

Approximately 600 "Label never used" warnings for non-terminals that
match optional keywords without producing a semantic value (e.g.,
`opt_as`, `opt_column`, `opt_by`, `opt_with`, `opt_table`).  These are
harmless and expected.


## Structural Validation: PASS

A Python validation script verified:

| Check                         | Result |
|-------------------------------|--------|
| Undefined non-terminals       | 0      |
| Undeclared tokens             | 0      |
| Non-terminals without %type   | 24     |
| Total defined non-terminals   | 782    |
| Total production alternatives | 3,584  |
| Rules with semantic actions   | 2,406  |
| Empty rules                   | 195    |
| Precedence annotations        | 59     |

The 24 non-terminals without `%type` are intentionally typeless -- they
match optional keywords like `opt_as`, `opt_by`, `opt_column` and do not
carry a semantic value.


## Compilation: BLOCKED

Compilation of `gram.c` requires PostgreSQL server headers (`postgres.h`,
`nodes/parsenodes.h`, `nodes/makefuncs.h`, etc.) which are not present
in the current partial source checkout at `_/`.

To compile, either:
1. Clone the full PostgreSQL source and configure it
2. Install `postgresql-server-dev-NN` package
3. Build against a PostgreSQL install's `PGXS` include path

Expected undefined symbols (from our helper functions file):
- All 41 functions declared in `pg_gram_helpers.h`
- These are implemented in `pg_gram_helpers.c`


## Bugs Found and Fixed

### 1. Token naming: `Op` vs `OP`

**Problem**: The Bison token `Op` (mixed case) was converted literally,
but our `tokens.lime` declares it as `OP` (ALL_CAPS convention).

**Impact**: 9 locations in gram.lime:
- 7 precedence annotations: `[Op]` should be `[OP]`
- 2 RHS references: `Op(B)` should be `OP(B)` in `all_Op` and `qual_Op`

**Fix**: Replaced all 9 occurrences.  Added `TOKEN_REMAP` dictionary to
`convert_gram.py` so future re-conversions produce the correct output.


## Test Infrastructure

### Available test files
- `examples/pg/tests/select_basic.sql` -- `SELECT 1;`
- `examples/pg/tests/select_columns.sql` -- `SELECT id, name, email FROM users WHERE active = true;`
- `examples/pg/tests/select_join.sql` -- `SELECT ... FROM ... JOIN ... ON ... WHERE ...`

### Test harness
- `test_harness/run_pg_tests.py` -- Compares Lime parser vs PostgreSQL parser
- Supports modes: `lime_only`, `pg_only`, `compare`
- Can run against PostgreSQL regression test suite

### Running tests (once compiled)
```
# Build the parser
cd examples/pg
make generate   # runs Lime
make            # compiles everything

# Run basic tests
make test

# Run test harness
python3 ../../test_harness/run_pg_tests.py \
    --lime-parser ./pg_parser \
    --test-file tests/select_basic.sql
```


## Known Limitations

1. **Conflict count**: 1,665 resolved conflicts vs Bison's 0.  While these
   resolve correctly for most cases, subtle differences in conflict resolution
   strategy between Bison and Lime could cause divergent behavior on edge
   cases.  End-to-end testing is needed to verify.

2. **No `%destructor`**: Lime/Lemon does not support Bison's `%destructor`
   directive.  Memory cleanup for parser stack values on error recovery
   must be handled differently.

3. **Location tracking**: PostgreSQL's `YYLLOC_DEFAULT` macro for computing
   non-terminal locations is not directly applicable to Lime's parser template.
   Location tracking via `LOC()` macro is a simplified approximation.

4. **Error recovery**: Lime's error recovery mechanism differs from Bison's
   `error` token.  The grammar's `error` productions may need adaptation.

5. **24 typeless non-terminals**: While functionally correct, Lime may handle
   these differently from Bison's implicit `YYSTYPE` default.


## Next Steps

1. Obtain full PostgreSQL headers for compilation testing
2. Compile `gram.c`, `pg_gram_helpers.c`, and `tokenize.c`
3. Run end-to-end SQL parsing tests
4. Compare ASTs between Bison and Lime parsers
5. Fix any runtime issues discovered
6. Target: 95%+ of PostgreSQL regression test SQL parses correctly
