#!/usr/bin/env python3
"""
Wrapper for Lime-generated parsers.

Provides a uniform interface for invoking Lime parser binaries and
collecting parse results in a structured format suitable for comparison
with Bison parser output.

The Lime parser binary is expected to:
  - Accept SQL on stdin
  - Emit a JSON AST on stdout
  - Return exit code 0 on success, non-zero on parse failure
"""

import json
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional


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


class LimeParser:
    """Uniform interface for Lime-generated parser binaries.

    Each instance wraps a single compiled parser binary.  Multiple
    instances can be created for different Lime grammars (e.g., the main
    SQL grammar, the jsonpath grammar, etc.).
    """

    def __init__(
        self,
        binary_path: Path,
        extra_args: Optional[List[str]] = None,
        timeout: float = 30.0,
        name: str = "lime",
        env: Optional[Dict[str, str]] = None,
    ):
        """
        Args:
            binary_path: Path to the compiled Lime parser binary.
            extra_args: Additional CLI arguments (e.g. ["--output=json"]).
            timeout: Per-statement timeout in seconds.
            name: Human-readable name for this parser instance.
            env: Optional environment variables to set for the subprocess.
        """
        self.binary_path = Path(binary_path)
        self.extra_args = extra_args or ["--parse", "--output=json"]
        self.timeout = timeout
        self.name = name
        self.env = env

    # ------------------------------------------------------------------
    # Availability
    # ------------------------------------------------------------------

    def is_available(self) -> bool:
        """Return True if the parser binary exists and is executable."""
        return self.binary_path.exists()

    def availability_message(self) -> str:
        if self.is_available():
            return f"{self.name}: available at {self.binary_path}"
        return f"{self.name}: binary not found at {self.binary_path}"

    # ------------------------------------------------------------------
    # Parsing
    # ------------------------------------------------------------------

    def parse(self, sql: str) -> ParseResult:
        """Parse *sql* and return a structured ParseResult."""
        start = time.monotonic()

        if not self.binary_path.exists():
            return ParseResult(
                success=False,
                error=f"Binary not found: {self.binary_path}",
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )

        cmd = [str(self.binary_path)] + self.extra_args
        try:
            proc = subprocess.run(
                cmd,
                input=sql.encode("utf-8"),
                capture_output=True,
                timeout=self.timeout,
                env=self.env,
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
        except OSError as exc:
            return ParseResult(
                success=False,
                error=f"OS error: {exc}",
                elapsed_ms=_elapsed_ms(start),
                parser_name=self.name,
            )

        elapsed = _elapsed_ms(start)

        if proc.returncode != 0:
            return ParseResult(
                success=False,
                error=proc.stderr.decode("utf-8", errors="replace"),
                raw_output=proc.stdout.decode("utf-8", errors="replace")[:500],
                elapsed_ms=elapsed,
                parser_name=self.name,
            )

        stdout = proc.stdout.decode("utf-8", errors="replace")
        if not stdout.strip():
            # Parser succeeded but produced no output -- treat as success
            # with an empty AST.
            return ParseResult(
                success=True,
                ast={},
                elapsed_ms=elapsed,
                parser_name=self.name,
            )

        try:
            ast = json.loads(stdout)
        except json.JSONDecodeError as exc:
            return ParseResult(
                success=False,
                error=f"Invalid JSON output: {exc}",
                raw_output=stdout[:500],
                elapsed_ms=elapsed,
                parser_name=self.name,
            )

        stmts = 0
        if isinstance(ast, dict) and "stmts" in ast:
            stmts = len(ast["stmts"])
        elif isinstance(ast, list):
            stmts = len(ast)

        return ParseResult(
            success=True,
            ast=ast,
            elapsed_ms=elapsed,
            parser_name=self.name,
            statement_count=stmts,
        )

    def parse_batch(self, statements: List[str]) -> List[ParseResult]:
        """Parse a list of SQL statements sequentially."""
        return [self.parse(sql) for sql in statements]

    def parse_file(self, path: Path) -> ParseResult:
        """Read and parse an entire SQL file."""
        try:
            sql = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            sql = path.read_text(encoding="latin-1")
        except FileNotFoundError:
            return ParseResult(
                success=False,
                error=f"File not found: {path}",
                parser_name=self.name,
            )
        return self.parse(sql)
