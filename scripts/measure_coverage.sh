#!/usr/bin/env bash
#
# Measure test coverage for the extensible SQL parser
#
# This script:
#   1. Builds the project with coverage instrumentation
#   2. Runs all tests
#   3. Generates coverage reports (HTML and summary)
#   4. Reports coverage percentage
#
# Requires: gcov, lcov (or gcovr)
#

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

BUILDDIR="builddir-coverage"
COVERAGE_DIR="coverage-report"

echo "================================================================="
echo "  Test Coverage Measurement"
echo "================================================================="
echo ""

# Clean previous coverage data
if [ -d "$BUILDDIR" ]; then
    echo "[1/5] Cleaning previous coverage build..."
    rm -rf "$BUILDDIR"
fi

if [ -d "$COVERAGE_DIR" ]; then
    echo "      Cleaning previous coverage report..."
    rm -rf "$COVERAGE_DIR"
fi

# Setup build with coverage
echo ""
echo "[2/5] Configuring build with coverage instrumentation..."
meson setup "$BUILDDIR" \
    -Dbuildtype=debug \
    -Db_coverage=true \
    -Dc_args="-O0 -fprofile-arcs -ftest-coverage" \
    -Dc_link_args="--coverage"

# Build
echo ""
echo "[3/5] Building project..."
ninja -C "$BUILDDIR"

# Run tests
echo ""
echo "[4/5] Running test suite..."
cd "$BUILDDIR"
meson test --print-errorlogs

# Generate coverage report
echo ""
echo "[5/5] Generating coverage report..."
cd "$PROJECT_ROOT"

# Try lcov first (more detailed)
if command -v lcov &> /dev/null && command -v genhtml &> /dev/null; then
    echo "      Using lcov/genhtml..."

    # Capture coverage data
    lcov --capture \
         --directory "$BUILDDIR" \
         --output-file "$BUILDDIR/coverage.info" \
         --rc lcov_branch_coverage=1 \
         2>/dev/null || true

    # Filter out system headers and test code
    lcov --remove "$BUILDDIR/coverage.info" \
         '/usr/*' \
         '*/tests/*' \
         '*/bench/*' \
         --output-file "$BUILDDIR/coverage_filtered.info" \
         --rc lcov_branch_coverage=1 \
         2>/dev/null || true

    # Generate HTML report
    genhtml "$BUILDDIR/coverage_filtered.info" \
            --output-directory "$COVERAGE_DIR" \
            --title "Extensible SQL Parser Coverage" \
            --legend \
            --rc lcov_branch_coverage=1 \
            2>/dev/null || true

    echo ""
    echo "HTML coverage report: $COVERAGE_DIR/index.html"
    echo ""

    # Print summary
    lcov --summary "$BUILDDIR/coverage_filtered.info" \
         --rc lcov_branch_coverage=1 2>&1 | grep -E "(lines|functions|branches)"

# Fall back to gcovr
elif command -v gcovr &> /dev/null; then
    echo "      Using gcovr..."

    mkdir -p "$COVERAGE_DIR"

    # Generate HTML report
    gcovr --root "$PROJECT_ROOT" \
          --filter 'src/.*\.c$' \
          --exclude '.*test.*' \
          --exclude '.*bench.*' \
          --html --html-details \
          --output "$COVERAGE_DIR/index.html" \
          "$BUILDDIR"

    echo ""
    echo "HTML coverage report: $COVERAGE_DIR/index.html"
    echo ""

    # Print summary
    gcovr --root "$PROJECT_ROOT" \
          --filter 'src/.*\.c$' \
          --exclude '.*test.*' \
          --exclude '.*bench.*' \
          "$BUILDDIR"

# Fall back to simple gcov
elif command -v gcov &> /dev/null; then
    echo "      Using gcov (basic)..."

    mkdir -p "$COVERAGE_DIR"

    cd "$BUILDDIR"
    for src in ../src/*.c; do
        base=$(basename "$src" .c)
        if [ -f "src/$base.c.gcda" ]; then
            gcov -o src "$src" > /dev/null 2>&1
        fi
    done

    # Count coverage
    total_lines=0
    covered_lines=0

    for gcov_file in *.gcov; do
        if [ -f "$gcov_file" ]; then
            lines=$(grep -v '^ *-:' "$gcov_file" | grep -c '^ *[0-9]' || true)
            covered=$(grep -v '^ *-:' "$gcov_file" | grep -c '^ *[1-9]' || true)
            total_lines=$((total_lines + lines))
            covered_lines=$((covered_lines + covered))
        fi
    done

    if [ $total_lines -gt 0 ]; then
        coverage_pct=$(awk "BEGIN {printf \"%.1f\", ($covered_lines / $total_lines) * 100}")
        echo ""
        echo "Line coverage: $covered_lines / $total_lines ($coverage_pct%)"
    fi

    mv *.gcov "$PROJECT_ROOT/$COVERAGE_DIR/" 2>/dev/null || true
    cd "$PROJECT_ROOT"

else
    echo ""
    echo "ERROR: No coverage tool found."
    echo "Please install one of: lcov, gcovr, gcov"
    exit 1
fi

echo ""
echo "================================================================="
echo "  Coverage analysis complete"
echo "================================================================="
echo ""
echo "Next steps:"
echo "  1. Review coverage report: $COVERAGE_DIR/index.html"
echo "  2. Identify uncovered code paths"
echo "  3. Add tests to improve coverage"
echo ""
