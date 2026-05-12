## Test Coverage Analysis

This document explains how to measure and improve test coverage for the Extensible SQL Parser.

## Quick Start

### Run All Tests

```bash
cd /home/gburd/ws/lime
nix develop
meson setup builddir
ninja -C builddir test
```

Expected output (test count and ordering can shift as new tests
are added; the `Fail: 0` / `Ok:` summary is what matters):

```
 1/33 version                    OK       0.02s
 2/33 tokenize                   OK       0.62s
 ...
33/33 alternation                OK       0.01s

Ok:                33
Expected Fail:      0
Fail:               0
Unexpected Pass:    0
Skipped:            0
Timeout:            0
```

### Measure Coverage

#### Using the Coverage Script

```bash
./scripts/measure_coverage.sh
```

This script:
1. Builds with coverage instrumentation (`-fprofile-arcs -ftest-coverage`)
2. Runs all tests
3. Generates HTML report
4. Prints coverage summary

Output:
```
=================================================================
  Test Coverage Measurement
=================================================================

[1/5] Cleaning previous coverage build...
[2/5] Configuring build with coverage instrumentation...
[3/5] Building project...
[4/5] Running test suite...
[5/5] Generating coverage report...

HTML coverage report: coverage-report/index.html

  lines......: 92.3% (1234 of 1337 lines)
  functions..: 95.1% (98 of 103 functions)
  branches...: 78.2% (456 of 583 branches)
```

#### Manual Coverage Measurement

```bash
# Build with coverage
meson setup builddir-cov -Db_coverage=true
ninja -C builddir-cov

# Run tests
meson test -C builddir-cov

# Generate report
ninja -C builddir-cov coverage-html

# View report
open builddir-cov/meson-logs/coveragereport/index.html
```

## Test Suite Overview

### Current Test Suites (33 executables)

The suite has grown well past the original 8.  The canonical list is
whatever `ls tests/test_*.c` returns; a grouped snapshot as of this
document's last refresh:

**Core parser / snapshot lifecycle**

| Test | Focus |
|------|-------|
| `test_version` | Version string surface |
| `test_snapshot` | Snapshot lifecycle, refcounting |
| `test_snapshot_modify` | Clone operations, independence |
| `test_snapshot_modify_extended` | Edge cases in clone / modify |
| `test_parse_context` | Context lifecycle, action lookups |
| `test_parse_context_coverage` | Coverage of rarely-exercised paths |

**Tokenizer / token-table**

| Test | Focus |
|------|-------|
| `test_tokenize` | SIMD tokenization, correctness |
| `test_token_table_coverage` | Token table edge cases |
| `test_shared_token_lifetime` | Token lifetime across snapshots |

**Extension framework**

| Test | Focus |
|------|-------|
| `test_extension` | Registry, conflicts |
| `test_extension_coverage` | Rarer extension paths |
| `test_extension_registry` | Registry data structures |
| `test_multi_extension` | Interactions between extensions |
| `test_dependency_resolver` | Load-order / requires graph |
| `test_runtime_management` | Runtime registry operations |

**Grammar modification & conflict resolution**

| Test | Focus |
|------|-------|
| `test_conflict_detector` | Conflict classification |
| `test_disambiguation` | Ambiguity resolution strategies |
| `test_strategy_priority_extended` | Priority-based disambiguation |
| `test_fork_resolve` | Fork-resolve disambiguation |
| `test_parser_composition` | Module / composition semantics |
| `test_parser_fork` | Parser state forking |
| `test_random_composition` | Fuzz-style composition |
| `test_execution_policy` | Execution-policy matrix |
| `test_grammar_context` | Grammar context state |
| `test_parser_manager` | Manager-level lifecycle |
| `test_merkle_tree` | Merkle-tree snapshot identity |
| `test_coverage_boost` | Additional coverage across modules |

**JIT**

| Test | Focus |
|------|-------|
| `test_jit` | JIT compilation, policy (runs against stubs when LLVM disabled) |

**Concurrency**

| Test | Focus |
|------|-------|
| `test_concurrent` | Thread safety, stress tests |

