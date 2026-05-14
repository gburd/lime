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
