# Meson build for Oracle SQL Compatibility Extension
#
# Builds:
#   1. liboracle_compat.a  - Static library
#   2. liboracle_compat.so - Shared library (plugin)
#   3. test_oracle          - Test executable
#
# Usage:
#   meson setup builddir
#   meson compile -C builddir

# Generate parser from Lime grammar
oracle_grammar = custom_target('oracle_grammar',
  input : 'oracle_grammar.lime',
  output : ['oracle_grammar.c', 'oracle_grammar.h'],
  command : [lime_exe, '@INPUT@', '-d', '@OUTDIR@'],
)

oracle_sources = [
  'oracle_semantics.c',
  'oracle_compat.c',
  oracle_grammar,
]

# Static library
oracle_compat_lib = static_library('oracle_compat',
  oracle_sources,
  include_directories : [inc, include_directories('.')],
  dependencies : [lime_parser_dep],
  install : false,
)

# Shared library (for dynamic plugin loading)
oracle_compat_shared = shared_library('oracle_compat',
  oracle_sources,
  include_directories : [inc, include_directories('.')],
  dependencies : [lime_parser_dep],
  install : false,
)

# Test executable
test_oracle_exe = executable('test_oracle',
  'test_oracle.c',
  include_directories : [inc, include_directories('.')],
  link_with : oracle_compat_lib,
  dependencies : [lime_parser_dep],
  install : false,
)

test('oracle_compat', test_oracle_exe)
