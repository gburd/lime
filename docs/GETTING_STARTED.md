# Getting Started

## Prerequisites

- A C11 compiler: GCC 13+ or Clang 15+
- Meson 0.60+ and Ninja (for the extension framework)
- Optional: LLVM 17+ (for JIT), Valgrind (for memory checking)

All dependencies are provided by `nix develop` if you use Nix.

## Building the Generator

Lime's parser generator is a single C file with no dependencies:

```bash
cc -o lime lime.c
```

That's it.  You now have a working parser generator.

## Your First Grammar

Create `expr.y`:

```
%token_type { int }
%type expr { int }

%left PLUS MINUS.
%left TIMES DIVIDE.

program ::= expr(A). { printf("Result: %d\n", A); }

expr(A) ::= expr(B) PLUS expr(C).   { A = B + C; }
expr(A) ::= expr(B) MINUS expr(C).  { A = B - C; }
expr(A) ::= expr(B) TIMES expr(C).  { A = B * C; }
expr(A) ::= expr(B) DIVIDE expr(C). { A = B / C; }
expr(A) ::= INTEGER(B).             { A = B; }
```

Generate the parser:

```bash
./lime expr.y
```

This produces `expr.c` and `expr.h`.  The generated parser is a
push-parser: you feed it tokens one at a time.

## Writing a Driver

Create `main.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include "expr.h"

void *ParseAlloc(void *(*allocProc)(size_t));
void ParseFree(void *parser, void (*freeProc)(void *));
void Parse(void *parser, int tokenCode, int tokenValue);

int main(void) {
    void *parser = ParseAlloc(malloc);

    /* Feed tokens: 3 + 4 * 2 */
    Parse(parser, INTEGER, 3);
    Parse(parser, PLUS, 0);
    Parse(parser, INTEGER, 4);
    Parse(parser, TIMES, 0);
    Parse(parser, INTEGER, 2);
    Parse(parser, 0, 0);  /* end of input */

    ParseFree(parser, free);
    return 0;
}
```

Compile and run:

```bash
cc -o calc main.c expr.c
./calc
# Output: Result: 11
```

## Building the Extension Framework

If you need runtime grammar extension, SIMD tokenization, or JIT:

```bash
meson setup builddir
ninja -C builddir
ninja -C builddir test
```

This builds:
- The `lime` generator
- `liblime_parser.a` — the extension framework library
- All tests and benchmarks

## Useful Flags

| Flag | Description |
|------|-------------|
| `-d dir` | Write output files to `dir` |
| `-T file` | Use a custom parser template instead of `limpar.c` |
| `-s` | Print parser statistics (states, rules, conflicts) |
| `-L` | Lint: validate grammar without generating code |
| `-F` | Format: rewrite grammar with consistent style |
| `-q` | Quiet: don't generate the `.out` report file |
| `-l` | Omit `#line` directives from generated code |
| `-x` | Print version and exit |

## Next Steps

- [Concepts](CONCEPTS.md) — understand snapshots, extensions, and
  conflict resolution
- [Integration](INTEGRATION.md) — embed Lime in your project
- [Extensions](EXTENSIONS.md) — write runtime grammar extensions
- [Examples](EXAMPLES.md) — working examples from calculators to
  full PostgreSQL grammars
- [API Reference](API.md) — complete C API documentation
