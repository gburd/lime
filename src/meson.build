# Library sources
lib_sources = files(
  'version.c',
  'snapshot.c',
  'rule_inline.c',
  'snapshot_build.c',
  'snapshot_create.c',
  'snapshot_diff.c',
  'token_table.c',
  'kw_table.c',
  'tokenize.c',
  'parse_context.c',
  'parse_engine.c',
  'parse_glr.c',
  'glr.c',
  'lime_ast.c',
  'extension.c',
  'conflict.c',
  'conflict_detector.c',
  'disambiguation.c',
  'strategy_priority.c',
  'strategy_fork_resolve.c',
  'strategy_bayesian.c',
  'multi_grammar.c',
  'snapshot_modify.c',
  'mod_serialize.c',
  'dependency_resolver.c',
  'parser_manager.c',
  'parser_plugin.c',
  'parser_operations.c',
  'merkle_tree.c',
  'parser_composition.c',
  'parser_fork.c',
  'utf8.c',
  'execution_policy.c',
  'extension_registry.c',
  'grammar_context.c',
  'context_switch.c',
  'lime_error.c',
)

thread_dep = dependency('threads')
dl_dep = cc.find_library('dl', required : false)

# Check if coverage is enabled
coverage_args = []
coverage_link_args = []
if get_option('b_coverage')
  coverage_args = ['--coverage']
  coverage_link_args = ['--coverage']
endif

# SIMD character classification.
# The AVX2 function uses __attribute__((target("avx2"))), so no special
# compiler flags are required -- it compiles on any x86_64 toolchain.
tokenize_simd_lib = static_library('tokenize_simd',
  'tokenize_simd.c',
  include_directories : inc,
  c_args : coverage_args,
  link_args : coverage_link_args,
)

lime_parser_lib = static_library('lime_parser',
  lib_sources,
  include_directories : inc,
  c_args : coverage_args,
  link_args : coverage_link_args,
  dependencies : [thread_dep, dl_dep],
  link_with : [tokenize_simd_lib],
  install : true,
)

lime_parser_dep = declare_dependency(
  # link_with lists ONLY lime_parser_lib here.  tokenize_simd_lib
  # is propagated transitively via lime_parser_lib's own link_with.
  # Listing it again would cause lld-link (Windows / clang-cl) to
  # error with 'duplicate symbol' -- GNU ld dedupes static-archive
  # contents but lld-link does not.
  link_with : [lime_parser_lib],
  include_directories : inc,
  dependencies : [thread_dep, dl_dep],
)

# .lex source-language frontend (M1 of the lexer project).
# Built as a separate static library because it is part of the
# lime generator binary, not the runtime library that ships with
# generated parsers.  The lexer subsystem will grow to include
# the AST/parser (lex_parse.c), pattern fragment resolution, and
# eventually the DFA compiler -- all linked into this lib.
lime_lex_compiler_lib = static_library('lime_lex_compiler',
  files(
    'lex/lex_tokenize.c',
    'lex/lex_ast.c',
    'lex/lex_parse.c',
    'lex/lex_resolve.c',
    'lex/lex_pretty.c',
    'lex/lex_main.c',
    'lex/lex_regex.c',
    'lex/lex_nfa.c',
    'lex/lex_dfa.c',
    'lex/lex_dfa_min.c',
    'lex/lex_compile.c',
    'lex/lex_emit.c',
    'lex/emit_rust_lex.c',
    'lex/emit_rust_skin_logos.c',
    'lex/emit_c_skin_flex.c',
    'lex/lex_introspect.c',
  ),
  include_directories : [inc, include_directories('lex')],
  c_args : coverage_args,
  link_args : coverage_link_args,
)

lime_lex_compiler_dep = declare_dependency(
  link_with : [lime_lex_compiler_lib],
  include_directories : [inc, include_directories('lex')],
)
