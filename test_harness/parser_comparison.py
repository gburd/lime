#!/usr/bin/env python3
"""
Main test driver for comparing Bison and Lime parsers.

Discovers SQL test files, runs them through both parser implementations
in parallel, compares results (AST structure, errors, timing), and
produces detailed reports via comparison_report.py.

Usage examples:

  # Compare Lime against pg_query on the bundled PostgreSQL regression tests
  ./parser_comparison.py \\
      --lime-parser ./builddir/examples/pg/pg_parser \\
      --pg-repo ./_

  # Lime-only smoke test (no Bison/pg_query needed)
  ./parser_comparison.py \\
      --lime-parser ./builddir/examples/pg/pg_parser \\
      --mode lime_only --pg-repo ./_

  # Run a single file
  ./parser_comparison.py \\
      --lime-parser ./builddir/examples/pg/pg_parser \\
      --test-file ./_/src/test/regress/sql/select.sql

  # Parallel execution on all 847 SQL files
  ./parser_comparison.py \\
      --lime-parser ./builddir/examples/pg/pg_parser \\
      --pg-repo ./_ --workers 8 --report-dir ./reports
"""

import argparse
import json
import os
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Any, Dict, Iterator, List, Optional, Tuple

# Ensure the package is importable when run as a script
_HERE = Path(__file__).resolve().parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))

from ast_compare import ASTComparator, PG_LOCATION_FIELDS
from bison_wrapper import BisonBackend, BisonParser
from bison_wrapper import ParseResult as BisonParseResult
from comparison_report import ComparisonEntry, ComparisonReport, Outcome
from extract_postgres_sql import SQLExtractor, SQLStatement
from lime_wrapper import LimeParser
from lime_wrapper import ParseResult as LimeParseResult


# ======================================================================
# Test-level data structures
# ======================================================================

class TestMode(Enum):
    COMPARE = "compare"
    LIME_ONLY = "lime_only"
    BISON_ONLY = "bison_only"


# SQL statement prefixes that indicate psql meta-commands.
PSQL_PREFIXES = ("\\",)

# Files known to need special handling (COPY FROM stdin, psql meta-commands).
KNOWN_PROBLEMATIC = {
    "copy.sql",
    "copyselect.sql",
    "largeobject.sql",
    "psql.sql",
    "psql_crosstab.sql",
}


# ======================================================================
# Test discovery
# ======================================================================

def discover_sql_files(pg_repo: Path) -> List[Path]:
    """Find all regression-test SQL files under the PG repo."""
    regress = pg_repo / "src" / "test" / "regress" / "sql"
    if not regress.is_dir():
        return []
    return sorted(regress.glob("*.sql"))


def discover_contrib_sql_files(pg_repo: Path) -> List[Path]:
    """Find SQL files in contrib/ test directories."""
    contrib = pg_repo / "contrib"
    if not contrib.is_dir():
        return []
    return sorted(contrib.rglob("sql/*.sql"))


def discover_test_cases(test_dir: Path) -> List[Path]:
    """Find .sql and .json test case files under *test_dir*."""
    paths = sorted(test_dir.rglob("*.sql"))
    paths += sorted(test_dir.rglob("*.json"))
    return paths


def extract_statements(sql_file: Path) -> List[SQLStatement]:
    """Split a single SQL file into individual statements."""
    extractor = SQLExtractor.__new__(SQLExtractor)
    extractor.repo_path = sql_file.parent
    extractor.regress_sql_dir = sql_file.parent
    return list(extractor.extract_from_file(sql_file))


def should_skip(stmt: SQLStatement) -> Optional[str]:
    """Return a skip reason, or None if the statement should be tested."""
    if stmt.source_file in KNOWN_PROBLEMATIC:
        return f"problematic file: {stmt.source_file}"
    stripped = stmt.statement.strip()
    if not stripped or stripped == ";":
        return "empty statement"
    for pfx in PSQL_PREFIXES:
        if stripped.startswith(pfx):
            return f"psql meta-command"
    # Skip COPY ... FROM stdin (multi-line data block)
    upper = stripped.upper()
    if upper.startswith("COPY ") and "FROM STDIN" in upper:
        return "COPY FROM stdin"
    return None


