# GLR (Generalized LR) Parser

## Status

**Status:** merged on main as of v0.3.4.  Opt-in via `lime_parse_glr()`; LALR path is byte-identical to v0.3.3 (zero overhead for callers that never enter GLR mode).

## What GLR is

GLR (Generalized LR) handles grammars that LALR(1) cannot, by
forking the parse stack on shift/reduce or reduce/reduce conflicts
and continuing all alternatives in parallel through a Graph-
Structured Stack (GSS).  Heads that converge to the same state are
merged.  When two reductions produce the same nonterminal at the
same position, a user-supplied disambiguation callback picks a
winner.  If none picks, the parse reports unresolvable ambiguity.

Classic uses:

- C-style `T(x);` declaration-vs-expression
- Natural-language grammars
- Domain-specific languages with deliberate ambiguity that the
  application resolves semantically (e.g. with a symbol table)

LALR(1) — what Lime ships by default — is faster but rejects
ambiguous grammars at generation time.

## Public API

```c
#include "glr.h"
#include "parser.h"

/* Disambiguation callback: called when two reductions produce the
 * same nonterminal at the same position.  Return 1 to prefer rule1,
 * 2 to prefer rule2, 0 to report unresolvable ambiguity. */
typedef int (*GLRDisambiguateFn)(uint32_t rule1_index,
                                 uint32_t rule2_index,
                                 void *user_data);

/* Parse via GLR mode.  ctx must already be set up with a snapshot
 * and inputs (same shape as lime_parse).  Returns 0 on accept,
 * -1 on unresolvable ambiguity, -2 if all parse heads die. */
int lime_parse_glr(ParseContext *ctx,
                   GLRDisambiguateFn disambig,
                   void *user_data);
```

## When to use it / when NOT to

**Use GLR when:**

- The grammar has genuine ambiguity that semantic context resolves
- The application can supply a meaningful disambiguation callback
- The 7-8× per-parse cost vs LALR is acceptable for the workload

**Stay on LALR when:**

- The grammar is unambiguous (the common case for SQL, JSON, most
  config languages, most programming languages)
- Performance matters and the grammar doesn't actually need GLR
- Conflict reports at generation time can be addressed with
  precedence directives or grammar refactoring

## Measured performance

`bench/glr_overhead.c`, 10000 iterations × 5 trials, median.

### m3pro (Apple Silicon, debugoptimized)

| path                | ns / parse |
|---------------------|-----------:|
| LALR baseline       |    330–480 |
| GLR (no conflicts)  |   2,800–3,330 |
| GLR overhead vs LALR| +595% to +749% (~7–8×) |

### x86_64 (i9-12900H Linux, debugoptimized)

| path                | ns / parse |
|---------------------|-----------:|
| LALR baseline       |    342.0   |
| GLR (no conflicts)  |    1798.9  |
| GLR overhead vs LALR| +426% (~5.3×) |

The overhead range across architectures is consistent: 5–8× of the
LALR fast path on unambiguous input.  x86_64 outperforms Apple
Silicon on GLR by ~40% (likely due to better branch prediction on
the predecessor-list traversals); both meet the merge gate of
"identical or better (within statistical variation) than the
m3pro baseline" set when the branch was resurrected.

Variance between runs is significant on a developer laptop; the
shape of the result is consistent.

The overhead comes from the GSS bookkeeping (node create / refcount
/ predecessor list management) that runs on every shift, even when
no forks occur.  That is **inherent to GLR**, not a bug -- GLR has
to track parallel-stack potential at every step in case the next
token forces a fork.

### LALR fast path: unchanged by the merge

The most important guarantee: `src/parse_engine.c`,
`src/parse_context.c`, `src/snapshot.c`, `src/jit_codegen.c`, and
`src/jit_context.c` are **byte-identical** between the v0.3.3 main
branch and the v0.3.4 GLR-merged tree.  No call site in any LALR
path references any GLR symbol; the GLR engine lives entirely in
`src/glr.c` + `src/parse_glr.c`.  Users who never call
`lime_parse_glr()` pay zero cost for GLR support being present in
the runtime, mathematically.

## Zero-overhead-when-not-used

`lime_parse()` (the LALR entry point) and `parse_token()` are
unchanged on this branch.  Users who never call `lime_parse_glr()`
pay nothing for GLR support being present in the runtime.

## Implementation

- `src/glr.c` — GSS node management + parser core (refactored from
  the as-was 19-parameter `glr_parser_feed` to take a
  `ParserSnapshot *`).
- `src/parse_glr.c` — `lime_parse_glr` entry point, glues the
  ParseContext to the GLR engine.
- `include/glr.h` — public types (GSSNode, GLRParser,
  GLRDisambiguateFn).

## Tests

- `tests/test_gss.c` — GSS node ops (create / acquire / release /
  add_predecessor)
- `tests/test_glr_no_conflict.c` — deterministic grammar parses
  identically through LALR and GLR (correctness baseline)
- `tests/test_glr_ambiguous.c` — small ambiguous grammar exercises
  forking and the disambiguation callback

Coverage: `src/glr.c` 78.6% lines / 60.4% branches.

## Open follow-ups

- Worked example under `examples/glr_demo/` (e.g. dangling-else or
  `T(x);` — the ambiguous test grammar is currently inline in the
  test file).
- Location tracking through merged GSS nodes (the `YYLOCATIONTYPE`
  fields exist; integration with `%locations` is not wired yet).
- Reduce-callback dispatch through GLR-mode parses (parallel to
  the LALR-side reduce-callback ROADMAP item).
