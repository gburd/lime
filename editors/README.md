# Editor support for Lime grammars

Syntax highlighting for `.lime` (and `.lex`) files in Emacs and Vim
/ Neovim.  Highlights:

  * `%`-directives (`%name`, `%token`, `%type`, `%left`, `%right`,
    `%symbol_prefix`, `%expect`, etc. -- 47 directives in total,
    sourced directly from `lime.c`)
  * The `::=` rule production arrow
  * Rule LHS (the non-terminal being defined)
  * Token codes (all-caps identifiers)
  * Symbol aliases like `expr(A)`
  * Precedence markers like `[UMINUS]`
  * Block (`/* ... */`) and line (`//`) comments
  * String and character literals

## Emacs

Copy or symlink `lime-mode.el` somewhere on your `load-path` and
`(require 'lime-mode)`, or with `use-package`:

```elisp
(use-package lime-mode
  :load-path "~/src/lime/editors"
  :mode ("\\.lime\\'" "\\.lex\\'"))
```

`lime-mode` is derived from `prog-mode`, supplies an `imenu` index
of rule LHS names, and uses standard `font-lock` faces so it
inherits sensible defaults from any colorscheme.

## Vim / Neovim

Drop `lime.vim` under `~/.vim/syntax/` (or
`~/.config/nvim/syntax/`) and add the contents of
`ftdetect-lime.vim` to `~/.vim/ftdetect/lime.vim` so Vim
recognises the filetype.  Or do it all in one go:

```bash
mkdir -p ~/.vim/syntax ~/.vim/ftdetect
cp lime.vim          ~/.vim/syntax/lime.vim
cp ftdetect-lime.vim ~/.vim/ftdetect/lime.vim
```

For Neovim users:

```bash
mkdir -p ~/.config/nvim/syntax ~/.config/nvim/ftdetect
cp lime.vim          ~/.config/nvim/syntax/lime.vim
cp ftdetect-lime.vim ~/.config/nvim/ftdetect/lime.vim
```

## Future work

  * **LSP server** -- currently only a design sketch
    (see `docs/LSP_DESIGN.md`).  Would offer go-to-definition for
    rule references, hover docs for directives, diagnostics from
    `lime -L`, completion for directive names and known token
    codes.
  * **Tab-completion code generator** -- emit a parser-state-aware
    completion oracle from a Lime grammar so a CLI built on the
    grammar can offer the same kind of context-sensitive
    completion psql does.  Design sketch in
    `docs/TAB_COMPLETION_DESIGN.md`.
