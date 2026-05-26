# Lime Language Server (`lime-lsp`)

`lime-lsp` is a Language Server Protocol server for `.lime` grammar
files.  It speaks LSP 3.17 (subset) over stdio and is a separate
binary from the `lime` parser generator: `lime-lsp` shells out to
`lime` for diagnostics and runs an in-process tokenizer for
navigation features.

This document describes the implementation that ships in v0.5.0.
`docs/LSP_DESIGN.md` is the original design sketch from v0.2.x;
the design split out two "phases", and Phase 1 is what landed
here.

## Capabilities

| Method                                  | Status | Notes                                          |
|-----------------------------------------|--------|------------------------------------------------|
| `initialize` / `initialized`            | done   | advertises ServerCapabilities                  |
| `shutdown` / `exit`                     | done   | clean lifecycle, exit code per spec            |
| `textDocument/didOpen`                  | done   | full-text sync, kicks off lint                 |
| `textDocument/didChange`                | done   | `TextDocumentSyncKind.Full`; relints           |
| `textDocument/didSave`                  | done   | re-runs diagnostics                            |
| `textDocument/didClose`                 | done   | clears document state, pushes empty diags     |
| `textDocument/publishDiagnostics`       | done   | server -> client notification                  |
| `textDocument/definition`               | done   | non-terminal RHS -> LHS jump                   |
| `textDocument/hover`                    | done   | directive docs + symbol info                   |
| `textDocument/documentSymbol`           | done   | flat outline; directives + terminals + rules   |
| `textDocument/completion`               | -      | deferred                                       |
| `textDocument/references`               | -      | deferred                                       |
| `textDocument/rename`                   | -      | deferred                                       |
| `textDocument/semanticTokens/full`      | -      | deferred (regex modes are good enough today)   |
| `textDocument/codeAction`               | -      | deferred (waits on linter auto-fix surface)    |
| `workspace/symbol`                      | -      | deferred (single-file MVP)                     |

The `ServerCapabilities` object the server returns at `initialize`
time is the authoritative list:

```json
{
  "textDocumentSync": { "openClose": true, "change": 1, "save": true },
  "definitionProvider": true,
  "hoverProvider": true,
  "documentSymbolProvider": true
}
```

## Process model

```
editor  <--JSON-RPC over stdio-->  lime-lsp  <--fork+exec-->  lime -L /tmp/<doc>.lime
                                       |
                                       \--->  in-process .lime tokenizer
                                              (definition, hover, outline)
```

**Why shell out for diagnostics?**  The `lime -L` linter already
produces every warning and error a developer would see on the
command line.  Re-implementing that pass inside `lime-lsp` would
duplicate the parser and lint code, and force the LSP to chase
every grammar-feature change in lockstep.  Forking is fine at LSP
timing scales: the lint pass is sub-millisecond on grammars up to
several thousand rules, and the user-visible debounce on
`didChange` (250 ms in the wire-up recipes) dwarfs the fork cost.

**Why an in-process tokenizer for navigation?**  `definition`,
`hover`, and `documentSymbol` all need the same simple symbol
table -- "what's the LHS line for this name", "is this a terminal
or a non-terminal" -- and they need it on every keystroke.
Spawning a subprocess for each would multiply latency by 100x
without buying anything.  The tokenizer in `src/lsp/lsp_navigation.c`
is intentionally tiny (~250 lines) and is rebuilt per request; it
recognises directives, rule LHS / RHS, terminal declarations
(`%token`), brace-balanced action blocks, and string / comment
trivia.  It does not produce a parse tree -- it does not need one.

## Wire format

JSON-RPC 2.0 framed with HTTP-style headers:

```
Content-Length: <N>\r\n
\r\n
<N bytes of UTF-8 JSON>
```

The bundled JSON parser (`src/lsp/lsp_json.[ch]`) is hand-rolled
in ~600 lines.  We do not link cJSON / jansson; LSP messages are
small and well-structured, and adding a runtime dependency for
this much code is not justified.  The parser handles UTF-16
surrogate pairs in `\uXXXX` escapes, integer round-tripping
through double, and the small set of escape sequences LSP
generates in practice.

## Diagnostics flow

1. On `didOpen` / `didChange` / `didSave`, the server invokes
   `lsp_diagnostics_run`.
2. The current document text is written to a temp file under
   `$TMPDIR` via `mkstemps`.
3. The server forks; the child execvp's `lime -L <tmp>` with
   stdout redirected to `/dev/null` and stderr piped back to the
   parent.
