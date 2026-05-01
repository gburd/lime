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
