# Test Harness

Tools for testing and comparing Bison and Lime SQL parsers.

## Quick Start

### Compare Bison (pg_query) vs Lime on PostgreSQL regression tests

```bash
# Full comparison with 8 parallel workers
python3 test_harness/parser_comparison.py \
    --lime-parser ./builddir/examples/pg/pg_parser \
    --pg-repo ./_ \
    --mode compare \
    --workers 8 \
    --report-dir ./reports

# Lime-only smoke test (no pg_query needed)
python3 test_harness/parser_comparison.py \
    --lime-parser ./builddir/examples/pg/pg_parser \
    --mode lime_only \
    --pg-repo ./_

# Single file
python3 test_harness/parser_comparison.py \
    --lime-parser ./builddir/examples/pg/pg_parser \
    --test-file ./_/src/test/regress/sql/select.sql

# Specific regression files only
python3 test_harness/parser_comparison.py \
    --lime-parser ./builddir/examples/pg/pg_parser \
    --pg-repo ./_ \
    --files select.sql insert.sql update.sql delete.sql
```

### JSON output

```bash
python3 test_harness/parser_comparison.py \
    --lime-parser ./builddir/examples/pg/pg_parser \
    --pg-repo ./_ --json
```

## Modules

### parser_comparison.py (Main Test Driver)

Orchestrates test discovery, parallel execution, and report generation.
Discovers SQL files from the PostgreSQL regression test suite (in `./_/`)
or from custom test directories, runs each statement through both parsers,
and produces comparison reports.

Key features:
- **Parallel execution** via `ProcessPoolExecutor` (`--workers N`)
- **Three modes**: `compare` (both parsers), `lime_only`, `bison_only`
- **File filtering**: `--files select.sql insert.sql` or `--test-file path`
- **Statement cap**: `--max-statements 1000` for quick sampling
- **Multi-format reports**: `--report-dir ./reports` writes JSON, CSV, HTML

### bison_wrapper.py

Uniform interface for Bison-generated parsers. Supported backends:

| Backend   | Description                                      | Install                  |
|-----------|--------------------------------------------------|--------------------------|
| pg_query  | Python bindings for libpg_query (PostgreSQL)     | `pip install pg_query`   |
| binary    | Any binary that reads SQL on stdin, emits JSON   | Provide `--bison-binary` |
| mock      | Returns canned results for testing the harness   | Built-in                 |

Usage from Python:
```python
from test_harness.bison_wrapper import BisonParser, BisonBackend

parser = BisonParser(backend=BisonBackend.PG_QUERY)
result = parser.parse("SELECT 1;")
print(result.success, result.ast)
```

### lime_wrapper.py

Uniform interface for Lime-generated parser binaries. Each instance wraps
a compiled parser binary that accepts SQL on stdin and emits JSON on stdout.

```python
from test_harness.lime_wrapper import LimeParser
from pathlib import Path

parser = LimeParser(Path("./builddir/examples/pg/pg_parser"))
result = parser.parse("SELECT 1;")
print(result.success, result.ast)
```

### comparison_report.py

Builds and formats comparison reports from test results. Supports:
- **Terminal** (ANSI text with per-file breakdown)
- **JSON** (full data or failures-only)
- **CSV** (one row per statement)
- **HTML** (self-contained, no external deps)

```python
from test_harness.comparison_report import ComparisonReport

report = ComparisonReport(entries=results, mode="compare")
print(report.format_terminal())
report.write_all(Path("./reports"))
```

### ast_compare.py

AST comparison utilities. Compares parse trees with configurable
ignore-fields (e.g., location info that varies between implementations).

### extract_postgres_sql.py

Extract individual SQL statements from PostgreSQL regression test files.

```bash
python3 test_harness/extract_postgres_sql.py /path/to/postgres/repo output_dir/
```

### run_tests.py

Simple single-parser test runner for `.sql` and `.json` test case files.

```bash
python3 test_harness/run_tests.py --parser ./builddir/lemon --test-dir ./test_cases
```

### run_pg_tests.py

PostgreSQL-specific Bison-vs-Lime runner (predecessor of parser_comparison.py).

```bash
python3 test_harness/run_pg_tests.py --lime-parser ./builddir/examples/pg/pg_parser
```

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

## Report Formats

Running with `--report-dir ./reports` produces:

| File          | Contents                                                |
|---------------|---------------------------------------------------------|
| report.json   | Full results with per-statement entries and performance  |
| report.csv    | One row per statement for spreadsheet analysis           |
| report.html   | Self-contained HTML with summary tables and failure list |

## Architecture

```
parser_comparison.py          -- CLI entry point & parallel driver
  |
  +-- bison_wrapper.py        -- BisonParser (pg_query / binary / mock)
  +-- lime_wrapper.py         -- LimeParser (subprocess to compiled binary)
  +-- ast_compare.py          -- ASTComparator (structural diff)
  +-- extract_postgres_sql.py -- SQLExtractor (split .sql into statements)
  +-- comparison_report.py    -- ComparisonReport (terminal/JSON/CSV/HTML)
```

The parallel driver serializes SQLStatement objects to dicts, dispatches
them to worker processes via `ProcessPoolExecutor`, and collects results
into a `ComparisonReport` for formatting.
