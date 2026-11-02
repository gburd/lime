# Lime LSP server -- standalone executable.
#
# Diagnostics come from liblime_compiler's in-process lint (below);
# navigation (definition, hover, documentSymbol) from a small private
# tokenizer in lsp_navigation.c.  Formatting still runs `lime -F`.

lime_lsp_sources = files(
  'lsp_main.c',
  'lsp_protocol.c',
  'lsp_documents.c',
  'lsp_diagnostics.c',
  'lsp_diagnostics_async.c',
  'lsp_navigation.c',
  'lsp_completion.c',
  'lsp_references.c',
  'lsp_rename.c',
  'lsp_semantic_tokens.c',
  'lsp_json.c',
  'lsp_format.c',
)

# lime-lsp links liblime_compiler for diagnostics: it lints the open
# buffer in process (lime_lint_grammar_fast_in_process, ~150 ms on PG's
# gram.lime) instead of running `lime -L` in a child process (6-20 s).
# Always linked; there is no build without it.
lime_lsp_deps = [lime_compiler_dep, lime_parser_dep]
# v0.13.0: background-diagnostics worker uses pthread_create.
# thread_dep adds -lpthread on POSIX; on Windows it's a no-op
# and lsp_diagnostics_async.c falls back to synchronous diagnostics.
lime_lsp_deps += [thread_dep]

lime_lsp_exe = executable('lime-lsp',
  lime_lsp_sources,
  dependencies : lime_lsp_deps,
  install : true,
)
