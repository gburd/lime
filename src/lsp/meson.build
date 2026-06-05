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
  'lsp_diagnostics_async.c',
  'lsp_navigation.c',
  'lsp_completion.c',
  'lsp_references.c',
  'lsp_rename.c',
  'lsp_semantic_tokens.c',
  'lsp_json.c',
  'lsp_format.c',
)

# lime-lsp links liblime_compiler.a so it can call
# lime_compile_grammar_in_process() directly for diagnostics
# (Lime-Letter-27 fix).  This avoids the per-request fork+exec+
# tempfile+cc cycle that made the LSP unusable on PG's gram.lime
# (~21 KLoC, 6-20 s/request -> ~150 ms with the in-process path).
#
# The link uses the weak-symbol fallback declared in
# src/lsp/lsp_diagnostics.c, so a static -Dlime_lsp_in_process=disabled
# build still works (it just falls through to the subprocess path
# the same way the v0.5.0 LSP did).  Default is to link the
# compiler lib because the perf win is huge and the binary-size
# cost (~1.2 MB) is acceptable for a developer tool.
lime_lsp_in_process = get_option('lime_lsp_in_process')
lime_lsp_deps = []
if lime_lsp_in_process.allowed()
  lime_lsp_deps += [lime_compiler_dep, lime_parser_dep]
endif
# v0.13.0: background-diagnostics worker uses pthread_create.
# thread_dep adds -lpthread on POSIX; on Windows it's a no-op
# and lsp_diagnostics_async.c falls back to synchronous diagnostics.
lime_lsp_deps += [thread_dep]

lime_lsp_exe = executable('lime-lsp',
  lime_lsp_sources,
  dependencies : lime_lsp_deps,
  install : true,
)
