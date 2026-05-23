# JSON example: real grammar with semantic actions

A worked example of using Lime to build a JSON parser end-to-end:

* **`json.lime`** — Lime grammar, 14 productions, with semantic
  actions that allocate `JsonValue` AST nodes via the helpers in
  `json_helpers.c`.  Demonstrates `%token_type`, `%type`,
  `%extra_argument`, and the typed-alias action style
  (`value(A) ::= STRING(S). { A = json_string_take((char *)S); }`).

* **`json.h`** + **`json_helpers.c`** — tagged-union `JsonValue`
  type and constructors.  Memory model: token-stream values for
  STRING and NUMBER are heap-allocated `char *`s the parser takes
  ownership of via `json_*_take`; `json_free` walks the tree and
  releases everything.

* **`json_tokenize.{c,h}`** — a 150-line single-pass tokenizer.
  Decodes most string escape sequences (`\"`, `\\`, `\/`, `\b`,
  `\f`, `\n`, `\r`, `\t`); passes Unicode escapes (`\uXXXX`)
  through unchanged; not a faithful UTF-8 validator.

* **`main.c`** — a CLI driver that reads JSON from stdin or the
  first argument, parses it, pretty-prints the AST, and frees
  everything.

## Build

```sh
make            # uses the Makefile here
./json_parser < tests/sample.json
```

The Makefile generates `json_parser.{c,h}` from `json.lime` via
the top-level `bin/lime` and links the result with the static
`lib/liblime_parser.a` produced by the parent `make`.

## Test

```sh
echo '{"a":1,"b":[true,null,"x"]}' | ./json_parser
```

prints

```
{
  "a": 1,
  "b": [
    true,
    null,
    "x"
  ]
}
```

## Why this is here

The parser-only benchmark in `bench/bench_flex_bison_compare/`
shows Lime ~1.81× faster than Bison+Flex on a JSON workload, but
that benchmark does not produce an AST -- it's recognition only.
This directory is the *full* path: real semantic actions, real
heap-allocated AST, real CLI surface.  Useful as a worked example
when porting a real grammar to Lime.

## Allocator modes (also benchmark fodder)

The example supports three allocator modes via CLI flags so the
parsing cost can be teased apart from the allocator cost:

```sh
./json_parser           # default: malloc per node + json_free walks the tree
./json_parser --leak    # malloc per node, json_free is a no-op (LEAKS)
./json_parser --arena   # bump-pointer from a 1 MB pre-allocated arena;
                        # arena resets between iterations (zero alloc steady)
```

The arena mode mirrors how simdjson manages memory: parser owns
big buffers, resets between parses.  See
`bench/bench_simdjson_compare/` for a head-to-head measurement
that uses all three modes.

## Caveats (not bugs, just scope)

* No Unicode handling for `\u` escapes (the JSON example we ship
  with the simdjson comparison is the right place to look at if
  you need that).
* Number literals decode via `strtod`; doesn't enforce JSON's
  numeric grammar (e.g. accepts leading zeros in implementations
  that strtod allows).
* No streaming parse API -- the whole input is read up front.
* No error recovery; one syntax error and we print a message and
  exit non-zero.

For the JSON-vs-simdjson throughput comparison see
`bench/bench_simdjson_compare/`.
