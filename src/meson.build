# Library sources
lib_sources = files(
  'version.c',
  'snapshot.c',
  'token_table.c',
  'tokenize.c',
  'parse_context.c',
  'extension.c',
  'conflict.c',
  'conflict_detector.c',
  'disambiguation.c',
  'strategy_priority.c',
  'strategy_fork_resolve.c',
  'snapshot_modify.c',
  'dependency_resolver.c',
  'parser_manager.c',
  'parser_plugin.c',
  'parser_operations.c',
  'merkle_tree.c',
  'parser_composition.c',
  'parser_fork.c',
  'utf8.c',
  'lime_ast.c',
  'glr.c',
  'execution_policy.c',
  'extension_registry.c',
  'grammar_context.c',
  'context_switch.c',
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

# LLVM JIT compilation support (optional).
# When LLVM is available, the JIT subsystem compiles parser hot paths
# into native code. Without LLVM, stub implementations are used.
# Prefer llvm-config over CMake so environment-provided LLVM (e.g. Nix)
# takes precedence over system/Homebrew installs.
llvm_dep = dependency('llvm', modules : ['core', 'executionengine', 'mcjit',
                                          'native', 'analysis', 'passes',
                                          'orcjit'],
                      method : 'config-tool',
                      required : false)

jit_sources = files('jit_context.c', 'jit_codegen.c', 'jit_policy.c', 'jit_tokenizer.c')
jit_c_args = []
jit_deps = []

if llvm_dep.found()
  jit_deps += llvm_dep
  message('LLVM found -- JIT compilation enabled')
else
  jit_c_args += '-DLIME_NO_JIT'
  message('LLVM not found -- JIT compilation disabled (stub mode)')
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
