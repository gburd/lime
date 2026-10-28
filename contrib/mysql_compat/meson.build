# Meson build for MySQL SQL Compatibility Extension
#
# Builds:
#   1. libmysql_compat.a  - Static library
#   2. libmysql_compat.so - Shared library (plugin)
#   3. test_mysql         - Test executable
#
# Usage:
#   meson setup builddir
#   meson compile -C builddir

# Generate parser from Lime grammar
mysql_grammar = custom_target('mysql_grammar',
  input : 'mysql_grammar.lime',
  output : ['mysql_grammar.c', 'mysql_grammar.h'],
  command : [lime_exe, '-T' + meson.project_source_root() / 'limpar.c', '@INPUT@', '-d@OUTDIR@'],
)

mysql_sources = [
  'mysql_semantics.c',
  'mysql_compat.c',
  mysql_grammar,
]

# Static library
mysql_compat_lib = static_library('mysql_compat',
  mysql_sources,
  include_directories : [inc, include_directories('.')],
  dependencies : [lime_parser_dep],
  install : false,
)

# Shared library (for dynamic plugin loading)
mysql_compat_shared = shared_library('mysql_compat',
  mysql_sources,
  include_directories : [inc, include_directories('.')],
  dependencies : [lime_parser_dep],
  install : false,
)

# Test executable
test_mysql_exe = executable('test_mysql',
  'test_mysql_compat.c',
  include_directories : [inc, include_directories('.')],
  link_with : mysql_compat_lib,
  dependencies : [lime_parser_dep],
  install : false,
)

test('mysql_compat', test_mysql_exe)
