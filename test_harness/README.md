# Test Harness

Tools for testing the extensible SQL parser.

## Tools

### run_tests.py
Run parser tests from test case files.

Usage:
```bash
./run_tests.py
```

### extract_postgres_sql.py
Extract SQL statements from PostgreSQL regression tests.

Usage:
```bash
./extract_postgres_sql.py /path/to/postgres/repo output_dir/
```

This will extract all SQL statements from `src/test/regress/sql/*.sql` in the PostgreSQL repository.

## Test Case Format

Test cases can be:
- `.sql` files (just SQL, parser success/failure only)
- `.json` files with SQL and expected AST:
  ```json
  {
    "name": "test_name",
    "sql": "SELECT 1;",
    "expected_ast": {},
    "should_fail": false
  }
  ```
