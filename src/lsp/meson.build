# Lime LSP server -- standalone executable.
#
# `lime-lsp` is intentionally decoupled from the lime parser
# library: it does NOT link `lime_parser_lib` and it does NOT
# include any runtime hot-path source.  Diagnostics are produced
# by exec'ing the `lime` binary; in-process analysis (definition,
# hover, documentSymbol) is backed by a small private tokenizer
# in lsp_navigation.c.
#
# Keeping the LSP a thin shim means it follows `lime` upgrades at
# runtime without a recompile and keeps the perf-gated runtime
# files (parse_engine.c, parse_glr.c, snapshot.c, ...) untouched.

lime_lsp_sources = files(
  'lsp_main.c',
  'lsp_protocol.c',
  'lsp_documents.c',
  'lsp_diagnostics.c',
  'lsp_navigation.c',
  'lsp_completion.c',
  'lsp_references.c',
  'lsp_rename.c',
  'lsp_semantic_tokens.c',
  'lsp_json.c',
)

lime_lsp_exe = executable('lime-lsp',
  lime_lsp_sources,
  install : true,
)