**Diagnostics**

| Test | Focus |
|------|-------|
| `test_diagnostics` | RFC 0059 diagnostics API (uses a tiny generated parser via `custom_target`) |

**Recent additions** (documented here so PRs that add tests extend the
table instead of shadowing these):

| Test | Focus |
|------|-------|
| `test_alternation` | `\|` alternation in rule RHS (P1-1); action propagation across alternatives |
| `test_reduce_fn_type` | Compile-time type check for `LimeReduceFn` + `MOD_ADD_RULE.reduce`/`reduce_user` fields (P0-2 scaffolding) |
| `test_mod_serialize` | `lime_modifications_to_grammar_text()` -- subprocess-fallback serializer |

The `test_diagnostics` suite additionally depends on a generated
parser (see `tests/meson.build`'s `custom_target` invocations); the
same pattern is used by `test_alternation`.

### Coverage by Component

Based on current test suite:

| Component | Estimated Coverage | Priority to Improve |
|-----------|-------------------|---------------------|
| snapshot.c | ~95% | ✓ Excellent |
| snapshot_modify.c | ~90% | ✓ Good |
| extension.c | ~88% | Medium |
| conflict.c | ~75% | **High** |
| tokenize.c | ~92% | ✓ Good |
| tokenize_simd.c | ~85% | Medium |
| token_table.c | ~90% | ✓ Good |
| parse_context.c | ~93% | ✓ Good |
| jit_context.c | ~80% | Medium |
| jit_codegen.c | ~70% | **High** |
| jit_policy.c | ~88% | Medium |

## Improving Coverage

### Target: 95%+ Line Coverage

**Current Status: ~85-90% overall**

### High-Priority Gaps

#### 1. Conflict Detection (conflict.c)

**Uncovered paths:**
- Multiple simultaneous conflicts
- Conflict resolution with precedence
- Complex shift/reduce scenarios

**Suggested tests:**
```c
// tests/test_conflict_advanced.c
- test_multiple_token_collisions()
- test_precedence_resolution()
- test_complex_shift_reduce()
- test_conflict_with_multiple_extensions()
```

#### 2. JIT Code Generation (jit_codegen.c)

**Uncovered paths:**
- Error handling in LLVM IR generation
- Edge cases in action table encoding
- State function generation failures

**Suggested tests:**
```c
// tests/test_jit_codegen.c
- test_codegen_empty_state()
- test_codegen_large_action_table()
- test_codegen_error_handling()
- test_generated_code_correctness()
```

#### 3. Extension System Edge Cases

**Uncovered paths:**
- Extension load failure recovery
- Circular dependencies between extensions
- Extension with invalid modifications

**Suggested tests:**
```c
// Add to test_extension.c
- test_load_invalid_modification()
- test_circular_extension_deps()
- test_extension_failure_recovery()
```

### Adding New Tests

#### Template for New Test File

```c
/*
** Unit tests for <component>
**
** Description of what this test suite covers.
*/
#include <stdio.h>
#include <stdlib.h>

#include "parser.h"
#include "<component>.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

static void test_example(void) {
    TEST("Example test description");

    /* Test implementation */

    if (/* success condition */) {
        PASS();
    } else {
        FAIL("Reason for failure");
    }
}

int main(void) {
    printf("\n<Component> Test Suite\n");
    printf("======================\n\n");

    test_example();

    printf("\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
```

#### Adding to Build System

1. Create test file: `tests/test_<name>.c`

2. Add to `tests/meson.build`:
```meson
test_<name> = executable('test_<name>',
  'test_<name>.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('<name>', test_<name>)
```

3. Rebuild and verify:
```bash
ninja -C builddir
meson test -C builddir <name>
```

## Coverage Analysis Workflow

### 1. Measure Baseline

```bash
./scripts/measure_coverage.sh
```

### 2. Identify Gaps

Open `coverage-report/index.html` and look for:
- **Red lines**: Not covered at all
- **Yellow/orange lines**: Partially covered branches
- **Functions at 0%**: Never called

### 3. Prioritize

Focus on:
1. Critical paths (error handling, edge cases)
2. Complex logic (conflict detection, JIT codegen)
3. Public APIs (ensure all paths callable by users)

### 4. Write Tests

For each gap:
1. Determine the input needed to trigger the path
2. Write a focused test case
3. Verify the line turns green

### 5. Verify Improvement

```bash
./scripts/measure_coverage.sh
# Check new coverage percentage
```

## Coverage Goals by Release

### v0.1 (Current)
- **Target**: 85% line coverage
- **Status**: ✓ **Achieved** (~85-90%)
- **Focus**: Core functionality tested

### v0.2 (Next)
- **Target**: 92% line coverage
- **Focus**:
  - Edge cases in conflict detection
  - JIT error handling
  - Extension failure recovery
  - Memory allocation failures

### v1.0 (Production)
- **Target**: 95%+ line coverage
- **Focus**:
  - All error paths
  - All edge cases
  - Security boundaries
  - Fuzzing integration

## Integration with CI

### GitHub Actions Example

```yaml
name: Test Coverage

on: [push, pull_request]

jobs:
  coverage:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y meson ninja-build lcov \
            llvm-dev

      - name: Build and test
        run: |
          meson setup builddir -Db_coverage=true
          ninja -C builddir test

      - name: Generate coverage
        run: |
          ninja -C builddir coverage-html

      - name: Check coverage threshold
        run: |
          coverage=$(lcov --summary builddir/coverage.info 2>&1 | \
            grep lines | awk '{print $2}' | tr -d '%')
          if (( $(echo "$coverage < 85.0" | bc -l) )); then
            echo "Coverage $coverage% is below threshold 85%"
            exit 1
          fi

      - name: Upload coverage report
        uses: codecov/codecov-action@v2
        with:
          files: builddir/coverage.info
```

## Sanitizer Coverage

In addition to code coverage, use sanitizers to find bugs:

### AddressSanitizer (Memory Errors)

```bash
meson setup builddir-asan -Db_sanitize=address
ninja -C builddir-asan test
```

### ThreadSanitizer (Data Races)

```bash
meson setup builddir-tsan -Db_sanitize=thread
ninja -C builddir-tsan test
```

### UndefinedBehaviorSanitizer

```bash
meson setup builddir-ubsan -Db_sanitize=undefined
ninja -C builddir-ubsan test
```

## Coverage Best Practices

### DO:
- ✓ Test error paths (NULL inputs, allocation failures)
- ✓ Test boundary conditions (empty, maximum, overflow)
- ✓ Test typical use cases
- ✓ Test edge cases (unusual but valid inputs)
- ✓ Test concurrent access patterns
- ✓ Use sanitizers to find bugs coverage doesn't catch

### DON'T:
- ✗ Write tests just to hit lines (test behavior, not coverage)
- ✗ Test internal implementation details (test public APIs)
- ✗ Ignore failing tests to maintain coverage numbers
- ✗ Aim for 100% coverage (diminishing returns >95%)
- ✗ Skip sanitizers (they find bugs coverage misses)

## Troubleshooting

### "No coverage tool found"

**Solution**: Install lcov or gcovr
```bash
# Ubuntu/Debian
sudo apt-get install lcov

# macOS
brew install lcov

# Or use gcovr
pip3 install gcovr
```

### "Coverage data not found"

**Solution**: Ensure tests actually ran
```bash
# Verify .gcda files exist
find builddir-coverage -name "*.gcda"

# If empty, tests didn't run with instrumentation
ninja -C builddir-coverage test
```

### "Coverage seems too low"

**Possible causes:**
1. **Source files not compiled with coverage**: Check meson.build has `-Db_coverage=true`
2. **Tests not exercising code**: Add more tests
3. **Dead code**: Remove unreachable code
4. **Generated code**: Exclude from coverage (e.g., lime.c)

## References

- [gcov Documentation](https://gcc.gnu.org/onlinedocs/gcc/Gcov.html)
- [lcov Homepage](https://github.com/linux-test-project/lcov)
- [Meson Test Coverage](https://mesonbuild.com/Unit-tests.html#coverage)
