# Library sources
lib_sources = files(
  'version.c',
  'snapshot.c',
  'snapshot_build.c',
  'snapshot_create.c',
  'token_table.c',
  'tokenize.c',
  'parse_context.c',
  'parse_engine.c',
  'extension.c',
  'conflict.c',
  'conflict_detector.c',
  'disambiguation.c',
  'strategy_priority.c',
  'strategy_fork_resolve.c',
  'strategy_bayesian.c',
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

# LLVM JIT compilation support.
# When LLVM is available and enabled, the JIT subsystem compiles parser
# hot paths into native code. Otherwise stub implementations (guarded
# by -DLIME_NO_JIT) are compiled and jit_is_available() returns false.
#
# Controlled by the 'llvm' feature option (see meson_options.txt):
#   auto      -- enable if llvm-config is found on PATH / $LLVM_CONFIG
#   enabled   -- require LLVM; hard error if it is absent
#   disabled  -- skip the probe entirely; always build stubs
#
# The 'llvm-static' boolean option (default false) switches between
# dynamic linking (libLLVM.so, runtime dep) and static linking
# (component .a archives, self-contained but larger binary).  It has
# effect only when LLVM is enabled.
#
# Minimum LLVM version floor is 14.  The compat shim in
# include/jit_llvm_compat.h hides API differences across 14-21,
# so all targets from 14 onward build with the same call sites.
#
# Passing the feature option as `required:` makes meson short-circuit
# the llvm-config probe when the feature is disabled, and hard-error
# (rather than silently falling back to stubs) when it is enabled.
#
# Prefer llvm-config over CMake so an environment-provided LLVM (e.g.
# the one pinned in flake.nix) takes precedence over system installs.
llvm_opt    = get_option('llvm')
llvm_static = get_option('llvm-static')
llvm_dep = dependency('llvm',
  version  : '>=14',
  modules  : ['core', 'executionengine', 'mcjit',
              'native', 'analysis', 'passes', 'orcjit'],
  method   : 'config-tool',
  static   : llvm_static,
  required : llvm_opt,
)

jit_sources = files('jit_context.c', 'jit_codegen.c', 'jit_policy.c')
jit_c_args = []
jit_deps = []

if llvm_dep.found()
  jit_deps += llvm_dep
  link_kind = llvm_static ? 'static' : 'shared'
  message('LLVM JIT compilation: enabled (' + llvm_dep.version() + ', ' + link_kind + ' link)')
else
  jit_c_args += '-DLIME_NO_JIT'
  if llvm_opt.disabled()
    message('LLVM JIT compilation: disabled by -Dllvm=disabled (stub mode)')
  else
    message('LLVM JIT compilation: disabled, llvm-config not found (stub mode)')
  endif
endif

jit_lib = static_library('lime_jit',
  jit_sources,
  include_directories : inc,
  c_args : jit_c_args + coverage_args,
  link_args : coverage_link_args,
  dependencies : [thread_dep] + jit_deps,
)

lime_parser_lib = static_library('lime_parser',
  lib_sources,
  include_directories : inc,
  c_args : coverage_args,
  link_args : coverage_link_args,
  dependencies : [thread_dep, dl_dep],
  link_with : [tokenize_simd_lib, jit_lib],
  install : true,
)

lime_parser_dep = declare_dependency(
  link_with : [lime_parser_lib, tokenize_simd_lib, jit_lib],
  include_directories : inc,
  dependencies : [thread_dep, dl_dep] + jit_deps,
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
