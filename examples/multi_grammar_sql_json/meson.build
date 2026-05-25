# multi_grammar_sql_json -- SQL host + JSON embedded.
#
# The host SQL recogniser is hand-rolled in multi_driver.c.  The
# embedded JSON parser is generated from
# examples/json/json_grammar.lime via the lime tool.  Both are
# packaged as a static library that:
#
#   - the example binary (multi_grammar_sql_json) wraps in main()
#   - the e2e tests (test_context_switch_e2e + _error) link against
#     to drive multi_parse_sql() directly.

# Generate the JSON parser sources from the existing grammar.
multi_json_gen = custom_target('multi_json_grammar',
  input   : ['../json/json_grammar.lime'],
  output  : ['json_grammar.c', 'json_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

multi_inc = include_directories('.', '../json')

multi_lib = static_library('multi_grammar_sql_json',
  [
    'multi_driver.c',
    'multi_helpers.c',
    '../json/json_helpers.c',
    '../json/json_tokenize.c',
  ],
  multi_json_gen,
  include_directories : [multi_inc, inc, include_directories('../../src')],
  dependencies : [lime_parser_dep],
  c_args : meson.get_compiler('c').get_supported_arguments(
    ['-Wno-unused-parameter']),
)

multi_dep = declare_dependency(
  link_with : multi_lib,
  sources : multi_json_gen,
  include_directories : multi_inc,
)

multi_bin = executable('multi_grammar_sql_json_bin',
  'main.c',
  multi_json_gen,
  include_directories : [multi_inc, inc],
  dependencies : [multi_dep, lime_parser_dep],
  c_args : meson.get_compiler('c').get_supported_arguments(
    ['-Wno-unused-parameter']),
)

# Smoke test that exercises the example binary end-to-end.  The
# in-source asserts in test_context_switch_e2e.c (registered from
# tests/meson.build) verify the AST shape; this run guards the
# example against bit-rot.
test('multi_grammar_sql_json_smoke', multi_bin,
  args : ['SELECT id, json \'{"a":1}\' FROM t WHERE id = 5;'])
