# Lime — Agent Context

Project context for AI coding assistants (Claude, Kiro, Copilot, Cursor,
etc.) working in this repository.

## Project Overview

Lime is an LALR(1) parser generator derived from Lemon (SQLite).  It
generates C parsers from grammar specifications.  The extension framework
in `src/` adds runtime grammar modification, SIMD tokenization, and LLVM
JIT compilation.

## Repository Structure

```
Root files:   lime.c, limpar.c, tokenize.c  (the generator, from Lemon)
src/          Extension framework library (27 .c files)
include/      Public API headers
tests/        C test suites (27 files, meson-registered)
bench/        Benchmarks
examples/     Example grammars and extensions
contrib/      SQL dialect extensions
docs/         Reference documentation + Doxyfile
man/          Man pages
scripts/      Shell scripts (coverage, valgrind, validation)
tools/        Composition and management utilities
```

The root `.c` files are intentionally NOT in `src/`.  They are the
original Lemon parser generator — a standalone tool.  `src/` is the
extension framework — a separate library linked by tests and benchmarks.

## Build System

**Primary:** Meson + Ninja.  **Convenience:** GNU Make wrapper.

```bash
# Minimal: compile just the generator
cc -o lime lime.c

# Full build
meson setup builddir
ninja -C builddir
ninja -C builddir test

# Nix users
nix develop    # provides GCC 13, LLVM 17, Meson, coverage tools
```

The Makefile in the project root wraps meson commands: `make build`,
`make test`, `make clean`.

### Build targets

- `lime` executable (from lime.c, standalone)
- `lime_parser` static library (from src/, the extension framework)
- 27 test executables (from tests/)
- 4 benchmark executables (from bench/)

### Meson structure

- `meson.build` — root project, compiles lime.c, includes subdirs
- `src/meson.build` — extension library (3 static libs: tokenize_simd,
  lime_jit, lime_parser)
- `tests/meson.build` — test registration
- `bench/meson.build` — benchmark executables

## Testing

```bash
ninja -C builddir test                    # all tests
meson test -C builddir <name>             # single test
./scripts/measure_coverage.sh             # coverage report
./scripts/check_memory.sh                 # valgrind
```

Sanitizer builds:
```bash
meson setup builddir-asan -Db_sanitize=address  && ninja -C builddir-asan test
meson setup builddir-tsan -Db_sanitize=thread   && ninja -C builddir-tsan test
meson setup builddir-ubsan -Db_sanitize=undefined && ninja -C builddir-ubsan test
```

Always run sanitizers after non-trivial changes.  The CI does this
automatically (see `.github/workflows/ci.yml`, `.woodpecker/ci.yml`).

## Code Conventions

### C style

- C11 standard.  Compiles with GCC 13+ and Clang 15+.
- `-Wall -Wextra` clean.  Fix all warnings.
- `lime_malloc`/`lime_calloc`/`lime_realloc` wrappers for all allocations
  in lime.c.  `lime_free_all()` cleans up at exit.
- `%destructor` directives in grammars prevent leaks during error recovery.
- Public headers in `include/`, internal headers co-located in `src/`.
- Functions ≤100 lines.  Prefer early returns.

### Naming

- `snake_case` for functions and variables.
- `PascalCase` for types and structs.
- `UPPER_CASE` for macros and constants.
- Prefix public API functions with the module name
  (e.g., `snapshot_acquire`, `extension_register`).

### Comments

- Doxygen annotations on all public API functions in `include/`.
- Block comments for non-obvious algorithms.  No commented-out code.

## Git Practices

- Conventional Commits: `type(scope): description`
  - Types: feat, fix, docs, test, refactor, perf, chore, ci
  - Imperative mood, ≤72 char subject line
- One logical change per commit.
- Never force push.  Never rewrite history.
- Never push directly to main — use branches.
- Build and test before every commit.
- Use `-P` flag on git commands that produce paginated output.

## Documentation

- `docs/` contains all reference documentation.
- `docs/Doxyfile` generates API docs: `cd docs && make doxygen`
- `man/lime.1` and `man/lime_grammar.5` are the man pages.
- `docs/README.md` is the documentation index.
- When adding a new public API function, add Doxygen annotations in the
  header and update `docs/API.md`.

