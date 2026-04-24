# Lime Module Format

**Author**: Lime Parser Generator Team
**Date**: 2026-04-24
**Status**: Active

## Overview

Lime supports modular grammar development through **directive-based metadata** embedded directly in `.lime` files. This format allows you to split large grammars into smaller, reusable modules while maintaining all composition capabilities (dependency tracking, symbol resolution, and topological ordering).

### Why Directive Format?

The directive format is designed for developers familiar with Bison/Yacc:

- **Familiar Syntax**: Uses `%` directives just like `%token`, `%type`, `%left`, etc.
- **Single File**: All metadata in the grammar file itself—no separate YAML or JSON
- **No Nesting**: Grammar rules are direct, not wrapped in Markdown or fenced blocks
- **Minimal Overhead**: ~4x less metadata overhead compared to literate format
- **C-Style Comments**: Standard `/* */` comments throughout
- **Professional Format**: Looks like production grammar files

### Comparison to Bison/Yacc

If you know Bison/Yacc, you already know most of Lime's module format:

| Bison/Yacc | Lime Module |
|------------|-------------|
| `%token` | Same |
| `%type` | Same |
| `%left`, `%right` | Same |
| *(not available)* | `%module_name` |
| *(not available)* | `%module_version` |
| *(not available)* | `%require`, `%export`, `%import` |

## Directive Syntax

### Module Identity

**Required for modular grammars:**

```lime
%module_name identifier
%module_version "major.minor.patch"
```

**Optional:**

```lime
%module_description "Short description"
```

**Example:**

```lime
%module_name expr
%module_version "1.0.0"
%module_description "Expression grammar with operators and precedence"
```

**Rules:**

- `module_name` must be alphanumeric plus underscores (e.g., `expr`, `pg_base`, `json_parser`)
- `module_version` must be semantic version format (e.g., `"1.0.0"`, `"2.1.3"`)
- `module_description` is optional, use quotes if it contains spaces
- If `%module_name` is present, `%module_version` is required

### Dependencies

**Syntax:**

```lime
%require module_name [version_constraint].
```

**Examples:**

```lime
%require base.
%require operators ">= 1.0.0".
%require utils ">= 2.1.0 < 3.0.0".
```

**Rules:**

- Module name must match a `%module_name` in another file
- Version constraint is optional, uses operators: `>=`, `<=`, `>`, `<`, `==`
- Constraint must be quoted if it contains spaces
- Ends with period (`.`)
- Multiple `%require` directives allowed

### Exports

**Syntax:**

```lime
%export symbol1 symbol2 symbol3.
```

**Example:**

```lime
%export a_expr b_expr c_expr func_expr.
```

**Rules:**

- Lists non-terminals that this module makes available to other modules
- Symbols must be defined in this module's grammar rules
- Ends with period (`.`)
- Multiple `%export` directives allowed (symbols are accumulated)

### Imports

**Syntax:**

```lime
%import symbol1 symbol2 ... from module_name.
```

**Example:**

```lime
%import IDENT INTEGER STRING from base.
%import PLUS MINUS STAR SLASH from operators.
```

**Rules:**

- Lists symbols (terminals or non-terminals) imported from another module
- `from module_name` clause specifies which module exports them
- Symbols must be exported by the specified module
- Ends with period (`.`)
- Multiple `%import` directives allowed

## Complete Example

```lime
/* ======================================================================== */
/* Expression Grammar                                                       */
/* ======================================================================== */

%module_name expr
%module_version "1.0.0"
%module_description "PostgreSQL expression grammar"

%require base ">= 1.0.0".
%require operators ">= 1.0.0".

%export a_expr b_expr c_expr.
%import IDENT INTEGER STRING from base.
%import PLUS MINUS STAR SLASH from operators.

%name ExprParser
%token_type {Node*}
%extra_argument {ParseState *state}

%left PLUS MINUS.
%left STAR SLASH.

%type a_expr {Node*}
%type b_expr {Node*}
%type c_expr {Node*}

/* ======================================================================== */
/* Grammar Rules                                                            */
/* ======================================================================== */

a_expr(A) ::= c_expr(B). {
  A = B;
}

a_expr(A) ::= a_expr(B) PLUS a_expr(C). [PLUS] {
  A = makeSimpleA_Expr(AEXPR_OP, "+", B, C);
}

a_expr(A) ::= a_expr(B) MINUS a_expr(C). [MINUS] {
  A = makeSimpleA_Expr(AEXPR_OP, "-", B, C);
}

b_expr(A) ::= c_expr(B). {
  A = B;
}

c_expr(A) ::= INTEGER(B). {
  A = makeIntConst(B.ival, B.location);
}

c_expr(A) ::= IDENT(B). {
  A = makeColumnRef(B.str, NIL, B.location, yyscanner);
}
```

