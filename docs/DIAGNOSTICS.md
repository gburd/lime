# Diagnostics and Error Recovery

This guide covers how to produce rich error messages from a Lime-generated
parser — precise source locations, expected-token hints, and recovery to
continue parsing after an error.

## Token Spans

Lime's built-in tokenizer (`include/tokenize.h`) already provides
everything needed for precise caret positioning:

```c
typedef struct Token {
    int type;           // Token type code
    const char *start;  // Pointer into source buffer
    size_t length;      // Byte length of the token
    uint32_t line;      // 1-based line number
    uint32_t column;    // 1-based column number
} Token;
```

If you write your own tokenizer instead of using Lime's, you should store
the same information in whatever token struct you pass to the parser —
the length is what lets you underline the exact run of characters that
caused the error, rather than placing a single caret at the start offset.

## The Diagnostics API

Every generated parser exports three introspection functions that let
your `%syntax_error` handler build context-rich messages:

```c
const char *ParseTokenName(int tokenCode);
int         ParseState(void *parser);
int         ParseExpectedTokens(int stateno, int *out, int max);
```

(The `Parse` prefix is replaceable via the `%name` directive or the
`-P` command-line flag; the examples below assume the default.)

### `ParseTokenName(code)` — look up a token's name

Returns the string name of a terminal token code, or `NULL` if the code
is out of range. Names come from the `yyTokenName[]` table that the
generator already emits for trace output.

```c
const char *name = ParseTokenName(yymajor);
printf("unexpected token: %s\n", name ? name : "(unknown)");
```

### `ParseState(parser)` — current parser state

Returns the numeric state the parser is in, or `-1` for an invalid
handle. A freshly-initialized parser is in state 0.

The state number is meaningful only as an input to
`ParseExpectedTokens()`. Treat it as opaque — the actual number depends
on the grammar and changes every time the parser is regenerated.

### `ParseExpectedTokens(state, out, max)` — what could come next

Fills `out` with the token codes that are valid at `stateno` and
returns the count written (up to `max`). If `out` is `NULL` or `max`
is `0`, returns the total count without writing anything — so callers
can size a buffer and call again.

```c
int s = ParseState(parser);
int n = ParseExpectedTokens(s, NULL, 0);
int *codes = malloc(n * sizeof(int));
ParseExpectedTokens(s, codes, n);
for (int i = 0; i < n; i++) {
    printf("  expected: %s\n", ParseTokenName(codes[i]));
}
free(codes);
```

### Convenience: `ParseExpectedTokensString(parser)`

Returns a heap-allocated comma-separated list of expected token names
at the current state:

```c
char *list = ParseExpectedTokensString(parser);
printf("expected one of: %s\n", list);
free(list);
```

This is equivalent to calling `ParseState` + `ParseExpectedTokens` +
`ParseTokenName` yourself, then joining with commas. Use it for quick
messages; use the three lower-level calls when you want more control
(e.g., filtering, sorting, localization).

## Identifiers in `%syntax_error`

Inside a `%syntax_error { ... }` block, these identifiers are available:

| Identifier   | Type                  | Meaning |
|--------------|-----------------------|---------|
| `yymajor`    | `int`                 | Token code of the offending lookahead.  `0` means end-of-input. |
| `yyminor`    | `ParseTOKENTYPE`      | Semantic value of the offending lookahead. |
| `TOKEN`      | `ParseTOKENTYPE`      | Alias for `yyminor`. |
| `yyloc`      | `YYLOCATIONTYPE`      | Source location of the offending lookahead.  See "Location semantics" below. |
| `TOKEN_LOC`  | `YYLOCATIONTYPE`      | Alias for `yyloc`. |
| `yypParser`  | `void *`              | Parser handle, for passing to `ParseState` etc. |

### Location semantics (P0-NEW-1, since v0.2.0)

When the grammar declares `%locations` and parsing is driven by
`ParseLoc()` (or `parse_token()` with a non-`LIME_LOC_UNKNOWN`
location), `yyloc` holds the source location of the **offending
lookahead** -- the token that the parser could not accept -- not
the location of the previously-shifted symbol on top of the stack.