# ======================================================================
# Single-statement test logic (runs inside worker processes)
# ======================================================================

def _run_one_compare(
    stmt_dict: Dict[str, Any],
    lime_bin: str,
    lime_args: List[str],
    bison_backend: str,
    bison_bin: Optional[str],
    bison_args: List[str],
    ignore_fields: List[str],
) -> Dict[str, Any]:
    """
    Worker function for parallel execution.

    Accepts and returns plain dicts so everything is pickle-friendly.
    """
    stmt = SQLStatement(
        source_file=stmt_dict["source_file"],
        line_number=stmt_dict["line_number"],
        statement=stmt_dict["statement"],
    )

    skip = should_skip(stmt)
    sql_preview = stmt.statement[:80].replace("\n", " ")

    if skip:
        return {
            "source_file": stmt.source_file,
            "line_number": stmt.line_number,
            "sql_preview": sql_preview,
            "outcome": "skip",
            "message": skip,
        }

    # -- Lime parse --
    lime = LimeParser(Path(lime_bin), extra_args=lime_args, name="lime")
    lime_result = lime.parse(stmt.statement)

    # -- Bison parse --
    backend = BisonBackend(bison_backend)
    bison = BisonParser(
        backend=backend,
        binary_path=Path(bison_bin) if bison_bin else None,
        binary_args=bison_args,
        name="bison",
    )
    bison_result = bison.parse(stmt.statement)

    # -- Compare --
    comparator = ASTComparator(ignore_fields=set(ignore_fields), strict=False)

    outcome = "pass"
    message = ""
    ast_match = None

    if not bison_result.success and not lime_result.success:
        message = "both parsers rejected (consistent)"
    elif bison_result.success and not lime_result.success:
        outcome = "fail"
        message = f"Lime failed: {lime_result.error or ''}"
    elif not bison_result.success and lime_result.success:
        outcome = "fail"
        message = f"Lime accepted but Bison rejected: {bison_result.error or ''}"
    else:
        # Both succeeded -- compare ASTs
        if bison_result.ast is not None and lime_result.ast is not None:
            equal, diff_msg = comparator.compare(bison_result.ast, lime_result.ast)
            ast_match = equal
            if equal:
                message = "ASTs match"
            else:
                outcome = "fail"
                message = f"AST mismatch: {diff_msg}"
        else:
            message = "both parsed (no AST comparison)"

    return {
        "source_file": stmt.source_file,
        "line_number": stmt.line_number,
        "sql_preview": sql_preview,
        "outcome": outcome,
        "message": message,
        "bison_success": bison_result.success,
        "lime_success": lime_result.success,
        "bison_ms": bison_result.elapsed_ms,
        "lime_ms": lime_result.elapsed_ms,
        "ast_match": ast_match,
    }


def _run_one_lime_only(
    stmt_dict: Dict[str, Any],
    lime_bin: str,
    lime_args: List[str],
) -> Dict[str, Any]:
    """Worker for lime_only mode."""
    stmt = SQLStatement(
        source_file=stmt_dict["source_file"],
        line_number=stmt_dict["line_number"],
        statement=stmt_dict["statement"],
    )

    skip = should_skip(stmt)
    sql_preview = stmt.statement[:80].replace("\n", " ")

    if skip:
        return {
            "source_file": stmt.source_file,
            "line_number": stmt.line_number,
            "sql_preview": sql_preview,
            "outcome": "skip",
            "message": skip,
        }

    lime = LimeParser(Path(lime_bin), extra_args=lime_args, name="lime")
    result = lime.parse(stmt.statement)

    return {
        "source_file": stmt.source_file,
        "line_number": stmt.line_number,
        "sql_preview": sql_preview,
        "outcome": "pass" if result.success else "fail",
        "message": "parsed OK" if result.success else (result.error or "parse failed"),
        "bison_success": None,
        "lime_success": result.success,
        "bison_ms": 0.0,
        "lime_ms": result.elapsed_ms,
        "ast_match": None,
    }


