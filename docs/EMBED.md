# `%embed lang` — directive sugar over the context-switch registry

The `%embed` directive is the v0.4.4 ergonomic layer over Lime's
runtime grammar-mode boundary detection.  It generates the C-side
wire-up (the `_embed_table[]`, the snapshot late-binder, and the
trigger-registration helper) that hosts like PostgreSQL and
`pg_mentat` would otherwise hand-write.

The directive is **pure sugar**: it changes only what the
`*_snapshot.c` codegen emits.  The runtime API in
`src/context_switch.c` is untouched.  See
[CONTEXT_SWITCH.md](CONTEXT_SWITCH.md) for the runtime layer and
[DIALECT.md](DIALECT.md) / [EXTENDS.md](EXTENDS.md) for the
compile-time alternatives.

## Syntax

```lime
%embed NAME TRIGGER 'lex' ENTRY_TOKEN TOKEN.
```

Where:

| Field            | Meaning                                                                                       |
|------------------|-----------------------------------------------------------------------------------------------|
| `NAME`           | Mode-label identifier.  Used as the registered `mode_name` string at runtime.                 |
| `'lex'`          | Quoted trigger lexeme.  Single or double quotes.  Free-form bytes (e.g. `'json'`, `':-'`).    |
| `TOKEN`          | Existing terminal in the host grammar.  Marks the entry-point lookahead for the embedded run. |

Multiple `%embed` directives compose; each registers one trigger,
in declaration order.

## Worked example: SQL with embedded JSON

The pre-v0.4.4 wiring (still present in
`examples/multi_grammar_sql_json/multi_driver.c`) registers the
`json` trigger by hand:

```c
GrammarContextStack *stack = grammar_context_create(root_snap);
if (register_json_trigger) {
    if (!context_switch_register_trigger(stack, "json", json_tag, "json")) {
        /* ... error handling ... */
    }
}
```

With `%embed`, the directive replaces the hand-written
`register_trigger` line:

```lime
/* sql.lime */
%name SqlParser
%token IDENT STRING NUMBER SELECT FROM WHERE.
%embed json TRIGGER 'json' ENTRY_TOKEN STRING.
```

The user still calls `SqlParserSetEmbedSnapshot("json", json_snap)`
once at startup to wire the runtime snapshot pointer, then
`SqlParserRegisterEmbedTriggers(stack)` to register every wired-up
trigger on the active `GrammarContextStack`.  The directive does not
load `.so` files — that remains the user's runtime concern.

## Generated artifacts

For a grammar with `%name SqlParser`, the snapshot file
(`sql_snapshot.c`, gated on `lime -n`) gets:

```c
typedef struct SqlParserEmbedEntry {
    const char       *name;
    const char       *trigger;
    int               entry_token_code;
    ParserSnapshot   *snap;          /* late-bound by user */
} SqlParserEmbedEntry;

static SqlParserEmbedEntry SqlParser_embed_table[] = {
    { "json", "json", /* STRING token code */, NULL },
    { NULL, NULL, 0, NULL }
};

int SqlParserSetEmbedSnapshot(const char *name, ParserSnapshot *snap);
int SqlParserRegisterEmbedTriggers(GrammarContextStack *stack);
```

The header (`sql.h`) gets matching public declarations so user code
can call them after a single `#include "sql.h"`.

## Late-binding contract

The `_embed_table[]` is populated at codegen time with name +
trigger + entry-token-code; the `snap` pointer is `NULL` until the
user calls `SetEmbedSnapshot`.  `RegisterEmbedTriggers` skips entries
whose `snap` is still `NULL`, so partial wiring is legal — register
only the embedded modes you actually need.

The recommended startup sequence:

```c
ParserSnapshot *root = SqlParserBuildSnapshot();
ParserSnapshot *json_snap = JsonParserBuildSnapshot();

GrammarContextStack *stack = grammar_context_create(root);

SqlParserSetEmbedSnapshot("json", json_snap);   /* wire snapshots */
SqlParserRegisterEmbedTriggers(stack);          /* register triggers */

/* Now drive the SQL parser; trigger lexemes context-switch. */
```

## Errors

| Diagnostic                                                              | Cause                                                       |
|-------------------------------------------------------------------------|-------------------------------------------------------------|
| `%embed: expected mode-label identifier`                                | Token after `%embed` is not an identifier.                  |
| `%embed NAME: expected TRIGGER keyword`                                 | Second token must be the literal word `TRIGGER`.            |
| `%embed NAME: expected quoted trigger lexeme`                           | Third token must be `'...'` or `"..."`.                     |
| `%embed NAME: trigger lexeme is empty`                                  | The quoted lexeme must contain at least one byte.           |
| `%embed NAME: expected ENTRY_TOKEN keyword`                             | Fourth token must be the literal word `ENTRY_TOKEN`.        |
| `%embed NAME: ENTRY_TOKEN must be a terminal name`                      | Fifth token must start with an upper-case letter.           |
| `%embed NAME: ENTRY_TOKEN "X" is not a declared terminal`               | The terminal must be declared via `%token X.` first.        |
| `%embed NAME: expected `.` terminator`                                  | The directive must end with `.`.                            |

## Round-trip (formatter)

`lime -F` preserves `%embed` directives in source-declaration order.
The trigger lexeme is normalized to single quotes on emit, even if
the source used double quotes.  Per-directive leading comments are
preserved (Lime-Letter-19 style).  `format(format(F)) == format(F)`
holds.

## Zero-cost when unused

If a grammar declares no `%embed` directives, the codegen emits
zero bytes of embed machinery — no `_embed_table[]`, no helper
functions, no `#include "grammar_context.h"`.  Existing grammars
keep their byte-identical generated output across the v0.4.3 →
v0.4.4 upgrade.

## Use without `-n`

The embed wiring lands in the snapshot file, which is only emitted
when `lime -n` is passed.  Without `-n`, `lime` warns that the
directive's effect is dropped and continues without emitting the
helpers.  `%embed` is meaningful only with snapshots, since
context-switching swaps `ParserSnapshot *` pointers.

## Cross-references

- [CONTEXT_SWITCH.md](CONTEXT_SWITCH.md) — the runtime API the directive sugars over.
- [DIALECT.md](DIALECT.md) — generator-time conditional rules (compile-time alternative).
- [EXTENDS.md](EXTENDS.md) — file-level inheritance with diamond resolution.
- `examples/multi_grammar_sql_json/` — the worked SQL+JSON example whose hand-written `register_trigger` calls `%embed` replaces.
