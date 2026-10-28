# Meson build for SQLite Compatibility Grammar Extension
#
# Builds:
#   1. libsqlite_compat.so - Shared library for dynamic loading
#   2. test_sqlite_compat  - Test runner (semantic tests only, does
#      not link the extension registry -- mirrors the Makefile's
#      `make test` target)
#
# Usage:
#   meson setup builddir
#   meson compile -C builddir

sqlite_compat_shared = shared_library('sqlite_compat',
  ['sqlite_compat.c', 'sqlite_semantics.c'],
  include_directories : [inc, include_directories('.')],
  dependencies : [lime_parser_dep],
  install : false,
)

test_sqlite_compat_exe = executable('test_sqlite_compat',
  ['test_sqlite_compat.c', 'sqlite_semantics.c'],
  include_directories : [include_directories('.')],
  install : false,
)

test('sqlite_compat', test_sqlite_compat_exe)
