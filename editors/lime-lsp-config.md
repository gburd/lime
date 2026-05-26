# Lime LSP -- editor configuration recipes

The Lime LSP server is `lime-lsp`, installed alongside `lime` by
the standard meson build (`meson install`) under
`${prefix}/bin/lime-lsp`.  See `docs/LSP.md` for the capability
surface.

This document is a paste-ready reference for the three most
common editor stacks.  Every recipe assumes:

  * `lime-lsp` is on `$PATH`, OR
  * you point your editor's LSP client at the absolute path of
    the binary.

## Emacs

### Built-in `eglot` (Emacs 29+)

```elisp
(require 'eglot)
(require 'lime-mode)  ; ships in editors/lime-mode.el

(add-to-list 'eglot-server-programs '(lime-mode . ("lime-lsp")))

(add-hook 'lime-mode-hook #'eglot-ensure)
```

If you keep your `lime` binary somewhere non-standard, point the
server at it explicitly:

```elisp
(add-to-list 'eglot-server-programs
             '(lime-mode . ("lime-lsp" "--lime" "/opt/lime/bin/lime")))
```

`eglot` will surface diagnostics, hover, go-to-definition
(`xref-find-definitions`, default `M-.`), and the outline through
`imenu` / `consult-imenu`.

### `lsp-mode`

```elisp
(require 'lsp-mode)
(require 'lime-mode)

(add-to-list 'lsp-language-id-configuration '(lime-mode . "lime"))

(lsp-register-client
 (make-lsp-client
  :new-connection  (lsp-stdio-connection
                    (lambda () (list "lime-lsp")))
  :major-modes     '(lime-mode)
  :server-id       'lime-lsp
  :priority        0))

(add-hook 'lime-mode-hook #'lsp-deferred)
```

To pass `--lime` or `--log` arguments, return them from the
lambda:

```elisp
:new-connection
  (lsp-stdio-connection
   (lambda () (list "lime-lsp"
                    "--lime" "/opt/lime/bin/lime"
                    "--log"  "/tmp/lime-lsp.log")))
```

## Neovim

### `nvim-lspconfig`

`nvim-lspconfig` does not ship a built-in `lime` server entry
yet; register one manually in your `init.lua`:

```lua
local lspconfig    = require('lspconfig')
local configs      = require('lspconfig.configs')
local util         = require('lspconfig.util')

if not configs.lime_lsp then
  configs.lime_lsp = {
    default_config = {
      cmd          = { 'lime-lsp' },
      filetypes    = { 'lime' },
      single_file_support = true,
      root_dir     = function(fname)
        return util.root_pattern('.git', '*.lime')(fname)
               or vim.fn.fnamemodify(fname, ':p:h')
      end,
      settings     = {},
    },
  }
end

lspconfig.lime_lsp.setup({
  -- on_attach, capabilities, etc.
})
```

You also need a filetype mapping so Neovim recognises `.lime`
files:

```lua
vim.filetype.add({
  extension = { lime = 'lime' },
})
```

Diagnostics, hover (`K`), go-to-definition (`gd`), and outline
(`require('telescope.builtin').lsp_document_symbols()`) work out
of the box once `setup` runs.

### Pointing at a custom `lime`

Pass `cmd` arguments through `setup`:

```lua
lspconfig.lime_lsp.setup({
  cmd = { 'lime-lsp', '--lime', '/opt/lime/bin/lime' },
})
```

## VS Code

There is no published Lime extension yet.  To wire `lime-lsp`
into VS Code locally, scaffold a minimal extension that points at
the binary:

```typescript
// src/extension.ts
import * as path from 'path';
import { workspace, ExtensionContext } from 'vscode';
import {
  LanguageClient, LanguageClientOptions, ServerOptions,
  TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate(context: ExtensionContext) {
  const serverOptions: ServerOptions = {
    command: 'lime-lsp',
    transport: TransportKind.stdio,
    args: [],
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: 'file', language: 'lime' }],
    synchronize: {
      fileEvents: workspace.createFileSystemWatcher('**/*.lime'),
    },
  };

  client = new LanguageClient(
    'lime-lsp',
    'Lime Language Server',
    serverOptions,
    clientOptions,
  );
  client.start();
}

export function deactivate(): Thenable<void> | undefined {
  if (!client) return undefined;
  return client.stop();
}
```

Add the language to `package.json`:

```json
{
  "name": "lime-lsp",
  "engines": { "vscode": "^1.80.0" },
  "activationEvents": ["onLanguage:lime"],
  "main": "./out/extension.js",
  "contributes": {
    "languages": [{
      "id": "lime",
      "extensions": [".lime"],
      "aliases": ["Lime"]
    }]
  },
  "dependencies": {
    "vscode-languageclient": "^9.0.1"
  }
}
```

Run with `npx vsce package` to produce a `.vsix`, then
`code --install-extension lime-lsp-<version>.vsix`.

If `lime-lsp` is not on `$PATH`, replace the `command` field with
the absolute path.

## Helix

Helix has built-in LSP support with a config-only registration.
Add to `~/.config/helix/languages.toml`:

```toml
[[language]]
name = "lime"
scope = "source.lime"
file-types = ["lime"]
roots = []
language-servers = ["lime-lsp"]
indent = { tab-width = 4, unit = "    " }

[language-server.lime-lsp]
command = "lime-lsp"
```

## Verification

After wiring up, open a `.lime` file and check:

  1. **Diagnostics:** introduce a `%type unused {int}` at the top
     of the file with no matching rule; you should see a yellow
     squiggle on the first column with the message
     `non-terminal 'unused' has type but no rules`.
  2. **Definition:** put the cursor on a symbol on a rule's RHS
     and trigger go-to-definition.  The cursor should jump to
     the LHS line of the defining rule.
  3. **Hover:** hover on `%token`; you should see the directive
     description.  Hover on a non-terminal; you should see its
     kind, declaration line, and reference count.
  4. **Outline:** open the document outline; you should see
     directives followed by terminals followed by non-terminals.

If any of these fail, run the server with `--log /tmp/lime-lsp.log`
and inspect the file -- it captures every method name the server
sees.

## Logs and diagnostics

`lime-lsp --log <path>` appends a one-line-per-message trace to
`<path>`.  This is intended for debugging editor wire-ups, not
for general use.

`lime-lsp --version` prints the server's version (matches the
`lime` package version it shipped with).
