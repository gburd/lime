#!/usr/bin/env python3
"""
PostgreSQL grammar test harness.

Compares parsing results between PostgreSQL's native bison parser
and the Lime-generated parser to validate grammar conversion correctness.

Usage:
    ./run_pg_tests.py --lime-parser ./builddir/examples/pg/pg_parser
    ./run_pg_tests.py --pg-repo ./_/ --lime-parser ./builddir/examples/pg/pg_parser
    ./run_pg_tests.py --test-file ./_/src/test/regress/sql/select.sql --lime-parser ...
"""

import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Dict, Iterator, List, Optional, Tuple

try:
    from ast_compare import ASTComparator
    from extract_postgres_sql import SQLExtractor, SQLStatement
except ImportError:
    from .ast_compare import ASTComparator
    from .extract_postgres_sql import SQLExtractor, SQLStatement


class TestOutcome(Enum):
    """Possible outcomes for a test case."""
    PASS = "pass"
    FAIL = "fail"
    SKIP = "skip"
    ERROR = "error"


@dataclass
class ParseResult:
    """Result from running a parser on a SQL statement."""
    success: bool
    ast: Optional[Dict[str, Any]] = None
    error: Optional[str] = None
    raw_output: Optional[str] = None
    elapsed_ms: float = 0.0


@dataclass
class TestResult:
    """Result of comparing two parsers on one SQL statement."""
    statement: SQLStatement
    outcome: TestOutcome
    message: str
    lime_result: Optional[ParseResult] = None
    pg_result: Optional[ParseResult] = None

    @property
    def passed(self) -> bool:
        return self.outcome == TestOutcome.PASS

    @property
    def skipped(self) -> bool:
        return self.outcome == TestOutcome.SKIP


# PostgreSQL regression test files that use features outside standard SQL
# parsing (e.g., psql metacommands, COPY FROM stdin, etc.) and should be
# skipped or handled specially.
KNOWN_PROBLEMATIC_FILES = {
    "copy.sql",          # COPY ... FROM stdin
    "copyselect.sql",    # COPY ... FROM stdin
    "largeobject.sql",   # lo_import psql metacommands
    "psql.sql",          # psql-specific backslash commands
    "psql_crosstab.sql", # psql-specific
}

# SQL statement prefixes that indicate psql metacommands, not SQL.
PSQL_METACOMMAND_PREFIXES = (
    "\\",       # backslash commands
    "COPY ",    # COPY FROM stdin requires special handling
)


class PostgreSQLParser:
    """Interface to PostgreSQL's native parser for reference results.

    This uses pg_query or a custom tool that wraps libpg_query to parse
    SQL statements using PostgreSQL's actual bison-generated parser.
    The output is the PostgreSQL parse tree in JSON format.
    """

    def __init__(self, method: str = "pg_query", pg_query_bin: Optional[Path] = None):
        """
        Initialize PostgreSQL parser interface.

        Args:
            method: How to invoke the PG parser. Options:
                - "pg_query": Use the pg_query Python library
                - "binary": Use a standalone binary that wraps libpg_query
            pg_query_bin: Path to binary (only for method="binary")
        """
        self.method = method
        self.pg_query_bin = pg_query_bin
        self._pg_query_available = None

    def is_available(self) -> bool:
        """Check if the PostgreSQL parser is available."""
        if self.method == "pg_query":
            if self._pg_query_available is None:
                try:
                    import pg_query
                    self._pg_query_available = True
                except ImportError:
                    self._pg_query_available = False
            return self._pg_query_available
        elif self.method == "binary":
            return self.pg_query_bin is not None and self.pg_query_bin.exists()
        return False

    def parse(self, sql: str) -> ParseResult:
        """Parse a SQL statement using PostgreSQL's parser."""
        start = time.monotonic()
        try:
            if self.method == "pg_query":
                return self._parse_pg_query(sql, start)
            elif self.method == "binary":
                return self._parse_binary(sql, start)
            else:
                return ParseResult(
                    success=False,
                    error=f"Unknown parse method: {self.method}",
                    elapsed_ms=_elapsed_ms(start),
                )
        except Exception as e:
            return ParseResult(
                success=False,
                error=f"Exception: {e}",
                elapsed_ms=_elapsed_ms(start),
            )

    def _parse_pg_query(self, sql: str, start: float) -> ParseResult:
        """Parse using the pg_query Python library."""
        try:
            import pg_query
        except ImportError:
            return ParseResult(
                success=False,
                error="pg_query Python package not installed. "
                      "Install with: pip install pg_query",
                elapsed_ms=_elapsed_ms(start),
            )

        try:
            result = pg_query.parse(sql)
            # pg_query returns a protobuf or JSON parse tree
            if hasattr(result, 'parse_tree'):
                ast = result.parse_tree
            else:
                # Newer versions return JSON directly
                ast = json.loads(str(result)) if not isinstance(result, dict) else result
            return ParseResult(
                success=True,
                ast=ast,
                elapsed_ms=_elapsed_ms(start),
            )
        except Exception as e:
            return ParseResult(
                success=False,
                error=str(e),
                elapsed_ms=_elapsed_ms(start),
            )

    def _parse_binary(self, sql: str, start: float) -> ParseResult:
        """Parse using a standalone binary that wraps libpg_query."""
        try:
            result = subprocess.run(
                [str(self.pg_query_bin), "--parse-json"],
                input=sql.encode("utf-8"),
                capture_output=True,
                timeout=30,
            )
            if result.returncode != 0:
                return ParseResult(
                    success=False,
                    error=result.stderr.decode("utf-8", errors="replace"),
                    elapsed_ms=_elapsed_ms(start),
                )
            ast = json.loads(result.stdout.decode("utf-8"))
            return ParseResult(
                success=True,
                ast=ast,
                elapsed_ms=_elapsed_ms(start),
            )
        except subprocess.TimeoutExpired:
            return ParseResult(
                success=False,
                error="PostgreSQL parser timed out (30s)",
                elapsed_ms=_elapsed_ms(start),
            )
        except json.JSONDecodeError as e:
            return ParseResult(
                success=False,
                error=f"Invalid JSON from pg parser: {e}",
                raw_output=result.stdout.decode("utf-8", errors="replace")[:500],
                elapsed_ms=_elapsed_ms(start),
            )