## Architecture Notes

### lime.c pipeline

`main()` at ~line 2048 orchestrates:
1. Parse grammar → 2. FindRulePrecedences → 3. FindFirstSets →
4. FindStates (LR(0) automaton) → 5. FindLinks → 6. FindFollowSets →
7. FindActions → 8. CompressTables → 9. ResortStates →
10. ReportOutput → 11. ReportTable → 12. ReportHeader

### Key data structures (in lime.c)

- `struct lime` — entire parser generator state
- `struct rule` — production rule (RHS symbols, precedence, action code)
- `struct symbol` — terminal or non-terminal
- `struct state` — LR(0) state with configurations and actions
- `struct config` — dotted production with lookahead/follow sets

### Extension framework (src/)

- **Snapshots**: Copy-on-write grammar state with atomic refcounting.
  `src/snapshot.{c,h}`, `src/snapshot_modify.{c,h}`
- **Extensions**: Runtime grammar modification with conflict detection.
  `src/extension.{c,h}`, `src/extension_registry.c`
- **Conflict detection**: Multi-grammar conflict analysis.
  `src/conflict.{c,h}`, `src/conflict_detector.c`
- **SIMD tokenization**: AVX2/NEON/scalar character classification.
  `src/tokenize_simd.{c,h}`
- **JIT**: LLVM-based action table compilation.
  `src/jit_context.{c,h}`, `src/jit_codegen.c`, `src/jit_policy.{c,h}`
- **Disambiguation**: Pluggable strategies (priority, fork-resolve).
  `src/disambiguation.c`, `src/strategy_priority.c`,
  `src/strategy_fork_resolve.c`

## Workflow for Common Tasks

### Adding a new source file to the extension framework

1. Create `src/new_module.c` (and `src/new_module.h` if internal,
   or `include/new_module.h` if public API)
2. Add to `src/meson.build` in the appropriate library source list
3. Write tests in `tests/test_new_module.c`
4. Register test in `tests/meson.build`
5. Build and test: `ninja -C builddir && meson test -C builddir`
6. Run sanitizers

### Adding a new grammar directive to lime.c

1. Add parsing in the `parseonetoken()` function
2. Add storage in `struct lime`
3. Add output in `ReportTable()` if it affects generated code
4. Test with a `.y` grammar file
5. Update `man/lime.1` and `man/lime_grammar.5`

### Adding a new test

1. Create `tests/test_<name>.c` following existing patterns
2. Add to `tests/meson.build`:
   ```meson
   test('name', executable('test_name',
     'test_name.c', dependencies: lime_parser_dep))
   ```
3. Run: `ninja -C builddir && meson test -C builddir name`

### Adding a new example extension

1. Create `examples/<name>/` with grammar, extension source, Makefile
2. Add to `.gitignore` if it produces binaries
3. Reference from `docs/EXTENSIONS.md`

## What Agents Should NOT Do

- Do not move `lime.c`, `limpar.c`, or `tokenize.c` into `src/`.
  They are the generator; `src/` is the library.  This is intentional.
- Do not add "Implementation Status", "Success Criteria", phase
  checklists, or acceptance-report language to documentation.
  Write as a human author would.
- Do not commit generated files (`.o`, `builddir/`, generated parsers
  in `examples/`).
- Do not add dependencies without justification.
- Do not create summary/report markdown files at the project root.
- Do not use the word "comprehensive" in commit messages or docs.

## CI

- **GitHub Actions**: `.github/workflows/ci.yml` — gcc/clang matrix,
  sanitizer matrix (asan, tsan, ubsan), valgrind job.
- **Woodpecker** (Codeberg): `.woodpecker/ci.yml` — gcc, clang,
  asan+ubsan steps.

Both run on push to main and on pull requests.

## Dependencies

**Build:** GCC 13+ or Clang 15+, Meson 0.60+, Ninja, pkg-config.
**Optional:** LLVM 17+ (JIT), lcov/gcovr (coverage), Valgrind, perf.
**Runtime:** pthreads, C11 standard library.

All provided by `nix develop` via `flake.nix`.
