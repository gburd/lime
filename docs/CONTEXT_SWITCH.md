# Context Switch — runtime grammar-mode boundary detection

Lime supports parsing inputs that mix multiple languages — for
example, SQL with embedded `json '{...}'` literals, or PostgreSQL
with `XMLPARSE(DOCUMENT ...)` clauses that contain XML.  The
**context-switch** layer is the small module that detects the
boundary between the host language and the embedded one, and that
swaps the active `ParserSnapshot` while the embedded region is
being parsed.

This document covers what the layer does, why the registration is
runtime-driven, and how to wire it into a host parser.

## What the layer does

The context-switch layer maintains a stack of `(grammar-mode,
ParserSnapshot)` pairs.  At any moment, the top of the stack
identifies the active grammar.  Tokens fed through `parse_token_lex`
are inspected against a registry of **trigger lexemes**: when a
trigger matches, the layer pushes a new entry onto the stack and
swaps the bound snapshot.  When the embedded region ends (either an
explicit exit token or a bracket-depth return), the stack pops and
the previous snapshot is restored.

The layer sits on top of `src/grammar_context.c`, which provides the
data structures, and exposes four public entry points in
`src/context_switch.c`:

| Function                                   | Role                                            |
|--------------------------------------------|-------------------------------------------------|
| `context_switch_register_trigger()`        | Register a lexeme/snapshot pair as a trigger.   |
| `context_switch_classify_lexeme()`         | Look up which mode (if any) a lexeme triggers.  |
| `context_switch_needed()`                  | Fast-path predicate; O(1) on the no-trigger path. |
| `context_switch_detect_exit()`             | Detect explicit-exit-token boundary completion. |

## Why it's runtime-registered

The earlier (1506723^) version of this module had four hard-coded
trigger lexemes baked into static const strings:

```c
static const char *TRIGGER_XQUERY = "xmlquery";
static const char *TRIGGER_XPATH  = "xpath";
static const char *TRIGGER_EDN    = "{:";
static const char *TRIGGER_JSON   = "json";
```

This was wrong on at least three counts:

1. **`xmlquery` isn't in any real SQL dialect.**  PostgreSQL uses
   `XMLPARSE(DOCUMENT ...)`; Oracle uses `XMLQuery(...)`; SQL Server
   uses `.query()` method-call syntax.  A static trigger called
   `xmlquery` matches none of them.
2. **The set isn't extensible at runtime.**  Adding a new embedded
   dialect (MongoDB query documents, JSONPath, regex, ...) requires
   editing `context_switch.c` and rebuilding.  Lime's whole purpose
   is to extend grammars at runtime.
3. **Two different host grammars want different trigger sets.**  An
   SQL host shouldn't have to live with a PostgreSQL-shaped trigger
   list, and vice versa.

The restoration drops the static globals and the
`context_switch_register_defaults` convenience function in favour
of a single `context_switch_register_trigger` entry point.  Each
host grammar registers its own trigger set against its own
`GrammarContextStack`.

## Registration API

```c
GrammarContextStack *stack = grammar_context_create(host_snap);

context_switch_register_trigger(stack,
    /* trigger_lexeme */ "json",
    /* embedded_snap  */ json_snap,
    /* mode_name      */ "json");

context_switch_register_trigger(stack,
    "XMLPARSE",   xml_snap,   "xml");

context_switch_register_trigger(stack,
    "$.",         jsonpath_snap, "jsonpath");
```

The trigger lexeme is matched as a **prefix** against the host
lexer's emitted lexemes.  This lets a host that emits multi-character
keywords as single lexemes still match short trigger prefixes
(`json` matching `json`, `XMLPARSE` matching `XMLPARSE(DOCUMENT`,
etc.).

Each registered trigger is assigned a fresh `GrammarMode` id
internally.  The id is opaque to callers; classify_lexeme returns it
and downstream code passes it back to `grammar_context_push()` /
`grammar_context_pop()`.

## Lexer cooperation requirement

The context-switch layer is consulted at the **lexer/driver** level.
The parse engine itself (`src/parse_engine.c`) has a hook that
swaps `ctx->snapshot` when a context-stack is attached, but the
canonical integration point is the host's tokenizer:

```c
/* Host tokenizer's main loop: */
const char *lexeme = read_next_lexeme(input);
GrammarMode m = context_switch_classify_lexeme(stack, lexeme);
if (m != MODE_NONE) {
    /* Pause host tokenisation, drive the embedded parser to
    ** completion, capture its result, then resume with a single
    ** "embedded literal" token whose value is the embedded AST. */
    JsonValue *root = drive_embedded_json(input);
    feed_host_token(host_parse_ctx, TK_JSON_LITERAL, root, location);
} else {
    feed_host_token(host_parse_ctx, classify(lexeme), value, location);
}
```

The reason for lexer-side cooperation: an LALR parse stack is a
single state machine.  You can't mid-parse swap the action tables
out from under it and expect the existing stack states to remain
valid.  The clean architecture is to drive the embedded grammar to
completion in a sub-parse and feed a single composite token to the
host -- that's how PostgreSQL handles `XMLPARSE`, how SQL Server
handles JSON_VALUE arguments, and how Lime's worked example
(below) handles `json '{...}'`.

## Parse-engine hook

`parse_engine_step()` consults the attached context stack on every
token via `context_switch_needed()`.  When no stack is attached
(`ctx->context_stack == NULL`), the cost is a single load + branch
that the compiler statically predicts as not-taken.  When a stack
is attached but no triggers are registered (`mode_count == 0`),
`context_switch_needed()` returns false after a second branch.

