# File-Level Grammar Inheritance (`%extends`)

The `%extends "base.lime"` directive lets one Lime grammar file
build on another at parser-generation time. The base file's rules,
tokens, types, includes, and directives are merged into the
derived file's grammar before code generation. Companion directives
`%override`, `%remove`, and `%override_type` give the derived file
fine-grained control over what it keeps, replaces, and drops.

This is the v0.4.1 mechanism. It complements (and does not replace)
the v0.4.0 `%dialect` directive and the runtime extension framework:

| Mechanism | When | Granularity |
|---|---|---|
| [`%extends`](#) (v0.4.1) | parser-generation time | one file extends another at the source level |
| [`%dialect`](DIALECT.md) (v0.4.0) | parser-generation time | conditional rule blocks inside one file |
| [runtime extensions](EXTENSIONS.md) | parse time (post-build) | swap rule sets in a running parser |

Use `%extends` when you have a stable base grammar (e.g. ANSI SQL)
and one or more derivatives (e.g. Oracle, MySQL) that each need to
ship as their own `.so`/`.a`. Use `%dialect` when you want one file
that builds many flavors via `-D`. Use runtime extensions when the
parser must reconfigure itself after build time.

## Syntax

```lime
/* derived.lime */
%extends "base.lime"

/* %extends may appear multiple times -- the diamond pattern: */
%extends "another_base.lime"

/* %override replaces a rule from a base file by IDENTITY. */
%override
expr(R) ::= expr(L) PLUS expr(M).   { R = my_plus(L, M); }

/* %remove drops a base rule entirely. */
%remove expr ::= expr MINUS expr.

/* %override_type widens a base file's %type. */
%override_type expr {MyExpr *}

/* New rules from the derived file are simply appended. */
expr(R) ::= expr(L) DOT_DOT_DOT.    { R = ellipsis(L); }
```

`%extends` accepts a single double-quoted path. Resolution order:

1. **Absolute path** if the string starts with `/`.
2. **Relative to the current file's directory.**
3. **`LIME_PATH` env var** entries (colon-separated), in order.

Resolution failure (no matching file) is a hard error. CWD is
intentionally not searched: it shifts under build systems and has
caused enough grief.

Cycles are detected. If `a.lime` extends `b.lime` extends `a.lime`,
Lime exits with `%extends cycle: "a.lime" is already being parsed`.

## The 10 locked design rules

These ten decisions, settled at design time, govern every edge case.
Examples below use the ANSI / Oracle / MySQL / unified fixture from
`tests/extends_fixtures/`.

### Rule 1 — Rule identity is `(LHS_name, RHS_symbol_sequence)`

Alias names (LHS alias, per-RHS aliases), action body, and
precedence mark are NOT part of identity. These two declarations
denote the SAME rule:

```lime
expr(A) ::= expr(B) PLUS expr(C).   { A = ... }
expr(X) ::= expr(Y) PLUS expr(Z).   { X = ... }
```

`%override` and `%remove` match on identity, so the derived file
can use whatever aliases it likes.

### Rule 2 — `%override` matches by identity, replaces the body

```lime
%extends "ansi_sql.lime"
%override
select_clause(R) ::= SELECT STAR FROM IDENT.   { R = 100; /* Oracle */ }
```

If the matched rule does not exist in the inherited grammar, Lime
errors with `%override: no rule of matching identity to override`.
This is always an error, never a warning -- a missing target
points at a real authoring mistake (typo in the rule shape, base
file evolved away from the override).

### Rule 3 — `%remove rule-shape.` drops a base rule

```lime
%remove where_clause ::= WHERE IDENT EQUALS NUMBER.
```

No action body; the directive ends at the rule's terminating `.`.
The matched rule is unlinked from the grammar.

If the target does not exist, the response depends on the build:

- **`LIME_STRICT`** (auto-defined for `meson buildtype=debug` or
  `debugoptimized`): hard error. The lime binary exits non-zero.
- **Release builds** (`buildtype=release`, `plain`, `minsize`):
  warning to stderr; the build continues.

The split exists because pinned derived grammars frequently outlive
the base file's exact rule shape; treating that case as a hard
error in production breaks downstream builds for what is usually a
silent no-op. Strict CI runs catch the drift early.

### Rule 4 — Diamond conflict on different paths is an error

```
ansi_sql.lime
  /        \
oracle.lime  mysql.lime    <- both %override the SAME rule with
  \        /                  DIFFERENT bodies
unified.lime                <- has neither %override
```

If `oracle.lime` and `mysql.lime` each `%override` rule X with
different bodies, and `unified.lime` extends both without its own
`%override` X, Lime exits with `diamond inheritance conflict: rule
'X' has conflicting %override / %remove from 'oracle.lime' and
'mysql.lime'; add a %override in the derived file to disambiguate`.

The conflict is detected at the second-to-arrive override (when
the merger sees an existing same-depth override from a different
file) and held until end-of-Parse(). A subsequent shallower
override (Rule 6) clears the flag.

### Rule 5 — Single-override path wins

```
ansi_sql.lime defines select_clause and where_clause.
oracle.lime overrides select_clause; leaves where_clause untouched.
mysql.lime overrides where_clause; leaves select_clause untouched.
unified.lime extends both.
```

Result:

- **select_clause**: oracle's body wins. mysql's transitive reload
  of ansi_sql.lime tries to re-add the base rule; the existing
  override on the rule object causes the re-add to silently
  dedup. Verified in `tests/test_extends.c::diamond_select_oracle`.
- **where_clause**: mysql's body wins. Same mechanism.

This is the common case. The override path expresses unambiguous
intent; the non-override path's silence does not contest it.

### Rule 6 — Last-wins for derived file's own decisions

```lime
%extends "oracle.lime"
%extends "mysql.lime"
%override
expr ::= expr PLUS expr.   { /* unified's body */ }
```

`unified.lime`'s `%override` runs at extends_depth=0 (the user-
invoked file). Both oracle.lime's and mysql.lime's overrides ran at
depth=1. A shallower-depth override wins unconditionally,
overwriting whichever sibling had won the depth-1 conflict (or
clearing the conflict entirely if both siblings had the same
shape).

