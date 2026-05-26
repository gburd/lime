# LINT.md — Lime Grammar Linter (`lime -L`)

> "The linter is like the compiler, only much more opinionated, with
> suggestions that help users of Lime maintain good code hygiene."

`lime -L grammar.lime` runs an opinionated static-analysis pass over a
Lime grammar.  It catches typos, dead code, and stylistic drift before
codegen runs.  v0.5.0 expanded the linter from a 4-rule stub to ~16
rules across three classes:

| Class       | Codes        | Default behaviour                                           |
|-------------|--------------|-------------------------------------------------------------|
| Errors      | E001-E005    | Block the lint pass; exit 1.                                |
| Warnings    | W001-W009    | Print but exit 0; promoted to errors with `--lint-strict`.  |
| Suggestions | S001-S002    | Off by default; opt in with `--lint-style`.                 |
| Module      | M001-M003    | Errors specific to module composition (`%module_name`).     |

## CLI

```
lime -L [--lint-strict] [--lint-style] [--lint-format=human|gcc|json] grammar.lime
```

| Flag                       | Purpose                                                                                           |
|----------------------------|---------------------------------------------------------------------------------------------------|
| `-L`                       | Enable the linter.  Suppresses parser-table codegen.                                              |
| `--lint-strict`            | Treat any warning as an error.  Exit 1 if any warning fires.  Pair with `-L` in CI.               |
| `--lint-style`             | Enable the opt-in style suggestions S001 and S002.                                                |
| `--lint-format=human`      | Default.  Human-readable lines on stderr plus a totals summary.                                   |
| `--lint-format=gcc`        | `path:line:col: severity: [code] message` on stderr; consumable by emacs/vim/IDE error lists.     |
| `--lint-format=json`       | JSON array on stdout.  CI-friendly; `[]` on a clean grammar.  Schema documented below.            |

The lint pass runs *after* the LALR analysis pipeline (`FindActions`),
so rules that depend on conflict counts (W005) have full data.  Code
generation is skipped — `lime -L` never writes a `.c` / `.h` / `.out`
file.

## Output Schema (JSON)

Each diagnostic is one object in a top-level array:

```json
[
  {
    "path":     "grammar.lime",
    "line":     42,
    "col":      1,
    "severity": "error" | "warning" | "note",
    "code":     "E001",
    "message":  "rule 'expr' references undeclared non-terminal 'unkown' (no rule produces it)",
    "fix":      null
  }
]
```

`fix` is reserved for future auto-fix payloads (e.g. for W004 the
canonical fix is "delete the action body").  Today it is always null;
clients should treat the field as optional.

A clean grammar produces exactly `[]\n`.

## Rule Catalog

### Errors

#### E001 — undeclared-rhs-symbol

A rule's RHS references a symbol that has no declaration anywhere:

- non-terminals must have at least one production rule with that LHS;
- terminals must be declared via `%token` / `%left` / `%right` /
  `%nonassoc` / `%fallback` / `%token_class` / `%wildcard`.

```lime
%token PLUS NUM.
expr ::= NUM PLUS unkown.   /* E001: 'unkown' has no rule */
```

Rationale: Lemon implicitly accepts undeclared uppercase symbols as
fresh terminals, which silently absorbs typos.  Lime treats this as
an opt-out behaviour: lint is opinionated, codegen still works.

#### E002 — undeclared-prec-symbol

A rule has a `[SYMBOL]` precedence override but `SYMBOL` has no
`%left` / `%right` / `%nonassoc` declaration:

```lime
%token MINUS NUM.
%left  MINUS.
expr ::= MINUS expr. [UMINUS]   /* E002: UMINUS has no precedence */
```

The override is a no-op without a precedence directive.

#### E003 — duplicate-token

The same `%token NAME.` declaration appears twice:

```lime
%token A B.
%token A.   /* E003: 'A' declared twice */
```

#### E004 — ambiguous-alias

Within one rule, two RHS slots use the same alias name:

```lime
expr(A) ::= expr(A) PLUS expr(A).   /* E004: 'A' on positions 1 and 3 */
```

Lemon's existing message-style diagnostic only fires when the rule
has an action body; E004 fires unconditionally.

#### E005 — unreachable-rule

A rule's LHS is never referenced from any other rule's RHS, and it is
not the start symbol:

```lime
start ::= A.
orphan ::= B C.   /* E005: 'orphan' is unreachable */
```

Almost always a typo or dead code left over after a refactor.

### Warnings

#### W001 — unused-token

A symbol is declared via `%token` but never referenced in any rule.
Skipped for `%fallback` sources/targets and the `%wildcard` token,
which are intentionally declared without use sites in their issuing
file.

#### W002 — unused-precedence

A symbol has `%left` / `%right` / `%nonassoc` but never appears in any
rule (RHS or `[SYMBOL]` override).  Either the precedence is dead
code or the rule was deleted without cleaning the directive.

#### W004 — trivial-action-body

The action body is just `{ $$ = $1; }` (modulo whitespace), which is
the default Lime emits when no body is present.  Suggest deleting
the body.

#### W005 — missing-expect

The grammar has shift/reduce or reduce/reduce conflicts but no
`%expect N` directive.  CI cannot detect when a future patch
introduces *new* conflicts, because absent `%expect`, Lime accepts
any conflict count.  Pin the count with `%expect N` to lock the
grammar's conflict surface.

This rule is computed *after* the LALR analysis pipeline, which is
why the lint pass moved to run after `FindActions()` in v0.5.0.

#### W006 — alias-without-action