4. The parent reads the captured stderr, parses each line as
   either:
     * `<path>:<line>:<col>: <severity>: <message>` (lint output)
     * `<path>:<line>: <message>` (parser-stage `ErrorMsg`)
5. Each parsed line becomes an LSP `Diagnostic` with `severity`
   mapped from the textual severity word (error -> 1, warning ->
   2, info/note -> 3, hint -> 4).
6. The server emits `textDocument/publishDiagnostics`.

The `lime` binary is found in this order:
  1. `--lime PATH` argument
  2. `$LIME_BIN` environment variable
  3. literal `lime` resolved on `PATH`

## Definition / hover / documentSymbol

Each of these operates over a per-request symbol table built by
`lsp_symtab_build`:

  * Walk the document buffer with a tokenizer that recognises
    directives (`%name`, `%token`, `%type`, ...), rule LHS
    (`IDENT ::=`), and rule RHS references.
  * Mark each `%token NAME ...` declaration as a terminal
    definition.
  * Mark each `IDENT ::=` as a non-terminal definition.
  * Count every other use as a reference.

`textDocument/definition` returns the LHS location of whichever
non-terminal the cursor sits on, or the `%token` declaration site
for a terminal.

`textDocument/hover` returns:
  * for a `%directive`, a one-line documentation string (see the
    `kDirectiveDocs` table in `src/lsp/lsp_navigation.c` for the
    full set);
  * for a symbol, its kind (terminal / non-terminal), the line
    where it is declared, and a usage count.

`textDocument/documentSymbol` returns a flat list of every
declared directive, terminal, and non-terminal in the document.
Within the response we order directives first, then terminals,
then non-terminals; within each group the order is the order they
appear in the file.

Symbol kinds:
  * directive  -> `Key` (20)
  * terminal   -> `Constant` (14)
  * nonterm    -> `Function` (12)

(LSP does not have a "grammar rule" SymbolKind; `Function` is the
closest analogue and is what tree-sitter's grammar mode in
VS Code uses too.)

## Source layout

```
src/lsp/
  lsp_main.c          # entry point, argument parsing, env wiring
  lsp_protocol.[ch]   # JSON-RPC 2.0 framing + LSP method dispatch
  lsp_documents.[ch]  # in-memory open-document table
  lsp_diagnostics.[ch]# fork+exec lime, parse stderr to Diagnostic[]
  lsp_navigation.[ch] # in-process tokenizer + symbol table +
                      # definition / hover / documentSymbol handlers
  lsp_json.[ch]       # minimal JSON parser/serializer
  meson.build         # standalone executable, no library deps
```

The LSP target is intentionally **decoupled** from
`lime_parser_lib`: it does not link any runtime hot-path code and
does not include any header that would force the LSP to
recompile when the parser internals change.  That is what keeps
the runtime perf gate (`parse_engine.c`, `parse_glr.c`,
`snapshot.c`, ...) honest: every byte the LSP ships is in
`src/lsp/`.

## Editor wire-up

See `editors/lime-lsp-config.md` for copy-paste recipes for
Emacs `lsp-mode` / `eglot`, Neovim `lspconfig`, and a VS Code
extension stub.

## Limitations and future work

* **Single document at a time.**  Definition, hover, and outline
  do not cross file boundaries.  `%import` / `%require` between
  modules is not followed.  Workspace symbol search is deferred.
* **Full-text sync only.**  We do not yet implement
  `TextDocumentSyncKind.Incremental`.  At LSP message sizes
  typical for grammar files (a few KB to ~100 KB), the cost
  difference is in the noise; we will revisit if profiling
  flags it.
* **No completion.**  Adding it would force the in-process
  tokenizer to handle partial / mid-edit input gracefully -- a
  meaningful project on its own.
* **No semantic tokens.**  The regex-based highlighting in
  `editors/lime-mode.el` covers the same cases at zero
  configuration cost.
* **No code actions.**  These pair with the linter's auto-fix
  surface; they will land alongside the linter expansion in a
  later release.
* **POSIX-only.**  `fork` / `execvp` / `pipe` are required for
  the diagnostics path.  On Windows, run the server inside WSL.
  A native Win32 implementation is a future-work item.

## Versioning

`lime-lsp` ships its version in `serverInfo.version` of the
`initialize` response, taken from `LIME_VERSION_STRING` at build
time.  Editors that pin a server version can read it from there.