## Module Composition

### Using lime-compose

The `lime-compose` tool combines multiple module files into a single grammar:

```bash
# Compose modules
lime-compose -o output.lime base.lime operators.lime expr.lime

# Verbose mode (show resolution order)
lime-compose -v -o output.lime base.lime operators.lime expr.lime

# List modules without composing
lime-compose --list-modules base.lime operators.lime expr.lime

# Dry-run (validate without output)
lime-compose -n base.lime operators.lime expr.lime
```

### How Composition Works

1. **Parse**: Extracts `%module_*`, `%require`, `%export`, `%import` directives from each file
2. **Validate**: Checks that all dependencies are satisfied and no circular dependencies exist
3. **Sort**: Performs topological sort to ensure dependencies come before dependents
4. **Strip**: Removes module directives from composed output
5. **Concatenate**: Combines modules in dependency order

### Composed Output

The composed grammar includes:

- Header comment listing all modules and versions
- Module provenance comments (source file, version)
- All grammar code with module directives removed
- All non-module directives preserved (`%token`, `%type`, `%left`, etc.)

**Example output header:**

```lime
/*
 * Composed grammar generated by lime-compose
 *
 * Modules (3):
 *   - base 1.0.0
 *     Base tokens and identifiers
 *   - operators 1.0.0
 *     Arithmetic and logical operators
 *   - expr 1.0.0
 *     Expression grammar
 */

/* ================================================================== */
/* Module: base                                                       */
/* Version: 1.0.0                                                     */
/* Source: base.lime                                                  */
/* ================================================================== */

/* grammar code here */
```

## Migration Guide

### From Markdown + YAML Format

If you have existing literate format modules, use the conversion script:

```bash
# Convert single file
python3 tools/convert_format.py input.md output.lime

# Convert all files in directory
for f in *.md; do
    python3 tools/convert_format.py "$f" "${f%.md}.lime"
done
```

**What changes:**

| Old Format (Markdown + YAML) | New Format (Directive) |
|-------------------------------|------------------------|
| ````yaml<br>name: expr<br>version: 1.0.0<br>`````` | `%module_name expr`<br>`%module_version "1.0.0"` |
| ````yaml<br>depends: [base, ops]<br>`````` | `%require base.`<br>`%require ops.` |
| ````yaml<br>provides: [a_expr, b_expr]<br>`````` | `%export a_expr b_expr.` |
| ````lime<br>a_expr ::= ...<br>`````` | `a_expr ::= ...` |

### From Monolithic Grammar

To modularize an existing single-file grammar:

1. **Identify Logical Sections**: Group related rules (e.g., expressions, statements, declarations)
2. **Create Base Module**: Extract common tokens and types
3. **Split Sections**: Move each logical section to its own module
4. **Add Dependencies**: Use `%require` for inter-module dependencies
5. **Define Exports**: Mark which symbols other modules can use
6. **Test Composition**: Verify the composed grammar matches the original

**Example:**

**Before (monolithic):**

```lime
%token IDENT INTEGER PLUS MINUS.
%type expr {int}
%type stmt {int}

expr ::= INTEGER.
expr ::= expr PLUS expr.
stmt ::= expr.
```

**After (modular):**

**base.lime:**
```lime
%module_name base
%module_version "1.0.0"
%export IDENT INTEGER.
%token IDENT INTEGER.
```

**expr.lime:**
```lime
%module_name expr
%module_version "1.0.0"
%require base.
%import INTEGER from base.
%export expr.

%token PLUS MINUS.
%type expr {int}

expr ::= INTEGER.
expr ::= expr PLUS expr.
```

