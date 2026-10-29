# Bison / yacc compatibility matrix

**`lime` does not read yacc or bison grammar files.** Lime has one grammar
syntax, inherited from Lemon (`lhs(A) ::= rhs(B). { A = B; }`). A `.y` file
fails at its first `%{` or `%%` with a diagnostic that says so and points
here. Every grammar in a 30-grammar real-world corpus (bison/byacc/flex/gawk/
one-true-awk/bc/C11/PostgreSQL) is rejected as-is; see
`tests/compat/run_corpus.py` and the report it feeds. A yacc front end is the
top item on the roadmap; until it lands, porting means rewriting the grammar
in Lime syntax (docs/MIGRATION_FROM_BISON.md).

What Lime does provide:

- A **Lemon-syntax grammar** language that accepts some bison spellings
  (rows marked *in Lime syntax* below).
- `--target=c:bison`, a **bison-shaped C skin** (`<grammar>_bison.h` /
  `_bison.c`). It puts `yyparse()` / `yylval` / `enum yytokentype` over Lime's
  push parser, so a bison consumer's call sites change little. It is not a
  drop-in for `bison -d` output.
- An LALR(1) engine that matches bison where a grammar can be expressed: the
  same conflict count on 21/21 corpus grammars converted by
  `tests/compat/y2lime_min.py` (`tests/compat/run_ymin.py`), and the same
  accept/reject and output on bison's calc / rpcalc / mfcalc examples and on
  C11 (`tests/compat/diff/run_diff.sh`, `run_c11.sh`).

Every row cites the test that checks it, or says **Unverified**.

Legend: **Supported** (works as in bison) · **Emulated** (a Lime equivalent
with a different shape) · **Rejected** (`lime` refuses it with a diagnostic
that names the construct and the Lime equivalent) · **Unsupported** (absent
or wrong, with no named diagnostic) · **Unverified** (not confirmed by a test
in this tree).

## Grammar file syntax