This is what lets a unified file sweep up a diamond conflict in one
line: `%override` once at the diamond top, and the conflict
diagnostic disappears.

### Rule 7 — `%include` blocks concatenate in DFS order

The base file's `%include { ... }` content lands first in the
generated `.c`, then derived files in the order they appear. The
existing Lime tokenizer already concatenates successive `%include`
bodies into a single string; recursive `%extends` calls feed that
mechanism in DFS order, so this rule works out of the box.

Test verification: `tests/test_extends.c::include_dfs_order` checks
that `#include <stdio.h>` (from ansi_sql.lime's `%include`) appears
before the generated `yy_action` table in unified.c.

### Rule 8 — `%remove` + `%override` on different paths is a conflict

If oracle.lime overrides expr+PLUS+expr and mysql.lime removes the
same shape, unified.lime extending both is a conflict, errored at
the same point as Rule 4. unified.lime must declare which side
wins (either re-`%override` or re-`%remove`).

### Rule 9 — Type compatibility is strict by default

A rule's symbols' `%type` declarations are part of the parser ABI
(they show up in the generated reduce-action stack). Re-declaring
the same type via a diamond is silently allowed (the second
declaration is a redundant re-load of the same base file).
Re-declaring with a DIFFERENT type is rejected: the user must
spell out their intent with `%override_type`.

```lime
%override_type expr {OracleExpr *}
```

When `%override_type` actually widens an existing type, Lime
prints to stderr:

```
file.lime:42: warning: %override_type widens expr from existing type;
                        user-responsibility ABI compat
```

The warning is intentionally noisy. The user is asserting that the
new type is ABI-compatible with the prior type wherever the
generated parser already uses it; Lime cannot verify that
assertion (it's a downstream property of the user's runtime).

### Rule 10 — Single-parent only? No: diamond IS allowed

A file may `%extends` multiple base files. The diamond resolution
rules above (4 through 8) handle the convergence cases.

There is no "linearisation" pass à la Python's MRO. Lime's order
is simple DFS in the order the `%extends` directives appear, with
identity-based dedup: the first time a rule lands, it stays;
subsequent re-loads of the same rule from a sibling arm are
silent unless they conflict on body or `%override` ownership.

## Worked example

```
ansi_sql.lime         (base; defines select_clause, where_clause)
oracle.lime           (%extends ansi; %overrides select_clause)
mysql.lime            (%extends ansi; %overrides where_clause)
unified.lime          (%extends oracle, %extends mysql)
```

When `lime unified.lime` runs:

1. unified.lime is opened. extends_depth=0.
2. `%extends "oracle.lime"` triggers a recursive load. extends_depth=1.
3. oracle.lime's `%extends "ansi_sql.lime"` recurses again. extends_depth=2.
4. ansi_sql.lime is parsed normally. Its rules (select_clause, where_clause)
   are added with origin_file="ansi_sql.lime", override_depth=INT_MAX.
5. Return to oracle.lime. `%override select_clause` fires. The rule
   is found by identity; its body is replaced with oracle's; its
   override_depth is set to 1, override_file="oracle.lime",
   is_overridden=1.
6. oracle.lime's extra rules (CONNECT_BY, ROWNUM) are added.
7. Return to unified.lime. extends_depth=1.
8. `%extends "mysql.lime"` recurses. extends_depth=2 inside mysql.
9. mysql's `%extends "ansi_sql.lime"` recurses again. extends_depth=3.
10. ansi_sql.lime is reloaded. Rule add for select_clause: identity
    match found, existing rule has is_overridden=1, so the add is
    silently deduped (Rule 5 -- override path wins).
    Rule add for where_clause: identity match, existing rule has
    is_overridden=0; silently dedup, the existing base rule stays.
    %type / %include / %token re-decls are silently absorbed by the
    diamond-aware paths in the parser.
11. Return to mysql.lime. `%override where_clause` fires; the
    inherited base rule's body is replaced with mysql's;
    override_depth=2, override_file="mysql.lime".
12. mysql.lime's extras (BACKTICK_IDENT, LIMIT) are added.
13. Return to unified.lime. End of file.
14. End-of-Parse() sweep: no rule has conflict_pending=1, so
    grammar emission proceeds.

Result: the generated parser contains:

- ANSI's tokens + start-symbol scaffolding,
- Oracle's `/* Oracle */`-marked select_clause body,
- MySQL's `/* MySQL */`-marked where_clause body,
- Oracle's CONNECT_BY rule,
- MySQL's LIMIT rule,
- ANSI's untouched epsilon-where_clause,
- ANSI's `%include { #include <stdio.h> ... }` ahead of the action
  table.

## When `%extends` is the wrong tool

- **You want runtime dialect switching.** Use the
  [runtime extension framework](EXTENSIONS.md). `%extends` decides
  what gets *built*; runtime extensions decide what gets *activated*
  in a running parser.
- **You want one source file, many builds.** Use
  [`%dialect`](DIALECT.md). `%extends` requires one source file per
  flavour.
- **You want to compose unrelated grammars.** Use the
  [module / `%require` / `%import`](MODULE_FORMAT.md) machinery.
  `%extends` is for one-file-extends-another, not for stitching
  independent grammars together.

## See also

- [DIALECT.md](DIALECT.md) — the v0.4.0 conditional-block sibling.
- [EXTENSIONS.md](EXTENSIONS.md) — the runtime extension framework.
- [MODULE_FORMAT.md](MODULE_FORMAT.md) — `%require` / `%import` for
  composing independent grammar modules.
- v0.4.2 (planned) — `--diff-conflicts` will pretty-print the merged
  rule set with conflict markers, useful for inspecting diamond
  resolution after the fact.
- v0.4.3 (planned) — `%embed lang` for inlining a runtime sub-grammar
  block.