**stmt.lime:**
```lime
%module_name stmt
%module_version "1.0.0"
%require expr.
%import expr from expr.
%export stmt.

%type stmt {int}
stmt ::= expr.
```

## Best Practices

### Module Organization

1. **One Concern Per Module**: Each module should handle one grammatical category
2. **Stable Interfaces**: Export symbols are your public API—keep them stable
3. **Version Carefully**: Use semantic versioning (major.minor.patch)
4. **Document Dependencies**: Use `%module_description` to explain what the module does

### Naming Conventions

1. **Module Names**: lowercase with underscores (e.g., `json_parser`, `sql_expr`)
2. **Exported Non-Terminals**: descriptive names (e.g., `a_expr`, `select_stmt`)
3. **Imported Terminals**: UPPERCASE (e.g., `IDENT`, `INTEGER`, `PLUS`)

### Dependency Management

1. **Minimize Dependencies**: Only `%require` what you actually need
2. **Avoid Cycles**: Never create circular dependencies (A→B→A)
3. **Version Constraints**: Use constraints for breaking changes (e.g., `">= 2.0.0"`)
4. **Explicit Imports**: Always list imported symbols with `%import`

### File Layout

Recommended order within a `.lime` file:

1. Header comment
2. Module directives (`%module_name`, `%module_version`, `%module_description`)
3. Dependencies (`%require`)
4. Exports/Imports (`%export`, `%import`)
5. Standard Lime directives (`%name`, `%token_type`, `%extra_argument`)
6. Token declarations (`%token`)
7. Precedence declarations (`%left`, `%right`, `%nonassoc`)
8. Type declarations (`%type`)
9. Grammar rules

## Validation

### Module Metadata Validation

Lime validates module directives during parsing:

- **Error**: `%module_name` without `%module_version`
- **Error**: Invalid semantic version format
- **Error**: Unknown directive keyword
- **Warning**: Undefined symbols in rules

### Composition Validation

`lime-compose` validates dependencies:

- **Error**: Missing required module
- **Error**: Circular dependency
- **Error**: Duplicate module names
- **Error**: Import from module that doesn't export the symbol
- **Warning**: Module exports symbols that no one imports

## Troubleshooting

### "Unknown declaration keyword: %module_name"

**Cause**: Using an older version of Lime without module support

**Fix**: Rebuild Lime from the latest source:

```bash
gcc -o lime lime.c
```

### "Module 'foo' requires 'bar', which is not available"

**Cause**: Missing dependency in composition command

**Fix**: Include all required modules:

```bash
lime-compose -o output.lime bar.lime foo.lime  # bar before foo
```

### "Cyclic dependency detected among modules"

**Cause**: Circular dependency (A→B→A)

**Fix**: Refactor to break the cycle:

1. Identify the cycle in error message
2. Extract common dependencies to a base module
3. Have both modules depend on the base instead of each other

### "Symbol 'X' is exported by both 'A' and 'B'"

**Cause**: Two modules export the same symbol

**Fix**: Rename one of the symbols or remove the duplicate export

## Advanced Topics

### Version Constraints

Supported operators:

- `>=` : Greater than or equal
- `<=` : Less than or equal
- `>` : Greater than
- `<` : Less than
- `==` : Exact match

Combine operators for ranges:

```lime
%require utils ">= 2.0.0 < 3.0.0".  /* 2.x only */
```

### Re-exporting Symbols

To import and then export a symbol:

```lime
%import common_expr from base.
%export common_expr.  /* Re-export for downstream modules */
```

### Optional Dependencies

Currently not supported. All `%require` dependencies must be satisfied.

### Conditional Compilation

Use Lime's preprocessor directives:

```lime
%ifdef ENABLE_JSON
%require json ">= 1.0.0".
%import json_expr from json.
%endif
```

## References

- **Lime Parser Generator**: Main tool documentation
- **Bison Manual**: https://www.gnu.org/software/bison/manual/
- **Semantic Versioning**: https://semver.org/

## Examples

See `examples/` directory:

- `examples/pg_modular/` - PostgreSQL grammar split into 35+ modules
- `examples/datalog/` - Datalog parser with module directives
- `examples/xpath/` - XPath query language modules
- `examples/xquery/` - XQuery language modules

---

**For questions or issues, see**: https://github.com/anthropics/lime/issues
