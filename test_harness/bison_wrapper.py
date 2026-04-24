#!/usr/bin/env python3
"""
Wrapper for Bison-generated parsers.

Provides a uniform interface for invoking Bison/yacc parsers (including
PostgreSQL's native parser via libpg_query) and collecting parse results
in a structured format suitable for comparison with Lime parser output.

Supported backends:
  - pg_query: Python bindings for libpg_query (PostgreSQL's parser)
  - binary:   Any standalone binary that accepts SQL on stdin and emits
              JSON AST on stdout
  - mock:     Returns canned results for testing the harness itself
"""

import json
import subprocess
import time
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Dict, List, Optional


class BisonBackend(Enum):
    """Available Bison parser backends."""
    PG_QUERY = "pg_query"
    BINARY = "binary"
    MOCK = "mock"


@dataclass
class ParseResult:
    """Structured result from a parser invocation."""
    success: bool
    ast: Optional[Dict[str, Any]] = None
    error: Optional[str] = None
    raw_output: Optional[str] = None
    elapsed_ms: float = 0.0
    parser_name: str = ""
    statement_count: int = 0

    def to_dict(self) -> Dict[str, Any]:
        d = {
            "success": self.success,
            "elapsed_ms": round(self.elapsed_ms, 3),
            "parser_name": self.parser_name,
            "statement_count": self.statement_count,
        }
        if self.ast is not None:
            d["ast"] = self.ast
        if self.error:
            d["error"] = self.error
        if self.raw_output:
            d["raw_output"] = self.raw_output
        return d


def _elapsed_ms(start: float) -> float:
    return (time.monotonic() - start) * 1000.0


class BisonParser:
    """Uniform interface for Bison-generated parsers.

    Wraps multiple backends behind a single .parse() method that returns
    a ParseResult.
    """

    def __init__(
        self,
        backend: BisonBackend = BisonBackend.PG_QUERY,
        binary_path: Optional[Path] = None,
        binary_args: Optional[List[str]] = None,
        timeout: float = 30.0,
        name: str = "bison",
    ):
        """
        Args:
            backend: Which backend to use.
            binary_path: Path to parser binary (for BINARY backend).
            binary_args: Extra CLI args passed to the binary before stdin.
            timeout: Per-statement timeout in seconds.
            name: Human-readable name for this parser instance.
        """
        self.backend = backend
        self.binary_path = binary_path
        self.binary_args = binary_args or []
        self.timeout = timeout
        self.name = name
        self._pg_query_available: Optional[bool] = None

    # ------------------------------------------------------------------
    # Availability
    # ------------------------------------------------------------------

    def is_available(self) -> bool:
        """Return True if the configured backend can be invoked."""
        if self.backend == BisonBackend.PG_QUERY:
            return self._check_pg_query()
        elif self.backend == BisonBackend.BINARY:
            return self.binary_path is not None and self.binary_path.exists()
        elif self.backend == BisonBackend.MOCK:
            return True
        return False

    def availability_message(self) -> str:
        """Return a human-readable status message."""
        if self.is_available():
            return f"{self.name} ({self.backend.value}): available"
        if self.backend == BisonBackend.PG_QUERY:
            return (
                f"{self.name}: pg_query Python package not installed. "
                "Install with: pip install pg_query"
            )
        if self.backend == BisonBackend.BINARY:
            path = self.binary_path or "<not set>"
            return f"{self.name}: binary not found at {path}"
        return f"{self.name}: unknown backend {self.backend}"

    # ------------------------------------------------------------------
    # Parsing
    # ------------------------------------------------------------------

    def parse(self, sql: str) -> ParseResult:
        """Parse *sql* and return a structured ParseResult."""
        start = time.monotonic()
        try:
            if self.backend == BisonBackend.PG_QUERY:
                return self._parse_pg_query(sql, start)
            elif self.backend == BisonBackend.BINARY:
                return self._parse_binary(sql, start)
            elif self.backend == BisonBackend.MOCK:
                return self._parse_mock(sql, start)
            return ParseResult(
                success=False,
                error=f"Unknown backend: {self.backend}",
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )
        except Exception as exc:
            return ParseResult(
                success=False,
                error=f"Exception: {exc}",
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )

    def parse_batch(self, statements: List[str]) -> List[ParseResult]:
        """Parse a list of SQL statements sequentially."""
        return [self.parse(sql) for sql in statements]

    # ------------------------------------------------------------------
    # Backend: pg_query
    # ------------------------------------------------------------------

    def _check_pg_query(self) -> bool:
        if self._pg_query_available is None:
            try:
                import pg_query  # noqa: F401
                self._pg_query_available = True
            except ImportError:
                self._pg_query_available = False
        return self._pg_query_available

    def _parse_pg_query(self, sql: str, start: float) -> ParseResult:
        try:
            import pg_query
        except ImportError:
            return ParseResult(
                success=False,
                error="pg_query not installed",
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )

        try:
            result = pg_query.parse(sql)
            # Handle different pg_query API versions
            if hasattr(result, "parse_tree"):
                ast = result.parse_tree
            elif isinstance(result, dict):
                ast = result
            else:
                ast = json.loads(str(result))

            stmts = 0
            if isinstance(ast, dict) and "stmts" in ast:
                stmts = len(ast["stmts"])
            elif isinstance(ast, list):
                stmts = len(ast)

            return ParseResult(
                success=True,
                ast=ast,
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
                statement_count=stmts,
            )
        except Exception as exc:
            return ParseResult(
                success=False,
                error=str(exc),
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )

    # ------------------------------------------------------------------
    # Backend: standalone binary
    # ------------------------------------------------------------------

    def _parse_binary(self, sql: str, start: float) -> ParseResult:
        if not self.binary_path or not self.binary_path.exists():
            return ParseResult(
                success=False,
                error=f"Binary not found: {self.binary_path}",
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )

        cmd = [str(self.binary_path)] + self.binary_args
        try:
            proc = subprocess.run(
                cmd,
                input=sql.encode("utf-8"),
                capture_output=True,
                timeout=self.timeout,
            )
        except subprocess.TimeoutExpired:
            return ParseResult(
                success=False,
                error=f"Timed out after {self.timeout}s",
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )
        except FileNotFoundError:
            return ParseResult(
                success=False,
                error=f"Binary not found: {self.binary_path}",
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )

        if proc.returncode != 0:
            return ParseResult(
                success=False,
                error=proc.stderr.decode("utf-8", errors="replace"),
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )

        stdout = proc.stdout.decode("utf-8", errors="replace")
        try:
            ast = json.loads(stdout)
        except json.JSONDecodeError as exc:
            return ParseResult(
                success=False,
                error=f"Invalid JSON: {exc}",
                raw_output=stdout[:500],
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )

        return ParseResult(
            success=True,
            ast=ast,
            elapsed_ms=_elapsed_ms(start),
            parser_name=self.name,
        )

    # ------------------------------------------------------------------
    # Backend: mock (for testing the harness)
    # ------------------------------------------------------------------

    def _parse_mock(self, sql: str, start: float) -> ParseResult:
        """Return a trivial AST for any non-empty SQL."""
        sql_stripped = sql.strip().rstrip(";").strip()
        if not sql_stripped:
            return ParseResult(
                success=False,
                error="empty statement",
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )
        return ParseResult(
            success=True,
            ast={"mock": True, "sql": sql_stripped[:100]},
            elapsed_ms=_elapsed_ms(start),
            parser_name=self.name,
            statement_count=1,
        )
