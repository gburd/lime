#!/usr/bin/env python3
"""
Extract SQL test cases from PostgreSQL regression tests.

Usage:
    ./extract_postgres_sql.py /path/to/postgres/repo output_dir/
"""

import re
import sys
from pathlib import Path
from typing import Iterator, Tuple
from dataclasses import dataclass


@dataclass
class SQLStatement:
    """Extracted SQL statement with metadata."""
    source_file: str
    line_number: int
    statement: str

    def to_test_case(self) -> dict:
        """Convert to test case format."""
        return {
            "name": f"{self.source_file}_{self.line_number}",
            "sql": self.statement,
            "source": {
                "file": self.source_file,
                "line": self.line_number
            }
        }


class SQLExtractor:
    """Extract SQL statements from PostgreSQL test files."""

    def __init__(self, postgres_repo: Path):
        self.repo_path = postgres_repo
        self.regress_sql_dir = postgres_repo / "src" / "test" / "regress" / "sql"

        if not self.regress_sql_dir.exists():
            raise ValueError(
                f"PostgreSQL regress SQL directory not found: {self.regress_sql_dir}"
            )

    def extract_from_regress_tests(self) -> Iterator[SQLStatement]:
        """Extract from src/test/regress/sql/*.sql"""
        for sql_file in sorted(self.regress_sql_dir.glob("*.sql")):
            yield from self.extract_from_file(sql_file)

    def extract_from_file(self, sql_file: Path) -> Iterator[SQLStatement]:
        """Extract SQL statements from a single file."""
        try:
            content = sql_file.read_text(encoding='utf-8')
        except UnicodeDecodeError:
            try:
                content = sql_file.read_text(encoding='latin-1')
            except Exception as e:
                print(f"Warning: Could not read {sql_file}: {e}", file=sys.stderr)
                return

        filename = sql_file.name

        for line_num, stmt in self.split_statements(content):
            yield SQLStatement(
                source_file=filename,
                line_number=line_num,
                statement=stmt
            )

    def split_statements(self, sql: str) -> Iterator[Tuple[int, str]]:
        """
        Split SQL into individual statements.
        Returns (line_number, statement) tuples.
        """
        lines = sql.split('\n')
        current_stmt = []
        start_line = 0
        in_multiline_comment = False

        for i, line in enumerate(lines, 1):
            # Handle multiline comments /* ... */
            if '/*' in line and '*/' not in line:
                in_multiline_comment = True
                continue
            if '*/' in line:
                in_multiline_comment = False
                continue
            if in_multiline_comment:
                continue

            # Strip single-line comments
            if '--' in line:
                line = line[:line.index('--')]

            line = line.strip()

            # Skip empty lines
            if not line:
                continue

            # Start of new statement
            if not current_stmt:
                start_line = i

            current_stmt.append(line)

            # Check if statement ends with semicolon
            if line.endswith(';'):
                stmt = '\n'.join(current_stmt)
                yield (start_line, stmt)
                current_stmt = []

        # Handle statement without trailing semicolon
        if current_stmt:
            stmt = '\n'.join(current_stmt)
            if stmt.strip():
                yield (start_line, stmt)

    def get_statistics(self) -> dict:
        """Get statistics about SQL files."""
        stats = {
            'total_files': 0,
            'total_statements': 0,
            'files': {}
        }

        for sql_file in self.regress_sql_dir.glob("*.sql"):
            count = sum(1 for _ in self.extract_from_file(sql_file))
            stats['total_files'] += 1
            stats['total_statements'] += count
            stats['files'][sql_file.name] = count

        return stats


def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print("Usage: extract_postgres_sql.py <postgres_repo> [output_dir]")
        print("\nExample:")
        print("  ./extract_postgres_sql.py ~/postgres ./test_cases/postgres")
        return 1

    postgres_repo = Path(sys.argv[1])
    output_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("./test_cases/postgres")

    if not postgres_repo.exists():
        print(f"Error: PostgreSQL repo not found: {postgres_repo}")
        return 1

    # Create output directory
    output_dir.mkdir(parents=True, exist_ok=True)

    # Extract SQL
    print(f"Extracting SQL from {postgres_repo}")
    extractor = SQLExtractor(postgres_repo)

    # Get statistics first
    stats = extractor.get_statistics()
    print(f"\nFound {stats['total_statements']} statements in {stats['total_files']} files")

    # Extract and save
    count = 0
    for stmt in extractor.extract_from_regress_tests():
        # Create subdirectory by source file
        source_name = stmt.source_file.replace('.sql', '')
        subdir = output_dir / source_name
        subdir.mkdir(exist_ok=True)

        # Save statement
        test_file = subdir / f"line_{stmt.line_number:04d}.sql"
        test_file.write_text(stmt.statement)
        count += 1

        if count % 100 == 0:
            print(f"Extracted {count} statements...", end='\r')

    print(f"\nExtracted {count} test cases to {output_dir}")

    # Print summary by file
    print("\nStatements per file:")
    for filename, stmt_count in sorted(stats['files'].items()):
        print(f"  {filename:30s} {stmt_count:4d} statements")

    return 0


if __name__ == '__main__':
    sys.exit(main())
