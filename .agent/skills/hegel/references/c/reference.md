# hegel-c — C99 reference

`hegel-c` is the C99 binding for Hegel's property-based testing
protocol, hosted at https://github.com/gburd/hegel-c.  Hegel's
shrinking and search engine run as a separate `hegel` process
that the library spawns; your test binary speaks to it over the
Hegel protocol.

This reference covers what an agent needs to write a hegel-c
test in a project that has no hegel-c integration yet.

## Prerequisites

  - C99 compiler (gcc or clang)
  - CMake 3.14+
  - libcbor
  - zlib
  - cmocka (only for hegel-c's own tests; not required to use the
    library)
  - The `hegel` binary (from
    https://github.com/hegeldev/hegel-core).  Set
    `HEGEL_SERVER_COMMAND=/path/to/hegel` if not on `PATH`.

`hegel` itself ships pre-built binaries; the prerequisites above
are for hegel-c only.

## Build / link

The library produces `libhegel.a` (or `.so`).  Link with libcbor
and zlib:

```sh
gcc -std=c99 -o my_test my_test.c -lhegel -lcbor -lz
```

For a project using meson/cmake, declare it as a system
dependency once installed system-wide:

```meson
hegel_dep = dependency('hegel', required : false)
if hegel_dep.found()
  pbt_test = executable('test_my_pbt', 'test_my_pbt.c',
    dependencies : [my_lib_dep, hegel_dep])
  test('my_pbt', pbt_test)
endif
```

Skip cleanly when the library isn't available -- mirrors how Lime
already gates `bench_simdjson_compare` on the simdjson dep.

## Test scaffolding

A hegel-c test is a single `int main()` that:

  1. Creates a session (which spawns the `hegel` server).
  2. Configures `hegel_settings`.
  3. Runs one or more property functions via `hegel_run_test`.
  4. Frees the results and the session.

```c
#include <hegel/hegel.h>
#include <hegel/generators.h>
#include <assert.h>
#include <stdlib.h>

static void prop_addition_commutes(hegel_test_case *tc, void *user_data) {
    (void)user_data;
    int64_t a = hegel_draw_int(tc, hegel_integers(INT64_MIN, INT64_MAX));
    int64_t b = hegel_draw_int(tc, hegel_integers(INT64_MIN, INT64_MAX));
    /* Use a wider type for the sum so the test code itself doesn't
    ** overflow when validating the property; that overflow would
    ** mask the property under test, not the library's behaviour. */
    __int128 lhs = (__int128)a + (__int128)b;
    __int128 rhs = (__int128)b + (__int128)a;
    assert(lhs == rhs);
}

int main(void) {
    hegel_session *s = hegel_session_new();
    if (s == NULL) {
        fprintf(stderr, "hegel: server unavailable -- skipping PBT\n");
        return 77; /* meson/automake "skip" exit code */
    }

    hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
    settings.max_examples = 200;

    hegel_results r = hegel_run_test(s, prop_addition_commutes, NULL, &settings);

    int rc = r.passed ? 0 : 1;
    hegel_results_free(&r);
    hegel_session_free(s);
    return rc;
}
```

`hegel_session_new` returns NULL when the `hegel` server isn't
on `PATH` (or `HEGEL_SERVER_COMMAND`).  Treat that as a skip,
not a failure -- the rest of the test suite must still run on
hosts without the hegel server.

## Generators

### Primitives

```c
hegel_integers(min, max)        /* int64_t in [min, max] */
hegel_floats(min, max)          /* double in [min, max] */
hegel_booleans()                /* bool */
hegel_text(min_size, max_size)  /* UTF-8 string (caller frees) */
hegel_binary(min_size, max_size)/* byte buffer (caller frees) */
```

### Collections

```c
hegel_lists(elem_gen, min_size, max_size)
hegel_lists_unique(elem_gen, min_size, max_size)  /* dedup */
hegel_tuples(gen_array, count)
hegel_dicts(key_gen, val_gen, min_size, max_size)
```

### Constants and sampling

```c
hegel_just_int(42)
hegel_just_string("hello")
hegel_sampled_from_strings(values, count)
hegel_sampled_from_ints(values, count)
```

### Format generators

For test fixtures that need plausible-looking real-world data:

```c
hegel_emails()
hegel_urls()
hegel_domains()
hegel_ip4_addresses()
hegel_ip6_addresses()
hegel_dates()
hegel_times()
hegel_datetimes()
```

### Combinators

```c
hegel_map(source, map_fn, ctx, free_fn)    /* transform values */
hegel_flat_map(source, flatmap_fn, ctx)    /* dependent gen */
hegel_filter(source, predicate_fn, ctx)    /* reject values */
hegel_one_of(gen_array, count)             /* alt choice */
hegel_optional(elem_gen)                   /* nullable variant */
```

## Draw functions (inside a test body)

```c
int64_t  val  = hegel_draw_int   (tc, gen);
double   fval = hegel_draw_float (tc, gen);
bool     bval = hegel_draw_bool  (tc, gen);
char    *sval = hegel_draw_string(tc, gen);             /* caller frees */
size_t   len;
uint8_t *data = hegel_draw_bytes (tc, gen, &len);       /* caller frees */
```

**Draw inside loops and conditionals freely.**  Hegel-c is
imperative -- you do not declare a strategy combinator up front.
Each `hegel_draw_*` call returns the next value in the test
case, and shrinking still works because the protocol records
every draw.

```c
size_t n = (size_t)hegel_draw_int(tc, hegel_integers(0, 300));
for (size_t i = 0; i < n; i++) {
    int64_t k = hegel_draw_int(tc, hegel_integers(INT64_MIN, INT64_MAX));
    int64_t v = hegel_draw_int(tc, hegel_integers(INT64_MIN, INT64_MAX));
    my_map_insert(map, k, v);
}
```

## Test body helpers

```c
hegel_assume(condition);            /* skip case if false */
hegel_target(value, "label");       /* guide search toward goal */
hegel_note("diagnostic message");   /* attach note to test case */
```

`hegel_assume(false)` aborts the current test case as INVALID
(neither passed nor failed); excessive rejection rates will make
hegel give up.  Prefer constructing valid inputs directly when
you can (see Generator Discipline in SKILL.md).

`hegel_target` is for search-guided fuzzing: hegel will try to
maximise the supplied value to drive the test toward harder
cases.  Useful for "find a counterexample with the largest
collision-causing key set" kinds of tests.

## Memory model

  - String / byte buffer return values from `hegel_draw_string`,
    `hegel_draw_bytes`, `hegel_text`, `hegel_binary`, etc. are
    **owned by the caller** and must be `free()`d.
  - The `hegel_test_case *tc` lifetime is the duration of the
    test function.  Don't retain it past the function return.
  - `hegel_session_free()` cleans up the session and shuts down
    the spawned `hegel` server.
  - `hegel_results_free(&r)` releases buffers in the results
    struct (counterexamples, notes, etc.).

Forgetting to free hegel-allocated strings is the most common
leak source.  Run the suite under valgrind / ASan periodically.

## Stateful (model-based) testing

The C binding does not yet have a high-level stateful-testing API
the way `hegel-rust` and `hegel-cpp` do.  Hand-roll the model
loop:

```c
typedef enum { OP_INSERT, OP_REMOVE, OP_GET } op_kind;

static void prop_map_matches_model(hegel_test_case *tc, void *u) {
    MyMap   *subject = my_map_new();
    HashMap *model   = hashmap_new();          /* known-good model */

    int n_ops = (int)hegel_draw_int(tc, hegel_integers(0, 200));
    for (int i = 0; i < n_ops; i++) {
        op_kind op = (op_kind)hegel_draw_int(tc, hegel_integers(0, 2));
        int64_t k  = hegel_draw_int(tc, hegel_integers(INT64_MIN, INT64_MAX));
        switch (op) {
        case OP_INSERT: {
            int64_t v = hegel_draw_int(tc, hegel_integers(INT64_MIN, INT64_MAX));
            my_map_insert(subject, k, v);
            hashmap_insert(model, k, v);
            break;
        }
        case OP_REMOVE:
            my_map_remove(subject, k);
            hashmap_remove(model, k);
            break;
        case OP_GET: {
            int64_t got_subject, got_model;
            bool s_present = my_map_get(subject, k, &got_subject);
            bool m_present = hashmap_get  (model,   k, &got_model);
            assert(s_present == m_present);
            if (s_present) assert(got_subject == got_model);
            break;
        }
        }
    }
    my_map_free(subject);
    hashmap_free(model);
}
```

Using a real reference implementation (`hashmap_new` from a known-
good library) as the model is the highest-value first PBT for any
data structure.  See SKILL.md "Tier 1: High-Value Patterns" for
the full taxonomy.

## Project layout for Lime / similar C projects

  - Each property test lives in `tests/test_<feature>_pbt.c`
    next to the unit test it complements.  Do **not** create a
    separate `tests/pbt/` subtree -- property tests are tests
    like any other.
  - Wire into meson:

    ```meson
    if hegel_dep.found()
      test_<feature>_pbt = executable('test_<feature>_pbt',
        'test_<feature>_pbt.c',
        dependencies : [lime_parser_dep, hegel_dep])
      test('<feature>_pbt', test_<feature>_pbt)
    endif
    ```

  - Each PBT exits 77 (meson/automake skip code) when
    `hegel_session_new` returns NULL so the rest of the suite
    keeps running on hosts without the hegel server.

## Lime-specific property targets (suggestions)

Concrete properties worth testing in this repo, ordered by impact-
per-effort:

  1. **Parser table roundtrip.**  For any grammar `g`,
     `parse_engine_step` driven against a randomly generated
     token stream over `g`'s alphabet should never crash and
     should always return one of {accept, reject, continue}.
     Generator: pick a snapshot, draw a token sequence of length
     up to 1000, drive it.

  2. **Snapshot acquire/release refcount invariant.**
     `acquire(release(snap))` must not double-free.  Stateful
     test: model = simple counter; subject = the actual
     snapshot's atomic refcount; rules = acquire / release /
     read.

  3. **JIT vs interpreter equivalence.**  For any
     (state, lookahead) pair, the JIT'd `find_shift_action`
     must return the same action as the table-driven version.
     This is the property test analogue of
     `tests/test_jit_parse_equivalence.c` -- broaden it.

  4. **int32 offset arithmetic on large grammars.**  Compute
     `yy_shift_ofst[s] + lookahead` against a randomly-large
     yy_shift_ofst array; assert no overflow inside the bounds
     check.  Direct shrinkable analogue of the int16->int32
     widening fix.

  5. **Allocator mode equivalence.**  For the JSON example, all
     three allocator modes (MALLOC / MALLOC_NOFREE / ARENA) must
     produce structurally-equal `JsonValue` trees on the same
     input.  Property: `parse(input, MALLOC) == parse(input,
     ARENA)` modulo pointer identity.

These are written to be small (each ~50-100 lines of test code).
The repo's existing test pattern -- one test per file, one
`main()` per file -- is exactly what hegel-c expects.

## When the hegel server is not available

`hegel_session_new` returns NULL.  Treat that as a soft skip:

```c
hegel_session *s = hegel_session_new();
if (s == NULL) return 77;  /* meson skip */
```

Do not abort the test binary, do not return 1 (failure).  77 is
the standard "test was skipped" exit code recognised by both
meson's `test()` and automake's testsuite driver.

## Known upstream limitation: protocol version skew (May 2026)

As of v0.2.5, the Lime flake ships hegel-c 0.1.0 (the only
release at gburd/hegel-c) and hegel-core 0.9.1 (latest).  These
two versions do not currently interoperate: hegel-c spawns the
server with a Unix-socket path as a positional argument, while
the modern hegel-core server takes no positional arguments and
communicates over stdin/stdout:

    Usage: hegel [OPTIONS]
    Error: Got unexpected extra argument (/tmp/hegel-XXXXX/socket)
    hegel: timed out waiting for server socket

The error is hegel-c-side -- it timed out waiting for a socket
that the server never created.  The flake correctly provides
both pieces, the C library compiles and links, the PBT binaries
launch cleanly, and they soft-skip (exit 77) when the
handshake fails.

This will resolve when either:

  * hegel-c is updated to use the stdin/stdout transport, or
  * the hegel-core flake input is pinned to a commit before
    the protocol switch.

Track upstream at https://github.com/gburd/hegel-c.

## Further reading

  - https://hegel.dev — Hegel home page, user docs
  - https://github.com/gburd/hegel-c — C99 binding source
  - https://github.com/hegeldev/hegel-core/blob/main/docs/library-api.md
    — full library API spec
  - https://github.com/hegeldev/hegel-skill — upstream skill
    repo (Rust / Go / C++ / TypeScript references)
  - https://hegel.dev/compatibility — protocol stability and
    breaking-change policy
