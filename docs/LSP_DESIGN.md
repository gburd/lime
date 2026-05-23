# Lime Language Server (LSP) — Design Sketch

**Status:** design only.  This document describes what an LSP
server for `.lime` files would do; **no implementation exists in
this repo today.**  Stub `editors/lime-mode.el` and
`editors/lime.vim` already provide font-lock-only support;
go-to-definition, hover, completion, and live diagnostics are
the LSP's job.

## Why an LSP

Editor support for `.lime` files today goes as far as syntax
highlighting (the editor mode files in `editors/`).  An LSP would
add:

  * **Go-to-definition** for non-terminal references -- click a
    rule name on the RHS of a production, jump to its LHS
    definition.
  * **Hover** with directive documentation (the `%name`,
    `%token_prefix`, `%expect`, etc. set), inline rule signatures,
    and aliases.
  * **Diagnostics in real time** -- run `lime -L` (already
    implemented in the CLI) on save and surface its messages as
    LSP `Diagnostic`s.
  * **Completion** for directive names, declared token codes, and
    in-scope non-terminal symbols.
  * **Document symbols / outline** -- mirror what the existing
    `imenu` integration in `lime-mode.el` already shows.
  * **Find references** for a non-terminal: every place it appears
    on a RHS.
  * **Rename symbol** for a non-terminal (rename LHS *and* every
    RHS reference + every alias-bound action body it appears in).
  * **Semantic tokens** -- richer highlighting than regex-based
    syntax modes can do (e.g. distinguish terminals from
    non-terminals based on whether they are declared with `%token`).

## Implementation sketch

### Process model

Standard LSP:

```
editor  <--JSON-RPC-->  lime-lsp  <--exec-->  lime -L --json
                            \
                             \--->  in-memory grammar model
```

`lime-lsp` is a small standalone binary in C (~1500-3000 lines) or
in Rust if we want to leverage `tower-lsp`.  C keeps the
single-file-friendly story; Rust gets us mature LSP plumbing for
free.

### Initial protocol surface (Phase 1)

  * `initialize` / `initialized` / `shutdown` / `exit`
  * `textDocument/didOpen`, `didChange`, `didClose`, `didSave`
  * `textDocument/publishDiagnostics`
  * `textDocument/hover`
  * `textDocument/definition`
  * `textDocument/completion`
  * `textDocument/documentSymbol`

### Diagnostics flow

Diagnostics are the easiest win and the most useful UX:

  1. On `didChange`, debounce 500 ms.
  2. Spawn `lime -L --json /tmp/<doc>.lime` against the buffer
     contents.
  3. Parse the JSON output (the `--json` flag does not exist yet;
     it would be a small `lime.c` patch to emit machine-readable
     diagnostics in the same shape humans see).
  4. Translate to LSP `Diagnostic` objects, publish.

Estimated effort to reach a working Phase 1: **3-5 days for one
focused engineer**, including end-to-end testing in VS Code, Neovim,
and Emacs (via `eglot`).

### Phase 2 (after Phase 1 ships)

  * Find-references, rename-symbol -- requires a persistent
    grammar model in the LSP, not just spawning `lime` per request.
  * Semantic tokens.
  * Code actions: "extract this rule to a separate file" (using
    `%import` / `%require` directives).

## Why this is not in this repo today

LSP work is its own multi-day project and would not have been a
responsible thing to ship inside the larger session this design
came out of.  Treating it as a follow-up keeps the headline
deliverables (parser correctness, JIT scaling, COBOL example,
Bayesian disambiguation, benchmark wins) honest.

If anyone wants to pick this up, the natural starting point is to
add `--json` to `lime -L` and prototype `lime-lsp` against that.
