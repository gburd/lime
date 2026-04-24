#!/bin/sh
# Example: Run a benchmark suite against a parser plugin.
#
# Usage: ./benchmark-suite.sh <plugin.so> <grammar.y> <testdir>

set -e

PARSE_MANAGER="${PARSE_MANAGER:-./parse-manager}"

if [ $# -lt 3 ]; then
    echo "Usage: $0 <plugin.so> <grammar.y> <testdir>"
    echo ""
    echo "Benchmarks a parser on all .sql files in <testdir>."
    echo ""
    echo "Example:"
    echo "  $0 sql_parser.so grammar.y bench_inputs/"
    exit 1
fi

PLUGIN="$1"
GRAMMAR="$2"
TESTDIR="$3"

echo "=== Lime Parser Benchmark Suite ==="
echo "Date: $(date -Iseconds)"
echo "Plugin: $PLUGIN"
echo "Grammar: $GRAMMAR"
echo "Test dir: $TESTDIR"
echo ""

echo "Loading plugin..."
$PARSE_MANAGER load "$PLUGIN"

PLUGIN_NAME=$($PARSE_MANAGER list 2>/dev/null | tail -n +3 | head -1 | awk '{print $2}')

echo "Activating with grammar..."
$PARSE_MANAGER activate "$PLUGIN_NAME" "$GRAMMAR"

echo ""
echo "=== Plugin Info ==="
$PARSE_MANAGER info "$PLUGIN_NAME"

# Collect all test files
FILES=""
for f in "$TESTDIR"/*.sql "$TESTDIR"/*.y "$TESTDIR"/*.lime; do
    [ -f "$f" ] && FILES="$FILES $f"
done

if [ -z "$FILES" ]; then
    echo "No test files found in $TESTDIR"
    $PARSE_MANAGER unload "$PLUGIN_NAME"
    exit 1
fi

echo ""
echo "=== Benchmark Results ==="
$PARSE_MANAGER benchmark "$PLUGIN_NAME" $FILES

echo ""
echo "=== Cleanup ==="
$PARSE_MANAGER unload "$PLUGIN_NAME"

echo "Done."
