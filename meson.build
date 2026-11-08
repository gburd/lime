project('lime-parser', 'c', 'cpp',
  version : '1.11.3',
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

# Compiler-warning suppression flags used by lime.c et al.  These are
# GCC/Clang spellings; MSVC's cl.exe parses them as 'invalid numeric
# argument /Wno-...' and fails the compile.  Probe for support and
# fall back to an empty list on MSVC.
lime_warning_suppress = cc.get_supported_arguments([
  '-Wno-unused-function',
  '-Wno-unused-parameter',
  '-Wno-format-overflow',
])

# v0.8.x perf hygiene: PGO-instrumentation flags driven by the
# -Dlime_pgo=<off|generate|use> option.  The audit at
# .agent/notes/c-perf-audit.md item #1 noted that LTO and PGO are
# both off in CI and there's no way for users to opt in cleanly.
# This adds the cleanest possible knob -- PGO via meson option,
# LTO via the standard -Db_lto=true.  See docs/PERFORMANCE.md
# for the production-build recipe (the short version: -Dbuildtype=
# release -Db_lto=true [-Dlime_pgo=use] for shipping; pair with
# -Dlime_pgo=generate, run benches, then -Dlime_pgo=use for the
# best codegen).
lime_pgo_mode = get_option('lime_pgo')
lime_pgo_args = []
if lime_pgo_mode == 'generate'
  lime_pgo_args = cc.get_supported_arguments(['-fprofile-generate'])
elif lime_pgo_mode == 'use'
  # -fprofile-use is gcc/clang; on a fresh build the .gcda files
  # must already exist or the compiler errors out.  Users invoke
  # 'meson configure -Dlime_pgo=use' AFTER running a 'generate'
  # round.
  lime_pgo_args = cc.get_supported_arguments([
    '-fprofile-use', '-fprofile-correction'
  ])
endif
add_project_arguments(lime_pgo_args, language : 'c')
add_project_arguments(lime_pgo_args, language : 'cpp')
add_project_link_arguments(lime_pgo_args, language : 'c')
add_project_link_arguments(lime_pgo_args, language : 'cpp')

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
#          refcount code relies on it.
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
  'include/lime_ast.h',
  'include/lime_compiler.h',
  'include/lime_error.h',
  'include/lime_location.h',
  'include/lime_lexer.h',
  'include/lime_policy.h',
  'include/lime_threads.h',
  'include/glr.h',
  'include/parse_context.h',
  'include/parser_fork.h',
  'include/multi_grammar.h',
  'include/parser.h',
  'include/parser_manager.h',
  'include/parser_operations.h',
  'include/snapshot_build.h',
  'include/snapshot_diff.h',
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
  'src/compose_policy.c',
  'src/emit_rust.c',
  'src/emit_rust_skin_lalrpop.c',
  'src/emit_rust_skin_nom.c',
  'src/emit_rust_skin_pest.c',
  'src/emit_rust_skin_chumsky.c',
  'src/emit_c_skin_bison.c',
  'src/rule_inline.c',
  link_with : [lime_lex_compiler_lib],
  dependencies : [thread_dep],
  include_directories : [inc, include_directories('src/lex')],
  c_args : ['-DLIME_HAS_LEX_COMPILER', '-DLIME_HAS_RUST_OUTPUT', '-DLIME_HAS_COMPOSE_POLICY',
            '-DLIME_DATADIR="' + get_option('prefix') / get_option('datadir') / 'lime' + '"'],
  install : true,
)

# The parser-driver template.  An installed `lime` finds it via
# LIME_DATADIR (above) when it is not next to the grammar or the binary;
# without this, `meson install` produced a lime that could not generate
# a C parser unless every caller passed -T.
install_data('limpar.c', install_dir : get_option('datadir') / 'lime')

# Track D: `lime-yacc` runs lime with the yacc/bison command line.  The
# yacc/bison names themselves are not installed (FRONTEND.md decision 4).
# (install_symlink needs meson 0.61; this project's floor is 0.60.)
meson.add_install_script('sh', '-c',
  'ln -sf lime "$MESON_INSTALL_DESTDIR_PREFIX/' + get_option('bindir') + '/lime-yacc"')
# Track E: `lime-lex` runs lime with the lex/flex command line.
meson.add_install_script('sh', '-c',
  'ln -sf lime "$MESON_INSTALL_DESTDIR_PREFIX/' + get_option('bindir') + '/lime-lex"')

# The two libraries, by when their code runs:
#
#   liblime_parser    what runs per parse: the push parser, snapshots,
#                     the extension registry.  No compiler, no process
#                     creation, no dynamic loader.
#   liblime_compiler  what runs at compose time: lime.c built as a
#                     library (-DLIME_HAVE_SNAPSHOT_BUILD, -DLIME_TEST_
#                     HARNESS drops main()), publish_modified_snapshot,
#                     the compose policy, create_base_snapshot /
#                     lime_compile_grammar_text.
#
# A consumer that composes (or builds a snapshot from grammar text) links
# lime_compiler_dep explicitly.  There is no weak reference and no runtime
# fallback: forgetting it is a link error.  tests/check_no_runtime_loader.sh
# (meson test no_runtime_loader) enforces both rules on the built archives.
#
# We do not pass -DLIME_HAS_LEX_COMPILER here because the in-process
# compile path never invokes the .lex frontend; the stub in lime.c
# (returning a clear error if -X is somehow reached) is sufficient.
lime_compiler_lib = static_library('lime_compiler',
  'lime.c',
  'src/lexicon_build.c',
  # Track B: compose-time lexer builders.  lex_program_build.c compiles
  # .lex text in process; it needs the .lex front end and the DFA
  # compiler, which are plain C with no dependencies.  They are ALSO in
  # lime_lex_compiler_lib (the generator's copy); no target links both.
  'src/lex_program_build.c',
  'src/lex/lex_tokenize.c',
  'src/lex/lex_ast.c',
  'src/lex/lex_parse.c',
  'src/lex/lex_resolve.c',
  'src/lex/lex_regex.c',
  'src/lex/lex_nfa.c',
  'src/lex/lex_dfa.c',
  'src/lex/lex_dfa_min.c',
  'src/lex/lex_compile.c',
  # Compose time: publish_modified_snapshot, the conflict policy, and
  # grammar file/text -> snapshot (create_base_snapshot,
  # lime_compile_grammar_text).
  'src/compose.c',
  'src/compose_policy.c',
  'src/snapshot_create.c',
  dependencies : [thread_dep],
  include_directories : [inc, include_directories('src'),
                         include_directories('src/lex')],
  c_args : ['-DLIME_TEST_HARNESS', '-DLIME_HAVE_SNAPSHOT_BUILD']
           + lime_warning_suppress,
  install : true,
)

