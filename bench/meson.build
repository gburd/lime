# Benchmark executables

# libm: present on POSIX (-lm), part of the CRT on Windows.  Use a
# meson find_library so the build is portable; on Windows the result
# is .found() == False and we just don't add anything to link_args.
math_dep = cc.find_library('m', required : false)

parser_bench = executable('parser_bench',
  'parser_bench.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

jit_comparison = executable('jit_comparison',
  'jit_comparison.c',
  include_directories : include_directories('../src'),
  dependencies : [lime_parser_dep, math_dep],
)

extension_overhead = executable('extension_overhead',
  'extension_overhead.c',
  include_directories : include_directories('../src'),
  dependencies : [lime_parser_dep, math_dep],
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
  include_directories : include_directories('../src'),
  dependencies : [lime_parser_dep, math_dep],
)


# Optional Flex/Bison comparison harness (skipped at configure time
# when flex or bison is unavailable -- see subdir's meson.build).
flex_prog  = find_program('flex',  required : false)
bison_prog = find_program('bison', required : false)
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

# GLR overhead benchmark.  Drives the same unambiguous arith input
# through both the LALR push parser and the GLR engine; reports the
# fixed overhead of the GSS allocation + per-token step machinery.
# Resurrected from feat/glr branch in v0.3.4.
glr_overhead = executable('glr_overhead',
  'glr_overhead.c',
  bench_arith_gen,
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

# ----------------------------------------------------------------------
# Larger PG-class grammar comparison: bench_grammar.{y,lime}
#
# 250-rule SQL-shaped expression language, the largest in-tree apples-
# to-apples comparison between Lime and Bison.  bench_runtime.c is
# compiled twice (-DUSE_LIME / -DUSE_BISON) and runs an identical pre-
# tokenized SQL-ish stream through each parser.  Skipped when bison or
# flex is unavailable.  Was previously orphaned (bench_runtime.c +
# bench_grammar.{y,lime} all existed but never wired) -- v0.4.4 perf-
# audit pass picked it up.
# ----------------------------------------------------------------------
if flex_prog.found() and bison_prog.found()
  bench_grammar_lime_gen = custom_target('bench_grammar_lime',
    input   : ['bench_grammar.lime'],
    output  : ['bench_grammar.c', 'bench_grammar.h'],
    command : [
      lime_exe,
      '-T' + meson.project_source_root() / 'limpar.c',
      '-d@OUTDIR@',
      '@INPUT@',
    ],
  )

  bench_grammar_bison_gen = custom_target('bench_grammar_bison',
    input   : ['bench_grammar.y'],
    output  : ['bench_grammar_bison.c', 'bench_grammar_bison.h'],
    command : [
      bison_prog,
      '-d',
      '-o', '@OUTPUT0@',
      '@INPUT@',
    ],
  )

  bench_runtime_lime = executable('bench_runtime_lime',
    'bench_runtime.c',
    bench_grammar_lime_gen,
    c_args : ['-DUSE_LIME', '-DLIME_OUT_H="bench_grammar.h"'],
    include_directories : [inc, include_directories('../src', '.')],
  )

  bench_runtime_bison = executable('bench_runtime_bison',
    'bench_runtime.c',
    bench_grammar_bison_gen,
    c_args : ['-DUSE_BISON', '-DBISON_OUT_H="bench_grammar_bison.h"'],
    include_directories : [inc, include_directories('../src', '.')],
  )
endif

