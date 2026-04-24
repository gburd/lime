# Comprehensive Validation Guide

## Overview

This document describes how to validate the Lime Modular Composition System including:
1. Clean builds with no warnings
2. Complete test suite execution
3. Performance comparison (Lime vs Bison)
4. Memory leak detection (Valgrind)
5. Performance profiling (Cachegrind/Callgrind)

## Prerequisites

Install required tools:
```bash
# On Debian/Ubuntu
sudo apt-get install valgrind meson ninja-build

# On NixOS (your system)
nix-shell -p valgrind meson ninja
```

## Quick Start

Run the automated validation script:
```bash
chmod +x validate_system.sh
./validate_system.sh
```

This will perform all validation steps automatically and generate reports in:
- `build.log` - Build output
- `test.log` - Test results
- `valgrind_results/` - Memory leak reports
- `cachegrind_results/` - Performance profiling data
- `parse_results/` - Parse tree comparison
- `benchmark_results.csv` - Performance benchmarks

## Manual Validation Steps

If you prefer to run validation steps manually:

### Step 1: Clean Build

```bash
# Remove old build directory
trash builddir 2>/dev/null || rm -rf builddir

# Setup fresh build
meson setup builddir

# Compile with warnings
meson compile -C builddir 2>&1 | tee build.log

# Check for warnings
grep -i "warning:" build.log && echo "WARNINGS FOUND" || echo "BUILD CLEAN"
```

**Expected Result**: Build completes with exit code 0 and no warnings.

### Step 2: Run All Tests

```bash
# Run complete test suite
meson test -C builddir --print-errorlogs

# List test results
meson test -C builddir --list

# Run specific test suites
meson test -C builddir test_merkle_tree
meson test -C builddir test_dependency_resolver
meson test -C builddir test_parser_composition
meson test -C builddir test_random_composition
```

**Expected Result**: All tests pass (0 failures).

**Test Coverage**:
- `test_merkle_tree`: 33 tests (merkle tree infrastructure)
- `test_dependency_resolver`: 32 tests (dependency tracking)
- `test_parser_composition`: 27 tests (parser composition)
- `test_random_composition`: 14 tests, 4000+ iterations (integration)

### Step 3: Build Parsers

```bash
# Build monolithic PostgreSQL parser
cd examples/pg
make clean && make
cd ../..

# Build modular PostgreSQL parser
cd examples/pg_modular
make clean && make
cd ../..

# Build other parsers
for dir in examples/datalog examples/xpath examples/xquery examples/mongodb; do
    cd $dir
    make clean && make
    cd - > /dev/null
done
```

**Expected Result**: All parsers compile successfully.

### Step 4: Performance Comparison

#### 4.1: Parse Test Files

```bash
# Find test SQL files
SQL_DIR="examples/pg/test"  # Adjust path as needed
mkdir -p parse_results

# Parse with both parsers 10 times
for sql_file in $SQL_DIR/*.sql; do
    filename=$(basename "$sql_file")
    echo "Testing $filename..."

    for i in {1..10}; do
        # Monolithic parser
        examples/pg/pg_parser < "$sql_file" > "parse_results/mono_${filename}_${i}.txt" 2>&1

        # Modular parser
        examples/pg_modular/pg_parser < "$sql_file" > "parse_results/mod_${filename}_${i}.txt" 2>&1
    done

    # Verify consistency across iterations
    for i in {2..10}; do
        diff "parse_results/mono_${filename}_1.txt" "parse_results/mono_${filename}_${i}.txt" || \
            echo "ERROR: Inconsistent output for $filename iteration $i"
    done

    # Compare monolithic vs modular
    diff "parse_results/mono_${filename}_1.txt" "parse_results/mod_${filename}_1.txt" && \
        echo "✓ Identical output for $filename" || \
        echo "✗ Different output for $filename"
done
```

**Expected Result**:
- All iterations of same parser produce identical output
- Monolithic and modular parsers produce identical output

#### 4.2: Performance Timing

```bash
# Time monolithic parser
echo "Monolithic parser (10 runs):"
for i in {1..10}; do
    time examples/pg/pg_parser < test.sql > /dev/null 2>&1
done

# Time modular parser
echo "Modular parser (10 runs):"
for i in {1..10}; do
    time examples/pg_modular/pg_parser < test.sql > /dev/null 2>&1
done

# Compare merkle tree overhead
builddir/tests/test_random_composition 2>&1 | grep -A5 "merkle_overhead"
```

