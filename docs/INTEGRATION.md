# Integrating Lime in Your Project

## Option 1: Generator Only (No Extension Framework)

If you only need to generate parsers from grammars — no runtime
extension, no SIMD, no JIT — embed two files:

```
your_project/
├── lime.c          # copy from Lime root
├── limpar.c        # copy from Lime root
└── Makefile
```

Build the generator:

```makefile
lime: lime.c
	$(CC) -o $@ $<
```

Generate a parser from your grammar:

```makefile
parser.c parser.h: grammar.y lime
	./lime grammar.y
```

That's it.  The generated `parser.c` has no external dependencies beyond
the C standard library.

## Option 2: Generator + Extension Framework (Meson)

To use runtime extensions, SIMD tokenization, or JIT, link against the
Lime extension library.

### As a Meson subproject

Create `subprojects/lime.wrap`:

```ini
[wrap-git]
url = https://codeberg.org/gregburd/lime.git
revision = main
depth = 1

[provide]
lime-parser = lime_parser_dep
```

In your `meson.build`:

```meson
lime_dep = dependency('lime-parser',
  fallback : ['lime', 'lime_parser_dep'])

executable('my_app', 'main.c',
  dependencies : lime_dep)
```

This gives you the full extension framework: `include/parser.h`,
`include/extension_registry.h`, `include/conflict.h`, etc.

### As a system library

Build and install Lime:

```bash
meson setup builddir --prefix=/usr/local
ninja -C builddir install
```

Then use `pkg-config` or find the headers in `/usr/local/include` and
the library in `/usr/local/lib`.

## Option 3: Generator + Extension Framework (Make / CMake)

### Makefile

Compile the extension library sources directly:

```makefile
LIME_SRC = snapshot.c snapshot_modify.c extension.c extension_registry.c \
           conflict.c conflict_detector.c disambiguation.c \
           strategy_priority.c strategy_fork_resolve.c \
           execution_policy.c context_switch.c grammar_context.c \
           parser_fork.c parser_manager.c parser_composition.c \
           parser_operations.c parser_plugin.c \
           tokenize.c tokenize_simd.c token_table.c \
           parse_context.c merkle_tree.c dependency_resolver.c \
           jit_context.c jit_codegen.c jit_policy.c jit_tokenizer.c \
           glr.c utf8.c lime_ast.c version.c

LIME_OBJ = $(LIME_SRC:%.c=lime/src/%.o)

lime/src/%.o: lime/src/%.c
	$(CC) -c -std=c11 -Ilime/include -o $@ $<

liblime_parser.a: $(LIME_OBJ)
	$(AR) rcs $@ $^

my_app: main.c liblime_parser.a
	$(CC) -std=c11 -Ilime/include -o $@ $< -L. -llime_parser -lpthread -ldl
```

### CMake

```cmake
add_subdirectory(lime)
target_link_libraries(my_app PRIVATE lime_parser)
target_include_directories(my_app PRIVATE lime/include)
```

(Lime does not ship a CMakeLists.txt, but the above pattern works if
you add one or compile the sources directly.)

## Linking

The extension framework requires:

| Library | Required | Purpose |
|---------|----------|---------|
| `pthreads` | Yes | Thread-safe registry, snapshot refcounting |
| `libdl` | Yes (Linux) | `dlopen`/`dlsym` for shared library extensions |
| `libm` | Optional | Math functions in benchmarks |
| `libLLVM` | Optional | JIT compilation (link with `llvm-config --libs`) |

On macOS, `dlopen` is in `libSystem` (no `-ldl` needed).

## Headers

All public headers are in `include/`.  The main entry points:

```c
#include "parser.h"              /* snapshots, parse sessions */
#include "extension_registry.h"  /* extension management */
#include "conflict.h"            /* conflict detection */
#include "disambiguation.h"      /* disambiguation strategies */
#include "execution_policy.h"    /* action dispatch policies */
#include "parser_manager.h"      /* high-level plugin management */
```

## Runtime Extension Loading

Extensions can be compiled as shared libraries and loaded at runtime:

```c
#include <dlfcn.h>
#include "extension_registry.h"

/* Load extension shared library */
void *lib = dlopen("./libmy_extension.so", RTLD_NOW);
ExtensionInitFunc init = dlsym(lib, "extension_init");

/* Register and load */
ExtensionRegistry *reg = extension_registry_create();
init(reg);  /* extension registers itself */
```

See `examples/calc/` for a complete working example of dlopen-based
extension loading, and `examples/plugin_template/` for a reusable
template.

## Generating Parsers at Build Time

Add a build step that runs `lime` on your grammar:

```makefile
# Generate parser from grammar
sql_parser.c sql_parser.h: sql_grammar.y lime
	./lime -d $(dir $@) $<
```

The generated parser includes the template code from `limpar.c`.  If
you want to customize the template (e.g., to add tracing or change
memory allocation), use `-T`:

```bash
./lime -T my_template.c grammar.y
```

## Thread Safety Checklist

- ✓ Snapshots are safe to share across threads (atomic refcount)
- ✓ Extension registry uses read-write locks
- ✓ Parse sessions (`ParseContext`) are per-thread, not shared
- ✓ Loading/unloading extensions is safe during concurrent parsing
- ✗ The `lime` generator itself is single-threaded (run once at build time)

## Further Reading

- [API Reference](API.md) — function signatures and semantics
- [Extensions](EXTENSIONS.md) — writing extension plugins
- [Architecture](ARCHITECTURE.md) — component diagram
- [Performance](PERFORMANCE.md) — tuning and overhead analysis
