# bench/lex_vectorize/meson.build -- C-emit SIMD throughput
# benchmark.  Compiles the same harness twice (one with the
# default --lex-vectorize emit, one with --lex-no-vectorize)
# and exposes both binaries so a side-by-side comparison can be
# run against the same fixture.
#
# The two .lex sources (json_vec.lex / json_novec.lex) are byte-
# identical except for %name_prefix; that lets both generators
# write into the same OUTDIR without colliding on output names.
#
# Generated lexers are intentionally NOT tracked in git -- they
# regenerate from the .lex sources on every build.

bench_lex_vec_gen = custom_target('bench_lex_vec_lex',
  input   : ['json_vec.lex'],
  output  : ['json_vec_lex.c', 'json_vec_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

bench_lex_novec_gen = custom_target('bench_lex_novec_lex',
  input   : ['json_novec.lex'],
  output  : ['json_novec_lex.c', 'json_novec_lex.h'],
  command : [
    lime_exe,
    '-X',
    '--lex-no-vectorize',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

# Vectorised harness binary: BENCH_HEADER -> json_vec_lex.h,
# BENCH_PREFIX -> Jvec, BENCH_PREFIX_UPPER -> JVEC.
bench_lex_vec = executable('bench_lex_vec',
  ['bench_lex_vectorize.c', bench_lex_vec_gen],
  c_args : [
    '-DBENCH_LABEL="lex+simd"',
    '-DBENCH_HEADER="json_vec_lex.h"',
    '-DBENCH_PREFIX=Jvec',
    '-DBENCH_PREFIX_UPPER=JVEC',
  ],
)

# Non-vectorised harness binary: BENCH_HEADER -> json_novec_lex.h,
# BENCH_PREFIX -> Jnovec, BENCH_PREFIX_UPPER -> JNOVEC.
bench_lex_novec = executable('bench_lex_novec',
  ['bench_lex_vectorize.c', bench_lex_novec_gen],
  c_args : [
    '-DBENCH_LABEL="lex+scalar"',
    '-DBENCH_HEADER="json_novec_lex.h"',
    '-DBENCH_PREFIX=Jnovec',
    '-DBENCH_PREFIX_UPPER=JNOVEC',
  ],
)
