# bench_simdjson_compare/meson.build
#
# Steady-state Lime+JIT vs simdjson throughput comparison.
# Skipped at configure time when simdjson is not on the system.

simdjson_dep = dependency('simdjson', required : false)

if simdjson_dep.found()
  message('simdjson comparison benchmark: ENABLED (simdjson ' +
          simdjson_dep.version() + ')')

  # Reuse the JSON example's parser sources.  meson custom_target
  # so the example regenerates if json_grammar.lime changes.
  bench_simdjson_json_gen = custom_target('bench_simdjson_json_parser',
    input   : meson.project_source_root() / 'examples/json/json_grammar.lime',
    output  : ['json_grammar.c', 'json_grammar.h', 'json_grammar_snapshot.c'],
    command : [
      lime_exe,
      '-T' + meson.project_source_root() / 'limpar.c',
      '-n',  # also emit *_snapshot.c so JsonBuildSnapshot is available
      '-d@OUTDIR@',
      '@INPUT@',
    ],
  )

  bench_simdjson_compare = executable('bench_simdjson_compare',
    'bench_simdjson_compare.cpp',
    bench_simdjson_json_gen,
    meson.project_source_root() / 'examples/json/json.h',
    meson.project_source_root() / 'examples/json/json_helpers.c',
    meson.project_source_root() / 'examples/json/json_tokenize.h',
    meson.project_source_root() / 'examples/json/json_tokenize.c',
    include_directories : include_directories(
      '../../examples/json',
      '../../include',
      '../../src',
    ),
    cpp_args : ['-std=c++17'],
    dependencies : [lime_parser_dep, simdjson_dep],
    link_args : ['-lm'],
  )
else
  message('simdjson comparison benchmark: SKIPPED ' +
          '(install simdjson to enable)')
endif
