#!/usr/bin/env python3
"""
Comparison report generator for Bison-vs-Lime parser testing.

Consumes the raw results produced by parser_comparison.py and produces
human-readable and machine-readable reports in several formats:

  - Terminal (ANSI text)
  - JSON
  - CSV
  - HTML (self-contained, no external dependencies)
"""

import csv
import io
import json
import statistics
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence


class Outcome(Enum):
    """Possible outcomes for a single test."""
    PASS = "pass"
    FAIL = "fail"
    SKIP = "skip"
    ERROR = "error"


@dataclass
class ComparisonEntry:
    """One row of the comparison report."""
    source_file: str
    line_number: int
    sql_preview: str
    outcome: Outcome
    message: str
    bison_success: Optional[bool] = None
    lime_success: Optional[bool] = None
    bison_ms: float = 0.0
    lime_ms: float = 0.0
    ast_match: Optional[bool] = None

    def to_dict(self) -> Dict[str, Any]:
        return {
            "source_file": self.source_file,
            "line_number": self.line_number,
            "sql_preview": self.sql_preview,
            "outcome": self.outcome.value,
            "message": self.message,
            "bison_success": self.bison_success,
            "lime_success": self.lime_success,
            "bison_ms": round(self.bison_ms, 3),
            "lime_ms": round(self.lime_ms, 3),
            "ast_match": self.ast_match,
        }


@dataclass
class ReportSummary:
    """Aggregate statistics for the entire test run."""
    total: int = 0
    passed: int = 0
    failed: int = 0
    skipped: int = 0
    errors: int = 0
    bison_parse_successes: int = 0
    lime_parse_successes: int = 0
    ast_matches: int = 0
    ast_mismatches: int = 0
    bison_times_ms: List[float] = field(default_factory=list)
    lime_times_ms: List[float] = field(default_factory=list)
    per_file: Dict[str, Dict[str, int]] = field(default_factory=lambda: defaultdict(lambda: {
        "total": 0, "pass": 0, "fail": 0, "skip": 0, "error": 0,
    }))
    timestamp: str = ""
    mode: str = ""
    bison_name: str = ""
    lime_name: str = ""

    @property
    def tested(self) -> int:
        return self.total - self.skipped

    @property
    def pass_rate(self) -> float:
        return (self.passed / self.tested * 100) if self.tested > 0 else 0.0

    @property
    def bison_avg_ms(self) -> float:
        return statistics.mean(self.bison_times_ms) if self.bison_times_ms else 0.0

    @property
    def lime_avg_ms(self) -> float:
        return statistics.mean(self.lime_times_ms) if self.lime_times_ms else 0.0

    @property
    def bison_p50_ms(self) -> float:
        return statistics.median(self.bison_times_ms) if self.bison_times_ms else 0.0

    @property
    def lime_p50_ms(self) -> float:
        return statistics.median(self.lime_times_ms) if self.lime_times_ms else 0.0

    @property
    def bison_p99_ms(self) -> float:
        if len(self.bison_times_ms) < 2:
            return self.bison_avg_ms
        return _percentile(self.bison_times_ms, 99)

    @property
    def lime_p99_ms(self) -> float:
        if len(self.lime_times_ms) < 2:
            return self.lime_avg_ms
        return _percentile(self.lime_times_ms, 99)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "timestamp": self.timestamp,
            "mode": self.mode,
            "bison_parser": self.bison_name,
            "lime_parser": self.lime_name,
            "total": self.total,
            "tested": self.tested,
            "passed": self.passed,
            "failed": self.failed,
            "skipped": self.skipped,
            "errors": self.errors,
            "pass_rate": round(self.pass_rate, 2),
            "bison_parse_successes": self.bison_parse_successes,
            "lime_parse_successes": self.lime_parse_successes,
            "ast_matches": self.ast_matches,
            "ast_mismatches": self.ast_mismatches,
            "performance": {
                "bison_avg_ms": round(self.bison_avg_ms, 3),
                "lime_avg_ms": round(self.lime_avg_ms, 3),
                "bison_p50_ms": round(self.bison_p50_ms, 3),
                "lime_p50_ms": round(self.lime_p50_ms, 3),
                "bison_p99_ms": round(self.bison_p99_ms, 3),
                "lime_p99_ms": round(self.lime_p99_ms, 3),
            },
            "per_file": dict(self.per_file),
        }


