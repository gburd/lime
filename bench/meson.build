# Benchmark executables
parser_bench = executable('parser_bench',
  'parser_bench.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

jit_comparison = executable('jit_comparison',
  'jit_comparison.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
  link_args : ['-lm'],  # Link math library for sqrt()
)

extension_overhead = executable('extension_overhead',
  'extension_overhead.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
  link_args : ['-lm'],
)

# jsonb extension example -- compiled as a static archive so it gets
# at least a smoke-build in CI even though no host links it.  This
# proves the example tracks the public extension API.
jsonb_extension_lib = static_library('jsonb_extension_example',
  '../examples/jsonb_extension.c',
  include_directories : [inc, include_directories('../src')],
  build_by_default : true,
)

# Real-parser JIT benchmark.  Generates a small arithmetic parser via
# the same `lime` invocation users get in examples/*, then compares
# parse time with and without lime_jit_compile().  Forensic, not a
# regression test.
bench_arith_gen = custom_target('bench_arith_parser',
  input   : ['bench_arith_grammar.y'],
  output  : ['bench_arith_grammar.c', 'bench_arith_grammar.h',
             'bench_arith_grammar_snapshot.c'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-n',                              # also emit *_snapshot.c
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

# JSON parser tables for the same head-to-head comparison harness.
# Real-world workload: object/array nesting, multiple value types,
# key/value pairs.  ~16 rules, 11 terminals -- a few times bigger
# than the arithmetic grammar, exercises the parser's nesting
# state-machine paths the arithmetic test cannot.
bench_json_gen = custom_target('bench_json_parser',
  input   : ['bench_json_grammar.y'],
  output  : ['bench_json_grammar.c', 'bench_json_grammar.h',
             'bench_json_grammar_snapshot.c'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-n',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

bench_jit_real_parser = executable('bench_jit_real_parser',
  'bench_jit_real_parser.c',
  bench_arith_gen,
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
  link_args : ['-lm'],
)

# GLR overhead benchmark.  Drives the same unambiguous arith input
# through both the LALR push parser and the GLR engine; reports the
# fixed overhead of the GSS allocation + per-token step machinery.
glr_overhead = executable('glr_overhead',
  'glr_overhead.c',
  bench_arith_gen,
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)


# Optional Flex/Bison comparison harness (skipped at configure time
# when flex or bison is unavailable -- see subdir's meson.build).
subdir('bench_flex_bison_compare')

# Optional simdjson throughput comparison (skipped when simdjson
# is not on the system).
subdir('bench_simdjson_compare')

# Microbenchmark proving the SIMD character-classification path
# delivers measurable speedup over the scalar fallback.  This is
# the load-bearing measurement that the README's SIMD-acceleration
# claim depends on; if it ever stops being faster the claim should
# be removed (see bench/bench_simd_classify.c for the reasoning).
bench_simd_classify = executable('bench_simd_classify',
  'bench_simd_classify.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)
