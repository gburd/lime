# Parser Generator Comparison Guide

This document compares Lime with other widely-used parser generators to help
you choose the right tool for your project and understand the trade-offs
involved.

## Overview

| Tool | Algorithm | Target Languages | License | First Release |
|------|-----------|-----------------|---------|---------------|
| **Lime** | LALR(1) | C | Public Domain | 2024 (Lemon: 2001) |
| **Yacc** | LALR(1) | C | Proprietary / BSD | 1975 |
| **Bison** | LALR(1), GLR, IELR(1) | C, C++, Java, D | GPL v3 | 1985 |
| **ANTLR** | LL(*), ALL(*) | Java, C#, Python, JS, Go, C++, Swift, Dart | BSD | 1992 |
| **Menhir** | LR(1) | OCaml | GPL v2 | 2005 |

## Feature Comparison Matrix

### Grammar and Parsing

| Feature | Lime | Yacc | Bison | ANTLR | Menhir |
|---------|------|------|-------|-------|--------|
| Grammar class | LALR(1) | LALR(1) | LALR(1)/GLR/IELR | LL(*)/ALL(*) | LR(1) |
| Left recursion | Yes | Yes | Yes | Indirect only (v4) | Yes |
| Right recursion | Yes | Yes | Yes | Yes | Yes |
| Operator precedence | Yes | Yes | Yes | Via alternatives | Yes |
| Mid-rule actions | No | Yes | Yes | N/A (listeners) | No |
| Named parameters | A, B, C... | $1, $2... | $1, $2 or $name | Labels | Pattern vars |
| Start symbol control | `%start_symbol` | First rule | `%start` | `grammar` rule | `%start` |
| Multiple start symbols | No | No | Yes (GLR mode) | Yes | Yes |
| Parameterized rules | No | No | No | Yes | Yes |
| Unicode support | Via tokenizer | Via lex | Via lex | Full Unicode | Full Unicode |
| Grammar modularity | Runtime extensions | None | None | `import` grammars | `%` includes |
| Conditional compilation | `%ifdef`/`%ifndef` | No | No | No | No |

### Code Generation and Integration

| Feature | Lime | Yacc | Bison | ANTLR | Menhir |
|---------|------|------|-------|-------|--------|
| Output language | C | C | C, C++, Java, D | 10+ languages | OCaml |
| Parser type | Push (event-driven) | Pull (yyparse loop) | Pull (yyparse) | Recursive descent | Push or pull |
| Reentrant by default | Yes | No | Optional (`%pure-parser`) | Yes | Yes |
| Thread safety | Yes (atomic refcount) | Manual | Manual | Yes | Yes |
| Generated file size | Small | Small | Medium | Large | Medium |
| Header generation | Automatic | Automatic | Automatic | N/A | Automatic |
| Custom template | `-T` flag | No | No (skeleton) | StringTemplate | No |
| Parser prefix/namespace | `%name` | `-p` flag | `%name-prefix` | Class name | Module name |
| Extra parser arguments | `%extra_argument` | Global variables | `%parse-param` | Constructor args | `%parameter` |

### Error Handling

| Feature | Lime | Yacc | Bison | ANTLR | Menhir |
|---------|------|------|-------|-------|--------|
| Error recovery | `error` token | `error` token | `error` token | Built-in recovery | `error` token |
| Syntax error callback | `%syntax_error` | `yyerror()` | `yyerror()` | `ErrorListener` | `%on_error_reduce` |
| Parse failure callback | `%parse_failure` | None | None | None | None |
| Conflict reporting | `.out` file | `y.output` | `.output` file | N/A (no conflicts) | `.conflicts` |
| Precedence conflict info | `-p` flag | No | `%expect` | N/A | Warnings |
| Error token destructor | `%destructor` | No | `%destructor` | N/A | N/A |

### Performance

| Feature | Lime | Yacc | Bison | ANTLR | Menhir |
|---------|------|------|-------|-------|--------|
| Parse time complexity | O(n) | O(n) | O(n) / O(n^3) GLR | O(n^4) worst case | O(n) |
| SIMD tokenization | Yes (AVX2/NEON) | No | No | No | No |
| JIT compilation | Yes (LLVM) | No | No | No | No |
| Table compression | Yes (`-c` to disable) | Yes | Yes | N/A | Yes |
| Typical parse latency | 0.2--3 us | 1--10 us | 1--10 us | 5--50 us | 1--10 us |
| Memory footprint | ~500 KB--1 MB | ~50--200 KB | ~50--200 KB | ~2--10 MB | ~100--500 KB |

### Runtime Extensibility

