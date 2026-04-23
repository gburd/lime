#!/usr/bin/env python3
"""
Test harness for extensible SQL parser.
Runs parser on SQL test cases and compares output.
"""

import subprocess
import json
import sys
from pathlib import Path
from typing import List, Dict, Optional, Any
from dataclasses import dataclass, field

try:
    from ast_compare import ASTComparator
except ImportError:
    from .ast_compare import ASTComparator


@dataclass
class TestCase:
    """Single test case with SQL and expected output."""
    name: str
    sql: str
    expected_ast: Optional[Dict[str, Any]] = None
    should_fail: bool = False

    @classmethod
    def from_file(cls, path: Path) -> 'TestCase':
        """Load test case from file."""
        # Support .sql files (just SQL, no expected output yet)
        if path.suffix == '.sql':
            return cls(
                name=path.stem,
                sql=path.read_text(),
                expected_ast=None
            )
        # Support .json files with SQL + expected AST
        elif path.suffix == '.json':
            data = json.loads(path.read_text())
            return cls(
                name=data.get('name', path.stem),
                sql=data['sql'],
                expected_ast=data.get('expected_ast'),
                should_fail=data.get('should_fail', False)
            )
        else:
            raise ValueError(f"Unsupported test file format: {path}")


@dataclass
class TestResult:
    """Result of running a single test case."""
    name: str
    passed: bool
    message: str
    error: Optional[str] = None


class ParserTestHarness:
    """Test harness for running parser tests."""

    def __init__(self, parser_binary: Path):
        self.parser_binary = parser_binary
        self.results: List[TestResult] = []
        self.comparator = ASTComparator()

    def run_parser(self, sql: str) -> Dict[str, Any]:
        """Run parser on SQL and return result."""
        try:
            result = subprocess.run(
                [str(self.parser_binary), "--parse", "--output=json"],
                input=sql.encode(),
                capture_output=True,
                timeout=30
            )

            if result.returncode != 0:
                return {
                    "error": result.stderr.decode(),
                    "parse_failed": True
                }

            # Parse JSON output
            try:
                return json.loads(result.stdout.decode())
            except json.JSONDecodeError as e:
                return {
                    "error": f"Invalid JSON output: {e}",
                    "raw_output": result.stdout.decode()[:500],
                    "parse_failed": True
                }

        except subprocess.TimeoutExpired:
            return {
                "error": "Parser timed out (30s)",
                "parse_failed": True
            }
        except FileNotFoundError:
            return {
                "error": f"Parser binary not found: {self.parser_binary}",
                "parse_failed": True
            }
        except Exception as e:
            return {
                "error": f"Exception running parser: {e}",
                "parse_failed": True
            }

    def run_test(self, test: TestCase) -> TestResult:
        """Run single test case and return result."""
        ast = self.run_parser(test.sql)

        # Check if failure is expected
        if test.should_fail:
            if ast.get("parse_failed"):
                result = TestResult(test.name, True, "expected failure")
                self.results.append(result)
                return result
            else:
                result = TestResult(test.name, False, "should have failed but didn't")
                self.results.append(result)
                return result

        # Check for unexpected failure
        if ast.get("parse_failed"):
            result = TestResult(
                test.name, False, "parse failed",
                error=ast.get("error")
            )
            self.results.append(result)
            return result

        # If we have expected AST, compare
        if test.expected_ast is not None:
            equal, msg = self.comparator.compare(test.expected_ast, ast)
            if equal:
                result = TestResult(test.name, True, "AST matches")
            else:
                result = TestResult(test.name, False, f"AST mismatch: {msg}")
            self.results.append(result)
            return result
        else:
            # No expected output, just check it parses
            result = TestResult(test.name, True, "parse only")
            self.results.append(result)
            return result

    def run_all_tests(self, tests: List[TestCase]) -> bool:
        """Run all tests and return overall success."""
        if not tests:
            print("No tests to run")
            return True

        self.results = []

        for test in tests:
            result = self.run_test(test)
            status = "PASS" if result.passed else "FAIL"
            detail = f" ({result.message})" if result.message else ""
            print(f"  [{status}] {result.name}{detail}")
            if result.error:
                print(f"         Error: {result.error}")

        passed = sum(1 for r in self.results if r.passed)
        failed = sum(1 for r in self.results if not r.passed)
        total = len(self.results)

        print(f"\n{'='*60}")
        print(f"Results: {passed}/{total} passed, {failed} failed ({100*passed/total:.1f}%)")
        print(f"{'='*60}")

        return failed == 0

    def get_summary(self) -> Dict[str, Any]:
        """Return summary of test results as dict."""
        return {
            "total": len(self.results),
            "passed": sum(1 for r in self.results if r.passed),
            "failed": sum(1 for r in self.results if not r.passed),
            "results": [
                {
                    "name": r.name,
                    "passed": r.passed,
                    "message": r.message,
                    "error": r.error
                }
                for r in self.results
            ]
        }


def load_test_cases(test_dir: Path) -> List[TestCase]:
    """Load all test cases from directory."""
    tests = []

    for path in sorted(test_dir.glob("**/*.sql")):
        tests.append(TestCase.from_file(path))

    for path in sorted(test_dir.glob("**/*.json")):
        tests.append(TestCase.from_file(path))

    return tests


def main():
    """Main entry point."""
    import argparse

    parser = argparse.ArgumentParser(description="Run SQL parser tests")
    parser.add_argument(
        "--parser", "-p",
        default="./builddir/lemon",
        help="Path to parser binary (default: ./builddir/lemon)"
    )
    parser.add_argument(
        "--test-dir", "-t",
        default="./test_cases",
        help="Path to test case directory (default: ./test_cases)"
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output results as JSON"
    )
    args = parser.parse_args()

    # Find parser binary
    parser_bin = Path(args.parser)
    if not parser_bin.exists():
        print(f"Error: Parser binary not found at {parser_bin}")
        print("Run 'meson compile -C builddir' first")
        return 1

    # Load test cases
    test_dir = Path(args.test_dir)
    if not test_dir.exists():
        print(f"Warning: Test directory {test_dir} not found")
        print("Creating example test case...")
        test_dir.mkdir(parents=True, exist_ok=True)
        simple_dir = test_dir / "simple"
        simple_dir.mkdir(exist_ok=True)
        (simple_dir / "select.sql").write_text("SELECT 1;\n")

    tests = load_test_cases(test_dir)
    print(f"Loaded {len(tests)} test case(s) from {test_dir}\n")

    # Run tests
    harness = ParserTestHarness(parser_bin)
    success = harness.run_all_tests(tests)

    if args.json:
        print(json.dumps(harness.get_summary(), indent=2))

    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
