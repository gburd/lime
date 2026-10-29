# Flex / lex compatibility matrix

**`lime -X` does not read lex or flex scanner files.** It reads Lime's own
`.lex` dialect (`rule NAME matches /re/ { action }`, `%state`, `%pattern`,
`%eof`). A `.l` file fails at its first `%{`, `%option`, definition line or
`%%` with `unexpected ERROR at top level`. All 22 scanners in a real-world
corpus (the flex manual and tests, flex's and bison's own scanners, bc, C11,
and six PostgreSQL scanners) are rejected (`tests/compat/run_corpus.py`).
No converter exists. Porting means rewriting the scanner in Lime syntax
(docs/MIGRATION_FROM_FLEX.md).

There are two different things here, and they should not be confused:

- **Lime's native lexer** (`lime -X`) is push-driven, reentrant, and has no
  globals: no `yytext` / `yyleng` / `yylineno` side channels. Use it for
  new code.
- **The flex skin** (`--target=c:flex`) is a thin *pull-driven* adapter over
  Lime's longest-match DFA. It does expose `yytext`, `yyleng`, `yylineno` and
  `yy_scan_string` for porting call sites. It **bypasses Lime action bodies**:
  `yylex()` returns `rule_id + 1` for each match.

Every row cites the test that checks it, or says **Unverified**.

Legend: **Supported** · **Emulated** · **Rejected** (refused with a
diagnostic) · **Unsupported** · **Unverified** (not confirmed by a test
here).

## Scanner file syntax (what `lime -X` reads)

| lex / flex construct | Status | Test / notes |
|---|---|---|
| `%{ %}`, definitions section, `%%`, user-code section | Unsupported | `tests/compat/run_corpus.py`: 0/22 accepted. The error is generic (`unexpected ERROR at top level`). |
| `pattern  action` rules | Emulated | `rule NAME matches /pattern/ { action }` (`tests/test_lex_parse.c`). |
| `name  regex` definitions + `{name}` | Emulated | `%pattern name /regex/.` + `{name}` (`tests/test_lex_resolve.c`). |
| `"quoted"` literals inside a pattern | Unsupported | Not in Lime's regex dialect. 13 of the 22 corpus scanners use them. |
| `%x S` / `%s S`, `<S>` rule prefixes | Emulated | `%exclusive_state S.` / `%state S.` (`tests/test_lex_actions.c`, `tests/test_lex_eof.c`, `tests/test_lex_buf.c`). |
| `<<EOF>>` | Emulated | Per-state `%eof` (`tests/test_lex_eof.c`). |
| `^` / `$` anchors | Emulated, **different meaning** | They parse (`tests/test_lex_regex.c`) but match start and end of *input*, not of line (docs/MIGRATION_FROM_FLEX.md). |
| Trailing context `r/s` | Unsupported | 0/22 corpus scanners use it (checked by hand). |
| `%option` | Unsupported | 12/22 corpus scanners use it. |
| Case-insensitive matching | Emulated | Only per keyword table: `%keyword_table t (case_insensitive)` (`tests/test_lex_parse.c`). No `%option caseless`. |

## Action-body primitives (native lexer)

| Flex | Status | Test / notes |
|---|---|---|
| `return TOK;` | Emulated | `LEX_EMIT(TOK);`, or an empty body auto-emits (`tests/test_lex_actions.c`). |
| `BEGIN(S)` | Emulated | `LEX_TRANSITION(S);` (`tests/test_lex_actions.c`). |
| `yyless(n)` | Emulated | `LEX_PUSHBACK(matched_len - n)`; the arithmetic is reversed (`tests/test_lex_m3_integration.c`). |
| `yyterminate()` | Emulated | `LEX_TERMINATE();` (`tests/test_lex_terminate_frame.c`). |
| `yytext` / `yyleng` | Emulated | `matched` / `matched_len` inside the action (`tests/test_lex_buf.c`, used in `test_lex_buf_grammar.lex`). |
| `ECHO`, `unput()`, `input()`, `yymore()`, `REJECT` | Unsupported | No equivalents. `REJECT` and `yymore` are rejected by design (docs/MIGRATION_FROM_FLEX.md). In the corpus: yyless 8, unput/input 8, ECHO 4, yymore 1, REJECT 1. |
| `yy_push_state` / `yy_pop_state` | Unsupported | 2/22 corpus scanners. |
| `%lexer_extra_argument` (Lime) vs `yyextra` | Emulated | `tests/test_lex_extra_arg.c`. |

## Generated API (`--target=c:flex`)

| Flex feature | Status | Test / notes |
|---|---|---|
| `int yylex(void)` | Emulated | `tests/test_skin_flex.c`: the same rule and text sequence as the native runtime; returns `rule_id + 1`, and 0 at EOF. |
| `yytext` / `yyleng` | Supported | `tests/test_skin_flex.c`. |
| `yylineno` | Supported | `tests/test_skin_flex.c` (3 after `"a\nb\nc"`). |
| Unmatched input | Emulated | Returns `YY_FLEX_INVALID` and skips one byte (`tests/test_skin_flex.c`). |
| `yy_scan_string()` | Supported | `tests/test_skin_flex.c`. |
| `yy_scan_buffer()`, `yy_create_buffer()`, `yy_switch_to_buffer()`, `yy_delete_buffer()`, `YY_BUFFER_STATE` | Unverified | Emitted (`src/lex/emit_c_skin_flex.c`), but no test calls them. |
| `yyin` / `yyout` | Unverified | Declared. The skin does not read `yyin` implicitly; use `yy_create_buffer(yyin, 0)`. |
| `yywrap()` | Unverified | Weak default that returns 1; not exercised by a test. |
| `BEGIN(state)` | Unverified | Expands to `yy_set_state()`; not exercised by a test. |
| Action bodies (`ECHO`, `yyless`, `REJECT`, `yymore`, `return`) | Unsupported | The skin does not run action bodies at all. |
| `yy_push_state` stack, multiple buffers | Unsupported | |
| `%option reentrant` / `bison-bridge` / `yylval` | Unsupported | The native lexer is reentrant. The skin's API is the non-reentrant flex one. |
| `%lexer_extra_argument` grammars | Rejected | The skin refuses to emit and names the directive (checked by hand: exit 2). |

## Porting guidance

- Rewrite the scanner in Lime `.lex` syntax (docs/MIGRATION_FROM_FLEX.md has
  the directive map and two PostgreSQL worked examples). There is no converter.
- If actions use `REJECT`, `yymore`, `unput`/`input()` or `ECHO`, restructure
  them with states, `LEX_PUSHBACK` or literal buffers first.
- Only use the flex skin when callers need `yylex()` / `yytext`. Its token
  codes are rule ids, not the token codes the actions returned.
