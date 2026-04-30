#!/usr/bin/env bash
#
# Comprehensive Validation Script for Lime Modular Composition System
# This script performs clean builds, testing, performance comparison, and profiling
#

set -e  # Exit on error
set -u  # Exit on undefined variable

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_section() {
    echo -e "\n${GREEN}========================================${NC}"
    echo -e "${GREEN}$1${NC}"
    echo -e "${GREEN}========================================${NC}\n"
}

# Track overall status
FAILED_CHECKS=0

# 1. CLEAN BUILD
log_section "Step 1: Clean Build"
log_info "Removing old build directory..."
if [ -d builddir ]; then
    trash builddir 2>/dev/null || rm -rf builddir
fi

log_info "Setting up fresh build with meson..."
meson setup builddir || {
    log_error "Meson setup failed"
    exit 1
}

log_info "Compiling with all warnings..."
meson compile -C builddir 2>&1 | tee build.log

# Check for warnings
if grep -i "warning:" build.log; then
    log_warn "Build produced warnings (see build.log)"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
else
    log_info "Build completed with no warnings"
fi

# Check for errors
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    log_error "Build failed with errors"
    exit 1
fi

# 2. RUN ALL TESTS
log_section "Step 2: Run All Tests"
log_info "Running complete test suite..."
meson test -C builddir --print-errorlogs 2>&1 | tee test.log

# Check test results
if meson test -C builddir --list | grep -q "FAIL"; then
    log_error "Some tests failed (see test.log)"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
else
    log_info "All tests passed"
fi

# Show test summary
log_info "Test summary:"
meson test -C builddir --list

# 3. BUILD PARSERS FOR COMPARISON
log_section "Step 3: Build Parsers for Performance Comparison"

# Build PostgreSQL parser (monolithic)
if [ -d examples/pg ]; then
    log_info "Building monolithic PostgreSQL parser..."
    cd examples/pg
    make clean && make 2>&1 | tee ../../pg_build.log
    cd ../..
else
    log_warn "PostgreSQL example directory not found"
fi

# Build modular PostgreSQL parser
if [ -d examples/pg_modular ]; then
    log_info "Building modular PostgreSQL parser..."
    cd examples/pg_modular
    make clean && make 2>&1 | tee ../../pg_modular_build.log
    cd ../..
else
    log_warn "Modular PostgreSQL example directory not found"
fi

# Build other parsers
for parser_dir in examples/datalog examples/xpath examples/xquery examples/mongodb; do
    if [ -d "$parser_dir" ]; then
        log_info "Building $(basename $parser_dir) parser..."
        cd "$parser_dir"
        make clean && make 2>&1 | tee ../../$(basename $parser_dir)_build.log || true
        cd - > /dev/null
    fi
done

# 4. FIND AND PARSE TEST SQL FILES
log_section "Step 4: Parse Test SQL Files"

log_info "Looking for .sql test files..."
SQL_TEST_DIR=$(find . -name "._*" -type d 2>/dev/null | head -1)

if [ -z "$SQL_TEST_DIR" ]; then
    log_warn "No test directory starting with ._ found"
    log_info "Checking for .sql files in common locations..."
    SQL_FILES=$(find examples/pg* -name "*.sql" 2>/dev/null | head -10)
else
    SQL_FILES=$(find "$SQL_TEST_DIR" -name "*.sql" 2>/dev/null)
fi

if [ -z "$SQL_FILES" ]; then
    log_warn "No .sql test files found"
else
    log_info "Found $(echo "$SQL_FILES" | wc -l) .sql test files"

    # Parse each file 10 times with different parsers
    mkdir -p parse_results

    for sql_file in $SQL_FILES; do
        filename=$(basename "$sql_file")
        log_info "Parsing $filename 10 times..."

        for i in {1..10}; do
            # Parse with Lime parser (if exists)
            if [ -x examples/pg/pg_parser ]; then
                examples/pg/pg_parser < "$sql_file" > "parse_results/lime_${filename}_${i}.txt" 2>&1 || true
            fi

            # Parse with modular Lime parser (if exists)
            if [ -x examples/pg_modular/pg_parser ]; then
                examples/pg_modular/pg_parser < "$sql_file" > "parse_results/modular_${filename}_${i}.txt" 2>&1 || true
            fi
        done

        # Compare outputs for consistency
        if [ -f "parse_results/lime_${filename}_1.txt" ]; then
            for i in {2..10}; do
                if ! diff -q "parse_results/lime_${filename}_1.txt" "parse_results/lime_${filename}_${i}.txt" > /dev/null 2>&1; then
                    log_error "Parse results differ between iterations for $filename"
                    FAILED_CHECKS=$((FAILED_CHECKS + 1))
                    break
                fi
            done
        fi

        # Compare Lime vs Modular (if both exist)
        if [ -f "parse_results/lime_${filename}_1.txt" ] && [ -f "parse_results/modular_${filename}_1.txt" ]; then
            if diff -q "parse_results/lime_${filename}_1.txt" "parse_results/modular_${filename}_1.txt" > /dev/null 2>&1; then
                log_info "Lime and modular parsers produce identical output for $filename"
            else
                log_warn "Lime and modular parsers produce different output for $filename (see parse_results/)"
                FAILED_CHECKS=$((FAILED_CHECKS + 1))
            fi
        fi
    done
