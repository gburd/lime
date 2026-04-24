#!/bin/sh
# Example: Compare two parser plugin versions on a set of test files.
#
# Usage: ./compare-parsers.sh <plugin1.so> <plugin2.so> <grammar.y> <testdir>

set -e

PARSE_MANAGER="${PARSE_MANAGER:-./parse-manager}"

if [ $# -lt 4 ]; then
    echo "Usage: $0 <plugin1.so> <plugin2.so> <grammar.y> <testdir>"
    echo ""
    echo "Compares two parser plugins on all .sql files in <testdir>."
    echo ""
    echo "Example:"
    echo "  $0 parser_v1.so parser_v2.so grammar.y tests/"
    exit 1
fi

PLUGIN1="$1"
PLUGIN2="$2"
GRAMMAR="$3"
TESTDIR="$4"

echo "=== Loading parser plugins ==="
$PARSE_MANAGER load "$PLUGIN1"
$PARSE_MANAGER load "$PLUGIN2"

echo ""
$PARSE_MANAGER list

# Get plugin names
NAME1=$($PARSE_MANAGER list 2>/dev/null | tail -n +3 | head -1 | awk '{print $2}')
NAME2=$($PARSE_MANAGER list 2>/dev/null | tail -n +3 | sed -n '2p' | awk '{print $2}')

echo ""
echo "=== Activating parsers with grammar ==="
$PARSE_MANAGER activate "$NAME1" "$GRAMMAR"
$PARSE_MANAGER activate "$NAME2" "$GRAMMAR"

echo ""
echo "=== Comparing on test files ==="
passed=0
failed=0
total=0

for f in "$TESTDIR"/*.sql; do
    [ -f "$f" ] || continue
    total=$((total + 1))
    echo ""
    echo "--- $f ---"
    if $PARSE_MANAGER compare "$NAME1" "$NAME2" "$f"; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
done

echo ""
echo "=== Summary ==="
echo "  Files tested: $total"
echo "  Passed: $passed"
echo "  Failed/Divergent: $failed"

# Cleanup
$PARSE_MANAGER unload "$NAME1"
$PARSE_MANAGER unload "$NAME2"