def _run_one_bison_only(
    stmt_dict: Dict[str, Any],
    bison_backend: str,
    bison_bin: Optional[str],
    bison_args: List[str],
) -> Dict[str, Any]:
    """Worker for bison_only mode."""
    stmt = SQLStatement(
        source_file=stmt_dict["source_file"],
        line_number=stmt_dict["line_number"],
        statement=stmt_dict["statement"],
    )

    skip = should_skip(stmt)
    sql_preview = stmt.statement[:80].replace("\n", " ")

    if skip:
        return {
            "source_file": stmt.source_file,
            "line_number": stmt.line_number,
            "sql_preview": sql_preview,
            "outcome": "skip",
            "message": skip,
        }

    backend = BisonBackend(bison_backend)
    bison = BisonParser(
        backend=backend,
        binary_path=Path(bison_bin) if bison_bin else None,
        binary_args=bison_args,
        name="bison",
    )
    result = bison.parse(stmt.statement)

    return {
        "source_file": stmt.source_file,
        "line_number": stmt.line_number,
        "sql_preview": sql_preview,
        "outcome": "pass" if result.success else "fail",
        "message": "parsed OK" if result.success else (result.error or "parse failed"),
        "bison_success": result.success,
        "lime_success": None,
        "bison_ms": result.elapsed_ms,
        "lime_ms": 0.0,
        "ast_match": None,
    }


# ======================================================================
# Main driver
# ======================================================================

