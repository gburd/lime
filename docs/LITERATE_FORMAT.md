# Literate Grammar Format Specification

This document specifies the literate `.lime` file format used by the
`lime-compose` tool to assemble modular grammars for the Lime parser
generator.

## Overview

A literate grammar file is a Markdown document (`.md`) that interleaves
human-readable documentation with machine-extractable grammar code and
module metadata. The `lime-compose` tool reads one or more literate files,
resolves inter-module dependencies, and produces a single `.lime` file
suitable for the Lime parser generator.

This format replaces ad-hoc `cat` concatenation (as seen in the PostgreSQL
example Makefile) with a structured, dependency-aware composition system.

## File Structure

A literate grammar file has three kinds of content:

1. **Markdown prose** -- documentation, headings, paragraphs, lists.
   Ignored by `lime-compose`; rendered by any Markdown viewer.
2. **YAML metadata block** -- exactly one fenced block tagged `yaml`
   containing module identity, capabilities, and dependencies.
3. **Lime code blocks** -- one or more fenced blocks tagged `lime`
   containing actual grammar directives and rules.

Everything outside of `yaml` and `lime` fenced code blocks is treated as
documentation and discarded during composition.

## Metadata Block

Each file must contain exactly one YAML metadata block:

    ```yaml
    name: my-module
    version: 1.0.0
    description: Brief description of this module
    provides: [tokens, precedence]
    depends: [core-types]
    ```

### Required Fields

| Field  | Type   | Description                          |
|--------|--------|--------------------------------------|
| `name` | string | Unique module identifier (ASCII)     |

### Optional Fields

| Field         | Type         | Default | Description                              |
|---------------|--------------|---------|------------------------------------------|
| `version`     | string       | 0.0.0   | Semantic version of this module          |
| `description` | string       | (empty) | One-line human-readable description      |
| `provides`    | list/string  | []      | Capabilities this module exports         |
| `depends`     | list/string  | []      | Capabilities this module requires        |

### Naming Rules

Module names must match `[A-Za-z_][A-Za-z0-9_.-]*` (ASCII only).

### Capabilities

Capabilities are arbitrary strings used to express inter-module contracts.
A module lists what it `provides` and what it `depends` on. During
composition, `lime-compose` verifies that every dependency is satisfied by
some module's provides list.

Common capability conventions:

- `tokens` -- the module defines token declarations
- `precedence` -- the module defines operator precedence
- `types` -- the module defines non-terminal type declarations
- `rules-<section>` -- the module defines grammar rules for a section

## Code Blocks

Grammar code lives in fenced blocks tagged `lime`:

    ```lime
    %token IDENT.
    %token ICONST.
    ```

Multiple code blocks per file are supported and are concatenated in order
of appearance. An optional label may follow the `lime` tag:

    ```lime token-declarations
    %token IDENT.
    %token ICONST.
    ```

Labels are included as comments in the composed output for traceability.

### Content Rules

- Code blocks contain standard Lime grammar directives: `%token`, `%type`,
  `%name`, `%include`, `%left`, `%right`, `%nonassoc`, `%start_symbol`,
  `%syntax_error`, `%parse_failure`, production rules, and C action code.
- Only ASCII characters are permitted in code blocks.
- Empty code blocks are allowed but serve no purpose.

## Composition Process

When `lime-compose` processes input files, it performs these steps:

1. **Parse** each file, extracting the metadata block and all code blocks.
2. **Validate** that every module has a unique name and that no capability
   is provided by more than one module.
3. **Resolve dependencies** by checking that every `depends` entry is
   satisfied by some module's `provides` list.
4. **Topological sort** modules so that providers come before consumers.
   Modules with no dependency relationship are ordered alphabetically for
   deterministic output.
5. **Compose** by concatenating code blocks in resolved order, with
   provenance comments marking module boundaries.

### Cyclic Dependencies

Cyclic dependencies cause `lime-compose` to exit with an error listing
the modules involved in the cycle.

### Duplicate Detection

- Duplicate module names across input files are rejected.
- Duplicate capability names provided by different modules are rejected.

## Output Format

The composed output is a valid `.lime` file containing:

1. A header comment listing all composed modules and their versions.
2. For each module (in dependency order):
   - A banner comment identifying the module, version, and source file.
   - The concatenated code blocks from that module.

## Example

Given two files:

**tokens.md**:

    # SQL Tokens

    Token definitions for the SQL parser.

    ```yaml
    name: sql-tokens
    version: 1.0.0
    description: SQL token declarations
    provides: [tokens, precedence]
    ```

    ## Non-keyword tokens

    ```lime token-declarations
    %token IDENT.
    %token ICONST.
    %token SCONST.
    ```

    ## Precedence

    ```lime precedence
    %left OR.
    %left AND.
    %right NOT.
    ```

**grammar.md**:

    # SQL Grammar Rules

    Production rules for basic SQL statements.

    ```yaml
    name: sql-grammar
    version: 1.0.0
    description: SQL production rules
    provides: [rules]
    depends: [tokens]
    ```

    ## Statements

    ```lime
    stmt ::= select_stmt.
    stmt ::= insert_stmt.
    ```

Running:

    lime-compose tokens.md grammar.md -o sql_parser.lime

Produces `sql_parser.lime` with tokens first (since grammar depends on
tokens), followed by grammar rules, each section marked with provenance
comments.

## Tool Reference

    lime-compose [options] <file.md>...

| Option            | Description                                      |
|-------------------|--------------------------------------------------|
| `-o FILE`         | Write output to FILE (default: stdout)           |
| `-v, --verbose`   | Print resolution order to stderr                 |
| `-n, --dry-run`   | Validate only, no output                         |
| `--no-validate`   | Skip dependency validation                       |
| `--list-modules`  | List modules and exit                            |
| `-h, --help`      | Show help message                                |
