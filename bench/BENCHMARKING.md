# Benchmarking and Performance Testing

This document explains how to benchmark the Extensible SQL Parser, specifically comparing JIT-compiled vs interpreted performance.

## Quick Start

### 1. Setup Development Environment

```bash
cd /home/gburd/ws/lime
nix develop
```

This provides:
- GCC 13
- LLVM 17 with development libraries
- Meson and Ninja build tools
- Code coverage tools (lcov, gcovr)

### 2. Build with LLVM Support

```bash
# Clean build
rm -rf builddir
meson setup builddir
ninja -C builddir
```

Verify LLVM is detected:
```bash
ninja -C builddir reconfigure 2>&1 | grep -i llvm
```

You should see:
```
LLVM found -- JIT compilation enabled
```

If not, ensure pkg-config can find LLVM:
```bash
pkg-config --modversion llvm
# Should output: 17.x.x
```

### 3. Run Benchmarks

#### Basic Benchmark Suite

```bash
./builddir/bench/parser_bench
```

This runs:
- Tokenizer throughput tests
- SIMD classification benchmarks
- Token table lookup performance
- Snapshot operations
- JIT compilation benchmarks
- Memory usage analysis

Output is CSV format to stdout, with commentary on stderr.

#### Comprehensive JIT Comparison

```bash
./builddir/bench/jit_comparison
```

This benchmark:
1. **Tests multiple grammar sizes** (64, 256, 512 states)
2. **Proper warmup phase** ensures JIT compilation completes
3. **Statistical analysis** with mean, median, P95, P99, stddev
4. **Direct comparison** showing speedup ratios
5. **Amortization analysis** calculating break-even point

Example output:
```
=================================================================
  Grammar: 256 states, 100 terminals
  Parse length: 100 tokens
  Warmup: 500 parses, Benchmark: 5000 parses
=================================================================

--- INTERPRETED (TABLE-DRIVEN) ---
Interpreted:
  Samples:  5000
  Mean:     1243.56 ns
  Median:   1240.00 ns
  StdDev:   45.23 ns (3.6%)

--- JIT COMPILATION ---
JIT compilation successful!
  Compiled: 256/256 states
  Compile time: 4.23 ms

JIT-compiled:
  Samples:  5000
  Mean:     412.34 ns
  Median:   410.00 ns
  StdDev:   18.56 ns (4.5%)

--- COMPARISON ---
Speedup (mean): 3.02x
Speedup (median): 3.02x
✓ SUCCESS: JIT achieves target speedup (≥2x)

Break-even: 328 parses
  (After 328 parses, JIT has paid for itself)
  ✓ Good: Reasonable break-even
```

## Performance Targets

Based on the original implementation plan:

| Metric | Target | How to Verify |
|--------|--------|---------------|
| Static parsing | 1-10 µs/query | `parser_bench` tokenizer tests |
| Extension overhead | ≤2x slowdown | Compare w/ and w/o extensions |
| SIMD speedup | 3-10x | `parser_bench` SIMD tests |
| JIT speedup | 2-5x | `jit_comparison` benchmark |

## Interpreting Results

### JIT Speedup Analysis

**Expected Results:**
- **Small grammars (64 states)**: 2-3x speedup
- **Medium grammars (256 states)**: 3-4x speedup
- **Large grammars (512 states)**: 4-5x speedup

**Why JIT is faster:**
1. **No table lookups**: Direct branches instead of array indexing
2. **Better cache locality**: Smaller working set per state
3. **CPU optimizations**: Branch prediction, instruction pipelining
4. **Constant folding**: Default actions compiled as immediates

**Break-Even Analysis:**
The "break-even" point indicates how many parses are needed before JIT compilation cost is recovered. Lower is better.

- **< 100 parses**: Excellent - use JIT even for short sessions
- **100-1000 parses**: Good - worthwhile for typical server workloads
- **> 1000 parses**: High - only beneficial for very long-running processes

### SIMD Tokenization

