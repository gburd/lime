# Test executables
test_version = executable('test_version',
  'test_version.c',
  dependencies : lime_parser_dep,
)

test('version', test_version)

test_tokenize = executable('test_tokenize',
  'test_tokenize.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('tokenize', test_tokenize)

test_jit = executable('test_jit',
  'test_jit.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('jit', test_jit)

test_snapshot = executable('test_snapshot',
  'test_snapshot.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('snapshot', test_snapshot)

test_extension = executable('test_extension',
  'test_extension.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('extension', test_extension)

test_concurrent = executable('test_concurrent',
  'test_concurrent.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('concurrent', test_concurrent, timeout : 120)

test_parse_context = executable('test_parse_context',
  'test_parse_context.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('parse_context', test_parse_context)

test_snapshot_modify = executable('test_snapshot_modify',
  'test_snapshot_modify.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('snapshot_modify', test_snapshot_modify)

# Coverage boost tests - all link against lime_parser_dep
test_token_table_coverage = executable('test_token_table_coverage',
  'test_token_table_coverage.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('token_table_coverage', test_token_table_coverage)

test_parse_context_coverage = executable('test_parse_context_coverage',
  'test_parse_context_coverage.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('parse_context_coverage', test_parse_context_coverage)

test_snapshot_modify_extended = executable('test_snapshot_modify_extended',
  'test_snapshot_modify_extended.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('snapshot_modify_extended', test_snapshot_modify_extended)

test_extension_coverage = executable('test_extension_coverage',
  'test_extension_coverage.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('extension_coverage', test_extension_coverage)

test_parser_manager = executable('test_parser_manager',
  'test_parser_manager.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('parser_manager', test_parser_manager, timeout : 120)

test_runtime_management = executable('test_runtime_management',
  'test_runtime_management.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('runtime_management', test_runtime_management, timeout : 120)

test_merkle_tree = executable('test_merkle_tree',
  'test_merkle_tree.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('merkle_tree', test_merkle_tree)

test_dependency_resolver = executable('test_dependency_resolver',
  'test_dependency_resolver.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('dependency_resolver', test_dependency_resolver)

test_parser_composition = executable('test_parser_composition',
  'test_parser_composition.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('parser_composition', test_parser_composition)

test_random_composition = executable('test_random_composition',
  'test_random_composition.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('random_composition', test_random_composition, timeout : 120)

test_shared_token_lifetime = executable('test_shared_token_lifetime',
  'test_shared_token_lifetime.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('shared_token_lifetime', test_shared_token_lifetime, timeout : 120)

test_coverage_boost = executable('test_coverage_boost',
  'test_coverage_boost.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('coverage_boost', test_coverage_boost)

# Multi-grammar conflict detection tests
test_conflict_detector = executable('test_conflict_detector',
  'test_conflict_detector.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('conflict_detector', test_conflict_detector)

# Disambiguation strategy tests
test_disambiguation = executable('test_disambiguation',
  'test_disambiguation.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('disambiguation', test_disambiguation)

# Multi-extension integration tests
test_multi_extension = executable('test_multi_extension',
  'test_multi_extension.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('multi_extension', test_multi_extension)

# Execution policy engine tests
test_execution_policy = executable('test_execution_policy',
  'test_execution_policy.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('execution_policy', test_execution_policy)

# Parser fork cloning tests
test_parser_fork = executable('test_parser_fork',
  'test_parser_fork.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('parser_fork', test_parser_fork)

# Fork-resolve disambiguation strategy tests
test_fork_resolve = executable('test_fork_resolve',
  'test_fork_resolve.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('fork_resolve', test_fork_resolve)

# Extended strategy priority tests (improved coverage)
test_strategy_priority_extended = executable('test_strategy_priority_extended',
  'test_strategy_priority_extended.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('strategy_priority_extended', test_strategy_priority_extended)

# Grammar context stack tests
test_grammar_context = executable('test_grammar_context',
  'test_grammar_context.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('grammar_context', test_grammar_context)

# Extension registry comprehensive tests
test_extension_registry = executable('test_extension_registry',
  'test_extension_registry.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('extension_registry', test_extension_registry)

# RFC 0059 diagnostics API -- generate a tiny parser and test against it
test_diag_gen = custom_target('test_diagnostics_parser',
  input   : ['test_diagnostics_grammar.y'],
  output  : ['test_diagnostics_grammar.c', 'test_diagnostics_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_diagnostics = executable('test_diagnostics',
  'test_diagnostics.c',
  test_diag_gen,
  dependencies : lime_parser_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('diagnostics', test_diagnostics)

# P1-1 | alternation smoke test -- generate a tiny parser using
# rule alternation, then exercise each alternative at runtime.
test_alt_gen = custom_target('test_alternation_parser',
  input   : ['test_alternation_grammar.y'],
  output  : ['test_alternation_grammar.c', 'test_alternation_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_alternation = executable('test_alternation',
  'test_alternation.c',
  test_alt_gen,
  dependencies : lime_parser_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('alternation', test_alternation)

# P0-NEW-4: %first_token N -- shift externally-visible terminal
# codes by N so they don't collide with ASCII characters.  The
# generated header emits #define FT_A 259 (= 1+258) etc.; the
# runtime template subtracts the offset before indexing the
# action table.  Required by callers like PostgreSQL whose
# scanner returns ASCII values for single-char tokens and would
# otherwise alias keyword token codes.
test_first_token_gen = custom_target('test_first_token_parser',
  input   : ['test_first_token_grammar.y'],
  output  : ['test_first_token_grammar.c', 'test_first_token_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_first_token = executable('test_first_token',
  'test_first_token.c',
  test_first_token_gen,
  dependencies : lime_parser_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('first_token', test_first_token)

# P0-NEW-2: proper @N location threading.
# Bison-compatible @<rhsalias> expands to the stack slot's yyloc field
# (not the token enum code), @<lhsalias>/@$ expand to the post-reduce
# LHS location, and YYLLOC_DEFAULT for empty productions falls back
# to the lookahead's loc.  Also exercises %location_type {int}.
test_locations_gen = custom_target('test_locations_parser',
  input   : ['test_locations_grammar.y'],
  output  : ['test_locations_grammar.c', 'test_locations_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_locations = executable('test_locations',
  'test_locations.c',
  test_locations_gen,
  dependencies : lime_parser_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('locations', test_locations)

# P0-NEW-6: user-defined YYLLOC_DEFAULT honored on every reduce.
# Bison-style override macro that produces span(start..end) across
# all RHS symbols.  Discriminator sub-test: a multi-RHS list
# reduction whose .end must march forward to the last element --
# unreachable with the pre-P0-NEW-6 slot-reuse default.  Mirrors
# ecpg's preproc grammar shape (string-typed YYLTYPE concatenated
# across each reduce); the LAST blocker for retiring bison from
# the PostgreSQL tree.
test_yylloc_default_gen = custom_target('test_yylloc_default_parser',
  input   : ['test_yylloc_default_grammar.y'],
  output  : ['test_yylloc_default_grammar.c', 'test_yylloc_default_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_yylloc_default = executable('test_yylloc_default',
  'test_yylloc_default.c',
  test_yylloc_default_gen,
  dependencies : lime_parser_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('yylloc_default', test_yylloc_default)

# P0-NEW-7: Bison-compatible YYLLOC_DEFAULT pre-action ordering.
# The action body's `@$ = expr` write must survive the LHS-yyloc
# commit at the end of the reduce.  Without this, ecpg's preproc.y
# (70+ rules with non-trivial @$ assignments building source-text
# accumulation) sees its action overrides silently overwritten by
# the YYLLOC_DEFAULT default, producing duplicated tokens in the
# emitted source (`int x x ;`).  Discriminator: a multi-rule list
# whose action overrides @$ via cat_str-style concatenation; final
# location must hold the assembled source-text, not just Rhs[1].
test_yylloc_action_assign_gen = custom_target('test_yylloc_action_assign_parser',
  input   : ['test_yylloc_action_assign_grammar.y'],
  output  : ['test_yylloc_action_assign_grammar.c', 'test_yylloc_action_assign_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_yylloc_action_assign = executable('test_yylloc_action_assign',
  'test_yylloc_action_assign.c',
  test_yylloc_action_assign_gen,
  dependencies : lime_parser_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('yylloc_action_assign', test_yylloc_action_assign)

# M1 phase 1 of the lexer subsystem: .lex source-language
# tokenizer.  No DFA, no parser yet -- just the byte-level
# token stream that the M1 parser will consume.
test_lex_tokenize = executable('test_lex_tokenize',
  'test_lex_tokenize.c',
  dependencies : lime_lex_compiler_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_tokenize', test_lex_tokenize)

# M1.2 of the lexer subsystem: .lex parser (token stream -> AST).
# Pattern resolution and block desugaring are M1.3/M1.4.
test_lex_parse = executable('test_lex_parse',
  'test_lex_parse.c',
  dependencies : lime_lex_compiler_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_parse', test_lex_parse)

# M1.3 of the lexer subsystem: %pattern fragment resolution with
# cycle detection.
test_lex_resolve = executable('test_lex_resolve',
  'test_lex_resolve.c',
  dependencies : lime_lex_compiler_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_resolve', test_lex_resolve)

# M1.5 of the lexer subsystem: pretty-printer round-trip.
# parse(text) -> A; pretty(A) -> text'; parse(text') -> A';
# pretty(A') -> text''.  Assert text' == text'' (idempotent)
# AND A == A' (structural).  M1 ship gate.
test_lex_round_trip = executable('test_lex_round_trip',
  'test_lex_round_trip.c',
  dependencies : lime_lex_compiler_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_round_trip', test_lex_round_trip)

# M2.1 of the lexer subsystem: regex parser.  Builds an AST
# from regex source strings (POSIX-extended subset).  Input to
# the M2.2 NFA construction (Thompson's algorithm).
test_lex_regex = executable('test_lex_regex',
  'test_lex_regex.c',
  dependencies : lime_lex_compiler_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_regex', test_lex_regex)

# M2.2 of the lexer subsystem: Thompson's NFA construction +
# bounded NFA simulator (testing only).  Inputs are regex ASTs
# from M2.1; output is a fragment NFA fed to M2.3 subset
# construction.
test_lex_nfa = executable('test_lex_nfa',
  'test_lex_nfa.c',
  dependencies : lime_lex_compiler_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_nfa', test_lex_nfa)

# M2.3 of the lexer subsystem: subset construction (NFA -> DFA).
test_lex_dfa = executable('test_lex_dfa',
  'test_lex_dfa.c',
  dependencies : lime_lex_compiler_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_dfa', test_lex_dfa)

# M2.4 of the lexer subsystem: DFA minimization (Moore's
# algorithm).  Asserts language preservation, no state growth,
# and strict reduction on redundant patterns.
test_lex_dfa_min = executable('test_lex_dfa_min',
  'test_lex_dfa_min.c',
  dependencies : lime_lex_compiler_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_dfa_min', test_lex_dfa_min)

# M2.5a of the lexer subsystem: multi-rule NFA combiner +
# accept-rule tagging.  Tests the NFA combine + subset
# construction's lowest-rule-id resolution on overlap.
test_lex_combine = executable('test_lex_combine',
  'test_lex_combine.c',
  dependencies : lime_lex_compiler_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_combine', test_lex_combine)

# M2.5b of the lexer subsystem: per-state spec compilation.
# End-to-end pipeline: spec source -> parse -> resolve ->
# compile per-state DFAs.
test_lex_compile = executable('test_lex_compile',
  'test_lex_compile.c',
  dependencies : lime_lex_compiler_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_compile', test_lex_compile)

# M3.1 of the lexer subsystem: emit DFA tables + longest-match
# driver as compilable C source.  Foundation for the M3.2+
# runtime API layer.
test_lex_emit = executable('test_lex_emit',
  'test_lex_emit.c',
  dependencies : lime_lex_compiler_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_emit', test_lex_emit)

# M3.3 of the lexer subsystem: push-driven runtime API.
# End-to-end pipeline test: lime -X compiles the .lex grammar
# to .c + .h, the test driver compiles against the generated
# header and exercises LexAlloc/Free/FeedBytes/FeedEOF/etc.
# This is the first test that actually compiles+runs lime-
# generated runtime code.
test_lex_runtime_gen = custom_target('test_lex_runtime_lex',
  input   : ['test_lex_runtime_grammar.lex'],
  output  : ['test_lex_runtime_grammar_lex.c',
             'test_lex_runtime_grammar_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_lex_runtime = executable('test_lex_runtime',
  'test_lex_runtime.c',
  test_lex_runtime_gen,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_runtime', test_lex_runtime)

# M3.5 of the lexer subsystem: LexInclude buffer-stack +
# LexPushback runtime support.  Mirrors the M3.3 wiring: lime -X
# generates the lexer .c+.h pair, the test driver compiles
# against the generated header and exercises the buffer stack
# (LexInclude / LexIncludeDepth / auto-pop) and runtime pushback
# (LexPushback bounds + rewind).  Three single-byte rules in the
# grammar are enough -- the work under test is the runtime, not
# the DFA.
test_lex_include_gen = custom_target('test_lex_include_lex',
  input   : ['test_lex_include_grammar.lex'],
  output  : ['test_lex_include_grammar_lex.c',
             'test_lex_include_grammar_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_lex_include = executable('test_lex_include',
  'test_lex_include.c',
  test_lex_include_gen,
  c_args : ['-Wno-unused-parameter'],
)

test('lex_include', test_lex_include)

# P0-NEW-8: opt-in eager default-reduce drain.  Discriminator
# mirrors ecpg's lex-time-vs-reduce-time channel interleaving:
# the driver appends a marker between Parse() calls (simulating
# pgc.c's echo_text), grammar action bodies append token text
# at reduce time.  Without Parse_drain, source order is wrong
# (lexer marker beats action's first-token write).  With
# Parse_drain between pushes, order matches Bison's pull-mode
# timing.
test_drain_gen = custom_target('test_drain_parser',
  input   : ['test_drain_grammar.y'],
  output  : ['test_drain_grammar.c', 'test_drain_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_drain = executable('test_drain',
  'test_drain.c',
  test_drain_gen,
  dependencies : lime_parser_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('drain', test_drain)

# P0-NEW-3 evidence test: confirm Lime's LALR(1) default-reduce for
# empty productions works as designed.  The PG migration team in
# Lime-Letter-4 claimed Lime fires %syntax_error on inputs that
# Bison reduces; the .out files for three reduced grammars all
# show {default} reduce actions for the empty alternatives, so
# this test verifies at runtime that the reduction actually
# happens.  If any case here regresses, the LALR machinery has
# a real bug -- not what the original letter described.
test_default_reduce_gen = custom_target('test_default_reduce_parser',
  input   : ['test_default_reduce_grammar.y'],
  output  : ['test_default_reduce_grammar.c', 'test_default_reduce_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_default_reduce = executable('test_default_reduce',
  'test_default_reduce.c',
  test_default_reduce_gen,
  dependencies : lime_parser_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('default_reduce', test_default_reduce)

# P0-NEW-5: action-time lookahead access (bison's yychar/yyclearin
# equivalent).  An empty production's action reads the parser's
# pending lookahead via Parse_get_lookahead() and signals it was
# consumed via Parse_clear_lookahead().  Required by PostgreSQL's
# plpgsql pl_gram.y -- decl_datatype and a dozen sibling rules
# follow the same pattern.
test_lookahead_gen = custom_target('test_lookahead_parser',
  input   : ['test_lookahead_grammar.y'],
  output  : ['test_lookahead_grammar.c', 'test_lookahead_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_lookahead = executable('test_lookahead',
  'test_lookahead.c',
  test_lookahead_gen,
  dependencies : lime_parser_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('lookahead', test_lookahead)

# P0-NEW-1: %syntax_error must distinguish EOF lookahead from a
# non-EOF unexpected token, and yyloc must reflect the offending
# lookahead's location -- not the stack top.  Smoke grammar +
# driver follow the same pattern as test_diagnostics.
test_syn_err_gen = custom_target('test_syntax_error_parser',
  input   : ['test_syntax_error_grammar.y'],
  output  : ['test_syntax_error_grammar.c', 'test_syntax_error_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_syntax_error = executable('test_syntax_error',
  'test_syntax_error.c',
  test_syn_err_gen,
  dependencies : lime_parser_dep,
  c_args : ['-Wno-unused-parameter'],
)

test('syntax_error', test_syntax_error)

# P0-2 scaffolding: compile-time check that LimeReduceFn and the
# extended MOD_ADD_RULE shape build cleanly.  No runtime dispatch
# (that lands with apply_add_rule, i.e. P0-1).
test_reduce_fn_type = executable('test_reduce_fn_type',
  'test_reduce_fn_type.c',
  include_directories : include_directories('..'),
  dependencies : lime_parser_dep,
)

test('reduce_fn_type', test_reduce_fn_type)

# Serializer for grammar modifications -> .lime text (subprocess
# fallback path for Phase 4 extension validation).
test_mod_serialize = executable('test_mod_serialize',
  'test_mod_serialize.c',
  include_directories : include_directories('..'),
  dependencies : lime_parser_dep,
)

test('mod_serialize', test_mod_serialize)
