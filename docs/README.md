# Lime Documentation

## Getting Started

| Document | Description |
|----------|-------------|
| [GETTING_STARTED.md](GETTING_STARTED.md) | Build Lime, write your first grammar, generate a parser |
| [CONCEPTS.md](CONCEPTS.md) | Snapshots, extensions, conflicts, disambiguation, JIT |
| [INTEGRATION.md](INTEGRATION.md) | Embed Lime in your project (Meson, Make, CMake) |
| [EXAMPLES.md](EXAMPLES.md) | All examples: calculators, PostgreSQL, query languages |

## Reference

| Document | Description |
|----------|-------------|
| [API.md](API.md) | C API reference for the extension framework |
| [ALGORITHM.md](ALGORITHM.md) | LALR(1) parsing theory and Lime's implementation |
| [ARCHITECTURE.md](ARCHITECTURE.md) | System design and component overview |
| [DIAGNOSTICS.md](DIAGNOSTICS.md) | Producing rich parser error messages and error recovery |
| [CI.md](CI.md) | Continuous integration setup, jobs, and runner tiers |
| [EXTENSIONS.md](EXTENSIONS.md) | Writing runtime grammar extensions |
| [DIALECT.md](DIALECT.md) | `%dialect NAME { ... }` directive: generator-time conditional rule inclusion |
| [EXTENDS.md](EXTENDS.md) | `%extends "base.lime"` + `%override` / `%remove` / `%override_type`: file-level grammar inheritance with diamond resolution |
| [DIFF_CONFLICTS.md](DIFF_CONFLICTS.md) | `lime --diff-conflicts base.lime ext.lime`: symbolic LALR-conflict diff for dialect-overlay review and CI |
| [LINT.md](LINT.md) | `lime -L`: opinionated grammar-hygiene linter (E001-E005, W001-W009, S001-S002) with `gcc` / `json` output formats and CI integration recipes |
| [CONTEXT_SWITCH.md](CONTEXT_SWITCH.md) | Multi-grammar parsing: register triggers that switch between sub-grammars at runtime |
| [EMBED.md](EMBED.md) | `%embed lang TRIGGER 'lex' ENTRY_TOKEN TOKEN.` directive: sugar over the context-switch trigger registry |
| [PERFORMANCE.md](PERFORMANCE.md) | Performance characteristics and tuning |
| [EXTENSION_PERFORMANCE.md](EXTENSION_PERFORMANCE.md) | Extension overhead analysis |
| [BENCHMARKS_VS_BISON.md](BENCHMARKS_VS_BISON.md) | Head-to-head comparison with GNU Bison |
| [COMPARISON.md](COMPARISON.md) | Feature comparison with Yacc, Bison, ANTLR, Menhir |
| [MIGRATION_FROM_BISON.md](MIGRATION_FROM_BISON.md) | Porting Bison grammars to Lime |
| [MIGRATION_FROM_YACC.md](MIGRATION_FROM_YACC.md) | Porting Yacc grammars to Lime |
| [MIGRATION_FROM_FLEX.md](MIGRATION_FROM_FLEX.md) | Porting flex scanners to Lime `.lex` grammars |
| [JIT_ANALYSIS.md](JIT_ANALYSIS.md) | JIT compilation cost-benefit analysis |
| [GLR.md](GLR.md) | Generalized-LR parser support: API, performance, when to use |
| [PARSER_PLUGIN_DESIGN.md](PARSER_PLUGIN_DESIGN.md) | Plugin system design |
| [LEXER_DESIGN.md](LEXER_DESIGN.md) | Lexer component design |
| [LEXER_SCANNER_AUDIT.md](LEXER_SCANNER_AUDIT.md) | Empirical audit of 6 PG flex scanners |
| [LSP.md](LSP.md) | `lime-lsp` Language Server: capability surface, diagnostics flow, editor wire-up. See also [`editors/lime-lsp-config.md`](../editors/lime-lsp-config.md). |
| [LSP_DESIGN.md](LSP_DESIGN.md) | Original (v0.2.x) LSP design sketch. Phase 1 implemented in v0.5.0; see LSP.md. |
| [MODULE_FORMAT.md](MODULE_FORMAT.md) | Modular grammar file format |
| [LITERATE_FORMAT.md](LITERATE_FORMAT.md) | Literate grammar format |

## Man Pages

Installed by `meson install` or readable directly:

- `man/lime.1` — command-line reference
- `man/lime_grammar.5` — grammar file format
- `man/lime_lex.5` — lexer (`.lex`) file format and runtime API

## API Documentation (Doxygen)

Generate searchable HTML docs from annotated headers:

```bash
cd docs && make doxygen
open api/html/index.html
```

Requires `doxygen`. The Doxyfile lives in this directory.