| Feature | Lime | Yacc | Bison | ANTLR | Menhir |
|---------|------|------|-------|-------|--------|
| Runtime grammar changes | Yes (extension API) | No | No | No | No |
| Add tokens at runtime | Yes | No | No | No | No |
| Add rules at runtime | Yes | No | No | No | No |
| Modify precedence at runtime | Yes | No | No | No | No |
| Extension conflict resolution | Callback-based | N/A | N/A | N/A | N/A |
| Copy-on-write snapshots | Yes | N/A | N/A | N/A | N/A |
| Hot grammar reload | Yes (zero downtime) | No | No | No | No |

### Build and Tooling

| Feature | Lime | Yacc | Bison | ANTLR | Menhir |
|---------|------|------|-------|-------|--------|
| Build system | Single C file | System package | System package | Java JAR | opam |
| Dependencies | None (core) | None | None | Java 11+ | OCaml |
| Self-contained | Yes | Yes | Yes | No (runtime lib) | No (runtime lib) |
| Meson integration | Yes | Manual | Manual | Gradle/Maven | dune |
| Nix support | Yes (flake.nix) | Via nixpkgs | Via nixpkgs | Via nixpkgs | Via nixpkgs |

## Detailed Comparisons

### Lime vs Yacc

Lime descends from the same LALR(1) tradition as Yacc, but with a modernized
design. Key differences:

- **Reentrancy**: Lime parsers are reentrant by default. Yacc parsers use
  global state (`yylval`, `yychar`) and require careful modification for
  thread safety.
- **Memory safety**: Lime uses `%destructor` directives to free semantic
  values during error recovery, avoiding the memory leaks common in Yacc
  parsers.
- **Push model**: Lime generates push parsers where the caller feeds tokens
  one at a time. Yacc generates pull parsers that call `yylex()` internally.
  The push model is easier to integrate with event-driven architectures and
  custom tokenizers.
- **Named symbols**: Lime uses letter labels (A, B, C) for semantic values
  instead of Yacc's positional `$1`, `$2` notation. This makes rules more
  readable when they have many symbols.
- **No mid-rule actions**: Yacc allows actions between grammar symbols; Lime
  requires all actions at the end of a rule. This simplifies the parser
  generator but occasionally requires grammar restructuring.
- **Single-file compilation**: Lime itself compiles from a single C file with
  no external dependencies, making it trivial to embed in build systems.

### Lime vs Bison

Bison is the GNU successor to Yacc, with many additional features. Lime
takes a different approach:

- **Simplicity**: Lime focuses on LALR(1) parsing done well. Bison offers
  GLR and IELR(1) modes that handle ambiguous and non-LALR grammars but
  add complexity.
- **Extensibility**: Lime's runtime extension system has no equivalent in
  Bison. With Lime, you can add tokens, rules, and precedence changes to
  a running parser without recompilation.
- **Performance**: Lime's SIMD tokenizer and LLVM JIT compiler provide
  significant speedups for hot parsing paths. Bison relies on the standard
  table-driven approach.
- **License**: Lime is Public Domain. Bison's generated parsers were
  historically GPL-licensed, though modern versions include an exception
  permitting use in proprietary software. Lime has no license complications.
- **Multiple languages**: Bison generates parsers in C, C++, Java, and D.
  Lime targets C only, relying on C's interoperability with other languages
  via FFI.
- **GLR parsing**: Bison's GLR mode can handle ambiguous grammars by
  exploring multiple parse trees in parallel. Lime does not support this
  but achieves similar flexibility through its runtime extension system,
  which can modify the grammar to resolve ambiguities.

### Lime vs ANTLR

ANTLR and Lime represent fundamentally different parsing philosophies:

- **Algorithm**: ANTLR uses LL(*) / ALL(*) parsing (top-down, recursive
  descent). Lime uses LALR(1) (bottom-up, table-driven). LL parsers cannot
  handle left recursion naturally, while LALR parsers cannot handle certain
  right-recursive patterns elegantly.
- **Ecosystem**: ANTLR has a rich ecosystem with IDE plugins, grammar
  repositories, and support for 10+ target languages. Lime is focused on
  high-performance C parsing.
- **Performance**: ANTLR's ALL(*) algorithm has O(n^4) worst-case
  complexity. Lime's LALR(1) algorithm is strictly O(n). For large inputs,
  this matters.
- **Grammar style**: ANTLR grammars combine lexer and parser rules in one
  file with EBNF notation. Lime grammars use BNF with separate tokenization.
- **Runtime overhead**: ANTLR requires a runtime library (50--200 KB
  depending on language). Lime parsers are self-contained.
- **Use case**: ANTLR excels at language tooling (IDE support, syntax
  highlighting, code generation). Lime excels at high-throughput embedded
  parsing (databases, compilers, protocol parsers).

### Lime vs Menhir

Menhir is an LR(1) parser generator for OCaml. Comparison is most relevant
for users choosing between the C and OCaml ecosystems:

- **Algorithm**: Menhir implements full LR(1), which is strictly more
  powerful than LALR(1). Some grammars accepted by Menhir require
  refactoring for Lime/Yacc/Bison.