lime_compiler_dep = declare_dependency(
  # link_whole: lime.c's in-process entry points are reached only through
  # other archive members (compose.c, snapshot_create.c); pull every object
  # in so a consumer never depends on archive member order.
  link_whole : [lime_compiler_lib],
  include_directories : inc,
  dependencies : [lime_parser_dep],
)

# Lime Language Server (lime-lsp), src/lsp/.  It links liblime_compiler and
# lints in process for diagnostics; formatting runs the `lime -F` binary
# (POSIX fork+exec, Win32 CreateProcessA).  See docs/LSP.md.
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
  description       : 'Lime runtime: the push parser and snapshots (per-parse code). '
                      + 'Composing extensions or building a snapshot from grammar '
                      + 'text needs lime-compiler.',
  version           : meson.project_version(),
  url               : 'https://codeberg.org/gregburd/lime',
  # No `subdirs:` -- we want callers to write  #include <lime/foo.h>
  # rather than  #include <foo.h>  so an installed Lime doesn't shadow
  # other libraries' similarly-named headers (extension.h is a common
  # name).  Cflags resolves to -I${prefix}/include and our headers
  # all live in include/lime/ inside that.
  extra_cflags      : ['-pthread'],
)

# Second pkg-config file `lime-compiler`: what runs at compose time.
#
#   liblime_parser.a    per parse: parse_begin / parse_token / parse_end,
#                       snapshots, the extension registry.
#                       `pkg-config --cflags --libs lime`
#   liblime_compiler.a  compose time: publish_modified_snapshot, the
#                       compose policy, lime_compile_grammar_in_process,
#                       create_base_snapshot / lime_compile_grammar_text.
#                       `pkg-config --cflags --libs lime-compiler`
#
# Compose needs liblime_compiler.  Neither library can spawn a process or
# load a shared object; linking only `lime` and calling a compose entry
# point is a link error, not a slower path.  Most consumers that only
# parse want just `lime` (the compiler is ~1.2 MB).
pkgconfig_mod.generate(
  lime_compiler_lib,
  name              : 'lime-compiler',
  filebase          : 'lime-compiler',
  description       : 'Lime compose-time library: in-process LALR(1) rebuild, '
                      + 'extension composition and conflict policy. Needed to compose.',
  version           : meson.project_version(),
  url               : 'https://codeberg.org/gregburd/lime',
  # The compiler archive needs the runtime parser at link time
  # (snapshot_release, parse_engine_step, ...).  Both are static
  # archives, so this is Requires, not Requires.private: .private is
  # only read with --static, and the documented
  # `pkg-config --libs lime-compiler` then left -llime_parser out.
  requires          : 'lime',
  extra_cflags      : ['-pthread'],
)

# Multi-grammar context-switch example (SQL host + JSON
# embedded).  Built BEFORE subdir('tests') so the e2e tests in
# tests/test_context_switch_e2e.c and test_context_switch_error.c
# can reference multi_dep (declared in the example's meson.build).
# See the directory's README.md for the architecture walk-through.
subdir('examples/multi_grammar_sql_json')

# Plugin-template example and the parse-manager CLI load parser plugins
# from shared libraries, so they exist only with the opt-in loader
# (-Dplugin_loader=enabled).  Wired BEFORE subdir('tests'), which
# references sql_plugin_lib / parse_manager_exe under the same option.
if get_option('plugin_loader').enabled()
  subdir('examples/plugin_template')
  subdir('tools')
endif

# Build tests
subdir('tests')

# Build benchmarks
subdir('bench')

# M5.1: PG bootscanner port (end-to-end demo + test).  Lives
# under examples/ but is wired as a meson test so the example
# stays compiled and the .lex -> .c+.h codegen path is
# exercised on every CI run.
subdir('examples/pg_bootscanner')

# contrib/ Goal-B demos (runtime grammar composition showcases).
# Off by default in the sense that only the subset which currently
# builds cleanly against the extension API is wired -- see
# contrib/meson.build for the per-directory rationale.  `disabled`
# skips contrib/ entirely; `auto`/`enabled` build that buildable
# subset (there is currently nothing in contrib/ that hard-errors
# when explicitly `enabled`, so the two behave the same today).
if not get_option('contrib').disabled()
  subdir('contrib')
endif

doxygen = find_program('doxygen', required : false)
if doxygen.found()
  custom_target('doxygen',
    output : 'html',
    command : [doxygen, meson.project_source_root() / 'docs' / 'Doxyfile'],
    build_by_default : false,
  )
endif
