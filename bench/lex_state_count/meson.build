# bench/lex_state_count/meson.build
#
# Cross-DFA-size lexer-throughput comparison.  Builds the same
# harness against three lexers covering a range of state counts:
#
#   small  : a tiny json lexer (~30-60 states)
#   medium : the json bench lexer (slightly larger)
#   large  : pg bootscanner (~120 states)
#
# Throughput differences across these tell us where transition-
# table memory cost starts to dominate, which is the precondition
# for prefetch / table-layout optimisations to pay off.
#
# The harness is small (<200 LOC) and shares no infrastructure with
# bench_lex_vectorize -- they answer different questions and the
# decoupling makes it easy to add more lexers later.
#
# Generated lexers are intentionally NOT tracked in git.

# Small json lexer (we already have one in lex_vectorize/).
bench_lsc_small_gen = custom_target('bench_lsc_small_lex',
  input   : ['../lex_vectorize/json_vec.lex'],
  output  : ['json_vec_lex.c', 'json_vec_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

# Medium: rust_compare's json.lex, slightly more complex.
bench_lsc_medium_gen = custom_target('bench_lsc_medium_lex',
  input   : ['../rust_compare/json.lex'],
  output  : ['json_lex.c', 'json_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

# Large: pg bootscanner (~120 states, exceeds L1 trans-table
# capacity once SIMD scratch buffers are accounted for).
bench_lsc_large_gen = custom_target('bench_lsc_large_lex',
  input   : ['../../examples/pg_bootscanner/bootscanner.lex'],
  output  : ['bootscanner_lex.c', 'bootscanner_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

# The %name_prefix in each .lex file determines the C symbol
# prefix.  We have to discover those:
#   json_vec.lex             -> "Jvec"
#   rust_compare/json.lex    -> Whatever its %name_prefix is
#   bootscanner.lex          -> "Boot"
# So the executable below uses the LITERAL prefixes from those
# files.  When adding more variants, check the .lex file's
# %name_prefix directive.

bench_lsc_small = executable('bench_lsc_small',
  ['bench_lex_state_count.c', bench_lsc_small_gen],
  c_args : [
    '-DBENCH_LABEL="small"',
    '-DBENCH_HEADER="json_vec_lex.h"',
    '-DBENCH_PREFIX=Jvec',
    '-DBENCH_PREFIX_UPPER=JVEC',
  ],
  include_directories : include_directories('.'),
  dependencies : [lime_parser_dep],
)

bench_lsc_medium = executable('bench_lsc_medium',
  ['bench_lex_state_count.c', bench_lsc_medium_gen],
  c_args : [
    '-DBENCH_LABEL="medium"',
    '-DBENCH_HEADER="json_lex.h"',
    '-DBENCH_PREFIX=Json',
    '-DBENCH_PREFIX_UPPER=JSON',
  ],
  include_directories : include_directories('.'),
  dependencies : [lime_parser_dep],
)

bench_lsc_large = executable('bench_lsc_large',
  ['bench_lex_state_count.c', bench_lsc_large_gen],
  c_args : [
    '-DBENCH_LABEL="large"',
    '-DBENCH_HEADER="bootscanner_lex.h"',
    '-DBENCH_PREFIX=Boot',
    '-DBENCH_PREFIX_UPPER=BOOT',
  ],
  include_directories : include_directories('.'),
  dependencies : [lime_parser_dep],
)
