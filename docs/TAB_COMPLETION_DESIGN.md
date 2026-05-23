# Tab-completion code generator — Design Sketch

**Status:** design only.  Lime today does not generate
tab-completion code from a grammar; this document outlines what
that capability would look like, with the PostgreSQL `psql` model
as the reference.  A working implementation is multi-day work and
is not in this repo.

## What the user wants

`psql` offers context-sensitive tab-completion on the SQL command
line:

```
postgres=> SELECT * FROM <TAB>
                       my_table  other_table  pg_class  ...
postgres=> CREATE TABLE foo (id <TAB>
                                INT  TEXT  BIGINT  TIMESTAMP  ...
```

It does this by knowing, at every cursor position, what tokens the
SQL grammar would accept next.  Implementations of similar
behaviour exist in `psql/tab-complete.c` (large hand-written rule
table), in `mysql --auto-rehash` (similar), and in shell completion
scripts of every database CLI ever shipped.

The mechanism is an LR(1) parser without action callbacks driven up
to the cursor position, then asking the parser "what set of
terminals would you accept next?".  Lime already has the building
block for the second half: the runtime engine plus
`ParseExpectedTokens()`, which returns exactly that set.

## What the codegen would emit

A new `lime` flag, e.g. `-T<dir>` or `-C<dir>`, that produces a
single file `<grammar>_complete.c` exporting:

```c
/*
 * Compute the set of terminals the grammar would accept at the end
 * of `prefix_tokens[0..n_prefix]`.  Returns the count via *out_n;
 * sets *out_tokens to a pointer into a static array.  Caller must
 * not free.
 */
int <Name>CompletionCandidates(const uint16_t *prefix_tokens,
                               size_t n_prefix,
                               const char ***out_completions,
                               size_t *out_n);
```

Internally the function:

  1. Allocates a fresh `ParseContext`.
  2. Drives every token in `prefix_tokens` via `parse_token`.
  3. Calls `ParseExpectedTokens(ctx)` to get the LR follow set
     for the current state.
  4. Translates each terminal code to its declared name string
     (the `yyTokenName` table the generator already emits).
  5. Caller-side filters by the partial typed token and renders
     completions.

The codegen path adds nothing the runtime doesn't already do; the
deliverable is a small wrapper that makes this convenient for CLI
authors.

## Real-world UX layer is not free

Returning the LR follow set at the cursor is the easy half.  The
hard half is the parts `psql` actually does:

  * **Identifier completions from semantic context.**  When the
    parser says "I expect IDENT here", that doesn't help -- the
    user wants table names, column names, function names from the
    catalog, not the literal string "IDENT".  The codegen can
    expose the *grammar's* expected tokens but cannot fill in the
    semantic universe; that is application code.
  * **Quoted identifiers.**  PG accepts `"My Table"` and unquoted
    `my_table`.  The grammar treats both as IDENT; the completer
    has to know which form to emit.
  * **Schema-qualified names.**  After `SELECT * FROM s.<TAB>` the
    completer should list tables in schema `s` only.
  * **Subquery scope.**  After `SELECT t.<TAB> FROM tbl t WHERE ...`
    the completer should list `tbl`'s columns; it has to track
    aliases the parser binds.

Lime's codegen would provide steps 1-3 of `psql`'s pipeline.  Steps
4+ remain application-level.

## Why this is not in this repo today

Like LSP, this is multi-day work that would have crowded out the
session's headline deliverables.  The infrastructure (the
push-parser engine, `ParseExpectedTokens`, the `yyTokenName`
table) is already in place.  Adding the codegen + a
`lime_complete.h` header + a worked example consuming the API
(probably the `examples/calc/` calculator with TAB-completion
glue) is a clean, scoped follow-up.

## Sketch of the example consumer

```c
/* Inside a CLI driver: */
char *line = readline("> ");
size_t n = 0;
uint16_t toks[256];
tokenize(line, toks, &n);     /* user-provided */

const char **cands;
size_t ncands;
CalcCompletionCandidates(toks, n, &cands, &ncands);

for (size_t i = 0; i < ncands; i++) {
    printf("  %s\n", cands[i]);
}
```

That's the full surface area.  The grammar generator has all the
information it needs; the codegen is essentially a couple of
hundred lines of C-emit code.