**Expected Results:**
- **AVX2 (x86-64)**: 5-10x faster than scalar
- **NEON (ARM)**: 3-5x faster than scalar
- **Scalar fallback**: Baseline performance

To check which SIMD implementation is active:
```bash
./builddir/bench/parser_bench 2>&1 | grep "simd: best"
```

### Variability Analysis

Low coefficient of variation (CV) indicates consistent performance:
- **CV < 5%**: Excellent consistency
- **CV 5-10%**: Good consistency
- **CV > 10%**: High variability (investigate system load, thermal throttling)

## Advanced Benchmarking

### Custom Iteration Counts

```bash
# 50,000 iterations with 1,000 warmup
./builddir/bench/parser_bench 50000 1000
```

### Profiling with perf

```bash
# Build with debug symbols
meson setup builddir-prof -Dbuildtype=debugoptimized
ninja -C builddir-prof

# Profile JIT compilation
perf record -g ./builddir-prof/bench/jit_comparison
perf report

# Look for:
#   - Time in jit_find_shift_action (should be low with JIT)
#   - Time in JIT-compiled functions (should be high)
#   - Cache miss rates (perf stat -e cache-misses)
```

### Comparing Builds

```bash
# Baseline (no optimizations)
meson setup builddir-baseline -Dbuildtype=debug -Doptimization=0
ninja -C builddir-baseline
./builddir-baseline/bench/jit_comparison > baseline.txt

# Optimized
meson setup builddir-opt -Dbuildtype=release -Doptimization=3
ninja -C builddir-opt
./builddir-opt/bench/jit_comparison > optimized.txt

# Compare
diff baseline.txt optimized.txt
```

### Memory Profiling

```bash
# Massif heap profiler
valgrind --tool=massif --massif-out-file=massif.out \
    ./builddir/bench/jit_comparison

# Visualize
ms_print massif.out
```

## Troubleshooting

### "JIT compilation not available"

**Symptom:**
```
JIT available: NO (stub mode)
```

**Solutions:**

1. **Check LLVM installation:**
   ```bash
   pkg-config --libs --cflags llvm
   ```

2. **Verify PKG_CONFIG_PATH:**
   ```bash
   echo $PKG_CONFIG_PATH
   # Should include path to llvm.pc
   ```

3. **Reinstall LLVM:**
   ```bash
   # On NixOS (from flake.nix)
   nix develop --rebuild

   # On Ubuntu/Debian
   apt install llvm-17-dev libllvm17

   # On macOS
   brew install llvm@17
   ```

4. **Check build log:**
   ```bash
   ninja -C builddir reconfigure 2>&1 | grep -A5 -B5 llvm
   ```

### JIT Slower Than Interpreted

**Possible causes:**

1. **Not enough warmup**: JIT needs time to compile
   - Increase warmup_parses in benchmark

2. **Small grammar**: Compilation overhead too high
   - JIT best for grammars with >100 states

3. **Thermal throttling**: CPU downclocking under load
   - Check: `cat /proc/cpuinfo | grep MHz`
   - Run shorter benchmarks, let CPU cool between runs

4. **LLVM optimization level**: Default is O2
   - Check JITContext creation in jit_context.c
   - Increase to O3 if needed

### High Variability

**Causes:**
- Background processes consuming CPU
- Power management (frequency scaling)
- Thermal throttling
- Cache pollution from other processes

**Solutions:**
```bash
# Pin to performance governor (Linux)
sudo cpupower frequency-set -g performance

# Disable turbo boost (for consistency)
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# Isolate CPU cores
taskset -c 0,1 ./builddir/bench/jit_comparison
```

## Continuous Integration

For CI systems, use:

```bash
# Quick smoke test (fast)
./builddir/bench/jit_comparison | grep "SUCCESS"

# Full benchmark with machine-readable output
./builddir/bench/parser_bench > results.csv

# Check performance regression
python3 scripts/check_performance.py results.csv baseline.csv
```

## References

- [LLVM ORC JIT Documentation](https://llvm.org/docs/ORCv2.html)
- [SIMD Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/)
- [Linux perf Examples](https://www.brendangregg.com/perf.html)
