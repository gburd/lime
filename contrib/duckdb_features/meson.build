# Meson build for DuckDB Features Extension
#
# Builds:
#   1. libduckdb_features.a  - Static library
#   2. libduckdb_features.so - Shared library (plugin)
#   3. test_duckdb           - Test executable
#
# Usage:
#   meson setup builddir
#   meson compile -C builddir

# Generate parser from Lime grammar
duckdb_grammar = custom_target('duckdb_grammar',
  input : 'duckdb_grammar.lime',
  output : ['duckdb_grammar.c', 'duckdb_grammar.h'],
  command : [lime_exe, '@INPUT@', '-d', '@OUTDIR@'],
)

duckdb_sources = [
  'duckdb_semantics.c',
  'duckdb_features.c',
  duckdb_grammar,
]

# Static library
duckdb_features_lib = static_library('duckdb_features',
  duckdb_sources,
  include_directories : [inc, include_directories('.')],
  dependencies : [lime_parser_dep],
  install : false,
)

# Shared library (for dynamic plugin loading)
duckdb_features_shared = shared_library('duckdb_features',
  duckdb_sources,
  include_directories : [inc, include_directories('.')],
  dependencies : [lime_parser_dep],
  install : false,
)

# Test executable
test_duckdb_exe = executable('test_duckdb',
  'test_duckdb.c',
  include_directories : [inc, include_directories('.')],
  link_with : duckdb_features_lib,
  dependencies : [lime_parser_dep],
  install : false,
)

test('duckdb_features', test_duckdb_exe)
