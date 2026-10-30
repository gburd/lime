# Bison / yacc compatibility matrix

**`lime` reads yacc and bison grammar files.** A file with a `%%` line at
column 0 is read as yacc/bison syntax (`lhs : a b | c ;`, `$$`/`$N`,
`'+'` literals); any other file is read as Lime/Lemon syntax
(`lhs(A) ::= rhs(B). { A = B; }`), whatever its extension (sqlite's Lemon
`parse.y` has none of these markers). `--yacc` and `--lemon` override the
detection. Both syntaxes build the same rules and symbols and use the same
LALR(1) engine and tables; yacc semantics (the `$accept` start rule,
terminal-ness by declaration, the yacc error model, bison token numbering,
`yyparse()`) apply only to yacc input. Lime-syntax grammars generate the
same bytes as before (checked on all 106 tracked `.y`/`.lime` grammars).

Run as `lime-yacc` (installed as a symlink), `yacc`, `bison`, or
`lime --yacc ...`, lime takes the yacc/bison command line:
`-d`/`--defines`/`--header[=F]`, `-o F`, `-b P`, `-y`, `-v`, `-t`, `-l`,
`-p P`/`--name-prefix=P`, `-W...` (ignored), `--version`. Output names
follow bison: `y.tab.c`/`y.tab.h` as yacc or with `-y`, `<grammar>.tab.c`
as bison, `-o foo.c` gives `foo.h`. The header is written only with `-d`.

Real programs built with `YACC=lime-yacc` (tests/compat/README.md):
- one-true-awk 2025-12-25: same 44 s/r + 85 r/r conflicts as bison;
  `proctab.c` (made by `maketab` from the token header) byte-identical to
  the bison build; `REGRESS` output identical apart from timings.
- bc 1.07.1 (`LEX=flex`): `libmath.h` byte-identical; bc's `Test/*.b`,
  `Examples/*.b` and 15 stdin cases give identical output and status
  (`tests/compat/bc_e2e_compare.sh`).

Corpus (`tests/compat/run_corpus.py --tiers`): T1 (POSIX yacc) 10/10,
T2 (+ PostgreSQL, byacc) 19/20, T3 (+ bison examples) 28/30. The misses:
bison's `glr/c++-types.y` (`%glr-parser`, named diagnostic) and byacc's
`test/grammar.y`, which bison rejects the same way (29 r/r conflicts with
`%expect 1`).

Behaviour (`tests/compat/diff/run_diff.sh`, `run_c11.sh`, with
`LIME_DIRECT=1`): on bison's calc/rpcalc/mfcalc, 40/40 inputs give the same
stdout, exit status and number of syntax errors as bison; on C11, 73/73
inputs give the same accept/reject and stop at the same token.

Legend: **Supported** (as bison) · **Accepted** (parsed, no effect on the
output) · **Rejected** (named diagnostic) · **Lime syntax** (the Lemon-syntax
equivalent, for `.lime` files).

## yacc / bison grammar syntax

