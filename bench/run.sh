#!/usr/bin/env bash
#
# Helper script to build and run JIT comparison benchmark
#
# This script must be run from within the nix development environment:
#   nix develop
#   ./run_jit_benchmark.sh
#

set -e

echo "==================================================================="
echo "  JIT Comparison Benchmark - Build and Run"
echo "==================================================================="
echo ""

# Check for required tools
if ! command -v meson &> /dev/null; then
    echo "ERROR: meson not found in PATH"
    echo ""
    echo "Please run this script from the nix development environment:"
    echo "  nix develop"
    echo "  ./run_jit_benchmark.sh"
    echo ""
    exit 1
fi

# Reconfigure to pick up new benchmark
echo "[1/3] Reconfiguring build..."
meson setup builddir --reconfigure

# Build
echo ""
echo "[2/3] Building project..."
ninja -C builddir

# Check if jit_comparison was built
if [ ! -f "builddir/bench/jit_comparison" ]; then
    echo ""
    echo "ERROR: jit_comparison benchmark was not built"
    echo "Check builddir/meson-logs/meson-log.txt for errors"
    exit 1
fi

# Run benchmark
echo ""
echo "[3/3] Running JIT comparison benchmark..."
echo ""
echo "==================================================================="
echo ""

./builddir/bench/jit_comparison

echo ""
echo "==================================================================="
echo "  Benchmark Complete"
echo "==================================================================="
echo ""