**Expected Performance**:
- Merkle tree overhead: <100μs per operation (actual: ~24μs)
- 10-module composition: <1s (actual: ~0.36ms average)
- Modular parser performance within 5% of monolithic

### Step 5: Memory Leak Detection

```bash
# Run all tests under Valgrind
mkdir -p valgrind_results

# Test merkle tree
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --log-file=valgrind_results/merkle_tree.log \
         builddir/tests/test_merkle_tree

# Test dependency resolver
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --log-file=valgrind_results/dependency_resolver.log \
         builddir/tests/test_dependency_resolver

# Test parser composition
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --log-file=valgrind_results/parser_composition.log \
         builddir/tests/test_parser_composition

# Test random composition (most comprehensive)
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --log-file=valgrind_results/random_composition.log \
         builddir/tests/test_random_composition

# Test parsers
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --log-file=valgrind_results/pg_parser.log \
         examples/pg/pg_parser < test.sql
```

**Check Results**:
```bash
# Look for "definitely lost" and "indirectly lost"
grep "definitely lost" valgrind_results/*.log
grep "indirectly lost" valgrind_results/*.log

# Ideal output: "definitely lost: 0 bytes in 0 blocks"
```

**Expected Result**:
- 0 bytes definitely lost
- 0 bytes indirectly lost
- All reachable or still-reachable blocks are acceptable (cleaned up at exit)

### Step 6: Performance Profiling

#### 6.1: Cachegrind (Cache Performance)

```bash
mkdir -p cachegrind_results

# Profile random composition test
valgrind --tool=cachegrind \
         --cachegrind-out-file=cachegrind_results/random_composition.out \
         builddir/tests/test_random_composition

# Annotate results
cg_annotate cachegrind_results/random_composition.out > cachegrind_results/random_composition_annotated.txt

# View top functions
cg_annotate cachegrind_results/random_composition.out | head -50

# Profile PostgreSQL parser
valgrind --tool=cachegrind \
         --cachegrind-out-file=cachegrind_results/pg_parser.out \
         examples/pg/pg_parser < test.sql

# Annotate
cg_annotate cachegrind_results/pg_parser.out | head -50
```

**What to Look For**:
- Cache miss rates (lower is better)
- I-cache misses (instruction cache)
- D-cache misses (data cache)
- L2 cache misses
- Branch mispredictions

#### 6.2: Callgrind (Function Call Profiling)

```bash
# Profile with callgrind
valgrind --tool=callgrind \
         --callgrind-out-file=cachegrind_results/random_composition.callgrind \
         builddir/tests/test_random_composition

# Annotate call graph
callgrind_annotate cachegrind_results/random_composition.callgrind > \
    cachegrind_results/random_composition_callgrind.txt

# View top functions by inclusive cost
callgrind_annotate cachegrind_results/random_composition.callgrind | head -50

# Visualize with kcachegrind (GUI tool)
# kcachegrind cachegrind_results/random_composition.callgrind
```

**What to Look For**:
- Hot functions (high inclusive/exclusive cost)
- Call counts
- Function call depth
- Opportunities for optimization

#### 6.3: Massif (Heap Profiling)

```bash
# Profile heap usage
valgrind --tool=massif \
         --massif-out-file=cachegrind_results/random_composition.massif \
         builddir/tests/test_random_composition

# Visualize heap usage
ms_print cachegrind_results/random_composition.massif > \
    cachegrind_results/random_composition_heap.txt

# View summary
cat cachegrind_results/random_composition_heap.txt | head -100
```

**What to Look For**:
- Peak heap usage
- Memory allocation patterns
- Memory held at exit (should be 0 or minimal)

### Step 7: Comparative Benchmarks

#### 7.1: Bison Comparison (if available)

If you have access to PostgreSQL source with gram.y:

```bash
# Build Bison parser
cd /path/to/postgresql/src/backend/parser
make gram.o

# Compare grammar statistics
echo "Bison Grammar:"
bison -v gram.y
cat gram.output | grep -A10 "Grammar"

echo "Lime Monolithic Grammar:"
cd /path/to/lemon/examples/pg
cat pg_gram.out | grep -A10 "Grammar"

echo "Lime Modular Grammar:"
cd /path/to/lemon/examples/pg_modular
cat pg_composed.out | grep -A10 "Grammar"
```

**Expected Result**:
- Similar number of states
- Similar number of conflicts resolved
- Lime may have different conflict resolution strategy

#### 7.2: LLVM Comparison (if LLVM backend exists)

