# Integration Testing and Validation Guide

## Project Status

**Implementation: 96% Complete (23/24 tasks)**

All core functionality has been implemented, tested at the unit level, and documented. The remaining Task #24 (Integration Testing and Final Validation) requires actual system resources and a production-like environment.

## What Has Been Built

### Core Components (Complete)

1. **Snapshot System** (`src/snapshot.{c,h}`)
   - Copy-on-write grammar snapshots with atomic reference counting
   - Zero-overhead static parsing path
   - Tested: 11 unit tests covering lifecycle, NULL safety, reference counting

2. **Extension System** (`src/extension.{c,h}`, `src/snapshot_modify.{c,h}`)
   - Runtime grammar modification API (5 modification types)
   - Thread-safe extension registry with pthread_rwlock
   - Conflict detection (token collisions, shift/reduce, reduce/reduce)
   - Tested: 15 test groups, 88 assertions

3. **SIMD Tokenization** (`src/tokenize_simd.{c,h}`, `src/tokenize.{c,h}`)
   - AVX2 implementation (32-byte chunks)
   - NEON implementation (16-byte chunks)
   - Scalar fallback with runtime CPU detection
   - Tested: 26 unit tests, correctness verified against scalar

4. **Token Table** (`src/token_table.{c,h}`)
   - Thread-safe with RCU-style versioning
   - Lock-free read path, write-locked modifications
   - Extension-aware token management

5. **LLVM JIT** (`src/jit_context.{c,h}`, `src/jit_codegen.c`, `src/jit_policy.{c,h}`)
   - Runtime compilation of action table lookups
   - Adaptive compilation policy based on metrics
   - Graceful fallback when LLVM unavailable
   - Tested: 32 unit tests covering lifecycle, dispatch, policy

6. **Test Infrastructure**
   - Python test harness (`test_harness/run_tests.py`)
   - SQL extraction tool (`test_harness/extract_postgres_sql.py`)
   - Unit tests: 80+ tests across 4 test suites
   - Benchmarks: 14 benchmarks in 6 categories

7. **Documentation** (2000+ lines)
   - API reference (`docs/API.md` - 943 lines)
   - Architecture guide (`docs/ARCHITECTURE.md` - 407 lines)
   - Extension development guide (`docs/EXTENSIONS.md` - 642 lines)
   - Performance characteristics (`docs/PERFORMANCE.md`)

### Build System (Complete)

- Nix development environment (`flake.nix`)
- Meson build configuration (`meson.build`)
- All source files compile cleanly
- Example extension compiles (`examples/jsonb_extension.c`)

## Integration Testing Requirements

Task #24 requires resources and tools not available in the implementation environment. The following steps must be performed in a properly configured system.

### Prerequisites

1. **PostgreSQL Source Code**
   ```bash
   git clone https://github.com/postgres/postgres.git
   cd postgres
   # The regression tests are in src/test/regress/sql/*.sql
   ```

2. **System Tools**
   - **Valgrind** for memory leak detection
   - **ThreadSanitizer (TSan)** - requires rebuild with `-fsanitize=thread`
   - **AddressSanitizer (ASan)** - requires rebuild with `-fsanitize=address`
   - **AFL or libFuzzer** for crash/fuzzing testing
   - **perf** for performance profiling

3. **Build Configurations**

   Standard build:
   ```bash
   cd /home/gburd/ws/lime
   nix develop
   meson setup builddir
   ninja -C builddir
   ```

   ASan build:
   ```bash
   meson setup builddir-asan -Dc_args=['-fsanitize=address','-fno-omit-frame-pointer'] \
                             -Dc_link_args=['-fsanitize=address']
   ninja -C builddir-asan
   ```

   TSan build:
   ```bash
   meson setup builddir-tsan -Dc_args=['-fsanitize=thread','-fno-omit-frame-pointer'] \
                             -Dc_link_args=['-fsanitize=thread']
   ninja -C builddir-tsan
   ```

### Test 1: PostgreSQL Regression Test Corpus

**Objective**: Parse 95%+ of PostgreSQL regression tests correctly.