A rule declares an `(alias)` on its LHS or an RHS slot, but the
action body never references it.  Either the alias is a leftover
from a previous version of the action, or the body is missing a use.

#### W007 — inconsistent-naming

Lemon convention: terminals are `ALL_UPPER`, non-terminals are
`all_lower`.  This rule flags mixed-case names like `MyToken` or
`my_Rule`.  Skipped for special symbols (`error`, `$`, `{default}`)
and `%token_class` synthetics.

#### W008 — long-rhs

A rule has more than `LIME_MAX_RHS_WARN` (default 8) RHS symbols.
Long sequences should be factored into named sub-rules for
readability and to localise reduce-time cost.

#### W009 — long-action-body

An action body exceeds `LIME_MAX_ACTION_LINES_WARN` (default 30)
lines.  Long bodies belong in a helper function inside the
`%include { ... }` block; the rule should call the helper.

### Suggestions (`--lint-style`)

#### S001 — missing-grammar-doc

The file has no header comment block.  Off by default.  Useful when
porting from Bison grammars that conventionally document copyright,
provenance, and dialect at the top of the file.

#### S002 — missing-rule-doc

A rule with `nrhs > 1` has no leading comment.  Off by default; opt
in for grammars where every non-trivial rule should be documented
(e.g. PostgreSQL's `gram.y` ports).

### Module-composition errors

#### M001 — module-name-without-version

`%module_name FOO.` requires a paired `%module_version "1.2.3".`.

#### M002 — invalid-semver

`%module_version` value does not parse as `MAJOR.MINOR.PATCH`.

#### M003 — undefined-export

`%module_export NAME.` references a symbol that is not declared
anywhere in the grammar.

#### W101 — terminal-export

`%module_export NAME.` references a terminal symbol.  Exports are
typically non-terminals (other modules import them and use them on
the RHS of their own rules).  Warning, not error — exporting a
terminal is occasionally legitimate.

## Output Format Examples

### `--lint-format=human` (default)

```
$ lime -L grammar.lime
Linting grammar.lime...
grammar.lime:7:1: error: [E001] rule 'expr' references undeclared non-terminal 'unkown' (no rule produces it)
grammar.lime:1:1: warning: [W001] token 'NEVER_USED' is declared by %token but never referenced in any rule

1 error(s), 1 warning(s), 0 note(s)
```

### `--lint-format=gcc`

Same per-line shape, no banner or summary.  Editor-friendly:

```
$ lime -L --lint-format=gcc grammar.lime
grammar.lime:7:1: error: [E001] rule 'expr' references undeclared non-terminal 'unkown' (no rule produces it)
grammar.lime:1:1: warning: [W001] token 'NEVER_USED' is declared by %token but never referenced in any rule
```

### `--lint-format=json`

```
$ lime -L --lint-format=json grammar.lime
[{"path":"grammar.lime","line":7,"col":1,"severity":"error","code":"E001","message":"...","fix":null}]
```

A clean grammar produces exactly `[]\n`.  All json output goes to
stdout; nothing else does.  Scripts can therefore use the customary
`> diagnostics.json` redirect without filtering.

## Integration Recipes

### CI gate

Block any merge that introduces an error or warning:

```bash
lime -L --lint-strict --lint-format=gcc grammar/*.lime
```

Exit code 0 = clean; 1 = at least one diagnostic fired.

### Editor jump-to-error (Vim)

```vim
:set errorformat=%f:%l:%c:\ %t%*[^:]:\ [%n]\ %m
:cexpr system("lime -L --lint-format=gcc " . expand("%"))
:copen
```

### Editor jump-to-error (Emacs `compile`)

`gcc`-format output already matches the default
`compilation-error-regexp-alist` patterns; no extra config required.

### JSON pipeline

Aggregate diagnostics across many files:

```bash
for f in grammar/*.lime; do
  lime -L --lint-format=json "$f"
done | jq -s 'add | group_by(.code) | map({code: .[0].code, count: length})'
```

### Pre-commit hook

```bash
#!/bin/sh
# .git/hooks/pre-commit
git diff --cached --name-only --diff-filter=ACM | grep '\.lime$' | while read f; do
  lime -L --lint-strict --lint-format=gcc "$f" || exit 1
done
```

## Tunables

The W008 / W009 thresholds are compile-time constants in `lime.c`:

```c
#define LIME_MAX_RHS_WARN          8
#define LIME_MAX_ACTION_LINES_WARN 30
```

Adjust if your project's house style differs.  Future versions may
expose these via CLI flags or a `.limelintrc` file.

## Behaviour Notes

- The lint pass runs the full LALR analysis pipeline so W005 has the
  conflict count.  Lemon's classic per-error stderr (e.g. "Nonterminal
  X has no rules.") may appear *alongside* lint output for the same
  underlying issue.  Consume the structured output (`--lint-format`
  json or gcc) for tooling; ignore lemon's stderr noise.
- The linter is read-only.  It never rewrites the grammar.  Use
  `lime -F` (the formatter) for the rewriting pass.
- Lint errors and parser errors share a single error budget.  If
  Parse() reports errors, `lime -L` exits with `lem.errorcnt` before
  the lint pass runs.

## Cross-references

- [EXTENDS.md](EXTENDS.md) — `%extends` and per-rule overrides
- [DIALECT.md](DIALECT.md) — `%dialect` conditional inclusion
- [DIFF_CONFLICTS.md](DIFF_CONFLICTS.md) — symbolic LALR conflict diff
- [DIAGNOSTICS.md](DIAGNOSTICS.md) — runtime parser error messages
- [MODULE_FORMAT.md](MODULE_FORMAT.md) — `%module_*` directives (M001-M003)