def _percentile(data: List[float], pct: float) -> float:
    """Simple percentile without numpy."""
    s = sorted(data)
    k = (len(s) - 1) * pct / 100.0
    f = int(k)
    c = f + 1 if f + 1 < len(s) else f
    d = k - f
    return s[f] + d * (s[c] - s[f])


class ComparisonReport:
    """Builds and formats a parser comparison report."""

    def __init__(
        self,
        entries: Optional[List[ComparisonEntry]] = None,
        mode: str = "compare",
        bison_name: str = "bison",
        lime_name: str = "lime",
    ):
        self.entries: List[ComparisonEntry] = entries or []
        self.mode = mode
        self.bison_name = bison_name
        self.lime_name = lime_name

    def add(self, entry: ComparisonEntry) -> None:
        self.entries.append(entry)

    # ------------------------------------------------------------------
    # Summary computation
    # ------------------------------------------------------------------

    def summarize(self) -> ReportSummary:
        s = ReportSummary(
            timestamp=datetime.now(timezone.utc).isoformat(),
            mode=self.mode,
            bison_name=self.bison_name,
            lime_name=self.lime_name,
        )

        for e in self.entries:
            s.total += 1
            fname = e.source_file or "<unknown>"
            file_stat = s.per_file[fname]
            file_stat["total"] += 1

            if e.outcome == Outcome.PASS:
                s.passed += 1
                file_stat["pass"] += 1
            elif e.outcome == Outcome.FAIL:
                s.failed += 1
                file_stat["fail"] += 1
            elif e.outcome == Outcome.SKIP:
                s.skipped += 1
                file_stat["skip"] += 1
            elif e.outcome == Outcome.ERROR:
                s.errors += 1
                file_stat["error"] += 1

            if e.bison_success is True:
                s.bison_parse_successes += 1
            if e.lime_success is True:
                s.lime_parse_successes += 1

            if e.ast_match is True:
                s.ast_matches += 1
            elif e.ast_match is False:
                s.ast_mismatches += 1

            if e.bison_ms > 0:
                s.bison_times_ms.append(e.bison_ms)
            if e.lime_ms > 0:
                s.lime_times_ms.append(e.lime_ms)

        return s

    # ------------------------------------------------------------------
    # Formatters
    # ------------------------------------------------------------------

    def format_terminal(self, max_failures: int = 50) -> str:
        """Format the report for terminal output."""
        s = self.summarize()
        lines: List[str] = []

        lines.append("=" * 72)
        lines.append("Parser Comparison Report")
        lines.append(f"  Generated: {s.timestamp}")
        lines.append(f"  Mode:      {s.mode}")
        lines.append(f"  Bison:     {s.bison_name}")
        lines.append(f"  Lime:      {s.lime_name}")
        lines.append("=" * 72)
        lines.append("")
        lines.append(f"  Total statements:   {s.total}")
        lines.append(f"  Tested:             {s.tested}")
        lines.append(f"  Passed:             {s.passed}  ({s.pass_rate:.1f}%)")
        lines.append(f"  Failed:             {s.failed}")
        lines.append(f"  Skipped:            {s.skipped}")
        lines.append(f"  Errors:             {s.errors}")
        lines.append("")

        if s.mode == "compare":
            lines.append(f"  Bison parse OK:     {s.bison_parse_successes}")
            lines.append(f"  Lime  parse OK:     {s.lime_parse_successes}")
            lines.append(f"  AST matches:        {s.ast_matches}")
            lines.append(f"  AST mismatches:     {s.ast_mismatches}")
            lines.append("")

        if s.bison_times_ms or s.lime_times_ms:
            lines.append("  Performance (ms):")
            lines.append(f"    {'':20s} {'avg':>8s} {'p50':>8s} {'p99':>8s}")
            if s.bison_times_ms:
                lines.append(
                    f"    {s.bison_name:20s} "
                    f"{s.bison_avg_ms:8.2f} {s.bison_p50_ms:8.2f} {s.bison_p99_ms:8.2f}"
                )
            if s.lime_times_ms:
                lines.append(
                    f"    {s.lime_name:20s} "
                    f"{s.lime_avg_ms:8.2f} {s.lime_p50_ms:8.2f} {s.lime_p99_ms:8.2f}"
                )
            lines.append("")

        # Per-file breakdown
        if s.per_file:
            lines.append("  Per-file breakdown:")
            for fname in sorted(s.per_file):
                fs = s.per_file[fname]
                tested = fs["total"] - fs["skip"]
                pct = f"{100 * fs['pass'] / tested:.0f}%" if tested > 0 else "N/A"
                lines.append(
                    f"    {fname:40s} {fs['pass']:4d}/{tested:<4d} "
                    f"({pct:>4s})  [{fs['fail']} failed, {fs['skip']} skipped]"
                )
            lines.append("")

        # Failures detail (capped)
        failures = [e for e in self.entries if e.outcome == Outcome.FAIL]
        if failures:
            shown = failures[:max_failures]
            lines.append(f"  Failures ({len(failures)} total, showing {len(shown)}):")
            for e in shown:
                lines.append(f"    {e.source_file}:{e.line_number}")
                lines.append(f"      SQL: {e.sql_preview}")
                lines.append(f"      {e.message}")
            if len(failures) > max_failures:
                lines.append(f"    ... and {len(failures) - max_failures} more")
            lines.append("")

        lines.append("=" * 72)
        return "\n".join(lines)

    def format_json(self, include_entries: bool = False) -> str:
        """Format the report as JSON."""
        s = self.summarize()
        data = s.to_dict()
        if include_entries:
            data["entries"] = [e.to_dict() for e in self.entries]
        else:
            # Include only failures in compact mode
            data["failures"] = [
                e.to_dict() for e in self.entries
                if e.outcome == Outcome.FAIL
            ][:200]
        return json.dumps(data, indent=2)

    def format_csv(self) -> str:
        """Format the report as CSV."""
        buf = io.StringIO()
        writer = csv.writer(buf)
        writer.writerow([
            "source_file", "line_number", "outcome", "message",
            "bison_success", "lime_success", "bison_ms", "lime_ms",
            "ast_match", "sql_preview",
        ])
        for e in self.entries:
            writer.writerow([
                e.source_file, e.line_number, e.outcome.value, e.message,
                e.bison_success, e.lime_success,
                round(e.bison_ms, 3), round(e.lime_ms, 3),
                e.ast_match, e.sql_preview,
            ])
        return buf.getvalue()

    def format_html(self) -> str:
        """Format the report as a self-contained HTML page."""
        s = self.summarize()
        failures = [e for e in self.entries if e.outcome == Outcome.FAIL]

        def esc(text: str) -> str:
            return (
                text.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;")
                .replace('"', "&quot;")
            )

        html_parts = [
            "<!DOCTYPE html>",
            "<html><head><meta charset='utf-8'>",
            "<title>Parser Comparison Report</title>",
            "<style>",
            "  body { font-family: monospace; margin: 2em; background: #fafafa; }",
            "  h1 { border-bottom: 2px solid #333; padding-bottom: .3em; }",
            "  table { border-collapse: collapse; width: 100%; margin: 1em 0; }",
            "  th, td { border: 1px solid #ccc; padding: 4px 8px; text-align: left; }",
            "  th { background: #eee; }",
            "  .pass { color: #2a2; }",
            "  .fail { color: #c22; }",
            "  .skip { color: #888; }",
            "  .stats td:first-child { font-weight: bold; }",
            "</style>",
            "</head><body>",
            f"<h1>Parser Comparison Report</h1>",
            f"<p>Generated: {esc(s.timestamp)}<br>",
            f"Mode: {esc(s.mode)} | Bison: {esc(s.bison_name)} | Lime: {esc(s.lime_name)}</p>",
            "<h2>Summary</h2>",
            "<table class='stats'>",
            f"<tr><td>Total</td><td>{s.total}</td></tr>",
            f"<tr><td>Tested</td><td>{s.tested}</td></tr>",
            f"<tr><td>Passed</td><td class='pass'>{s.passed} ({s.pass_rate:.1f}%)</td></tr>",
            f"<tr><td>Failed</td><td class='fail'>{s.failed}</td></tr>",
            f"<tr><td>Skipped</td><td class='skip'>{s.skipped}</td></tr>",
            f"<tr><td>Errors</td><td>{s.errors}</td></tr>",
            "</table>",
        ]

        if s.bison_times_ms or s.lime_times_ms:
            html_parts.append("<h2>Performance</h2>")
            html_parts.append("<table>")
            html_parts.append("<tr><th>Parser</th><th>avg (ms)</th><th>p50</th><th>p99</th></tr>")
            if s.bison_times_ms:
                html_parts.append(
                    f"<tr><td>{esc(s.bison_name)}</td>"
                    f"<td>{s.bison_avg_ms:.2f}</td>"
                    f"<td>{s.bison_p50_ms:.2f}</td>"
                    f"<td>{s.bison_p99_ms:.2f}</td></tr>"
                )
            if s.lime_times_ms:
                html_parts.append(
                    f"<tr><td>{esc(s.lime_name)}</td>"
                    f"<td>{s.lime_avg_ms:.2f}</td>"
                    f"<td>{s.lime_p50_ms:.2f}</td>"
                    f"<td>{s.lime_p99_ms:.2f}</td></tr>"
                )
            html_parts.append("</table>")

        if failures:
            html_parts.append(f"<h2>Failures ({len(failures)})</h2>")
            html_parts.append("<table>")
            html_parts.append(
                "<tr><th>File</th><th>Line</th><th>SQL</th><th>Message</th></tr>"
            )
            for e in failures[:200]:
                html_parts.append(
                    f"<tr><td>{esc(e.source_file)}</td>"
                    f"<td>{e.line_number}</td>"
                    f"<td>{esc(e.sql_preview)}</td>"
                    f"<td>{esc(e.message)}</td></tr>"
                )
            if len(failures) > 200:
                html_parts.append(
                    f"<tr><td colspan='4'>... and {len(failures) - 200} more</td></tr>"
                )
            html_parts.append("</table>")

        html_parts.append("</body></html>")
        return "\n".join(html_parts)

    # ------------------------------------------------------------------
    # File output helpers
    # ------------------------------------------------------------------

    def write_json(self, path: Path, include_entries: bool = False) -> None:
        path.write_text(self.format_json(include_entries))

    def write_csv(self, path: Path) -> None:
        path.write_text(self.format_csv())

    def write_html(self, path: Path) -> None:
        path.write_text(self.format_html())

    def write_all(self, output_dir: Path, prefix: str = "report") -> Dict[str, Path]:
        """Write all report formats and return a map of format -> path."""
        output_dir.mkdir(parents=True, exist_ok=True)
        paths = {}
        for fmt, ext, writer in [
            ("json", ".json", lambda p: self.write_json(p, include_entries=True)),
            ("csv", ".csv", self.write_csv),
            ("html", ".html", self.write_html),
        ]:
            p = output_dir / f"{prefix}{ext}"
            writer(p)
            paths[fmt] = p
        return paths