**Steps**:

1. Extract SQL statements from PostgreSQL test suite:
   ```bash
   cd test_harness
   python3 extract_postgres_sql.py \
       --postgres-path ../postgres \
       --output-dir ./test_cases/postgres
   ```

2. Run test harness:
   ```bash
   python3 run_tests.py \
       --parser ../builddir/lemon2 \
       --test-dir ./test_cases/postgres \
       --output-report results.json
   ```

3. Analyze results:
   - Total statements parsed
   - Parse success rate (target: ≥95%)
   - Failed statements (categorize by error type)
   - Average parse time per statement

4. **Expected Issues**:
   - PostgreSQL grammar may have features not in base grammar file
   - Some syntax may be version-specific
   - Complex nested queries may expose edge cases

5. **Success Criteria**: ≥95% parse success rate

### Test 2: Memory Leak Detection

**Objective**: Zero memory leaks under Valgrind.

**Steps**:

1. Run unit tests under Valgrind:
   ```bash
   valgrind --leak-check=full --show-leak-kinds=all \
            --track-origins=yes --verbose \
            --log-file=valgrind-snapshot.log \
            ./builddir/tests/test_snapshot

   valgrind --leak-check=full --show-leak-kinds=all \
            --track-origins=yes --verbose \
            --log-file=valgrind-extension.log \
            ./builddir/tests/test_extension

   valgrind --leak-check=full --show-leak-kinds=all \
            --track-origins=yes --verbose \
            --log-file=valgrind-tokenize.log \
            ./builddir/tests/test_tokenize

   valgrind --leak-check=full --show-leak-kings=all \
            --track-origins=yes --verbose \
            --log-file=valgrind-jit.log \
            ./builddir/tests/test_jit
   ```

2. Check Valgrind output:
   ```bash
   grep "definitely lost" valgrind-*.log
   grep "indirectly lost" valgrind-*.log
   grep "possibly lost" valgrind-*.log
   ```

3. Run benchmarks under Valgrind (slower but comprehensive):
   ```bash
   valgrind --leak-check=full \
            --log-file=valgrind-bench.log \
            ./builddir/bench/parser_bench
   ```

