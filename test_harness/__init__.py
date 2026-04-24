"""Test harness for extensible SQL parser.

Modules:
    bison_wrapper       - Uniform interface for Bison/yacc parsers (pg_query, binary, mock)
    lime_wrapper        - Uniform interface for Lime-generated parser binaries
    parser_comparison   - Main test driver with parallel execution
    comparison_report   - Report generator (terminal, JSON, CSV, HTML)
    ast_compare         - AST comparison utilities
    extract_postgres_sql - Extract SQL from PostgreSQL regression tests
    run_tests           - Simple single-parser test runner
    run_pg_tests        - PostgreSQL-specific Bison-vs-Lime runner
"""
