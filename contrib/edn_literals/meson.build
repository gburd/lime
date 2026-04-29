# Meson build for EDN Literals Extension
#
# Builds:
#   1. libedn_literals.a  - Static library
#   2. libedn_literals.so - Shared library (plugin)
#   3. test_edn           - Test executable
#
# Usage:
#   meson setup builddir
#   meson compile -C builddir

# Generate parser from Lime grammar
edn_grammar = custom_target('edn_grammar',
  input : 'edn_grammar.lime',
  output : ['edn_grammar.c', 'edn_grammar.h'],
  command : [lime_exe, '@INPUT@', '-d', '@OUTDIR@'],
)

edn_sources = [
  'edn_semantics.c',
  'edn_literals.c',
  edn_grammar,
]

# Static library
edn_literals_lib = static_library('edn_literals',
  edn_sources,
  include_directories : [inc, include_directories('.')],
  dependencies : [lime_parser_dep],
  install : false,
)

# Shared library (for dynamic plugin loading)
edn_literals_shared = shared_library('edn_literals',
  edn_sources,
  include_directories : [inc, include_directories('.')],
  dependencies : [lime_parser_dep],
  install : false,
)

# Test executable
test_edn_exe = executable('test_edn',
  'test_edn.c',
  include_directories : [inc, include_directories('.')],
  link_with : edn_literals_lib,
  dependencies : [lime_parser_dep],
  install : false,
)

test('edn_literals', test_edn_exe)