class LimeParser:
    """Interface to the Lime-generated PostgreSQL parser."""

    def __init__(self, binary: Path):
        """
        Initialize Lime parser interface.

        Args:
            binary: Path to the compiled Lime parser binary.
        """
        self.binary = binary

    def is_available(self) -> bool:
        """Check if the Lime parser binary exists."""
        return self.binary.exists()

    def parse(self, sql: str) -> ParseResult:
        """Parse a SQL statement using the Lime-generated parser."""
        start = time.monotonic()
        try:
            result = subprocess.run(
                [str(self.binary), "--parse", "--output=json"],
                input=sql.encode("utf-8"),
                capture_output=True,
                timeout=30,
            )

            if result.returncode != 0:
                return ParseResult(
                    success=False,
                    error=result.stderr.decode("utf-8", errors="replace"),
                    elapsed_ms=_elapsed_ms(start),
                )

            try:
                ast = json.loads(result.stdout.decode("utf-8"))
            except json.JSONDecodeError as e:
                return ParseResult(
                    success=False,
                    error=f"Invalid JSON output: {e}",
                    raw_output=result.stdout.decode("utf-8", errors="replace")[:500],
                    elapsed_ms=_elapsed_ms(start),
                )

            return ParseResult(
                success=True,
                ast=ast,
                elapsed_ms=_elapsed_ms(start),
            )

        except subprocess.TimeoutExpired:
            return ParseResult(
                success=False,
                error="Lime parser timed out (30s)",
                elapsed_ms=_elapsed_ms(start),
            )
        except FileNotFoundError:
            return ParseResult(
                success=False,
                error=f"Lime parser binary not found: {self.binary}",
                elapsed_ms=_elapsed_ms(start),
            )
        except Exception as e:
            return ParseResult(
                success=False,
                error=f"Exception running Lime parser: {e}",
                elapsed_ms=_elapsed_ms(start),
            )


