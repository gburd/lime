# Bison compatibility matrix (`--target=c:bison`)

`lime --target=c:bison grammar.lime` emits a **bison-shaped C skin**
(`<grammar>_bison.h` / `<grammar>_bison.c`) alongside the standard Lime
output, to minimise call-site churn when porting a bison consumer. It is
**not** a drop-in replacement for `bison -d` output: the invocation model
differs (bison is pull, `yyparse()` calls `yylex()`; Lime's core is push),
and several bison features are unsupported or emulated.

This table is grounded in the skin's own design notes
(`src/emit_c_skin_bison.c`, module header). Rows marked **Unverified**
have not been confirmed against a passing test in this tree and should be
treated as unknown, not as supported.

Legend: **Supported** (works as bison does) · **Emulated** (works via a
Lime equivalent with a different shape) · **Rejected** (`lime` refuses the
construct with a diagnostic naming it and the Lime equivalent; checked
by `tests/test_bison_diagnostics.sh`) · **Unsupported** (silently absent / not provided)
· **Unverified** (not confirmed from code or a test).

| Bison feature | Status | Notes / Lime equivalent |
|---|---|---|
| `yyparse()` (pull driver over `yylex()`) | Emulated | Skin drives Lime's push parser from `yylex()` in a loop; returns 0 / 1 (syntax error) / 2 (alloc failure). `src/emit_c_skin_bison.c`. |
| `YYSTYPE` from `%token_type {T}` | Supported | `typedef T YYSTYPE;`. |
| `%union { body }` -> `YYSTYPE` | Supported | `typedef union { body } YYSTYPE;`, guarded by `YYSTYPE_IS_DECLARED`; matches the standard parser stack byte-for-byte. |
| `YYSTYPE` default (neither) | Supported | `typedef void * YYSTYPE;`. |
| `YYDEBUG` / `yydebug` trace flag | Emulated | `int yydebug = 0;` at file scope; `yyparse_extra()` routes to `<Name>Trace(stderr, ...)` when set. Compiled out under `-DNDEBUG`. |
| `enum yytokentype` (bison 258+ numbering) | Supported | Emit-time translation table maps bison codes `258+(i-1)` to Lime codes; bison 0 == Lime 0 (EOF). |
| `yyerror()` / `yylex()` as extern | Supported | Caller-supplied, exactly like bison. |
| `%extra_argument` | Emulated | Emitted as `yyparse_extra(<arg>)`; strict `yyparse()` routes through a weak global. |
| `%parse-param` / `%lex-param` | Rejected | `lime` names the construct and points at `%extra_argument` (single param, different shape). |
| Tagged tokens `%token<field> NAME` | Unsupported | Angle-bracketed tag not parsed. Workaround: `yylex()` writes `yylval.<field>` directly. |
| `yychar` / `yynerrs` globals | Unsupported | Lime does not expose lookahead state via globals. |
| Mid-rule actions `{ ... }` inside a production | Rejected | Architectural gap (bison synthesises anonymous non-terminals); not implemented. `lime` reports `mid-rule action: ...` with the split-the-rule rewrite instead of "illegal character". |
| `error` token + `yyerrok` / `yyclearin` | Unsupported | Lime uses a different error model (`%syntax_error` / `%parse_failure`). |
| `%locations` / `YYLTYPE` / `@$` / `@N` | Unverified | Lime carries `ParseLoc`; bison `YYLTYPE` mapping not confirmed here. |
| `%pure-parser` / `api.pure` | N/A | Lime's parser is already reentrant. |
| `YYABORT` / `YYACCEPT` / `YYBACKUP` | Unverified | Not confirmed from the skin. |
| `%precedence` (distinct from `%nonassoc`) | Rejected | Not accepted; the diagnostic points at `%nonassoc`, which assigns a level without associativity. |
| `%expect N` | Supported | Verified: `%expect 0` on a one-conflict grammar fails with "Expected 0 conflict(s) but found 1"; matching N exits 0. |
| `%expect-rr` | Unsupported | Parsed as `%expect` followed by junk ("Expected integer argument"). |
| `%printer` | Rejected | Not provided; the diagnostic points at `%destructor`, which is supported. |
| `%code {top,requires,provides}` blocks | Unverified | Not confirmed from the skin. |
| `%glr-parser` + `%merge` | Emulated | Separate `glr_parse()` API (`docs/GLR.md`). The bison directive itself is rejected with a diagnostic pointing there. |
| `%define ...` | Rejected | No `%define` variables. `api.pure` is the default (reentrant); `parse.error verbose` maps to `%syntax_error { ... }`. |
| Positional values `$$`, `$1`, `$<type>2` | Rejected | Lime names values by alias (`a(L) ::= b(X). { L = X; }`). Caught at generation time with the rewrite; previously passed through and failed later in `cc` with "'$$' undeclared". Ignored inside string literals and comments. |

## Porting guidance

- Action bodies that call `yychar`, manipulate lookahead, or re-enter the
  lexer will not port mechanically because Lime's core is push-driven.
- Mid-rule actions must be lifted into explicit non-terminals by hand.
- See `docs/MIGRATION_FROM_BISON.md` for the narrative porting guide and
  `docs/SKINS.md` for the skin's emit contract.

## Status of this matrix

The **Unverified** rows are the honest state: the skin's header documents
what it emits, but this tree does not yet ship a per-row conformance test
or a real-`.y`-corpus CI job. Do not read **Unverified** as **Supported**.