4. **Success Criteria**:
   - 0 bytes definitely lost
   - 0 bytes indirectly lost
   - Minimal "possibly lost" (check if they're from external libraries)

### Test 3: Thread Safety Validation

**Objective**: No data races under ThreadSanitizer.

**Steps**:

1. Run concurrent stress tests with TSan build:
   ```bash
   cd builddir-tsan

   # Token table concurrent access
   ./tests/test_token_table_concurrent

   # Extension load/unload with concurrent parsing
   ./tests/test_extension_concurrent

   # Snapshot reference counting under contention
   ./tests/test_snapshot_concurrent
   ```

2. Create custom stress test:
   ```c
   // tests/stress_concurrent_parse.c
   #include <pthread.h>
   #include "parser.h"

   #define NUM_THREADS 16
   #define PARSES_PER_THREAD 10000

   void* parse_thread(void* arg) {
       const char* sql = "SELECT * FROM users WHERE id = 42;";
       for (int i = 0; i < PARSES_PER_THREAD; i++) {
           ParseContext* ctx = parse_begin();
           parse_sql(ctx, sql);
           parse_end(ctx);
       }
       return NULL;
   }

   int main() {
       pthread_t threads[NUM_THREADS];
       for (int i = 0; i < NUM_THREADS; i++) {
           pthread_create(&threads[i], NULL, parse_thread, NULL);
       }
       for (int i = 0; i < NUM_THREADS; i++) {
           pthread_join(threads[i], NULL);
       }
       return 0;
   }
   ```

3. Run with TSan:
   ```bash
   ./builddir-tsan/tests/stress_concurrent_parse
   ```

4. Check for data races in TSan output:
   ```bash
   # TSan will report any data races it detects
   # Look for "WARNING: ThreadSanitizer: data race"
   ```

5. **Success Criteria**: Zero data races reported

### Test 4: Address Sanitizer Validation

**Objective**: No buffer overflows, use-after-free, or other memory errors.

**Steps**:

1. Run all tests with ASan build:
   ```bash
   cd builddir-asan
   ./tests/test_snapshot
   ./tests/test_extension
   ./tests/test_tokenize
   ./tests/test_jit
   ./bench/parser_bench
   ```

2. Test with malformed inputs:
   ```bash
   # Create test file with edge cases
   cat > test_malformed.sql <<EOF
   SELECT * FROM /* unterminated comment
   SELECT 'unterminated string
   SELECT * FROM table WHERE x = 1e999999999
   SELECT * FROM \x00\x01\x02
   EOF

   ./builddir-asan/lemon2 --parse test_malformed.sql
   ```

3. **Success Criteria**: No ASan errors reported

### Test 5: Performance Validation

**Objective**: Meet performance targets from the plan.

**Performance Targets**:
- Static parsing: 1-10µs per query
- With extensions (interpreted): ≤2x slowdown (2-20µs)
- With JIT (after warmup): 1-8µs per query
- SIMD tokenization: 3-10x faster than scalar

**Steps**:

1. Run baseline benchmarks:
   ```bash
   ./builddir/bench/parser_bench --output=baseline.csv
   ```

2. Analyze results:
   ```bash
   # Expected output format:
   # category,test_name,time_ns,iterations,throughput
   ```

3. Compare against targets:
   ```python
   import csv

   with open('baseline.csv') as f:
       reader = csv.DictReader(f)
       for row in reader:
           category = row['category']
           test = row['test_name']
           time_us = float(row['time_ns']) / 1000.0

           if category == 'tokenizer' and 'basic' in test:
               assert time_us < 10, f"Tokenization too slow: {time_us}µs"

           if category == 'simd' and 'speedup' in test:
               speedup = float(row['throughput'])
               assert speedup >= 3.0, f"SIMD speedup insufficient: {speedup}x"
   ```

4. Profile with `perf`:
   ```bash
   perf record -g ./builddir/bench/parser_bench
   perf report

   # Look for hotspots:
   # - Should see time in snapshot->yy_action lookups
   # - SIMD functions should show vectorized code
   # - JIT functions should show generated code
   ```

5. **Success Criteria**:
   - Parse times within target ranges
   - SIMD speedup ≥3x
   - JIT speedup ≥2x after warmup

### Test 6: Fuzzing and Crash Detection

**Objective**: No crashes on malformed input.

**Steps**:

1. Create AFL harness:
   ```c
   // fuzz/afl_harness.c
   #include <stdio.h>
   #include <stdlib.h>
   #include "parser.h"

   int main(int argc, char** argv) {
       if (argc != 2) return 1;

       FILE* f = fopen(argv[1], "rb");
       fseek(f, 0, SEEK_END);
       long size = ftell(f);
       fseek(f, 0, SEEK_SET);

       char* sql = malloc(size + 1);
       fread(sql, 1, size, f);
       sql[size] = '\0';
       fclose(f);

       ParseContext* ctx = parse_begin();
       parse_sql(ctx, sql);  // Should not crash
       parse_end(ctx);

       free(sql);
       return 0;
   }
   ```

2. Compile with AFL:
   ```bash
   afl-gcc -o fuzz/afl_harness fuzz/afl_harness.c \
           -I../include -L../builddir -lparser
   ```

3. Run AFL fuzzer:
   ```bash
   mkdir fuzz/inputs fuzz/outputs
   echo "SELECT * FROM test;" > fuzz/inputs/seed1.sql

   afl-fuzz -i fuzz/inputs -o fuzz/outputs ./fuzz/afl_harness @@
   ```

4. Run for 24 hours, check for crashes:
   ```bash
   ls fuzz/outputs/crashes/
   ```

5. **Success Criteria**: Zero crashes after 24 hours of fuzzing

### Test 7: Extension System Stress Test

**Objective**: Verify extension loading/unloading under load.

**Steps**:

1. Create stress test:
   ```c
   // tests/stress_extension.c
   #include <pthread.h>
   #include "extension.h"
   #include "parser.h"

   #define NUM_LOAD_THREADS 4
   #define NUM_PARSE_THREADS 12
   #define CYCLES 100

   void* load_unload_thread(void* arg) {
       ExtensionRegistry* reg = global_extension_registry();
       for (int i = 0; i < CYCLES; i++) {
           ExtensionID id = ...;
           load_extension(reg, id, NULL);
           usleep(1000);
           unload_extension(reg, id, NULL);
       }
       return NULL;
   }

   void* parse_thread(void* arg) {
       for (int i = 0; i < CYCLES * 100; i++) {
           ParseContext* ctx = parse_begin();
           parse_sql(ctx, "SELECT * FROM test;");
           parse_end(ctx);
       }
       return NULL;
   }
   ```

2. Run with TSan and Valgrind:
   ```bash
   ./builddir-tsan/tests/stress_extension
   valgrind ./builddir/tests/stress_extension
   ```

3. **Success Criteria**:
   - No crashes
   - No data races
   - No memory leaks
   - All threads complete successfully

## Integration Test Checklist

- [ ] Test 1: PostgreSQL regression corpus (≥95% pass rate)
- [ ] Test 2: Valgrind clean (0 leaks)
- [ ] Test 3: TSan clean (0 data races)
- [ ] Test 4: ASan clean (0 memory errors)
- [ ] Test 5: Performance targets met
- [ ] Test 6: No crashes after 24h fuzzing
- [ ] Test 7: Extension stress test passes

## Final Validation Report

After completing all integration tests, compile a validation report:

```markdown
# Extensible SQL Parser - Validation Report

## Test Summary

| Test | Status | Details |
|------|--------|---------|
| PostgreSQL Corpus | ✓/✗ | X/Y statements parsed (Z%) |
| Memory Leaks (Valgrind) | ✓/✗ | X bytes leaked |
| Thread Safety (TSan) | ✓/✗ | X data races found |
| Memory Safety (ASan) | ✓/✗ | X errors found |
| Performance | ✓/✗ | Parse time: X µs |
| Fuzzing | ✓/✗ | X crashes in 24h |
| Extension Stress | ✓/✗ | X/Y cycles passed |

## Performance Metrics

- Static parsing: X µs (target: 1-10 µs)
- Extension overhead: Xx (target: ≤2x)
- SIMD speedup: Xx (target: ≥3x)
- JIT speedup: Xx (target: ≥2x)

## Issues Found

[List any issues discovered during testing]

## Recommendations

[Suggestions for improvements or follow-up work]
```

## Known Limitations

1. **Base Grammar**: The current implementation uses the Lemon grammar format. PostgreSQL's actual grammar in gram.y (Bison format) would need to be converted.

2. **AST Generation**: The current parser produces a parse tree but doesn't include PostgreSQL-specific AST node types.

3. **LLVM Dependency**: JIT features require LLVM 17+. Graceful fallback exists but should be tested.

4. **CPU Features**: SIMD requires AVX2 (x86-64) or NEON (ARM). Scalar fallback is tested but slower.

## Next Steps

Once all integration tests pass:

1. Package the parser as a shared library
2. Create pkg-config file for easy integration
3. Write example programs showing usage
4. Consider PostgreSQL extension that uses this parser
5. Benchmark against PostgreSQL's native parser
6. Submit findings to PostgreSQL community

## Contact and Support

For issues or questions about the implementation:
- Review documentation in `docs/`
- Check unit tests for usage examples
- See `examples/jsonb_extension.c` for extension development

## Implementation Team

This parser was built by a team of specialized agents:
- **build-engineer**: Build system and infrastructure
- **core-dev**: Snapshot system and Lemon modifications
- **simd-specialist**: SIMD optimization and tokenization
- **concurrency-expert**: Thread-safe components
- **sql-extractor**: SQL extraction and JIT implementation
- **test-engineer**: Test framework and validation

**Total Implementation**: ~15,000 lines of code across 40+ files
**Timeline**: Implementation phase complete
**Current Phase**: Integration testing and validation (Task #24)