class PostgreSQLTestRunner:
    """
    Test runner that compares PostgreSQL and Lime parser outputs.

    Supports multiple test modes:
    - "compare": Parse with both PG and Lime, compare ASTs
    - "lime_only": Parse with Lime only, check for parse success
    - "pg_only": Parse with PG only (for baseline/reference)
    """

    def __init__(
        self,
        lime_parser: LimeParser,
        pg_parser: Optional[PostgreSQLParser] = None,
        comparator: Optional[ASTComparator] = None,
        mode: str = "lime_only",
        verbose: bool = False,
    ):
        self.lime_parser = lime_parser
        self.pg_parser = pg_parser
        self.comparator = comparator or ASTComparator()
        self.mode = mode
        self.verbose = verbose
        self.results: List[TestResult] = []

    def should_skip_statement(self, stmt: SQLStatement) -> Optional[str]:
        """Check if a statement should be skipped. Returns reason or None."""
        if stmt.source_file in KNOWN_PROBLEMATIC_FILES:
            return f"known problematic file: {stmt.source_file}"

        sql_stripped = stmt.statement.strip()
        for prefix in PSQL_METACOMMAND_PREFIXES:
            if sql_stripped.startswith(prefix):
                return f"psql metacommand: {prefix}..."

        # Skip empty statements
        if not sql_stripped or sql_stripped == ";":
            return "empty statement"

        return None

    def run_single(self, stmt: SQLStatement) -> TestResult:
        """Run a single test on one SQL statement."""
        skip_reason = self.should_skip_statement(stmt)
        if skip_reason:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.SKIP,
                message=skip_reason,
            )

        if self.mode == "compare":
            return self._run_compare(stmt)
        elif self.mode == "lime_only":
            return self._run_lime_only(stmt)
        elif self.mode == "pg_only":
            return self._run_pg_only(stmt)
        else:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.ERROR,
                message=f"Unknown test mode: {self.mode}",
            )

    def _run_lime_only(self, stmt: SQLStatement) -> TestResult:
        """Test that the Lime parser can parse the statement."""
        lime_result = self.lime_parser.parse(stmt.statement)
        if lime_result.success:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.PASS,
                message="parsed successfully",
                lime_result=lime_result,
            )
        else:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.FAIL,
                message=f"parse failed: {lime_result.error}",
                lime_result=lime_result,
            )

    def _run_pg_only(self, stmt: SQLStatement) -> TestResult:
        """Test that the PostgreSQL parser can parse the statement (baseline)."""
        if not self.pg_parser:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.ERROR,
                message="PostgreSQL parser not configured",
            )

        pg_result = self.pg_parser.parse(stmt.statement)
        if pg_result.success:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.PASS,
                message="PG parsed successfully",
                pg_result=pg_result,
            )
        else:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.FAIL,
                message=f"PG parse failed: {pg_result.error}",
                pg_result=pg_result,
            )

    def _run_compare(self, stmt: SQLStatement) -> TestResult:
        """Compare parse results between PostgreSQL and Lime parsers."""
        if not self.pg_parser:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.ERROR,
                message="PostgreSQL parser not configured for compare mode",
            )

        pg_result = self.pg_parser.parse(stmt.statement)
        lime_result = self.lime_parser.parse(stmt.statement)

        # Both fail: that is acceptable (statement might be intentionally invalid)
        if not pg_result.success and not lime_result.success:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.PASS,
                message="both parsers rejected (consistent)",
                lime_result=lime_result,
                pg_result=pg_result,
            )

        # PG succeeds but Lime fails
        if pg_result.success and not lime_result.success:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.FAIL,
                message=f"Lime failed but PG succeeded: {lime_result.error}",
                lime_result=lime_result,
                pg_result=pg_result,
            )

        # PG fails but Lime succeeds (over-acceptance)
        if not pg_result.success and lime_result.success:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.FAIL,
                message=f"Lime accepted but PG rejected: {pg_result.error}",
                lime_result=lime_result,
                pg_result=pg_result,
            )

        # Both succeed: compare ASTs
        equal, diff_msg = self.comparator.compare(pg_result.ast, lime_result.ast)
        if equal:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.PASS,
                message="ASTs match",
                lime_result=lime_result,
                pg_result=pg_result,
            )
        else:
            return TestResult(
                statement=stmt,
                outcome=TestOutcome.FAIL,
                message=f"AST mismatch: {diff_msg}",
                lime_result=lime_result,
                pg_result=pg_result,
            )

    def discover_tests(self, pg_repo: Path) -> Iterator[SQLStatement]:
        """Discover test SQL statements from PostgreSQL regression tests."""
        extractor = SQLExtractor(pg_repo)
        yield from extractor.extract_from_regress_tests()

    def discover_from_file(self, sql_file: Path) -> Iterator[SQLStatement]:
        """Discover test SQL statements from a single file."""
        extractor = SQLExtractor.__new__(SQLExtractor)
        extractor.repo_path = sql_file.parent
        extractor.regress_sql_dir = sql_file.parent
        yield from extractor.extract_from_file(sql_file)

    def run_file(self, sql_file: Path) -> List[TestResult]:
        """Run all tests from a single SQL file."""
        results = []
        for stmt in self.discover_from_file(sql_file):
            result = self.run_single(stmt)
            results.append(result)
            self.results.append(result)
            if self.verbose:
                self._print_result(result)
        return results

    def run_regression_suite(
        self,
        pg_repo: Path,
        file_filter: Optional[List[str]] = None,
        max_statements: int = 0,
    ) -> List[TestResult]:
        """
        Run the full PostgreSQL regression test suite.

        Args:
            pg_repo: Path to the PostgreSQL source repository.
            file_filter: If set, only run files whose names are in this list.
            max_statements: If > 0, stop after this many statements.
        """
        self.results = []
        count = 0

        regress_dir = pg_repo / "src" / "test" / "regress" / "sql"
        if not regress_dir.exists():
            print(f"Error: Regression SQL directory not found: {regress_dir}")
            return self.results

        for sql_file in sorted(regress_dir.glob("*.sql")):
            if file_filter and sql_file.name not in file_filter:
                continue

            if self.verbose:
                print(f"\n--- {sql_file.name} ---")

            extractor = SQLExtractor.__new__(SQLExtractor)
            extractor.repo_path = pg_repo
            extractor.regress_sql_dir = regress_dir
            for stmt in extractor.extract_from_file(sql_file):
                result = self.run_single(stmt)
                self.results.append(result)
                count += 1

                if self.verbose:
                    self._print_result(result)

                if max_statements > 0 and count >= max_statements:
                    return self.results

        return self.results

    def _print_result(self, result: TestResult):
        """Print a single test result."""
        status_map = {
            TestOutcome.PASS: "PASS",
            TestOutcome.FAIL: "FAIL",
            TestOutcome.SKIP: "SKIP",
            TestOutcome.ERROR: "ERR ",
        }
        status = status_map[result.outcome]
        source = f"{result.statement.source_file}:{result.statement.line_number}"
        sql_preview = result.statement.statement[:60].replace("\n", " ")
        print(f"  [{status}] {source:30s} {sql_preview}")
        if result.outcome == TestOutcome.FAIL and self.verbose:
            print(f"         {result.message}")

    def print_summary(self):
        """Print a summary of all test results."""
        total = len(self.results)
        passed = sum(1 for r in self.results if r.outcome == TestOutcome.PASS)
        failed = sum(1 for r in self.results if r.outcome == TestOutcome.FAIL)
        skipped = sum(1 for r in self.results if r.outcome == TestOutcome.SKIP)
        errors = sum(1 for r in self.results if r.outcome == TestOutcome.ERROR)
        tested = total - skipped

        print(f"\n{'=' * 70}")
        print(f"PostgreSQL Grammar Test Results")
        print(f"{'=' * 70}")
        print(f"  Total statements:  {total}")
        print(f"  Tested:            {tested}")
        print(f"  Passed:            {passed}", end="")
        if tested > 0:
            print(f"  ({100 * passed / tested:.1f}%)")
        else:
            print()
        print(f"  Failed:            {failed}")
        print(f"  Skipped:           {skipped}")
        print(f"  Errors:            {errors}")
        print(f"{'=' * 70}")

        # Per-file breakdown
        if self.results:
            print(f"\nPer-file breakdown:")
            file_stats: Dict[str, Dict[str, int]] = {}
            for r in self.results:
                fname = r.statement.source_file
                if fname not in file_stats:
                    file_stats[fname] = {"pass": 0, "fail": 0, "skip": 0, "error": 0}
                file_stats[fname][r.outcome.value] += 1

            for fname in sorted(file_stats.keys()):
                s = file_stats[fname]
                t = sum(s.values())
                p = s["pass"]
                f = s["fail"]
                sk = s["skip"]
                rate = f"{100 * p / (t - sk):.0f}%" if (t - sk) > 0 else "N/A"
                print(f"  {fname:35s} {p:4d}/{t - sk:<4d} passed ({rate})"
                      f"  [{f} failed, {sk} skipped]")

    def get_summary(self) -> Dict[str, Any]:
        """Return summary of test results as a dict."""
        total = len(self.results)
        passed = sum(1 for r in self.results if r.outcome == TestOutcome.PASS)
        failed = sum(1 for r in self.results if r.outcome == TestOutcome.FAIL)
        skipped = sum(1 for r in self.results if r.outcome == TestOutcome.SKIP)
        errors = sum(1 for r in self.results if r.outcome == TestOutcome.ERROR)

        # Collect failed statements for reporting
        failures = []
        for r in self.results:
            if r.outcome == TestOutcome.FAIL:
                failures.append({
                    "source": r.statement.source_file,
                    "line": r.statement.line_number,
                    "sql": r.statement.statement[:200],
                    "message": r.message,
                })

        return {
            "total": total,
            "passed": passed,
            "failed": failed,
            "skipped": skipped,
            "errors": errors,
            "failures": failures[:100],  # Cap at 100 for readability
        }


