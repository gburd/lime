project('lime-parser', 'c', 'cpp',
  version : '0.5.1',
  default_options : [
    'c_std=c11',
    'warning_level=2',
    'buildtype=debugoptimized',
    # Define NDEBUG (stripping assert() in the hot path) for release and
    # plain builds, but keep asserts live for debug/debugoptimized.
    # Downstream consumers wanting max throughput should use
    # buildtype=release.  See docs/PERFORMANCE.md.
    'b_ndebug=if-release',
  ],
  license : 'Public Domain',
  meson_version : '>= 0.60.0',
)

cc = meson.get_compiler('c')

# Expose POSIX and GNU extensions (needed for pthread_rwlock_t,
# strdup, clock_gettime, etc.).  Multiple macros are defined for
# portability across libc implementations:
#   _GNU_SOURCE         -- glibc / musl: enables every extension
#   _POSIX_C_SOURCE     -- portable POSIX feature-test macro,
#                          required on Solaris / illumos for
#                          functions like strdup
#   __EXTENSIONS__      -- Solaris / illumos magic word for
#                          BSD/SVR4 extensions on top of POSIX
#   _DARWIN_C_SOURCE    -- macOS: exposes BSD extensions in
#                          standards-conforming mode
add_project_arguments(
  '-D_GNU_SOURCE',
  '-D_POSIX_C_SOURCE=200809L',
  '-D__EXTENSIONS__',
  '-D_DARWIN_C_SOURCE',
  '-DLIME_VERSION_STRING="' + meson.project_version() + '"',
  language : 'c',
)
add_project_arguments(
  '-D_GNU_SOURCE',
  '-D_POSIX_C_SOURCE=200809L',
  '-D__EXTENSIONS__',
  '-D_DARWIN_C_SOURCE',
  '-DLIME_VERSION_STRING="' + meson.project_version() + '"',
  language : 'cpp',
)

# v0.4.1: LIME_STRICT toggles strict diagnostics for `%extends`-time
# rule mutations.  Auto-on for buildtypes that already imply
# debugging:
#   - debug          : asserts on, optimizations off
#   - debugoptimized : asserts on, optimizations on (project default)
# Release builds (`buildtype=release`, `plain`, `minsize`) leave the
# strict path off so production users see warnings instead of hard
# errors when `%remove` targets a non-existent rule (the canonical
# soft-failure case -- still useful when a base file evolves under a
# pinned derived file).  See docs/EXTENDS.md for the full list of
# diagnostics gated on LIME_STRICT.
if get_option('buildtype') == 'debug' or get_option('buildtype') == 'debugoptimized'
  add_project_arguments('-DLIME_STRICT', language : 'c')
  add_project_arguments('-DLIME_STRICT', language : 'cpp')
endif

# MSVC-specific compile flags.
#   /experimental:c11atomics
#       -- enable <stdatomic.h> support; default-off in MSVC's
#          C frontend without this flag, and our snapshot
#          refcount + JIT policy code rely on it.
#   /D_CRT_SECURE_NO_WARNINGS
#       -- silence the C4996 warnings for posix-style names
#          (strerror, strdup, sprintf, etc.) that map to
#          _strerror_s / _strdup / _sprintf_s in MSVC libc.
#          Lime is C11 portable; we use the standard names.
#   /Dstrdup=_strdup
#       -- strdup is renamed in MSVC's CRT; map back so the
#          source compiles unchanged.
cc = meson.get_compiler('c')
if cc.get_id() == 'msvc'
  add_project_arguments(
    '/experimental:c11atomics',
    '/D_CRT_SECURE_NO_WARNINGS',
    '/Dstrdup=_strdup',
    language : 'c',
  )
  add_project_arguments(
    '/D_CRT_SECURE_NO_WARNINGS',
    language : 'cpp',
  )
endif

# Project-wide include directory
inc = include_directories('include')

# Build the lime parser generator tool from root lime.c.  The
# executable also links in the lex compiler subsystem so the -X
# flag dispatches to the .lex frontend (M1 of the lexer
# project).  Defined after subdir('src') so lime_lex_compiler_lib
# is in scope.

# Install man pages
install_man('man/lime.1', 'man/lime-lsp.1', 'man/lime_grammar.5', 'man/lime_lex.5')