class ParserComparisonDriver:
    """Orchestrates discovery, parallel execution, and reporting."""

    def __init__(
        self,
        lime_binary: Path,
        lime_args: Optional[List[str]] = None,
        bison_backend: BisonBackend = BisonBackend.PG_QUERY,
        bison_binary: Optional[Path] = None,
        bison_args: Optional[List[str]] = None,
        mode: TestMode = TestMode.COMPARE,
        workers: int = 1,
        max_statements: int = 0,
        verbose: bool = False,
        file_filter: Optional[List[str]] = None,
        ignore_fields: Optional[List[str]] = None,
    ):
        self.lime_binary = lime_binary
        self.lime_args = lime_args or ["--parse", "--output=json"]
        self.bison_backend = bison_backend
        self.bison_binary = bison_binary
        self.bison_args = bison_args or []
        self.mode = mode
        self.workers = max(1, workers)
        self.max_statements = max_statements
        self.verbose = verbose
        self.file_filter = set(file_filter) if file_filter else None
        self.ignore_fields = ignore_fields or list(PG_LOCATION_FIELDS)

        self.report = ComparisonReport(
            mode=mode.value,
            bison_name=f"bison ({bison_backend.value})",
            lime_name=f"lime ({lime_binary.name})",
        )

    def _collect_statements(
        self, pg_repo: Optional[Path] = None, test_files: Optional[List[Path]] = None
    ) -> List[Dict[str, Any]]:
        """Gather all SQL statements to test, as serializable dicts."""
        stmts: List[Dict[str, Any]] = []

        sources: List[Path] = []
        if test_files:
            sources = test_files
        elif pg_repo:
            sources = discover_sql_files(pg_repo)

        if self.file_filter:
            sources = [f for f in sources if f.name in self.file_filter]

        for sql_file in sources:
            for s in extract_statements(sql_file):
                stmts.append({
                    "source_file": s.source_file,
                    "line_number": s.line_number,
                    "statement": s.statement,
                })
                if self.max_statements and len(stmts) >= self.max_statements:
                    return stmts

        return stmts

    def _dict_to_entry(self, d: Dict[str, Any]) -> ComparisonEntry:
        return ComparisonEntry(
            source_file=d["source_file"],
            line_number=d["line_number"],
            sql_preview=d.get("sql_preview", ""),
            outcome=Outcome(d["outcome"]),
            message=d.get("message", ""),
            bison_success=d.get("bison_success"),
            lime_success=d.get("lime_success"),
            bison_ms=d.get("bison_ms", 0.0),
            lime_ms=d.get("lime_ms", 0.0),
            ast_match=d.get("ast_match"),
        )

    def run(
        self,
        pg_repo: Optional[Path] = None,
        test_files: Optional[List[Path]] = None,
    ) -> ComparisonReport:
        """
        Run the comparison and return a populated ComparisonReport.

        Provide either *pg_repo* (to auto-discover regression SQL) or
        *test_files* (explicit list of SQL file paths).
        """
        stmts = self._collect_statements(pg_repo=pg_repo, test_files=test_files)
        total = len(stmts)
        if total == 0:
            print("No statements found.")
            return self.report

        print(f"Collected {total} SQL statements")
        print(f"Mode: {self.mode.value} | Workers: {self.workers}")
        print()

        start = time.monotonic()
        completed = 0

        if self.workers <= 1:
            # Sequential -- avoids pickle overhead
            for s in stmts:
                result_dict = self._run_single(s)
                entry = self._dict_to_entry(result_dict)
                self.report.add(entry)
                completed += 1
                if self.verbose:
                    self._print_progress(entry, completed, total)
                elif completed % 100 == 0 or completed == total:
                    self._print_counter(completed, total)
        else:
            # Parallel via ProcessPoolExecutor
            futures = {}
            with ProcessPoolExecutor(max_workers=self.workers) as pool:
                for s in stmts:
                    fut = pool.submit(self._run_single, s)
                    futures[fut] = s

                for fut in as_completed(futures):
                    result_dict = fut.result()
                    entry = self._dict_to_entry(result_dict)
                    self.report.add(entry)
                    completed += 1
                    if self.verbose:
                        self._print_progress(entry, completed, total)
                    elif completed % 100 == 0 or completed == total:
                        self._print_counter(completed, total)

        elapsed = time.monotonic() - start
        print(f"\nFinished in {elapsed:.1f}s")
        return self.report

    def _run_single(self, stmt_dict: Dict[str, Any]) -> Dict[str, Any]:
        """Dispatch to the appropriate worker function based on mode."""
        if self.mode == TestMode.COMPARE:
            return _run_one_compare(
                stmt_dict,
                str(self.lime_binary),
                self.lime_args,
                self.bison_backend.value,
                str(self.bison_binary) if self.bison_binary else None,
                self.bison_args,
                self.ignore_fields,
            )
        elif self.mode == TestMode.LIME_ONLY:
            return _run_one_lime_only(
                stmt_dict,
                str(self.lime_binary),
                self.lime_args,
            )
        elif self.mode == TestMode.BISON_ONLY:
            return _run_one_bison_only(
                stmt_dict,
                self.bison_backend.value,
                str(self.bison_binary) if self.bison_binary else None,
                self.bison_args,
            )
        else:
            return {
                "source_file": stmt_dict["source_file"],
                "line_number": stmt_dict["line_number"],
                "sql_preview": "",
                "outcome": "error",
                "message": f"unknown mode: {self.mode}",
            }

    def _print_progress(
        self, entry: ComparisonEntry, done: int, total: int
    ) -> None:
        tag = entry.outcome.value.upper()
        src = f"{entry.source_file}:{entry.line_number}"
        print(f"  [{done}/{total}] [{tag:4s}] {src:40s} {entry.sql_preview[:40]}")

    def _print_counter(self, done: int, total: int) -> None:
        pct = 100 * done / total if total else 0
        print(f"  {done}/{total} ({pct:.0f}%)", end="\r", flush=True)