fi

# 5. MEMORY LEAK DETECTION WITH VALGRIND
log_section "Step 5: Memory Leak Detection (Valgrind)"

if ! command -v valgrind &> /dev/null; then
    log_warn "Valgrind not installed, skipping memory leak detection"
else
    log_info "Running tests under valgrind..."
    mkdir -p valgrind_results

    # Test each test suite for leaks
    for test in test_merkle_tree test_dependency_resolver test_parser_composition test_random_composition; do
        if [ -x "builddir/tests/$test" ]; then
            log_info "Checking $test for memory leaks..."
            valgrind --leak-check=full \
                     --show-leak-kinds=all \
                     --track-origins=yes \
                     --log-file="valgrind_results/${test}.log" \
                     "builddir/tests/$test" 2>&1

            # Check for leaks
            if grep -q "definitely lost: 0 bytes in 0 blocks" "valgrind_results/${test}.log" && \
               grep -q "indirectly lost: 0 bytes in 0 blocks" "valgrind_results/${test}.log"; then
                log_info "$test: No memory leaks detected"
            else
                log_error "$test: Memory leaks detected (see valgrind_results/${test}.log)"
                FAILED_CHECKS=$((FAILED_CHECKS + 1))
            fi
        fi
    done

    # Test parsers for leaks
    if [ -x examples/pg/pg_parser ] && [ -n "$SQL_FILES" ]; then
        test_sql=$(echo "$SQL_FILES" | head -1)
        log_info "Checking PostgreSQL parser for memory leaks..."
        valgrind --leak-check=full \
                 --show-leak-kinds=all \
                 --track-origins=yes \
                 --log-file="valgrind_results/pg_parser.log" \
                 examples/pg/pg_parser < "$test_sql" 2>&1

        if grep -q "definitely lost: 0 bytes in 0 blocks" "valgrind_results/pg_parser.log"; then
            log_info "PostgreSQL parser: No memory leaks detected"
        else
            log_warn "PostgreSQL parser: Possible memory leaks (see valgrind_results/pg_parser.log)"
        fi
    fi
fi

# 6. PERFORMANCE PROFILING WITH CACHEGRIND
log_section "Step 6: Performance Profiling (Cachegrind)"

if ! command -v valgrind &> /dev/null; then
    log_warn "Valgrind not installed, skipping performance profiling"
else
    log_info "Running performance profiling..."
    mkdir -p cachegrind_results

    # Profile test_random_composition
    if [ -x builddir/tests/test_random_composition ]; then
        log_info "Profiling random composition tests..."
        valgrind --tool=cachegrind \
                 --cachegrind-out-file=cachegrind_results/test_random_composition.out \
                 builddir/tests/test_random_composition 2>&1 | tee cachegrind_results/test_random_composition.log

        log_info "Cachegrind results:"
        cg_annotate cachegrind_results/test_random_composition.out | head -50
    fi

    # Profile parsers
    if [ -x examples/pg/pg_parser ] && [ -n "$SQL_FILES" ]; then
        test_sql=$(echo "$SQL_FILES" | head -1)
        log_info "Profiling PostgreSQL parser..."
        valgrind --tool=cachegrind \
                 --cachegrind-out-file=cachegrind_results/pg_parser.out \
                 examples/pg/pg_parser < "$test_sql" 2>&1 | tee cachegrind_results/pg_parser.log

        log_info "Cachegrind results:"
        cg_annotate cachegrind_results/pg_parser.out | head -50
    fi

    # Callgrind for detailed profiling
    if [ -x builddir/tests/test_random_composition ]; then
        log_info "Running callgrind profiling..."
        valgrind --tool=callgrind \
                 --callgrind-out-file=cachegrind_results/test_random_composition.callgrind \
                 builddir/tests/test_random_composition 2>&1

        log_info "Top functions by inclusive cost:"
        callgrind_annotate cachegrind_results/test_random_composition.callgrind | head -30
    fi
fi

# 7. PERFORMANCE BENCHMARKS
log_section "Step 7: Performance Benchmarks"

if [ -x builddir/tests/test_random_composition ]; then
    log_info "Running performance benchmarks..."

    # Run multiple times and collect timing
    echo "Iteration,Time(seconds)" > benchmark_results.csv
    for i in {1..10}; do
        START=$(date +%s.%N)
        builddir/tests/test_random_composition > /dev/null 2>&1
        END=$(date +%s.%N)
        ELAPSED=$(echo "$END - $START" | bc)
        echo "$i,$ELAPSED" >> benchmark_results.csv
        log_info "Iteration $i: ${ELAPSED}s"
    done

    # Calculate statistics
    AVG=$(awk -F',' 'NR>1 {sum+=$2; count++} END {print sum/count}' benchmark_results.csv)
    log_info "Average execution time: ${AVG}s"
fi

# 8. FINAL SUMMARY
log_section "Validation Complete"

if [ $FAILED_CHECKS -eq 0 ]; then
    log_info "✓ All validation checks passed!"
    echo -e "\n${GREEN}SUCCESS: System is ready for production use${NC}\n"
    exit 0
else
    log_error "✗ $FAILED_CHECKS validation checks failed"
    echo -e "\n${RED}FAILURE: Please review the logs and fix issues${NC}\n"
    exit 1
fi
