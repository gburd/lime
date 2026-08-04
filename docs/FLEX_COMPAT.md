# Flex compatibility matrix (`--target=c:flex`)

`lime -X --target=c:flex foo.lex` emits a **flex-shaped C scanner skin**
(`<basename>_flex.h` / `<basename>_flex.c`) alongside the standard Lime
lexer output, to minimise action-body and call-site churn when porting a
flex scanner. It is **not** a drop-in replacement for `lex.yy.c`.

Two distinct things live in this repo, do not confuse them:

- **Lime's native lexer** (`lime -X`) is push-driven, reentrant, and
  zero-globals by construction: no `yytext` / `yyleng` / `yylineno` side
  channels. This is the recommended target for new code.
- **The flex skin** (`--target=c:flex`) is a thin *pull-driven* adapter
  over Lime's longest-match DFA that DOES expose `yytext`, `yyleng`,
  `yylineno`, `yy_scan_string`, etc., for porting. It bypasses Lime action
  bodies and calls the lower-level `<Prefix>_match()` primitive directly.

This table is grounded in the skin's own design notes
(`src/lex/emit_c_skin_flex.c`, module header). Rows marked **Unverified**
have not been confirmed against a passing test in this tree.

Legend: **Supported** · **Emulated** · **Rejected** (skin refuses to emit
with a diagnostic) · **Unsupported** · **Unverified**.

| Flex feature | Status | Notes / Lime equivalent |
|---|---|---|
| `int yylex(void)` | Emulated | Pull driver over `<Prefix>_match()`; returns `rule_id + 1` (0 reserved for EOF). |
| `yytext` / `yyleng` | Supported | Set to the matched bytes on each `yylex()` call. |
| `yylineno` | Supported | Updated by counting `'\n'` in each match. |
| `yyin` / `yyout` | Supported | Emitted in the flex public surface. |
| `yy_scan_string()` | Supported | Copies the string into a fresh buffer and switches to it. |
| `yy_scan_buffer()` | Supported | Adopts the caller's buffer. |
| `yy_create_buffer()` | Supported | Slurps a `FILE *` into a fresh allocation. |
| `yy_switch_to_buffer()` / `yy_delete_buffer()` | Supported | Single-buffer swap; `owns` flag governs free. |
| `YY_BUFFER_STATE` | Supported | Tiny `yy_buffer_state` (`bytes`, `len`, `pos`, `owns`). |
| `yywrap()` | Emulated | Weak default returns 1 (stop); consumers override with a strong definition. |
| `BEGIN(state)` / start conditions | Emulated | `BEGIN` expands to `yy_set_state(state)`; state constants from the standard `<basename>_lex.h`. |
| `yy_push_state` / `yy_pop_state` / `yy_top_state` (start-condition stack) | Unsupported | Single-buffer / single-state only (as of v0.9.3). |
| `unput()` / `input()` | Unverified | Not documented in the skin header. |
| `yyless()` | Unsupported | Flex action-side macro not honoured (skin bypasses action bodies). |
| `yyrestart()` | Unverified | Not documented in the skin header. |
| `ECHO` | Unsupported | Action-side macro not honoured. |
| `REJECT` | Unsupported | Not honoured; PG flex audit (`docs/LEXER_SCANNER_AUDIT.md`) found zero uses. |
| `yymore` | Unsupported | Not honoured; audit found zero uses. |
| `<<EOF>>` rules | Emulated | Lime has `%eof`; flex syntax differs. Workaround: `yywrap()` returns non-zero. |
| Trailing context `r/s` | Unverified | Not documented in the skin header. |
| `^` / `$` anchors | Unverified | Not documented in the skin header. |
| `%option reentrant` + `%option bison-bridge` | Unverified | Lime native lexer is reentrant; skin bridge to bison unconfirmed. |
| `%option yylineno` | Supported | `yylineno` maintained (see above). |
| `%option case-insensitive` | Unverified | Not documented in the skin header. |
| `%option full` / `%option fast` | N/A | Table-generation options with no skin-visible effect. |
| `%lexer_extra_argument` grammars | Rejected | Skin refuses to emit (no flex-API equivalent for the threaded parameter). |

## Porting guidance

- Action bodies that rely on `REJECT`, `yymore`, `yyless`, or `ECHO` must
  be rewritten; the skin bypasses Lime action bodies entirely.
- Scanners needing a start-condition STACK (not just `BEGIN`) must be
  ported to Lime's native `LexFeedBytes` runtime.
- The PostgreSQL flex audit (six scanners, ~5,300 lines) that found zero
  uses of REJECT / `yymore` / captures / assertions is published as
  `docs/LEXER_SCANNER_AUDIT.md`.
- See `docs/MIGRATION_FROM_FLEX.md` for the narrative porting guide.

## Status of this matrix

The **Unverified** rows are the honest state: the skin header documents
what it emits, but this tree does not yet ship a per-row conformance test
or a byte-for-byte token-stream comparison against real flex output. Do
not read **Unverified** as **Supported**.