| Bison / yacc construct | Status | Test / notes |
|---|---|---|
| `%{ ... %}` prologue, `%%` sections, epilogue | Rejected | `tests/test_bison_diagnostics.sh` ("%% section", "%{ prologue"): one diagnostic naming yacc syntax, then stop. |
| `lhs : a \| b ;` rules | Rejected | `test_bison_diagnostics.sh` ("yacc colon rule"): asks for `::=`. Lime syntax accepts `lhs ::= a \| b.` with one shared trailing action (`tests/test_alternation.c`). |
| `$$`, `$1`, `$<type>2` | Rejected | `test_bison_diagnostics.sh` ("$$ = $1", "$<type>N"). Lime names values by alias. Ignored inside strings and comments (same script). |
| Character-literal tokens `'+'` | Rejected | `test_bison_diagnostics.sh` ("char literal"). Declare a named token. |
| String-alias tokens `"+"`, `%token NUM "number"` | Rejected | `test_bison_diagnostics.sh` ("string alias"). |
| `%empty` | Rejected | `test_bison_diagnostics.sh` ("%empty"). Write an empty RHS: `a ::= .` |
| Mid-rule actions `{ ... }` inside a production | Rejected | `test_bison_diagnostics.sh` ("mid-rule action"). Split the rule by hand. |
| Start symbol used on its own RHS (yacc's `input: \| input line ;`) | Rejected | `test_bison_diagnostics.sh` ("start symbol on RHS"): clean error naming the wrapper rule. yacc adds `$accept` itself; Lime does not. Before 2026-10 this was a SIGABRT, as it still is in Lemon. |
| Terminal vs nonterminal by declaration | Unsupported | Lime decides by the first letter (`Foo` terminal, `foo` nonterminal). `%token num` gives "should be a token". |
| `%union { ... }` | Supported *in Lime syntax* | `tests/test_skin_bison_union.c`. |
| `%token<field> NAME.` | Supported *in Lime syntax* | `tests/test_tagged_tokens.c`. The tag is recorded and shown in the bison-skin header. |
| `%type <tag> a b` | Unsupported | Lime's form is `%type a {CType}`, one symbol per directive. |
| `%start sym` | Supported *in Lime syntax* | Alias for `%start_symbol`. **Unverified** by a dedicated test (hand check: accepted). |
| `%name-prefix` | Emulated | `%name_prefix foo` / `%name foo` (hand check: `%name_prefix Foo` emits `FooAlloc`). The dashed form is not parsed. **Unverified** by a dedicated test. |
| `%left` / `%right` / `%nonassoc` | Supported *in Lime syntax* | A `.` is required. `tests/compat/diff/mfcalc.lime` (differential vs bison). |
| `%prec TOK` | Emulated | `[TOK]` after the rule. `tests/compat/diff/mfcalc.lime` (`-2^2` matches bison). |
| `%precedence` | Rejected | `test_bison_diagnostics.sh` ("%precedence"): points at `%nonassoc`. |
| `%expect N` | Supported | `test_bison_diagnostics.sh` ("%expect mismatch", "%expect match"). Exact match, like bison 3.8 (checked: bison 3.8.2 also fails when 1 conflict is found and 2 are expected). Counts s/r + r/r together. |
| `%expect-rr N` | Rejected | `test_bison_diagnostics.sh` ("%expect-rr"). |
| `%define ...` | Rejected | `test_bison_diagnostics.sh` ("%define"). |
| `%parse-param` / `%lex-param` | Rejected | `test_bison_diagnostics.sh` ("%parse-param"): points at `%extra_argument` (one parameter). |
| `%printer` | Rejected | `test_bison_diagnostics.sh` ("%printer"): `%destructor` is supported (`tests/test_destructor*`). |
| `%glr-parser` + `%merge` | Rejected | `test_bison_diagnostics.sh` ("%glr-parser"): points at the separate `glr_parse()` API (`tests/test_glr_*.c`, docs/GLR.md). |
| `%code { ... }` | Emulated | Unqualified `%code {}` is accepted and emitted at the end of the generated .c, after the parser (hand check). `%code top { }` (and requires/provides) fails with "no prior rule upon which to attach the code fragment". **Unverified** by a dedicated test. |
| `%locations`, `@$`, `@alias`, `YYLLOC_DEFAULT` | Emulated | `%locations` + `%location_type`. `tests/test_locations.c`, `tests/test_yylloc_default.c`, `tests/test_yylloc_action_assign.c`. |
| Numeric `@1`, `@2` | Unsupported | Copied verbatim into the generated C, so the C compiler fails later; lime does not diagnose it (checked by hand). Use `@alias`. |
| `%pure-parser` / `api.pure` | N/A | Lime parsers are always reentrant. `%pure-parser` itself is rejected with that hint (`lime.c` bison_hints). |

## Runtime / generated API (`--target=c:bison`)

| Bison feature | Status | Test / notes |
|---|---|---|
| `yyparse()` pulling from `yylex()` | Emulated | `tests/test_skin_bison.c`: drives Lime's push parser from `yylex()`; returns 0 / 1 / 2. |
| `enum yytokentype` (named tokens from 258) | Supported | `tests/test_skin_bison.c` (258 maps to the first Lime token). |
| Char-code tokens (`yylex()` returns `'+'`) | Unsupported | The skin's `yy_xlat_token` returns -1 for codes < 258, so `yyparse()` reports "invalid token". A hand driver must map char codes (`tests/compat/diff/lime_yyparse.h`). |
| `YYSTYPE` from `%token_type` / `%union` / default | Supported | `tests/test_skin_bison.c`, `tests/test_skin_bison_union.c`. |
| `yydebug` | Emulated | `tests/test_skin_bison_union.c` (routes to `<Name>Trace`). Compiled out under `-DNDEBUG`. |
| `yyerror()` / `yylex()` extern | Supported | `tests/test_skin_bison.c`. The user's `%syntax_error` must call `yyerror()`; the skin does not do it. |
| `%extra_argument` -> `yyparse_extra(arg)` | Emulated | `tests/test_skin_bison.c`. |
| `yylloc` | Unsupported | Declared but not threaded into the parser (see the skin source). |
| `yychar` / `yynerrs` globals | Unsupported | `Parse_get_lookahead()` / `Parse_clear_lookahead()` are the Lime equivalents (`tests/test_lookahead.c`). |
| `error` token in rules | Emulated | Lemon-style recovery. `tests/compat/diff/run_diff.sh` (calc, mfcalc: `error '\n'` gives the same output and error count as bison on 28 inputs). |
| No `error` rule: abort on the first syntax error | Unsupported | Lemon discards the bad token and continues, and can then accept: rpcalc `1 +\n` prints `1` and accepts where bison returns 1 (`run_diff.sh` with `LIME_ABORT_ON_ERROR` unset). |
| `yyerrok` / `yyclearin` | Unsupported | No macros. `yyclearin` maps to `Parse_clear_lookahead()` (`tests/test_lookahead.c`). |
| `YYABORT` / `YYACCEPT` / `YYERROR` / `YYBACKUP` | Unsupported | Not defined in `limpar.c`. |

## Porting guidance

- Use docs/MIGRATION_FROM_BISON.md. Rename char literals, flip
  nonterminal case where needed, name values by alias, lift mid-rule actions
  into helper nonterminals, and add `start ::= old_start.` if the start symbol
  is recursive.
- If the grammar has no `error` rule and the caller relies on `yyparse()`
  returning 1 at the first error, stop feeding tokens after `%syntax_error`
  fires.
- The PostgreSQL port's converter (out of tree,
  `postgres/lime/src/tools/lime_convert_gram.py`) handles all nine PG
  grammars. It is PG-specific and converts 0 of the other 22 corpus grammars.
