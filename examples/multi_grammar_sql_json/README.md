# multi_grammar_sql_json -- SQL host + JSON embedded

This example demonstrates Lime's runtime-registered context-switch
trigger registry by parsing a SELECT statement that contains an
embedded `json '{...}'` literal.

It is the canonical worked example for `docs/CONTEXT_SWITCH.md`.

## What it shows

Given input like:

```
SELECT id, json '{"a":1, "b":[2,3]}' FROM t WHERE id = 5;
```

The driver:

1. Tokenises the input as SQL.
2. When it sees the lexeme `json`, consults the registered trigger
   table via `context_switch_classify_lexeme()`.  The lexeme matches
   the registered `json` trigger, which maps to a `JsonValue *`
   ParserSnapshot.
3. The driver pauses SQL tokenisation, scans the following `'...'`
   body, and drives the Lime-generated JSON parser
   (`examples/json/json_grammar.lime`) over its contents.
4. The resulting `JsonValue *` is stored in the SQL AST as a
   `JSON_LITERAL` column.  Tokenisation resumes in SQL mode.
5. The final AST shows both halves -- SQL outer, JSON inner --
   parsed correctly with line / column positions.

## Architecture

| Layer        | Implementation                                                                |
|--------------|-------------------------------------------------------------------------------|
| Host (SQL)   | Hand-rolled tokenizer + recogniser in `multi_driver.c`.  ~150 lines.          |
| Embedded     | Lime-generated JSON parser from `examples/json/json_grammar.lime`.            |
| Boundary     | `GrammarContextStack` + `context_switch_register_trigger()` in `multi_driver.c`. |

The host parser is hand-rolled here for clarity -- the example is
about the *boundary* mechanism, not SQL parsing.  Substituting a
Lime-generated SQL parser would not change the trigger-registry
plumbing.

## Files

| File                | Role                                                                |
|---------------------|---------------------------------------------------------------------|
| `multi.h`           | Shared SQL AST types + driver entry point declaration.              |
| `multi_helpers.c`   | SQL AST node constructors / destructor / pretty printer.            |
| `multi_driver.c`    | The actual demonstration: tokenizer + trigger registry.             |
| `main.c`            | Thin `main()` wrapper that prints the AST.                          |
| `Makefile`          | Standalone build (uses `examples/json` for the embedded grammar).   |
| `meson.build`       | Wired into the project's meson build.                               |

## Running standalone

```sh
make            # builds multi_grammar_sql_json
make test       # parses a sample input and prints the AST
```

## Running through meson

The example is built unconditionally as part of the project build,
and the `multi_grammar_sql_json_smoke` meson test exercises it on
every `meson test` run.  The `tests/test_context_switch_e2e` and
`tests/test_context_switch_error` suites drive `multi_parse_sql()`
directly to assert on the produced AST shape.

```sh
meson test -C builddir multi_grammar_sql_json_smoke
meson test -C builddir context_switch_e2e
meson test -C builddir context_switch_error
```

## Sample run

Input: `SELECT id, json '{"a":1, "b":[2,3]}' FROM t WHERE id = 5;`

Output:

```
SELECT
  ident: "id"  (line 1, col 8),
  json:  (line 1, col 12)
{
          "a": 1,
          "b": [
            2,
            3
          ]
        }
FROM "t"
WHERE "id" = 5
```

Position tracking is preserved across the SQL/JSON boundary: each SQL
column carries its line/col; the embedded JSON values are printed
with their nested structure intact.

## Why this is interesting

A naive integration would lex JSON character classes inside the SQL
lexer (treating the JSON body as just an opaque string until a real
JSON pass runs later).  That pattern can't be extended at runtime --
adding a new embedded dialect (XPath, MongoDB query docs, ...) means
editing the SQL lexer and rebuilding.

Lime's design instead lets each embedded dialect register its
trigger lexeme + parser snapshot at runtime, so a single SQL host
binary can be extended without recompilation.  The previous
(commit 1506723^) implementation hard-coded the trigger set to four
specific lexemes (`xmlquery`, `xpath`, `{:`, `json`) -- this
restoration drops those statics and exposes the registration entry
point.
