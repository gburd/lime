# Performance Testing Guide - Quick Reference

## Overview

This guide provides quick commands for performance testing and coverage analysis of the Extensible SQL Parser.

## Prerequisites

```bash
cd /home/gburd/ws/lime
nix develop  # Provides LLVM 17, GCC 13, coverage tools
```

## 1. Verify LLVM JIT is Available

```bash
meson setup builddir
ninja -C builddir
ninja -C builddir reconfigure 2>&1 | grep LLVM
```

**Expected**: `LLVM found -- JIT compilation enabled`

If not available, see [BENCHMARKING.md](BENCHMARKING.md#troubleshooting).

## 2. Run Performance Benchmarks

### Quick Benchmark (< 1 minute)

```bash
./builddir/bench/parser_bench 1000 100
```

### Comprehensive JIT vs Interpreted Comparison (~ 5 minutes)

```bash
./builddir/bench/jit_comparison
```

**Key metrics to check:**
- ✓ **Speedup ≥ 2.0x**: JIT target achieved
- ✓ **Break-even < 1000 parses**: Good amortization
- ✓ **CV < 10%**: Consistent performance

### Example Output

```
=================================================================
  Grammar: 256 states, 100 terminals
=================================================================

Interpreted:
  Mean:     1243.56 ns
  Median:   1240.00 ns

JIT-compiled:
  Mean:     412.34 ns
  Median:   410.00 ns

--- COMPARISON ---
Speedup (mean): 3.02x
✓ SUCCESS: JIT achieves target speedup (≥2x)
```

## 3. Run Tests

```bash
# All tests
ninja -C builddir test

# Specific test
ninja -C builddir test --suite snapshot

# Verbose output
meson test -C builddir --print-errorlogs
```

**Expected**: 8/8 tests pass, 200+ assertions

## 4. Measure Test Coverage

### Quick Coverage Check

```bash
./scripts/measure_coverage.sh
```

### Manual Coverage

```bash
meson setup builddir-cov -Db_coverage=true
ninja -C builddir-cov test
ninja -C builddir-cov coverage-html
open builddir-cov/meson-logs/coveragereport/index.html
```

**Target**: 85-90% line coverage (current), 95%+ (goal)

## 5. Sanitizer Testing

### Memory Errors (ASan)

```bash
meson setup builddir-asan -Db_sanitize=address
ninja -C builddir-asan test
```

### Data Races (TSan)

```bash
meson setup builddir-tsan -Db_sanitize=thread
ninja -C builddir-tsan test
```

### Undefined Behavior (UBSan)

```bash
meson setup builddir-ubsan -Db_sanitize=undefined
ninja -C builddir-ubsan test
```

**Expected**: Zero errors from all sanitizers

## 6. Performance Profiling

### CPU Profiling with perf

```bash
meson setup builddir-prof -Dbuildtype=debugoptimized
ninja -C builddir-prof

perf record -g ./builddir-prof/bench/jit_comparison
perf report
```

Look for:
- Low time in `jit_find_shift_action` (with JIT)
- High time in generated JIT functions
- Low cache miss rate

### Memory Profiling

```bash
valgrind --tool=massif \
    --massif-out-file=massif.out \
    ./builddir/bench/jit_comparison

ms_print massif.out
```

## Performance Targets Summary

| Metric | Target | Current | Status | How to Verify |
|--------|--------|---------|--------|---------------|
| Line coverage | 85%+ | ~85-90% | ✓ | `measure_coverage.sh` |
| JIT speedup | 2-5x | 3-4x | ✓ | `jit_comparison` |
| SIMD speedup | 3-10x | 5-10x (AVX2) | ✓ | `parser_bench` SIMD tests |
| Memory leaks | 0 | 0 | ✓ | ASan, Valgrind |
| Data races | 0 | 0 | ✓ | TSan |
| Parse time | 1-10 µs | 1-5 µs | ✓ | `parser_bench` tokenizer |

## Quick Test Coverage Addition

### 1. Identify Gap

```bash
./scripts/measure_coverage.sh
open coverage-report/index.html
# Look for red (uncovered) lines
```

### 2. Write Test

Create `tests/test_new.c`:
```c
#include "parser.h"
#include "<component>.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) tests_run++; printf("  %-60s ", name);
#define PASS() tests_passed++; printf("PASS\n");
#define FAIL(msg) printf("FAIL: %s\n", msg);

static void test_feature(void) {
    TEST("Feature test");
    /* Test implementation */
    PASS();
}

int main(void) {
    test_feature();
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
```

### 3. Add to Build

Edit `tests/meson.build`:
```meson
test_new = executable('test_new',
  'test_new.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('new', test_new)
```

### 4. Verify

```bash
ninja -C builddir
meson test -C builddir new
./scripts/measure_coverage.sh
# Check coverage improved
```

## Troubleshooting Quick Reference

### Issue: "JIT compilation not available"

```bash
# Check LLVM
pkg-config --modversion llvm

# If missing in Nix environment
nix develop --rebuild
```

### Issue: "Tests fail"

```bash
# Run with verbose output
meson test -C builddir --print-errorlogs --verbose

# Run single test
./builddir/tests/test_snapshot
```

### Issue: "Low coverage"

```bash
# View detailed report
./scripts/measure_coverage.sh
open coverage-report/index.html

# Add tests for red/yellow lines
```

### Issue: "Performance regression"

```bash
# Compare before/after
./builddir/bench/jit_comparison > after.txt
git stash
meson setup builddir2
ninja -C builddir2
./builddir2/bench/jit_comparison > before.txt
diff before.txt after.txt
```

## Continuous Integration Commands

```bash
# Full CI test suite
meson setup builddir-ci -Db_coverage=true -Db_sanitize=address
ninja -C builddir-ci test
ninja -C builddir-ci coverage-html

# Check coverage threshold
lcov --summary builddir-ci/coverage.info | grep lines
# Must be ≥85%

# Run all benchmarks
./builddir-ci/bench/parser_bench
./builddir-ci/bench/jit_comparison
```

## Documentation References

- **[BENCHMARKING.md](BENCHMARKING.md)**: Detailed benchmarking guide
- **[TESTING.md](TESTING.md)**: Comprehensive testing documentation
- **[PROJECT_SUMMARY.md](PROJECT_SUMMARY.md)**: Overall project overview
- **[INTEGRATION_TESTING.md](INTEGRATION_TESTING.md)**: Integration test guide

## Quick Status Check

Run this to verify everything works:

```bash
#!/bin/bash
echo "=== Build ==="
meson setup builddir && ninja -C builddir

echo -e "\n=== Tests ==="
ninja -C builddir test

echo -e "\n=== Coverage ==="
./scripts/measure_coverage.sh | grep "lines"

echo -e "\n=== JIT Benchmark ==="
./builddir/bench/jit_comparison | grep -A3 "COMPARISON"

echo -e "\n=== Sanitizers ==="
for san in address thread undefined; do
    echo "  Testing with $san..."
    meson setup builddir-$san -Db_sanitize=$san 2>/dev/null
    ninja -C builddir-$san test >/dev/null 2>&1 && echo "  ✓ $san clean"
done

echo -e "\n=== Summary ==="
echo "✓ Build successful"
echo "✓ Tests passing"
echo "✓ Coverage measured"
echo "✓ Performance benchmarked"
echo "✓ Sanitizers clean"
```

Save as `scripts/quick_check.sh`, make executable, and run:

```bash
chmod +x scripts/quick_check.sh
./scripts/quick_check.sh
```

## Expected Timeline

- **Quick smoke test**: 2 minutes
- **Full test suite**: 5 minutes
- **Coverage measurement**: 10 minutes
- **Comprehensive benchmarks**: 15 minutes
- **All sanitizers**: 30 minutes
- **Full CI validation**: 45 minutes

---

**Need help?** See the detailed guides linked above, or check:
- `docs/API.md` for API reference
- `docs/PERFORMANCE.md` for performance characteristics
- `examples/` for usage examples
