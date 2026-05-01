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

## Avoiding Symbol Collisions

### Generated parser symbols

A generated parser exports functions like `ParseAlloc`, `ParseFree`,
`Parse`, `ParseTrace`, plus macros like `ParseARG_STORE` and
`ParseCTX_FETCH`.  If you link multiple generated parsers into one
binary, or your project already defines a symbol called `Parse`, you
need to rename these.

Two options:

**Per-grammar, via the `%name` directive:**

```
%name SqlParser.
```

This renames every exported symbol to the prefix `SqlParser`:
`SqlParserAlloc`, `SqlParserFree`, `SqlParser`, etc.

**Per-invocation, via the `-P` command-line flag:**

```bash
./lime -PSqlParser grammar.y
```

The `-P` flag overrides `%name` without editing the grammar, which is
useful when you don't control the grammar file or want to generate the
same grammar under several names.

The two mechanisms are equivalent; pick whichever fits your build
better.  For example, in a Makefile:

```makefile
sql_parser.c: sql_grammar.y
	./lime -PSqlParser $<

json_parser.c: json_grammar.y
	./lime -PJsonParser $<
```

### Library (extension framework) symbols

The runtime extension library exports symbols in three naming schemes:

| Prefix | Scope | Examples |
|--------|-------|----------|
| `lime_*`, `Lime*`, `LIME_*` | Preferred modern API | `lime_arena_create`, `LimePluginHandle`, `LIME_PLUGIN_ABI_VERSION` |
| `lemon_*` | Legacy (snapshot/registry) | `lemon_snapshot_create`, `lemon_parser_version` |
| Unprefixed | Internal/runtime API | `parse_begin`, `Token`, `Tokenizer`, `ExtensionRegistry`, `snapshot_acquire` |

If you embed the library directly in your project (Option 3), the
unprefixed symbols may collide with existing identifiers in your
codebase.  Mitigations:

- **Link the library as a shared object** (`liblime_parser.so` /
  `.dylib`) so symbol resolution happens at load time rather than
  link time.  Conflicts become local to the library.
- **Use a separate translation unit per concern.**  Most unprefixed
  symbols are concentrated in `include/parse_context.h`,
  `include/token_table.h`, `include/conflict.h`, etc.  You typically
  only need `#include "parser.h"` in most of your code and can isolate
  the lower-level headers to one file.
- **Namespace via preprocessor** (advanced).  Before including any
  Lime header, add:

  ```c
  #define Token          LimeToken
  #define Tokenizer      LimeTokenizer
  #define ParseContext   LimeParseContext
  /* etc. */
  ```

  This is awkward, but works if you have a hard collision you cannot
  resolve otherwise.

The `STRAT_*` and `EXEC_*` enum values in `include/disambiguation.h`
and `include/execution_policy.h` are distinct from the `DISAMBIG_*`
and `EXEC_SEQUENTIAL`/`EXEC_PARALLEL` values in
`include/extension_registry.h`.  The runtime enums are typed
`LimeStrategy` and `LimeExecMode`; the metadata enums retain the
names `DisambiguationStrategy` and `ExecutionPolicy`.  Both sets
coexist without conflict in the same translation unit.

## Parser Allocation and Reuse

By default, the generated parser is heap-allocated via `ParseAlloc`.
For applications that parse repeatedly (database query engines,
interactive REPLs, language servers), the malloc/free cycle per
parse becomes a noticeable fraction of total time. Two patterns
eliminate it:

### Pattern A: Allocate once, reset between parses

`ParseInit(void *rawParser)` resets parser state without freeing
memory. The allocation covers the whole parser including its stack
(`YYSTACKDEPTH` entries, default 100), so one `malloc` is enough
unless the grammar needs deeper nesting for pathological input.

```c
void *parser = ParseAlloc(malloc);

while (have_input()) {
    ParseInit(parser);                      // reset to initial state
    for (tok = next_token(); tok.type; tok = next_token()) {
        Parse(parser, tok.type, tok.value);
    }
    Parse(parser, 0, 0);                    // EOF
    /* consume result */
}

ParseFinalize(parser);                      // run destructors
free(parser);
```

For a 55-token SQL parse this saves ~25 ns per parse (about 4% on
Apple Silicon; more on systems with slower mallocs).

### Pattern B: Stack-allocated parser (zero mallocs)

If your grammar has a known maximum stack depth, define
`%stack_size` accordingly and compile with
`-DParse_ENGINEALWAYSONSTACK`. This removes `ParseAlloc` and
`ParseFree` from the generated code — you provide the buffer:

```c
/* In your grammar file: */
%stack_size 200.

/* Compile with: -DParse_ENGINEALWAYSONSTACK */

/* At call site: */
struct yyParser parser;   /* or any buffer of sizeof(struct yyParser) */
ParseInit(&parser);
/* ... feed tokens ... */
ParseFinalize(&parser);
```

Zero allocations during parsing. Suitable for embedded use, real-time
systems, or any situation where you want to avoid malloc entirely.

The `%destructor` directive may still allocate semantic values;
control that separately by arena-allocating them in your reduction
actions.

## NDEBUG and Performance

Lime's generated parser contains `assert()` calls in its hot path
(the same as Lemon does; inherited from SQLite lineage). In
production builds these should be stripped:

```bash
# Meson: use release buildtype (NDEBUG set automatically)
meson setup builddir --buildtype=release

# Or keep debugoptimized but force b_ndebug
meson setup builddir -Db_ndebug=true

# Direct compilation:
cc -O2 -DNDEBUG -o parser parser.c
```

Without `-DNDEBUG`, asserts add ~20-35 ns per parse on this Apple
Silicon machine (roughly 5-10% of parse time). With `-DNDEBUG` they
vanish entirely.

The project's `meson.build` sets `b_ndebug=if-release`, so NDEBUG
is automatically defined for `--buildtype=release` and `plain`, but
not for the `debug` or `debugoptimized` defaults. If you use Lime's
meson build directly, pass `--buildtype=release` for benchmark-quality
builds.

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
