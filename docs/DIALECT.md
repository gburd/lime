# Dialect-Conditional Grammar (`%dialect`)

The `%dialect NAME { ... }` directive lets one Lime grammar source carry
rules and tokens that are conditionally included at parser-generation
time. Lime v0.4.0 ships this as **Model A**: every dialect combination
produces its own `.c` / `.h` (and, by extension, its own `.so` if you
build a shared library). There is no runtime dialect switch.

## When to use it

You have one mostly-shared grammar with small dialect-specific
extensions, and you want to ship one `.so` per dialect rather than a
runtime configuration knob. The motivating example is the PostgreSQL
extension family — `pg_oracle`, `pg_mysql`, `pg_duckdb`, `pg_infer` —
which each replace pieces of the SQL grammar with vendor-specific
constructs (Oracle's `ROWNUM`, MySQL's backtick identifiers, etc.).

If you need the same parser to switch dialects at runtime, see
[EXTENSIONS.md](EXTENSIONS.md) for the runtime extension framework.
The two mechanisms are complementary: `%dialect` decides what gets
*built*, runtime extensions decide what gets *activated*.

For file-level inheritance — one grammar that `%extends` another and
overrides a handful of rules — see the v0.4.1 `%extends` directive
(separate, builds on top of `%dialect`).

## Syntax

```lime
%dialect oracle {
    %token ROWNUM CONNECT_BY.
    expr ::= ROWNUM.            { /* ... */ }
    expr ::= CONNECT_BY expr.   { /* ... */ }
}
```

The body between `{` and `}` is included verbatim in the generated
parser when the macro `dialect_NAME` is defined at lime invocation
time. When it is not defined, the entire region is removed.

`%dialect` is **sugar over `%ifdef dialect_NAME { body } %endif`** —
same inclusion rule, brace-scoped instead of `%endif`-scoped. The
desugaring runs as a preprocessing pass before the existing
`%ifdef` / `%ifndef` / `%else` / `%endif` machinery, so the body of
an included `%dialect` block can itself contain those directives.

## CLI

```sh
# Vanilla build: %dialect blocks dropped, only always-active rules.
lime foo.lime

# Single dialect.
lime -Ddialect=oracle foo.lime

# Multiple dialects compose.  Both bodies are included.
lime -Ddialect=oracle -Ddialect=mysql foo.lime

# Equivalent legacy form (no shorthand).  Byte-identical output.
lime -Ddialect_oracle foo.lime
```

`-Ddialect=NAME` is a CLI shorthand recognized **only** for the
`dialect=` key. It expands to `-Ddialect_NAME` before the macro
table is updated. All other `-D` invocations still take a bare
macro name; the historical `=value` suffix continues to be dropped.

The shorthand validates `NAME` against `[A-Za-z_][A-Za-z0-9_]*` and
exits with a clear diagnostic on a bad name (empty, leading digit,
illegal character).

### No umbrella macro

`-Ddialect=oracle` defines exactly `dialect_oracle`. It does **not**
also define an umbrella `dialect`. A grammar that wants to test
"any dialect active" must spell out each one:

```lime
%ifdef dialect_oracle
    /* shared between any DB-vendor dialect */
%endif
%ifdef dialect_mysql
    /* same body, repeated */
%endif
```

This is deliberate. There is no canonical set of dialects, and an
umbrella macro would need to track every name the user invents. The
explicit form keeps the macro table predictable.

## Worked example

Source `dialecttest.lime`:

```lime
%name DialectTest
%token_type {int}
%type result {int}
%extra_argument {int *out}

%token NUMBER PLUS.

%dialect oracle {
    %token ROWNUM CONNECT_BY.
    result(R) ::= ROWNUM.            { R = -1; *out = R; }
    result(R) ::= CONNECT_BY NUMBER(N).  { R = -10 - N; *out = R; }
}

%dialect mysql {
    %token BACKTICK_IDENT.
    result(R) ::= BACKTICK_IDENT.    { R = -2; *out = R; }
}

result(R) ::= NUMBER(N) PLUS NUMBER(M).  { R = N + M; *out = R; }
```

Vanilla build:

```sh
$ lime dialecttest.lime
$ grep '#define' dialecttest.h
#define NUMBER 1
#define PLUS   2
```

Oracle build:

```sh
$ lime -Ddialect=oracle dialecttest.lime
$ grep '#define' dialecttest.h
#define NUMBER     1
#define PLUS       2
#define ROWNUM     3
#define CONNECT_BY 4
```

Combined Oracle+MySQL build:

```sh
$ lime -Ddialect=oracle -Ddialect=mysql dialecttest.lime
$ grep '#define' dialecttest.h
#define NUMBER         1
#define PLUS           2
#define ROWNUM         3
#define CONNECT_BY     4
#define BACKTICK_IDENT 5
```

## Shared token namespace

`%dialect` blocks share the same token namespace as the rest of the
grammar. If two `%dialect` blocks each declare `%token IDENT`, the
generated parser has one `IDENT` token (no conflict). This is
intentional — it lets dialects extend a shared lexer without
renaming.

If two `%dialect` blocks declare the same token with different
`%type` annotations, LALR construction fails the same way it would
today on type mismatches. That is the grammar author's responsibility,
not the desugarer's.

## Design constraints enforced by the desugarer

The Lime desugarer rejects the following at preprocessing time, with
file-and-line diagnostics:

- **Bad name.** `%dialect 99foo { ... }` — name must match
  `[A-Za-z_][A-Za-z0-9_]*`.
- **Missing brace.** `%dialect foo bar` — name must be followed by
  `{` (with optional whitespace, possibly across newlines).
- **Unterminated body.** `%dialect foo {` reaching EOF without a
  matching `}` errors with the directive's start line.
- **Nested `%dialect`.** `%dialect foo { %dialect bar { ... } }` is
  rejected. Users that genuinely need multi-dialect-conditional rules
  spell that out as nested `%ifdef`s.

The brace scan is **string- and comment-aware**: a `}` inside `"..."`,
`'...'`, `/* ... */`, or `// ...` does not close the dialect block.
This matches the semantics of Lime's existing C-action brace scanner.

## Implementation notes

- Desugaring is in-place. Bytes corresponding to the directive opener
  (`%dialect NAME ... {`) and matching `}` are blanked to spaces when
  the dialect is active, leaving the body verbatim. When inactive, the
  entire region is blanked.
- Newlines are preserved in either branch so error messages from
  later passes carry the correct line number.
- The pass runs before the existing `%ifdef` / `%ifndef` pass; the
  inclusion lookup hits the same `azDefine[]` table so a CLI
  `-Ddialect=oracle` activates both `%dialect oracle { ... }` blocks
  and any `%ifdef dialect_oracle` blocks the user wrote by hand.

## Idempotence

`lime -Ddialect=oracle foo.lime` is byte-deterministic across runs
on the same input (modulo `#line` directives that include the output
directory name). Running it twice produces identical `.c` and `.h`.
This is a CI guarantee for downstream grammar repositories that
diff-check generator output.

## See also

- [EXTENSIONS.md](EXTENSIONS.md) — runtime grammar extensions, a
  different mechanism for the same problem family. Use when the
  dialect choice is data, not a build-time switch.
- [GETTING_STARTED.md](GETTING_STARTED.md) — Lime basics.
- `man/lime.1` — `-D` CLI reference.