If Lime has an LLVM optimization backend:

```bash
# Build with LLVM optimizations
meson configure builddir -Dllvm=enabled
meson compile -C builddir

# Rebuild parsers with LLVM
cd examples/pg
make clean && make CFLAGS="-O3 -flto"

# Compare performance
echo "Standard build:"
time examples/pg/pg_parser < test.sql > /dev/null

echo "LLVM optimized build:"
time examples/pg/pg_parser_llvm < test.sql > /dev/null
```

## Validation Checklist

Use this checklist to verify all validation steps:

- [ ] Clean build completes with no warnings
- [ ] Clean build completes with no errors
- [ ] All 33 merkle tree tests pass
- [ ] All 32 dependency resolver tests pass
- [ ] All 27 parser composition tests pass
- [ ] All 14 random composition tests pass (4000+ iterations)
- [ ] Monolithic PostgreSQL parser builds successfully
- [ ] Modular PostgreSQL parser builds successfully
- [ ] Modular parser output matches monolithic parser output
- [ ] Parse output is consistent across 10 iterations
- [ ] Datalog/EDN parser builds and runs
- [ ] XPath parser builds and runs
- [ ] XQuery parser builds and runs
- [ ] MongoDB parser builds and runs
- [ ] Valgrind reports 0 bytes definitely lost (merkle_tree)
- [ ] Valgrind reports 0 bytes definitely lost (dependency_resolver)
- [ ] Valgrind reports 0 bytes definitely lost (parser_composition)
- [ ] Valgrind reports 0 bytes definitely lost (random_composition)
- [ ] Merkle tree overhead < 100μs per operation
- [ ] 10-module composition < 1 second
- [ ] Cachegrind shows acceptable cache performance
- [ ] Callgrind shows no obvious hot spots
- [ ] Massif shows no memory leaks

## Success Criteria

The system passes validation if:

1. **Build Quality**: No warnings or errors in clean build
2. **Test Coverage**: All 106 tests pass (33+32+27+14)
3. **Correctness**: Modular and monolithic parsers produce identical output
4. **Memory Safety**: No memory leaks detected by Valgrind
5. **Performance**: Meets performance targets (merkle <100μs, composition <1s)
6. **Stability**: Consistent output across multiple iterations

## Troubleshooting

### Build Warnings

If you see warnings:
```bash
# Review warnings in build log
grep -i "warning:" build.log

# Common issues:
# - Unused variables: Review and remove
# - Implicit conversions: Add explicit casts
# - Missing includes: Add necessary headers
```

### Test Failures

If tests fail:
```bash
# Run failing test with verbose output
meson test -C builddir test_name --verbose

# Run under debugger
gdb builddir/tests/test_name
(gdb) run
(gdb) bt  # backtrace on failure
```

### Memory Leaks

If Valgrind reports leaks:
```bash
# Get detailed leak information
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --verbose \
         builddir/tests/test_name 2>&1 | tee leak_detail.log

# Look for allocation stack traces
grep -A10 "definitely lost" leak_detail.log
```

### Performance Issues

If performance is below targets:
```bash
# Profile with callgrind
valgrind --tool=callgrind builddir/tests/test_name

# Identify hot functions
callgrind_annotate callgrind.out.* | head -50

# Review hot function implementations
# Consider optimizations:
# - Reduce allocations
# - Improve cache locality
# - Optimize data structures
```

## Automated Validation

The `validate_system.sh` script automates all these steps:

```bash
# Run full validation
./validate_system.sh

# Results are saved in:
# - build.log
# - test.log
# - valgrind_results/
# - cachegrind_results/
# - parse_results/
# - benchmark_results.csv
```

Exit codes:
- `0`: All validation passed
- `1`: One or more validation checks failed (see logs)

## Continuous Integration

To integrate into CI/CD:

```yaml
# Example .gitlab-ci.yml or .github/workflows/validate.yml
validate:
  script:
    - chmod +x validate_system.sh
    - ./validate_system.sh
  artifacts:
    paths:
      - build.log
      - test.log
      - valgrind_results/
      - cachegrind_results/
      - benchmark_results.csv
    expire_in: 1 week
```

## Next Steps

After successful validation:

1. **Deploy**: System is ready for production use
2. **Document**: Update user documentation with performance characteristics
3. **Monitor**: Set up monitoring for memory usage and performance
4. **Optimize**: If needed, address any performance hot spots identified in profiling

For questions or issues, refer to the main project documentation or file an issue in the bug tracker.
