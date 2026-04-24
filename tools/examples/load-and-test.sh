#!/bin/sh
# Example: Load a parser plugin and test it against a SQL file.
#
# Usage: ./load-and-test.sh <plugin.so> <grammar.y> <test.sql>

set -e

PARSE_MANAGER="${PARSE_MANAGER:-./parse-manager}"

if [ $# -lt 3 ]; then
    echo "Usage: $0 <plugin.so> <grammar.y> <test.sql>"
    echo ""
    echo "Example:"
    echo "  $0 /usr/lib/lime/sql_parser.so sql_grammar.y query.sql"
    exit 1
fi

PLUGIN_PATH="$1"
GRAMMAR="$2"
TEST_FILE="$3"

echo "=== Loading plugin ==="
$PARSE_MANAGER load "$PLUGIN_PATH"

echo ""
echo "=== Listing plugins ==="
$PARSE_MANAGER list

echo ""
echo "=== Getting plugin info ==="
PLUGIN_NAME=$($PARSE_MANAGER list 2>/dev/null | tail -n +3 | head -1 | awk '{print $2}')
if [ -n "$PLUGIN_NAME" ]; then
    $PARSE_MANAGER info "$PLUGIN_NAME"
fi

echo ""
echo "=== Activating with grammar ==="
$PARSE_MANAGER activate "$PLUGIN_NAME" "$GRAMMAR"

echo ""
echo "=== Testing ==="
$PARSE_MANAGER test "$PLUGIN_NAME" "$TEST_FILE"

echo ""
echo "=== Benchmarking ==="
$PARSE_MANAGER benchmark "$PLUGIN_NAME" "$TEST_FILE"

echo ""
echo "=== Unloading ==="
$PARSE_MANAGER unload "$PLUGIN_NAME"

echo ""
echo "Done."
