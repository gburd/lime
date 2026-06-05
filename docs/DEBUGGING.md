# Debugging Lime

This guide covers debugging Lime itself and Lime-generated parsers.

## Pretty-printers for GDB and LLDB

Lime ships pretty-printers for the most-debugged types:

  * `ParserSnapshot` -- decodes magic + abi + refcount + state/rule counts
  * `ParseContext` -- shows the snapshot it pins + engine state
  * `ParseEngine` -- runtime parser internals (initialised/accepted/errored, stack depth)
  * `LimeLexCompiled` -- emitted-lexer compile state (for `emit_*.c` debugging)

### GDB

One-shot, in a session:

```
(gdb) source /path/to/lime/scripts/lime-gdb.py
```

Persistent:

```
echo "source /path/to/lime/scripts/lime-gdb.py" >> ~/.gdbinit
```

Once loaded:

```
(gdb) print snap
$1 = ParserSnapshot{magic=LIME abi=2 ver=1 refcount=1 nrule=9 nstate=11}

(gdb) lime-snapshot snap
  magic:          0x4c494d45  (LIME, ok)
  abi_version:    2
  version:        1
  refcount:       1
  nstate:         11
  nrule:          9
  nsymbol:        12
  nterminal:      8
  nfallback:      0
  yy_action:      0x555555563440
  yy_lookahead:   0x555555563480
  yy_default:     0x555555563550
  yy_max_shift:   10
  yy_min_reduce:  28
  jit_ctx:        0x0
  jit_find_fn:    0x0

(gdb) lime-stack ctx
  depth:    3 / 64
  flags:    init
    [  0] state=   0  major=  0
    [  1] state=   3  major=  4
    [  2] state=   8  major=  6

(gdb) lime-actions snap
  action_count=37
  yy_action[head]:    11 13 25 26 27 14 15 16
  yy_lookahead[head]: 1 2 3 4 5 6 7 8
```

### LLDB

```
(lldb) command script import /path/to/lime/scripts/lime-lldb.py
```

Persistent:

```
echo "command script import /path/to/lime/scripts/lime-lldb.py" >> ~/.lldbinit
```

Same commands and summaries: `lime-snapshot`, `lime-stack`, `lime-actions`.

## Sanitisers

Build-time:

```
meson setup build-asan -Db_sanitize=address,undefined
meson setup build-tsan -Db_sanitize=thread
meson setup build-msan -Db_sanitize=memory       # clang only
ninja -C build-asan && meson test -C build-asan
```

The CI matrix covers `address+undefined` and `thread` on every push.

## Valgrind

```
meson setup build-valgrind --buildtype=debug
ninja -C build-valgrind
meson test -C build-valgrind --wrap='valgrind --error-exitcode=1 --leak-check=full'
```

CI runs valgrind on a small subset of tests on each push.

## Reproducing a parse step-by-step

1. Build with `--buildtype=debug` so `parse_engine.c` is not inlined.
2. Set a breakpoint at `parse_engine_step`:
   ```
   (gdb) b parse_engine_step
   ```
3. Each call through the breakpoint lets you inspect `ctx`, the parse
   stack, and `token_code`. Use `lime-stack ctx` to dump the stack.

For grammar bugs (rules not firing, ambiguity, conflicts):

1. Run `lime -g grammar.y` to get a reprint with all rule numbers and
   action labels.
2. Use `lime -L grammar.y --lint-format=gcc` to get jumpable diagnostics
   (`<file>:<line>:<col>: warning: ...`) ready for editor integration.
3. Run with `LIME_TRACE=1` (if your generated parser is built with
   `Parse(..., yyTraceFILE)` enabled) to log every shift/reduce.

## JIT debugging

When a JIT'd parser misbehaves but the interpreted parser works, set:

```
export LIME_NO_JIT=1
```

This forces the runtime to use the interpreted path. Compare the two
runs to localise the divergence.

To inspect the JIT'd machine code, build with `LIME_JIT_DUMP=1`. Per-state
disassembly goes to stderr.

## See also

  * [`docs/LSP.md`](LSP.md) -- LSP-driven editor diagnostics
  * [`docs/PERFORMANCE.md`](PERFORMANCE.md) -- production build recipe
  * [`docs/LINT.md`](LINT.md) -- linter rule catalogue
