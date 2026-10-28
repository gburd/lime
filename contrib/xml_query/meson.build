# Meson build for the XML Query extension (XQuery/XPath embedded in SQL)
#
# Builds:
#   1. xquery_grammar / xpath_grammar - generated parsers
#   2. xml_query_demo                 - standalone integration demo,
#      run as a smoke test (mirrors the Makefile's `make test`)
#
# Usage:
#   meson setup builddir
#   meson compile -C builddir

xquery_grammar = custom_target('xquery_grammar',
  input : 'xquery_grammar.lime',
  output : ['xquery_grammar.c', 'xquery_grammar.h'],
  command : [lime_exe, '-T' + meson.project_source_root() / 'limpar.c', '@INPUT@', '-d@OUTDIR@'],
)

xpath_grammar = custom_target('xpath_grammar',
  input : 'xpath_grammar.lime',
  output : ['xpath_grammar.c', 'xpath_grammar.h'],
  command : [lime_exe, '-T' + meson.project_source_root() / 'limpar.c', '@INPUT@', '-d@OUTDIR@'],
)

xml_query_demo_exe = executable('xml_query_demo',
  ['xml_query.c', 'xml_semantics.c', xquery_grammar, xpath_grammar],
  include_directories : [inc, include_directories('.')],
  dependencies : [lime_parser_dep],
  c_args : ['-DXML_QUERY_STANDALONE'],
  install : false,
)

test('xml_query', xml_query_demo_exe)
