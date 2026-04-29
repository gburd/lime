# Calculator Example

A four-operation calculator built with the Lime parser generator, extended at
runtime with shared library plugins.  This example demonstrates:

- Base grammar definition with Lime
- Runtime extension loading via `dlopen`
- **Conflicting grammar extensions** and conflict detection/resolution

## Files

| File | Description |
|------|-------------|
| `calc.lime` | Base grammar: `+`, `-`, `*`, `/` |
| `calc_power.lime` | Power extension: `^` as exponentiation |
| `calc_bitwise.lime` | Bitwise extension: `&`, `\|`, `^` (XOR), `<<`, `>>` |
| `calc_power_ext.c` | Power plugin (shared library entry point) |
| `calc_bitwise_ext.c` | Bitwise plugin (shared library entry point) |
| `main.c` | Demo: base parser + power extension |
| `main_conflicts.c` | Demo: conflict detection between power and bitwise |
| `tokenize.c` / `tokenize.h` | Shared tokenizer for all grammars |

## Building

```
make all
```

This produces:

- `calc` -- base calculator with power extension loading
- `libcalc_power.so` -- power extension shared library
- `libcalc_bitwise.so` -- bitwise extension shared library
- `calc_conflicts` -- conflict demonstration executable

## Running

### Base calculator with power extension

```
make test
```

### Conflict demonstration

```
make test-conflicts
```

## Grammar Extension Conflicts

The power and bitwise extensions both define the `^` (CARET) token, but with
different semantics:

| Extension | `^` means | `2 ^ 3` result | Associativity |
|-----------|-----------|-----------------|---------------|
| `power_operator` | Exponentiation | 8 | Right |
| `bitwise_operators` | Bitwise XOR | 1 | Left |

### Detected conflicts

When both extensions are loaded, the conflict detection system identifies
three conflicts:

1. **Token code conflict** -- Both extensions claim token code 8 for CARET,
   but assign it different semantics (exponentiation vs. XOR).

2. **Rule conflict** -- Both extensions add the same production shape
   `expr -> expr CARET expr`, but with different reduction actions.

3. **Precedence conflict** -- Both extensions set precedence for the CARET
   symbol, but with different values and associativity (right-associative
   at precedence 3 for power vs. left-associative at precedence 2 for
   bitwise).

### Resolution strategy

Since each extension compiles to its own complete parser (a separate `.so`
file with its own LALR(1) tables), conflicts are resolved at the
**dispatch level**: the host program chooses which extension's `eval()`
function to call for expressions containing the disputed `^` operator.

The `main_conflicts.c` demonstration shows all four scenarios:

| Scenario | Extensions loaded | Priority | `2 ^ 3` |
|----------|-------------------|----------|---------|
| 1 | Power only | -- | 8 |
| 2 | Bitwise only | -- | 1 |
| 3 | Both | Power wins | 8 |
| 4 | Both | Bitwise wins | 1 |

### How the plugin descriptor enables conflict detection

Each plugin exports a `CalcGrammarModification` array describing its grammar
changes (tokens added, rules added, precedence modifications).  The host
program inspects these descriptors to detect overlapping token codes,
identical production shapes, and conflicting precedence assignments -- all
without needing access to the original `.lime` source files.

## Bitwise extension operators

The bitwise extension adds five operators with C-standard precedence
ordering (lowest to highest):

| Operator | Token | Meaning | Precedence |
|----------|-------|---------|------------|
| `\|` | PIPE | Bitwise OR | 1 (lowest) |
| `^` | CARET | Bitwise XOR | 2 |
| `&` | AMPERSAND | Bitwise AND | 3 |
| `<<` | LSHIFT | Left shift | 4 |
| `>>` | RSHIFT | Right shift | 4 |

Arithmetic operators (`+`, `-`, `*`, `/`) retain higher precedence than all
bitwise operators, matching C semantics.