def _elapsed_ms(start: float) -> float:
    """Calculate elapsed time in milliseconds."""
    return (time.monotonic() - start) * 1000.0


def main():
    """Main entry point for the PostgreSQL test runner."""
    import argparse

    parser = argparse.ArgumentParser(
        description="PostgreSQL grammar test harness for Lime parser validation"
    )
    parser.add_argument(
        "--lime-parser",
        default="./builddir/examples/pg/pg_parser",
        help="Path to Lime-generated PG parser binary "
             "(default: ./builddir/examples/pg/pg_parser)",
    )
    parser.add_argument(
        "--pg-repo",
        default="./_",
        help="Path to PostgreSQL source repository (default: ./_)",
    )
    parser.add_argument(
        "--test-file",
        help="Run tests from a single SQL file instead of the full suite",
    )
    parser.add_argument(
        "--files",
        nargs="+",
        help="Only run specific regression test files (e.g., select.sql insert.sql)",
    )
    parser.add_argument(
        "--mode",
        choices=["compare", "lime_only", "pg_only"],
        default="lime_only",
        help="Test mode: compare both parsers, test Lime only, or PG only "
             "(default: lime_only)",
    )
    parser.add_argument(
        "--pg-method",
        choices=["pg_query", "binary"],
        default="pg_query",
        help="How to invoke PostgreSQL parser (default: pg_query)",
    )
    parser.add_argument(
        "--pg-binary",
        help="Path to pg_query binary (for --pg-method binary)",
    )
    parser.add_argument(
        "--max-statements",
        type=int,
        default=0,
        help="Maximum number of statements to test (0 = unlimited)",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output results as JSON",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Verbose output: print each test result",
    )
    args = parser.parse_args()

    # Set up Lime parser
    lime_binary = Path(args.lime_parser)
    lime_parser = LimeParser(lime_binary)

    if not lime_parser.is_available():
        print(f"Warning: Lime parser not found at {lime_binary}")
        print("Build it first, or specify --lime-parser path.")
        if args.mode != "pg_only":
            print("Continuing in pg_only mode for baseline testing...")
            args.mode = "pg_only"

    # Set up PostgreSQL parser (optional, for compare/pg_only modes)
    pg_parser = None
    if args.mode in ("compare", "pg_only"):
        pg_binary = Path(args.pg_binary) if args.pg_binary else None
        pg_parser = PostgreSQLParser(method=args.pg_method, pg_query_bin=pg_binary)
        if not pg_parser.is_available():
            if args.mode == "compare":
                print("Warning: PostgreSQL parser not available.")
                print("For compare mode, install pg_query: pip install pg_query")
                print("Falling back to lime_only mode.")
                args.mode = "lime_only"
            elif args.mode == "pg_only":
                print("Error: PostgreSQL parser not available for pg_only mode.")
                print("Install pg_query: pip install pg_query")
                return 1

    # Set up comparator with PostgreSQL-specific ignore fields
    comparator = ASTComparator(
        ignore_fields={"location", "stmt_location", "stmt_len", "stmt_location"}
    )

    # Create test runner
    runner = PostgreSQLTestRunner(
        lime_parser=lime_parser,
        pg_parser=pg_parser,
        comparator=comparator,
        mode=args.mode,
        verbose=args.verbose,
    )

    # Run tests
    if args.test_file:
        test_file = Path(args.test_file)
        if not test_file.exists():
            print(f"Error: Test file not found: {test_file}")
            return 1
        print(f"Running tests from {test_file}")
        runner.run_file(test_file)
    else:
        pg_repo = Path(args.pg_repo)
        if not pg_repo.exists():
            print(f"Error: PostgreSQL repo not found at {pg_repo}")
            print("Clone it with: git clone --depth 1 "
                  "https://github.com/postgres/postgres.git _")
            return 1

        print(f"Running PostgreSQL regression tests from {pg_repo}")
        print(f"Mode: {args.mode}")
        if args.files:
            print(f"Files: {', '.join(args.files)}")
        if args.max_statements:
            print(f"Max statements: {args.max_statements}")
        print()

        runner.run_regression_suite(
            pg_repo=pg_repo,
            file_filter=args.files,
            max_statements=args.max_statements,
        )

    # Output results
    if args.json:
        print(json.dumps(runner.get_summary(), indent=2))
    else:
        runner.print_summary()

    # Exit code: 0 if all tested (non-skipped) passed, 1 otherwise
    summary = runner.get_summary()
    return 0 if summary["failed"] == 0 and summary["errors"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