- **Type safety**: Menhir leverages OCaml's type system for fully type-safe
  semantic actions. Lime uses C `void*` and struct types, which offer less
  compile-time safety.
- **Parameterized rules**: Menhir supports parameterized non-terminals
  (like generic types). Lime does not.
- **Error reporting**: Menhir has sophisticated error message generation
  via `.messages` files. Lime provides `%syntax_error` and `%parse_failure`
  callbacks.
- **Performance**: Both generate efficient table-driven parsers. Lime's JIT
  and SIMD features give it an edge for raw throughput on large inputs.

## When to Use Each Tool

### Use Lime when you need:

- **Runtime grammar modification** -- no other tool supports this
- **Maximum parsing throughput** -- SIMD + JIT optimizations
- **Thread-safe concurrent parsing** -- built-in snapshot architecture
- **Minimal dependencies** -- single C file, Public Domain license
- **Push-based parsing** -- integrate with event loops and custom tokenizers
- **C-language parsers** with modern memory safety practices

### Use Yacc when you need:

- **Maximum portability** -- available on virtually every Unix system
- **POSIX compliance** -- when standards require it
- **Smallest possible parser** -- minimal generated code
- **Legacy compatibility** -- maintaining existing yacc grammars

### Use Bison when you need:

- **GLR parsing** -- for ambiguous or non-LALR grammars
- **Multiple output languages** -- C++, Java, D targets
- **Mature tooling** -- decades of documentation and community support
- **IELR(1)** -- automatic resolution of LALR(1) inadequacies
- **`%expect`** -- to document and track expected conflicts

### Use ANTLR when you need:

- **IDE and tooling integration** -- grammar debugger, parse tree viewer
- **Multiple target languages** -- 10+ supported
- **EBNF notation** -- more natural for some grammar authors
- **Visitor/listener patterns** -- auto-generated tree walkers
- **Rapid prototyping** -- grammar development with immediate visualization

### Use Menhir when you need:

- **OCaml integration** -- native OCaml type system support
- **Full LR(1)** -- more powerful than LALR(1)
- **Parameterized rules** -- generic non-terminal definitions
- **Incremental parsing** -- Menhir supports incremental error recovery
- **Verified correctness** -- Menhir has been formally verified (CompCert)

## Migration Paths

If you are migrating from another parser generator to Lime, see the
dedicated migration guides:

- **[MIGRATION_FROM_BISON.md](MIGRATION_FROM_BISON.md)** -- Bison to Lime
- **[MIGRATION_FROM_YACC.md](MIGRATION_FROM_YACC.md)** -- Yacc to Lime

These guides include directive mapping tables, syntax translation rules,
and worked examples from the `examples/bootstrap/` directory (a real
PostgreSQL grammar converted from Bison to Lime).

## Performance Benchmarks

These numbers are from the Lime benchmark suite on Linux x86_64 (GCC 14,
-O2). See [PERFORMANCE.md](PERFORMANCE.md) for full methodology.

### Parse Latency

| Grammar Size | Lime (interpreted) | Lime (JIT) | Typical Bison/Yacc |
|--------------|-------------------|------------|-------------------|
| Small (64 states) | 424 ns | 168 ns | ~500 ns |
| Medium (256 states) | 1,244 ns | 412 ns | ~1,500 ns |
| Large (512 states) | 2,890 ns | 689 ns | ~3,500 ns |

### Tokenizer Throughput

| Implementation | Throughput |
|---------------|-----------|
| Lime (scalar) | ~343 MB/s |
| Lime (AVX2) | ~1.5 GB/s (estimated) |
| Typical flex-generated | ~200--400 MB/s |
| ANTLR lexer | ~50--150 MB/s |

### Memory Usage

| Tool | Base Snapshot | Per-Thread Overhead |
|------|--------------|-------------------|
| Lime | ~300 KB | ~4 KB (ParseContext + stack) |
| Bison/Yacc | ~50--200 KB | ~2 KB (parser stack) |
| ANTLR | ~2--10 MB | ~500 KB (parse tree nodes) |

Note: Lime's higher base memory reflects the snapshot architecture that
enables runtime extensibility and thread safety. When no extensions are
loaded, the active parsing overhead is comparable to Bison/Yacc.

## License Comparison

| Tool | License | Generated Code License |
|------|---------|----------------------|
| Lime | Public Domain | Public Domain |
| Yacc | Varies (BSD on most systems) | Same as tool |
| Bison | GPL v3 | GPL with exception (free to use) |
| ANTLR | BSD 3-Clause | BSD 3-Clause |
| Menhir | GPL v2 | Unencumbered (with `--only-preprocess`) |

Lime's Public Domain status means there are zero licensing restrictions on
the generator tool, the generated parser code, or the runtime template.
This makes it suitable for any project regardless of its own license.