# Install public headers (P1-NEW-1).  Both include/*.h and src/*.h
# go to ${prefix}/include/lime/ -- the original public/internal split
# was aspirational (include/extension.h was an empty placeholder)
# and downstream consumers like PostgreSQL's grammar-extension layer
# need access to the full GrammarModification + serializer surface
# in src/extension.h, src/mod_serialize.h, src/snapshot_modify.h.
# Anyone embedding Lime should #include <lime/foo.h>.
lime_public_headers = [
  # Truly public surface (originally include/)
  'include/conflict.h',
  'include/dependency_resolver.h',
  'include/disambiguation.h',
  'include/execution_policy.h',
  'include/extension_registry.h',
  'include/grammar_context.h',
  'include/jit_context.h',
  'include/jit_llvm_compat.h',
  'include/jit_policy.h',
  'include/lime_ast.h',
  'include/lime_error.h',
  'include/lime_location.h',
  'include/parse_context.h',
  'include/parser_fork.h',
  'include/parser.h',
  'include/parser_manager.h',
  'include/parser_operations.h',
  'include/snapshot_build.h',
  'include/tokenize.h',
  'include/tokenize_simd.h',
  'include/token_table.h',
  'include/utf8.h',
  # Extension authoring + grammar-modification serializer surface
  # (originally src/, hoisted to public install path so downstream
  # callers don't have to vendor or re-implement it).
  'src/extension.h',
  'src/mod_serialize.h',
  'src/snapshot_modify.h',
  'src/snapshot.h',
  'src/merkle_tree.h',
  'src/dependency_resolver.h',
  'src/parser_composition.h',
]
install_headers(lime_public_headers, subdir : 'lime')

# Build the library components from src/
subdir('src')

lime_exe = executable('lime',
  'lime.c',
  link_with : [lime_lex_compiler_lib],
  include_directories : [inc, include_directories('src/lex')],
  c_args : ['-DLIME_HAS_LEX_COMPILER'],
  install : true,
)

# v0.5.0: standalone Lime Language Server (lime-lsp).  Lives under
# src/lsp/ and links nothing from lime_parser_lib so the runtime
# hot-path stays untouched -- it shells out to the lime binary
# above for diagnostics.  See docs/LSP.md.
subdir('src/lsp')

# pkg-config file so downstream meson/cmake/autotools builds can
# locate Lime via dependency('lime') / pkg_check_modules(LIME ..) /
# PKG_CHECK_MODULES.  Generated AFTER subdir('src') because we
# pass lime_parser_lib (defined there) as the library argument.
# The subdirs : 'lime' field points cflags at ${prefix}/include/lime
# so callers write  #include <lime/extension.h>  unambiguously.
pkgconfig_mod = import('pkgconfig')
pkgconfig_mod.generate(
  lime_parser_lib,
  name              : 'lime',
  filebase          : 'lime',
  description       : 'Lime parser generator runtime library',
  version           : meson.project_version(),
  url               : 'https://codeberg.org/gregburd/lime',
  # No `subdirs:` -- we want callers to write  #include <lime/foo.h>
  # rather than  #include <foo.h>  so an installed Lime doesn't shadow
  # other libraries' similarly-named headers (extension.h is a common
  # name).  Cflags resolves to -I${prefix}/include and our headers
  # all live in include/lime/ inside that.
  extra_cflags      : ['-pthread'],
  libraries_private : ['-ldl'],
)

# Multi-grammar context-switch example (SQL host + JSON
# embedded).  Built BEFORE subdir('tests') so the e2e tests in
# tests/test_context_switch_e2e.c and test_context_switch_error.c
# can reference multi_dep (declared in the example's meson.build).
# See the directory's README.md for the architecture walk-through.
subdir('examples/multi_grammar_sql_json')

# Plugin-template example: builds the example sql_plugin.so + host
# binaries and runs the load/identity/unload smoke test.  Wired BEFORE
# subdir('tests') so the e2e composition test in tests/ can reference
# sql_plugin_lib.full_path() if it ever needs a stand-in plugin path.
# Skipped at configure time on Windows (the directory's meson.build
# calls subdir_done() before declaring any targets).
subdir('examples/plugin_template')

# parse-manager CLI tool: meson-built replacement for the old
# tools/Makefile.  Links lime_parser_dep directly -- no stubs -- and
# installs to ${prefix}/bin/parse-manager.
subdir('tools')

# Build tests
subdir('tests')

# Build benchmarks
subdir('bench')

# M5.1: PG bootscanner port (end-to-end demo + test).  Lives
# under examples/ but is wired as a meson test so the example
# stays compiled and the .lex -> .c+.h codegen path is
# exercised on every CI run.
subdir('examples/pg_bootscanner')
doxygen = find_program('doxygen', required : false)
if doxygen.found()
  custom_target('doxygen',
    output : 'html',
    command : [doxygen, meson.project_source_root() / 'docs' / 'Doxyfile'],
    build_by_default : false,
  )
endif