| Construct | Status | Test / notes |
|---|---|---|
| `%{ %}`, `%%` sections, epilogue | Supported | `tests/test_yacc_frontend.sh`. `#line` directives point at the .y. |
| `a : b c \| d ;`, empty alternatives, optional `;` | Supported | `test_yacc_frontend.sh` (calc). |
| Terminal vs nonterminal by declaration | Supported | Names given to `%token`/`%left`/... are terminals, the rest nonterminals, any case. |
| `'c'` literals, escapes (`'\n'`, `'\012'`, `'\x41'`) | Supported | External code = the character. `test_yacc_frontend.sh`, one-true-awk, C11. |
| `%token NAME "alias"`, `_("alias")`, `"alias"` in rules | Supported | byacc quote_calc, bison parse-gram.y. |
| bison token numbering (258.., explicit `%token N 300`) | Supported | `test_yacc_frontend.sh` (header `#define`s equal bison's). |
| `$$`, `$N`, `$<t>N`, `$-N` (prefix of a mid-rule) | Supported | `$$` starts as `$1`, as bison. |
| `@$`, `@N`, `%locations`, `YYLTYPE`, `YYLLOC_DEFAULT` | Supported | `test_yacc_frontend.sh` (pure case). |
| Named references `$name`, `$[name]`, `sym[alias]` | Supported | Unknown or ambiguous names are rejected. |
| Mid-rule actions | Supported | As bison's `$@N : %empty` rules. `test_yacc_frontend.sh`. |
| Automatic `$accept : start $end` | Supported | The start symbol may appear on a right-hand side. |
| `%union`, `%type <t>`, `%token <t>`, `%nterm` | Supported | `test_yacc_frontend.sh` (calc). |
| `%define api.value.type union` / `{T}` | Supported | bison calc.y. |
| `%left` `%right` `%nonassoc` `%precedence` `%prec` | Supported | `%precedence` is a no-associativity level. |
| `%start`, `%empty` | Supported | |
| `%expect N`, `%expect-rr N` | Supported | As bison 3.8 LALR: `%expect` counts s/r; any r/r fails once `%expect` is given; `%expect-rr` warns "applies only to GLR parsers". |
| `%pure-parser`, `%define api.pure` | Supported | `yylval`/`yylloc`/`yychar`/`yynerrs` local to `yyparse()`; `yylex(&yylval[, &yylloc][, lex-params])`. |
| `%name-prefix`, `api.prefix`, `-p` | Supported | bison's `#define yyparse <p>parse` renames. |
| `%parse-param`, `%lex-param`, `%param` (several) | Supported | Passed to `yyerror()` as bison does. `test_yacc_frontend.sh`. |
| `%code`, `%code top/requires/provides` | Supported | bison's placements (`requires`/`provides` also in the header). |
| `%define api.push-pull push\|both` | Supported | `yypstate_new`/`yypush_parse`/`yypstate_delete`, `YYPUSH_MORE`. `test_yacc_frontend.sh`. |
| `%define api.token.prefix`, `%initial-action`, `%debug`, `%require`, `%language "c"` | Supported | |
| `%printer`, `%destructor` | Accepted | Values are not printed in traces and not freed during error recovery. |
| `%define parse.error detailed\|verbose\|custom`, `parse.lac` | Accepted | Messages are always `syntax error`, not `syntax error, unexpected X, expecting Y`. |
| `%glr-parser` | Rejected | Names the LALR(1) limit and `glr_parse()` (docs/GLR.md). |
| Other `%define` variables, `%skeleton` other than `yacc.c`, `%language` other than C, `%no-default-prec` | Rejected | Named diagnostics. |

## Runtime (yacc input)

| Feature | Status | Test / notes |
|---|---|---|
| `yyparse()` / `yylex()` / `yyerror()` / `yylval` / `yychar` / `yynerrs` / `yydebug` | Supported | `test_yacc_frontend.sh`; awk and bc. Default reductions fire before the next `yylex()`, as bison's. |
| No `error` rule: the first syntax error makes `yyparse()` return 1 | Supported | `test_yacc_frontend.sh` (abort cases); rpcalc differential. |
| `error` token recovery (bison's yyerrlab, 3-shift quiet period) | Supported | `test_yacc_frontend.sh`; calc/mfcalc differential; awk `T.errmsg`. |
| `yyerrok`, `yyclearin`, `YYACCEPT`, `YYABORT`, `YYERROR`, `YYRECOVERING()` | Supported | `test_yacc_frontend.sh` (YYERROR matches bison on 5 inputs). |
| `YYBACKUP` | Rejected | (not defined; the C compiler reports it). |
| Stack depth | Supported | Fixed at 10000 (bison's `YYMAXDEPTH`); overflow calls `yyerror("memory exhausted")` and `yyparse()` returns 2, as bison (checked by hand on 20000 nested parentheses). |
| Reentrancy of `yyparse()` across threads | Not supported | Driver state is file-static; one parse per grammar at a time. |

## Lime-syntax grammars with bison-shaped output (`--target=c:bison`)

| Feature | Status | Test / notes |
|---|---|---|
| `yyparse()` over Lime's push parser | Supported | `tests/test_skin_bison.c`. |
| `enum yytokentype` from 258 | Supported | `tests/test_skin_bison.c`. |
| `yylex()` returning char codes | Supported | For tokens named `CHAR_<code>`. `tests/test_skin_bison_charcodes.sh`. |
| `YYSTYPE` from `%token_type` / `%union` | Supported | `tests/test_skin_bison_union.c`. |

`--target=c:bison` on yacc input is an error: yacc input already gets
bison's API in the main `.c`.