This matches Bison's `*yylloc` semantics in `yyerror()`: the
location Bison passes is the location of the token that triggered
the error.  Concretely, this distinguishes two cases that previous
Lime versions could not:

  * **Error at end of input** (`yymajor == 0`): the parser failed
    on the EOF marker.  `yyloc` is whatever location the caller
    passed for the EOF marker (typically `LIME_LOC_UNKNOWN` or a
    sentinel like the byte offset just past the input's end).

  * **Error at a specific token** (`yymajor != 0`): the parser
    failed on a real token.  `yyloc` is that token's location.

Without this distinction, an error message that said "at or near
X" would print the wrong token name in one of the two cases.  PG-
compatible callers can write:

```c
%syntax_error {
    if (yymajor == 0) {
        ereport(ERROR, errmsg("syntax error at end of input"),
                       parser_errposition(yyloc.first_column));
    } else {
        ereport(ERROR, errmsg("syntax error at or near \"%s\"",
                              parse_get_yytext(yypParser)),
                       parser_errposition(yyloc.first_column));
    }
}
```

When the grammar does *not* declare `%locations`, or parsing is
driven by the location-less `Parse()` entry point, `yyloc` is
zero-initialised at `ParseInit()` time and remains so.  Treat
zero as "location unknown" in that case.

A complete handler looks like this:

```c
%syntax_error {
    fprintf(stderr, "syntax error near token %s\n",
            ParseTokenName(yymajor));

    int s = ParseState(yypParser);
    int n = ParseExpectedTokens(s, NULL, 0);
    if (n > 0) {
        fprintf(stderr, "  expected one of:");
        int *codes = malloc(n * sizeof(int));
        ParseExpectedTokens(s, codes, n);
        for (int i = 0; i < n; i++) {
            fprintf(stderr, " %s", ParseTokenName(codes[i]));
        }
        free(codes);
        fprintf(stderr, "\n");
    }
}
```

## Accumulating Errors: the `LimeError` helpers

`include/lime_error.h` provides an optional linked-list error type for
accumulating multiple errors during a parse. Hosts that want a
different structure can ignore it and roll their own.

```c
#include "lime_error.h"

LimeError *errs = NULL;

/* In %syntax_error: */
errs = lime_error_append(
    errs,
    "unexpected token",           /* message */
    expected_list_string,         /* expected tokens (free'd by _free) */
    line, column,
    filename
);

/* After parsing: */
size_t n = lime_error_count(errs);
for (LimeError *e = errs; e; e = e->next) {
    fprintf(stderr, "%s:%u:%u: %s\n",
            e->filename ? e->filename : "<input>",
            e->line, e->column, e->message);
}
lime_error_free(errs);
```

The `message` and `expected` fields are duplicated on append; the
`filename` is borrowed (not copied) so make sure it outlives the
error chain, or pass `NULL`.

## Error Recovery with the `error` Token

Lime inherits Lemon's error-recovery mechanism, which lets a parser
continue after a syntax error by resynchronizing at a known grammar
point. The host program then sees a list of all errors, not just the
first.

### The built-in `error` nonterminal

`error` is a special nonterminal that matches "any stretch of input we
couldn't parse." You add productions that use `error` at points where
recovery makes sense — typically statement boundaries or block
delimiters.

For an SQL-like grammar:

```
stmt_list ::= stmt.
stmt_list ::= stmt_list SEMI stmt.

stmt ::= select_stmt.
stmt ::= insert_stmt.
stmt ::= update_stmt.
stmt ::= error.                 /* recovery rule */
```

When a syntax error occurs inside a statement, the parser:

1. Calls `%syntax_error` so you can record the error.
2. Pops states off its stack until it finds one where `error` can be
   shifted.
3. Shifts `error` and continues parsing, skipping input tokens until
   it finds something that follows `error` in the recovery production
   (here, `SEMI` at the statement level).

### The three-token resync rule

After shifting `error`, the parser refuses to report another syntax
error until it has successfully shifted at least three real tokens.
This prevents a cascade of spurious errors from a single mistake.

If you want a different threshold, use `%error_sync N` to set it
(e.g., `%error_sync 1` makes every error report immediately, at the
cost of lower-quality output on cascading errors).

### `%parse_failure` — when recovery fails

If the parser runs out of tokens (or hits EOF) before recovering, it
calls `%parse_failure`:

```
%parse_failure {
    fprintf(stderr, "parse failed; no more recovery possible\n");
}
```

This is your signal to stop trying and return whatever errors you've
accumulated.

### Complete recovery example

```
%syntax_error {
    /* Record without abandoning the parse */
    errs = lime_error_append(errs,
        "syntax error",
        ParseExpectedTokensString(yypParser),
        TOKEN_LOC.first_line, TOKEN_LOC.first_column,
        source_filename);
}

%parse_failure {
    /* Called at most once, after recovery is exhausted */
    errs = lime_error_append(errs,
        "parser cannot recover", NULL, 0, 0, source_filename);
}

stmt_list ::= stmt.
stmt_list ::= stmt_list SEMI stmt.

stmt ::= select_stmt.
stmt ::= error.             /* allow recovery at statement boundary */
```

With this grammar, parsing `SELECT * FROM ?garbage; INSERT INTO t VALUES (1);`
produces both errors (the `?garbage` and any follow-on), instead of
stopping at the first.

## Putting It Together: Rust-Compiler-Style Output

A minimal example of the full pipeline — tokenize, parse, report errors
with caret underlines:

```c
static void report(FILE *out, const char *source, Token tok,
                   const char *msg, const char *expected)
{
    /* Find the start of the line containing tok */
    const char *line_start = tok.start;
    while (line_start > source && line_start[-1] != '\n') line_start--;
    const char *line_end = tok.start;
    while (*line_end && *line_end != '\n') line_end++;

    fprintf(out, "error: %s\n", msg);
    fprintf(out, "  --> %s:%u:%u\n", filename, tok.line, tok.column);
    fprintf(out, "   |\n");
    fprintf(out, "%3u| %.*s\n", tok.line,
            (int)(line_end - line_start), line_start);
    fprintf(out, "   | %*s", (int)(tok.start - line_start), "");
    for (size_t i = 0; i < tok.length; i++) fputc('^', out);
    fprintf(out, "\n");
    if (expected && *expected) {
        fprintf(out, "   = expected one of: %s\n", expected);
    }
}

%syntax_error {
    char *expected = ParseExpectedTokensString(yypParser);
    report(stderr, source_text, *current_token,
           "syntax error", expected);
    free(expected);
}
```

Output looks like:

```
error: syntax error
  --> query.sql:3:15
   |
 3| SELECT * FROM ?garbage WHERE x=1;
   |               ^^^^^^^
   = expected one of: IDENT, LPAREN
```

## Further Reading

- `man/lime_grammar.5` — grammar directive reference (including `%syntax_error`,
  `%parse_failure`, `%error_sync`, `%locations`)
- [API.md](API.md) — full C API reference
- [EXTENSIONS.md](EXTENSIONS.md) — runtime grammar extensions (unrelated to
  parse-time error recovery)