# ======================================================================
# CLI
# ======================================================================

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Compare Bison and Lime parsers on SQL test files",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    p.add_argument(
        "--lime-parser",
        default="./builddir/examples/pg/pg_parser",
        help="Path to Lime-generated parser binary",
    )
    p.add_argument(
        "--lime-args",
        nargs="*",
        default=["--parse", "--output=json"],
        help="CLI arguments for the Lime parser",
    )
    p.add_argument(
        "--pg-repo",
        default="./_",
        help="Path to PostgreSQL source repository (default: ./_)",
    )
    p.add_argument(
        "--test-file",
        action="append",
        help="Run tests from specific SQL file(s) (repeatable)",
    )
    p.add_argument(
        "--test-dir",
        help="Run tests from all .sql/.json files under this directory",
    )
    p.add_argument(
        "--files",
        nargs="+",
        help="Only run specific regression test files by name",
    )
    p.add_argument(
        "--mode",
        choices=["compare", "lime_only", "bison_only"],
        default="compare",
        help="Test mode (default: compare)",
    )
    p.add_argument(
        "--bison-backend",
        choices=["pg_query", "binary", "mock"],
        default="pg_query",
        help="Bison parser backend (default: pg_query)",
    )
    p.add_argument(
        "--bison-binary",
        help="Path to Bison parser binary (for --bison-backend binary)",
    )
    p.add_argument(
        "--bison-args",
        nargs="*",
        default=[],
        help="CLI arguments for the Bison binary",
    )
    p.add_argument(
        "--workers", "-j",
        type=int,
        default=1,
        help="Number of parallel workers (default: 1)",
    )
    p.add_argument(
        "--max-statements",
        type=int,
        default=0,
        help="Cap on total statements to test (0 = unlimited)",
    )
    p.add_argument(
        "--report-dir",
        help="Write JSON/CSV/HTML reports to this directory",
    )
    p.add_argument(
        "--json",
        action="store_true",
        help="Print JSON summary to stdout",
    )
    p.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Print each test result as it completes",
    )

    return p


def main(argv: Optional[List[str]] = None) -> int:
    args = build_parser().parse_args(argv)

    lime_binary = Path(args.lime_parser)
    mode = TestMode(args.mode)

    # Validate availability
    if mode in (TestMode.COMPARE, TestMode.LIME_ONLY):
        if not lime_binary.exists():
            print(f"Warning: Lime parser not found at {lime_binary}")
            if mode == TestMode.LIME_ONLY:
                print("Cannot proceed without Lime parser.")
                return 1
            print("Falling back to bison_only mode.")
            mode = TestMode.BISON_ONLY

    bison_backend = BisonBackend(args.bison_backend)
    bison_binary = Path(args.bison_binary) if args.bison_binary else None

    if mode in (TestMode.COMPARE, TestMode.BISON_ONLY):
        probe = BisonParser(backend=bison_backend, binary_path=bison_binary)
        if not probe.is_available():
            print(f"Warning: {probe.availability_message()}")
            if mode == TestMode.BISON_ONLY:
                print("Cannot proceed without Bison parser.")
                return 1
            print("Falling back to lime_only mode.")
            mode = TestMode.LIME_ONLY

    driver = ParserComparisonDriver(
        lime_binary=lime_binary,
        lime_args=args.lime_args,
        bison_backend=bison_backend,
        bison_binary=bison_binary,
        bison_args=args.bison_args,
        mode=mode,
        workers=args.workers,
        max_statements=args.max_statements,
        verbose=args.verbose,
        file_filter=args.files,
    )

    # Determine input sources
    test_files: Optional[List[Path]] = None
    pg_repo: Optional[Path] = None

    if args.test_file:
        test_files = [Path(f) for f in args.test_file]
        missing = [f for f in test_files if not f.exists()]
        if missing:
            for m in missing:
                print(f"Error: file not found: {m}")
            return 1
    elif args.test_dir:
        test_files = discover_test_cases(Path(args.test_dir))
        if not test_files:
            print(f"No test files found under {args.test_dir}")
            return 1
    else:
        pg_repo = Path(args.pg_repo)
        if not pg_repo.exists():
            print(f"Error: PostgreSQL repo not found at {pg_repo}")
            return 1

    # Run
    report = driver.run(pg_repo=pg_repo, test_files=test_files)

    # Output
    if args.json:
        print(report.format_json())
    else:
        print(report.format_terminal())

    if args.report_dir:
        paths = report.write_all(Path(args.report_dir))
        print(f"\nReports written to:")
        for fmt, p in paths.items():
            print(f"  {fmt}: {p}")

    summary = report.summarize()
    return 0 if summary.failed == 0 and summary.errors == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
