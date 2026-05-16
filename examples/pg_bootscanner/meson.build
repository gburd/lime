# M5.1: PG bootscanner.l port end-to-end test.
#
# Drives the .lex -> .c+.h codegen pipeline against the
# real-PG-shape grammar in examples/pg_bootscanner/, then
# compiles + runs a driver that asserts on the token stream
# the original flex scanner would have produced.  Mirrors the
# wiring of tests/test_lex_runtime, tests/test_lex_actions,
# etc.  Lives under examples/ rather than tests/ because the
# .lex file is also intended as a worked-example artefact for
# downstream reading -- the test alongside it just keeps the
# example honest.
test_pg_bootscanner_gen = custom_target('pg_bootscanner_lex',
  input   : ['bootscanner.lex'],
  output  : ['bootscanner_lex.c', 'bootscanner_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_pg_bootscanner = executable('test_pg_bootscanner',
  'test_bootscanner.c',
  test_pg_bootscanner_gen,
  c_args : ['-Wno-unused-parameter'],
)

test('pg_bootscanner', test_pg_bootscanner)