The hook handles **token-code-based** triggers from `parse_engine`
itself.  Lexeme-based triggers fire from `parse_token_lex()` in
`src/parse_context.c`, which takes an explicit lexeme parameter.
Most embedded-dialect triggers are lexeme-based; the parse-engine
hook is therefore primarily a no-op fast-path that exists so a
future lexer integration can hook in without rewiring the parse
loop.

The cost contract:

| Configuration                              | Cost per token        |
|--------------------------------------------|-----------------------|
| No context stack attached                  | 1 load + 1 branch (predicted not-taken) |
| Stack attached, no triggers registered     | 1 extra branch        |
| Stack attached, triggers, root-only mode   | 1 lexeme prefix scan  |
| Inside an embedded mode                    | 1 detect_exit call    |

Verified against `bench/parser_bench` on the LALR fast path; see
the **Performance** section below.

## Worked example walkthrough

The full example lives at `examples/multi_grammar_sql_json/`.  The
driver parses the input

```
SELECT id, json '{"a":1, "b":[2,3]}' FROM t WHERE id = 5;
```

into the AST

```
SELECT
  ident: "id"  (line 1, col 8),
  json:  (line 1, col 12)
{
  "a": 1,
  "b": [2, 3]
}
FROM "t"
WHERE "id" = 5
```

The host SQL recogniser is hand-rolled in `multi_driver.c` (~150
lines); the embedded JSON parser is generated by Lime from
`examples/json/json_grammar.lime`.  The registration at startup is

```c
context_switch_register_trigger(stack, "json", json_snap, "json");
```

When the SQL tokenizer consumes the lexeme `json`, it consults
`context_switch_classify_lexeme(stack, "json")`, which returns the
mode id assigned to JSON.  The driver then:

1. `grammar_context_push(stack, mode, offset)` -- mark the start of
   the embedded region.
2. Reads the `'...'` body verbatim from the input.
3. Drives the JSON parser over the body, producing a `JsonValue *`.
4. `grammar_context_pop(stack)` -- back to host SQL.
5. Stores the `JsonValue *` in the SQL AST as a `JSON_LITERAL`
   column.

The full source is short enough to read in one sitting; see
`examples/multi_grammar_sql_json/multi_driver.c::multi_parse_sql`.

## Performance

Cost when no triggers are registered (the common case for
single-grammar parsers): **zero** measurable overhead on
`bench/parser_bench` (LALR fast path).  The hook in
`parse_engine_step` is a single load + statically-predicted-not-taken
branch.

Cost when triggers are registered but the parser is in root-mode:
one prefix-scan per emitted token over the registered trigger list.
The prefix scan is `O(N * L)` where `N` is the number of registered
triggers and `L` is the trigger length; both are small in practice
(a handful of triggers, four-to-six characters each), and the scan
is cache-resident after the first match.

Cost while inside an embedded region: one explicit-exit-token
comparison per token.  The bracket-depth-driven exit path runs
through `grammar_context_close_bracket()` and is independent of
`context_switch_detect_exit()`.

## Design tradeoffs and known follow-ups

- **Single-trigger-per-lexeme.**  The registry rejects duplicate
  trigger lexemes, so two grammars cannot both claim `json`.  This
  is intentional -- the alternative is non-deterministic
  classification.  Mixing two SQL dialects would register
  distinct trigger sets against distinct stacks.

- **Prefix matching is greedy by registration order.**  If trigger
  `js` and trigger `json` are both registered, `json` will match
  `js` first because it was registered earlier (or vice versa).
  This is unlikely to bite in practice -- trigger lexemes are
  typically full keywords -- but a future change could sort by
  length-descending to prefer the longest match.

- **Lexer-side switching is the caller's responsibility.**  The
  parse-engine hook is a fast-path no-op; the actual swap-and-resume
  orchestration lives in the host's tokenizer / driver code.  See
  the worked example for the canonical pattern.

- **Bracket-depth exit only.**  The current registration path
  (`context_switch_register_trigger`) hard-codes `exit_token = -1`
  (bracket-depth-driven exit).  Callers that need explicit-exit-token
  semantics can drop down to `grammar_context_register_mode()`
  directly.

## Files

| File                              | Purpose                                          |
|-----------------------------------|--------------------------------------------------|
| `src/context_switch.c`            | The four entry points (~140 lines).              |
| `include/grammar_context.h`       | Public declarations + `MODE_NONE` sentinel.      |
| `src/grammar_context.c`           | Stack data structures + registration plumbing.   |
| `src/parse_context.c`             | `parse_attach_context_stack`, `parse_token_lex`. |
| `src/parse_engine.c`              | Token-code-based hook on the LALR hot path.      |
| `examples/multi_grammar_sql_json/`| Worked example (SQL host + JSON embedded).       |
| `tests/test_context_switch_unit.c`| Unit tests for the registry primitives.          |
| `tests/test_context_switch_e2e.c` | End-to-end SQL+JSON parse with AST assertions.   |
| `tests/test_context_switch_error.c`| Error paths (unterminated, malformed, NULL).    |

## See also

- `docs/CONCEPTS.md` -- snapshot, parse-context, and extension model.
- `docs/EXTENSIONS.md` -- writing runtime grammar extensions.
- `examples/multi_grammar_sql_json/README.md` -- the worked example.
