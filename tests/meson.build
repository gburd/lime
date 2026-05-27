# Test executables
# Per-test extra warnings.  Several tests intentionally take
# parameters they don't reference (callback signatures fixed by
# Lime's API), so silence -Wunused-parameter where it would be
# noise.  Use cc.get_supported_arguments so MSVC silently drops
# the gcc/clang-only flag rather than failing with
# 'invalid numeric argument /Wno-unused-parameter'.
cc = meson.get_compiler('c')
nounusedparam = cc.get_supported_arguments(['-Wno-unused-parameter'])

test_version = executable('test_version',
  'test_version.c',
  dependencies : lime_parser_dep,
)

test('version', test_version)

# v0.5.0: end-to-end Lime LSP exercise.  Spawns the real
# `lime-lsp` binary, drives it over JSON-RPC framed stdin/stdout,
# and asserts on:
#   - lifecycle (initialize/initialized/shutdown/exit)
#   - publishDiagnostics from a buffer with a known lint warning
#   - textDocument/definition (cursor on `expr` -> `expr ::=` line)
#   - textDocument/hover (directive doc + non-terminal info)
#   - textDocument/documentSymbol (full outline)
# Args: <lime-lsp-bin> <lime-bin> <fixtures-dir>.
test_lsp = executable('test_lsp',
  'test_lsp.c',
  c_args : nounusedparam,
)

test('lsp', test_lsp,
  args : [
    lime_lsp_exe.full_path(),
    lime_exe.full_path(),
    meson.current_source_dir() / 'test_lsp_fixtures',
  ],
  depends : [lime_lsp_exe, lime_exe],
  timeout : 60,
)

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

# ROADMAP item 1, phase 1: in-process LimeCompilerContext smoke test.
# Compiles tests/test_compiler_context.c, which #includes ../lime.c
# with -DLIME_TEST_HARNESS so the generator's main() is omitted and
# the test acts as the driver.  Links the lex compiler library so
# the lime.c references to lime_lex_run_compiler resolve (the
# reference itself is inside the gated main() and is not exercised,
# but link-resolution still requires the symbol).
test_compiler_context = executable('test_compiler_context',
  'test_compiler_context.c',
  link_with : [lime_lex_compiler_lib],
  include_directories : [inc, include_directories('../src/lex')],
  c_args : ['-DLIME_TEST_HARNESS', '-DLIME_HAS_LEX_COMPILER',
            '-Wno-unused-function', '-Wno-unused-parameter',
            '-Wno-format-overflow'],
)

test('compiler_context', test_compiler_context)

# ROADMAP item 1, phase 3: in-process LALR rebuild library.
# test_lime_compile_in_process exercises the new public API
# lime_compile_grammar_in_process(text, len, &snap, &err).
# Like test_compiler_context, this test #includes ../lime.c with
# LIME_TEST_HARNESS so main() is omitted; LIME_HAVE_SNAPSHOT_BUILD
# additionally turns on the in-process snapshot path inside lime.c
# and pulls in the runtime headers.  Linking lime_parser_dep gives
# us snapshot_build_from_tables, parse_begin/parse_token/parse_end,
# snapshot_release, and (for the equivalence sub-test) the
# subprocess-driven lime_compile_grammar_text comparison baseline.
# include_directories includes ../src so snapshot.h / snapshot_build.h
# resolve when lime.c is compiled with LIME_HAVE_SNAPSHOT_BUILD.
test_lime_compile_in_process = executable('test_lime_compile_in_process',
  'test_lime_compile_in_process.c',
  link_with : [lime_lex_compiler_lib],
  dependencies : lime_parser_dep,
  include_directories : [inc, include_directories('../src'),
                         include_directories('../src/lex')],
  c_args : ['-DLIME_TEST_HARNESS', '-DLIME_HAVE_SNAPSHOT_BUILD',
            '-DLIME_HAS_LEX_COMPILER',
            '-Wno-unused-function', '-Wno-unused-parameter',
            '-Wno-format-overflow'],
  # Export the executable's runtime symbols so the .so produced
  # by the subprocess path's dlopen (used in the equivalence sub-
  # test) can resolve snapshot_release / parse_token / etc.
  # Same trick test_snapshot_create + test_extension_rebuild use.
  export_dynamic : true,
)

test('lime_compile_in_process', test_lime_compile_in_process,
  # Point the subprocess fallback at the just-built lime + parser lib
  # so the equivalence and perf-comparison sub-tests run against the
  # real subprocess pipeline rather than skipping when `lime` is not
  # on $PATH.  This is a runtime env, not a build-time link.
  # LIME_INCLUDE / LIME_SRC_INCLUDE are read by snapshot_create.c so
  # the subprocess `cc` invocation finds the runtime headers.
  #
  # ROADMAP-1 phase 4: this test predates the in-process dispatch
  # default in src/snapshot_create.c::lime_compile_grammar_text and
  # explicitly compares the in-process and subprocess pipelines.
  # Force subprocess for `lime_compile_grammar_text` so the
  # comparison still measures the fork+exec pipeline rather than the
  # transparently-dispatched in-process path.  Direct calls to
  # `lime_compile_grammar_in_process` (the subject of this test) are
  # unaffected by LIME_FORCE_SUBPROCESS.
  env : [
    'LIME_BIN=' + lime_exe.full_path(),
    'LIME_LIBDIR=' + meson.current_build_dir() / '..' / 'src',
    'LIME_INCLUDE=' + meson.project_source_root() / 'include',
    'LIME_SRC_INCLUDE=' + meson.project_source_root() / 'src',
    'LIME_SNAPSHOT_BUILD_C=' + meson.project_source_root() / 'src' / 'snapshot_build.c',
    'LIME_FORCE_SUBPROCESS=1',
  ],
  depends : [lime_exe],
  workdir : meson.project_source_root(),
  timeout : 60)

# ROADMAP item 1, phase 4: in-process dispatch through
# lime_compile_grammar_text and (transitively) publish_modified_snapshot
# / parser_composition.  This test executable is the only consumer
# that links lime_compiler_dep, so the weak
# `lime_compile_grammar_in_process` reference inside
# src/snapshot_create.c resolves and the fast path activates.  The
# rest of the suite continues to dispatch to the subprocess pipeline
# (weak ref unresolved -> NULL -> fallback), preserving v0.5.4
# functional behaviour.  See src/snapshot_create.c for the dispatch
# policy and LIME_FORCE_SUBPROCESS opt-out.
#
# Skipped on Windows along with test_snapshot_create / test_extension_rebuild:
# the subprocess fallback path uses fork/exec/dlopen which is stubbed
# on Windows, so several sub-tests would unconditionally fail there.
if host_machine.system() != 'windows'
  test_in_process_dispatch = executable('test_in_process_dispatch',
    'test_in_process_dispatch.c',
    include_directories : include_directories('../src'),
    dependencies : [lime_parser_dep, lime_compiler_dep],
    # Export executable symbols so the .so produced by the
    # subprocess fallback (sub-tests 2 + 3) at runtime can resolve
    # snapshot_release / parse_token / etc. via dlopen against the
    # parent binary.  Same trick test_snapshot_create uses.
    export_dynamic : true,
  )

  test('in_process_dispatch', test_in_process_dispatch,
    # The forced-subprocess and fallback sub-tests need a working
    # external pipeline; supply the same env wiring test_snapshot_create
    # uses so the runtime cc + lime invocations find the correct lime
    # binary, library, and headers from this build tree.
    env : [
      'LIME_BIN=' + lime_exe.full_path(),
      'LIME_LIBDIR=' + meson.current_build_dir() / '..' / 'src',
      'LIME_INCLUDE=' + meson.project_source_root() / 'include',
      'LIME_SRC_INCLUDE=' + meson.project_source_root() / 'src',
      'LIME_SNAPSHOT_BUILD_C=' + meson.project_source_root() / 'src' / 'snapshot_build.c',
    ],
    depends : [lime_exe],
    workdir : meson.project_source_root(),
    is_parallel : false,
    timeout : 120)
endif

# v0.6.x: regression for the dlopen-handle-leak fix in
# src/snapshot_create.c.  Verifies destroy_snapshot's new
# snapshot_dlopen_release hook actually dlclose()s the .so when the
# snapshot's last reference drops to zero.  Skipped at runtime when
# the lime CLI isn't reachable (the subprocess pipeline can't run).
if host_machine.system() != 'windows'
  test_snapshot_dlopen_cleanup = executable('test_snapshot_dlopen_cleanup',
    'test_snapshot_dlopen_cleanup.c',
    include_directories : include_directories('../src'),
    dependencies : lime_parser_dep,
    export_dynamic : true,
  )

  test('snapshot_dlopen_cleanup', test_snapshot_dlopen_cleanup,
    env : [
      'LIME_BIN=' + lime_exe.full_path(),
      'LIME_LIBDIR=' + meson.current_build_dir() / '..' / 'src',
      'LIME_INCLUDE=' + meson.project_source_root() / 'include',
      'LIME_SRC_INCLUDE=' + meson.project_source_root() / 'src',
      'LIME_SNAPSHOT_BUILD_C=' + meson.project_source_root() / 'src' / 'snapshot_build.c',
    ],
    depends : [lime_exe],
    workdir : meson.project_source_root(),
    is_parallel : false,
    timeout : 120)
endif

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

# UTF-8 unit tests -- direct exercise of utf8_decode / utf8_encode /
# utf8_char_length / utf8_is_id_start / utf8_is_id_continue.  Closes
# the coverage gap where src/utf8.c was previously only exercised via
# src/tokenize.c's narrow happy paths.
test_utf8 = executable('test_utf8',
  'test_utf8.c',
  dependencies : lime_parser_dep,
)

test('utf8', test_utf8)

# Lime-Letter-18 regression: `lime -F` must preserve copyright
# headers and brace-block directive bodies (e.g. %include) that
# the parser cannot meaningfully reformat, and must be idempotent
# under repeated runs (so PG can wire it as a CI format-check).
test_formatter = executable('test_formatter',
  'test_formatter.c',
  dependencies : lime_parser_dep,
)

test('formatter', test_formatter,
  args : [
    lime_exe.full_path(),
    meson.current_source_dir() / 'test_formatter_grammar.lime',
  ],
)

# Lime-Letter-19 regression: `lime -F` must (A) preserve inter-
# directive and inter-rule comments PG's grammar maintainers rely
# on for documentation, and (B) preserve leading whitespace inside
# brace bodies (PG's tab-indented %syntax_error / %parse_failure /
# action bodies).  v0.3.1 stripped both, costing PG 33.5% of every
# .lime file's bytes through the formatter.  Eight sub-tests cover
# header, block-comment, //-stack, brace-indent, rule-leading
# comment, idempotence, empty-grammar regression, and the
# documented per-token-comment limitation.
test_formatter_comments = executable('test_formatter_comments',
  'test_formatter_comments.c',
  dependencies : lime_parser_dep,
)

test('formatter_comments', test_formatter_comments,
  args : [
    lime_exe.full_path(),
    meson.current_source_dir() / 'test_formatter_comments_grammar.lime',
  ],
)

# Lime-Letter-20: regression test asserting LHS / RHS letter labels
# (`(A)`, `(B)`, etc.) and explicit `[SYMBOL]` precedence markers
# survive `lime -F`.  Pre-fix v0.3.2 stripped both, producing formatted
# grammars that lint clean but break gcc on the action bodies'
# undeclared identifiers.  Six sub-tests cover LHS labels, RHS labels,
# mixed labeled+unlabeled symbols, precedence markers, label-count
# parity (formatted vs source), and idempotence.
test_formatter_labels = executable('test_formatter_labels',
  'test_formatter_labels.c',
  dependencies : lime_parser_dep,
)

test('formatter_labels', test_formatter_labels,
  args : [
    lime_exe.full_path(),
    meson.current_source_dir() / 'test_formatter_labels_grammar.lime',
  ],
)

# Lime-Letter-21 (v0.3.5): per-%token-group and per-%type-group
# leading-comment preservation.  v0.3.4 emitted both sections in
# symbol-index order with no comment slots, dropping ~120-150 lines
# of section banners on every PG `lime -F` round-trip across
# gram.lime's 12 keyword-section banners + 5 type-block banners.
# This is the cheap variant PG accepted: one banner slot per RUN
# of contiguous same-kind directives, inter-symbol comments inside
# a run dropped.  Eight sub-tests cover header preservation, three
# %token banners, two %type banners, declaration-order emit (vs
# index-order shuffle), and idempotence.
test_formatter_token_groups = executable('test_formatter_token_groups',
  'test_formatter_token_groups.c',
  dependencies : lime_parser_dep,
)

test('formatter_token_groups', test_formatter_token_groups,
  args : [
    lime_exe.full_path(),
    meson.current_source_dir() / 'test_formatter_token_groups_grammar.lime',
  ],
)

# v0.3.6 / Letter-22 regression: `lime -F` must preserve %left /
# %right / %nonassoc precedence directives.  Pre-fix the formatter
# silently dropped them, exploding PG's gram.lime canonicalize pass
# from 0 to 1682 parsing conflicts.  Six sub-tests cover keyword
# survival, group preservation, [SYMBOL] override, conflict-count
# parity (source vs formatted), and idempotence.
test_formatter_precedence = executable('test_formatter_precedence',
  'test_formatter_precedence.c',
  dependencies : lime_parser_dep,
)

test('formatter_precedence', test_formatter_precedence,
  args : [
    lime_exe.full_path(),
    meson.project_source_root() / 'limpar.c',
    meson.current_source_dir() / 'test_formatter_precedence_grammar.lime',
  ],
)

# Regression test for the precedence-directive leading-comment
# preservation gap: comments above %left / %right / %nonassoc were
# silently dropped on every release through v0.4.4.  Five sub-tests
# cover banner survival across three precedence levels, idempotence,
# and that each banner emits adjacent to its directive (not stranded
# elsewhere in the output).
test_formatter_prec_comments = executable('test_formatter_prec_comments',
  'test_formatter_prec_comments.c',
  dependencies : lime_parser_dep,
)

test('formatter_prec_comments', test_formatter_prec_comments,
  args : [
    lime_exe.full_path(),
    meson.current_source_dir() / 'test_formatter_prec_comments_grammar.lime',
  ],
)

# Lime-Letter-23: regression test for %first_token / %locations /
# %location_type formatter preservation.  Pre-fix all three were
# silently dropped on every release v0.3.0 .. v0.5.1; PG's gram.lime
# and pl_gram.lime hit gcc errors on every action body that
# referenced &yyloc.  Four sub-tests cover each directive's survival
# plus idempotence.
test_formatter_loc_directives = executable('test_formatter_loc_directives',
  'test_formatter_loc_directives.c',
  dependencies : lime_parser_dep,
)

test('formatter_loc_directives', test_formatter_loc_directives,
  args : [
    lime_exe.full_path(),
    meson.current_source_dir() / 'test_formatter_loc_directives_grammar.lime',
  ],
)

# Lime-Letter-23 follow-up: table-driven directive-emit registry
# regression test.  Sub-tests check coverage, idempotence,
# category placement, and byte-identity vs the v0.5.5 baseline.
# #includes ../lime.c with LIME_TEST_HARNESS so it can read the
# static lime_directives[] array directly; takes the lime binary
# path on argv[1] for subprocess-driven fixture rounds.  See the
# top-of-file comment in test_formatter_directive_registry.c.
test_formatter_directive_registry = executable(
  'test_formatter_directive_registry',
  'test_formatter_directive_registry.c',
  link_with : [lime_lex_compiler_lib],
  include_directories : [inc, include_directories('../src/lex')],
  c_args : ['-DLIME_TEST_HARNESS', '-DLIME_HAS_LEX_COMPILER',
            '-Wno-unused-function', '-Wno-unused-parameter',
            '-Wno-format-overflow', '-Wno-format-truncation'],
)

test('formatter_directive_registry', test_formatter_directive_registry,
  args : [
    lime_exe.full_path(),
    meson.project_source_root(),
  ],
  depends : [lime_exe],
)

# v0.6.x: explicit byte-equality regression locking single-pass
# formatter idempotence -- format(format(F)) == format(F) -- across
# every formatter fixture.  Catches any future drift in directive-
# emit ordering, prec-symbol attribution, body-trim semantics, or
# token-group categorisation.
test_formatter_idempotence = executable('test_formatter_idempotence',
  'test_formatter_idempotence.c',
)

test('formatter_idempotence', test_formatter_idempotence,
  args : [
    lime_exe.full_path(),
    meson.project_source_root(),
  ],
  depends : [lime_exe],
)

# Lime-Letter-23: mid-RHS comment preservation in `lime -F`.  Formatter
# now captures and re-emits comments appearing between ::= and . at their
# original positions.  Two sub-tests: (1) format preserves all mid-RHS
# comments, (2) idempotence -- format(format(F)) == format(F).
test_formatter_mid_rhs_comments = executable('test_formatter_mid_rhs_comments',
  'test_formatter_mid_rhs_comments.c',
  dependencies : lime_parser_dep,
)

test('formatter_mid_rhs_comments', test_formatter_mid_rhs_comments,
  args : [
    lime_exe.full_path(),
    meson.current_source_dir() / 'test_formatter_mid_rhs_comments_grammar.lime',
  ],
)

# v0.5.0: lint pass expanded from a 4-rule stub to ~16 opinionated
# rules.  Each fixture in tests/test_lint_fixtures/ is constructed
# to fire exactly one rule (plus incidental neighbours like W005
# in conflict-bearing fixtures); the harness runs lime -L on each
# and asserts the structured JSON diagnostic for the targeted rule
# appears.  See docs/LINT.md for the rule catalog.
test_lint = executable('test_lint',
  'test_lint.c',
)

test('lint', test_lint,
  args : [
    lime_exe.full_path(),
    meson.current_source_dir() / 'test_lint_fixtures',
  ],
)

# v0.4.0: %dialect NAME { ... } directive (generator-time conditional
# inclusion).  Drives the lime binary on test_dialect_grammar.lime
# under several -D combinations and asserts the build fingerprint
# matches expected inclusion.  Also exercises the CLI shorthand
# (-Ddialect=NAME == -Ddialect_NAME), idempotence, brace-in-string
# scan, unterminated/nested error paths, and bad-name rejection.
# 11 sub-tests; see tests/test_dialect.c.
test_dialect = executable('test_dialect',
  'test_dialect.c',
)

test('dialect', test_dialect,
  args : [
    lime_exe.full_path(),
    meson.project_source_root() / 'limpar.c',
    meson.current_source_dir() / 'test_dialect_grammar.lime',
  ],
)

# v0.4.1: %extends + %override + %remove + %override_type +
# diamond inheritance.  Drives lime against tests/extends_fixtures/
# (ansi_sql / oracle / mysql / unified) and several inline
# fixtures generated at runtime (cycle, conflict, alias-identity).
# 12 sub-tests; see tests/test_extends.c for the full matrix.
test_extends = executable('test_extends',
  'test_extends.c',
)
test('extends', test_extends,
  args : [
    lime_exe.full_path(),
    meson.project_source_root() / 'limpar.c',
    meson.current_source_dir() / 'extends_fixtures',
  ],
)

# v0.4.3: --diff-conflicts CLI mode.  Drives lime against three
# fixtures in tests/diff_conflicts_fixtures/ (clean base, clean
# extension, conflict-introducing extension) and asserts the
# NEW / RESOLVED / UNCHANGED bucket counts, exit-code contract
# (0=safe / 1=NEW conflicts / 2=arg-error), JSON well-formedness,
# and symbolic-key stability across runs.
# 11 sub-tests; see tests/test_diff_conflicts.c for the matrix.
test_diff_conflicts = executable('test_diff_conflicts',
  'test_diff_conflicts.c',
)
test('diff_conflicts', test_diff_conflicts,
  args : [
    lime_exe.full_path(),
    meson.project_source_root() / 'limpar.c',
    meson.current_source_dir() / 'diff_conflicts_fixtures',
  ],
)

# v0.4.4: %embed lang TRIGGER 'lex' ENTRY_TOKEN TOKEN.  Sugar over
# context_switch_register_trigger() that auto-generates the
# <Prefix>_embed_table[] + <Prefix>SetEmbedSnapshot() +
# <Prefix>RegisterEmbedTriggers() helpers in the snapshot file.
# Sub-tests 1-8 drive the lime binary; sub-test 9 links against a
# pre-built fixture parser and exercises the runtime classify path.
test_embed_gen = custom_target('test_embed_parser',
  input   : ['test_embed_grammar.lime'],
  output  : ['test_embed_grammar.c',
             'test_embed_grammar.h',
             'test_embed_grammar_snapshot.c'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-n',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_embed = executable('test_embed',
  'test_embed.c',
  test_embed_gen,
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
  c_args : nounusedparam,
)

test('embed', test_embed,
  args : [
    lime_exe.full_path(),
    meson.project_source_root() / 'limpar.c',
    meson.current_source_dir() / 'test_embed_grammar.lime',
  ],
)

test_parse_context_coverage = executable('test_parse_context_coverage',
  'test_parse_context_coverage.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('parse_context_coverage', test_parse_context_coverage)

# ----------------------------------------------------------------------
# GLR (Generalized LR) parser tests.
# test_gss is a direct unit test of the Graph-Structured Stack
# primitives.  test_glr_no_conflict and test_glr_ambiguous drive
# the arith snapshot (generated below as test_runtime_arith_gen)
# and require GLR's lime_parse_glr_* runtime entry points.
# Resurrected from feat/glr branch in v0.3.4.
# ----------------------------------------------------------------------
test_gss = executable('test_gss',
  'test_gss.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('gss', test_gss)


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

# ====================================================================
# v0.5.2: end-to-end composition test against runtime-loaded plugins
# ====================================================================
#
# Builds three small .so plugins (sql_lite_plugin, json_plugin,
# xml_plugin) under tests/composition_e2e_fixtures/.  Each plugin
# implements lime_plugin_entry() and a create_snapshot() callback
# that produces an identifiable synthetic ParserSnapshot.  The
# test driver dlopens them through ParserManager, composes the
# resulting snapshots via compose_snapshots() with merkle-root
# computation, and asserts:
#
#   1. Composition succeeds with expected summed counts.
#   2. Re-composing identical inputs yields identical merkle roots
#      (the determinism property the merkle-cache architecture
#      relies on -- see docs/COMPOSITION.md).
#   3. Composing with a different plugin yields a different merkle
#      root (regression guard against silent cache-key drift).
#
# Skipped on Windows: ParserManager's plugin loader is dlopen()-based.
# Same skip rationale as test_snapshot_create above.
if host_machine.system() != 'windows'
  # Plugin .so build args mirror examples/plugin_template/meson.build:
  # plugins do not link the parser dep -- they resolve at dlopen()
  # time against the host process (the test driver, built with
  # export_dynamic : true).  Override -Wl,--no-undefined accordingly.
  composition_e2e_plugin_link_args = []
  if host_machine.system() == 'darwin'
    composition_e2e_plugin_link_args += ['-Wl,-undefined,dynamic_lookup']
  else
    composition_e2e_plugin_link_args += ['-Wl,--unresolved-symbols=ignore-in-object-files']
  endif

  composition_e2e_sql_so = shared_library('composition_e2e_sql_lite',
    'composition_e2e_fixtures/sql_lite_plugin.c',
    include_directories : [include_directories('../include'),
                           include_directories('../src')],
    link_args : composition_e2e_plugin_link_args,
    install : false,
  )

  composition_e2e_json_so = shared_library('composition_e2e_json',
    'composition_e2e_fixtures/json_plugin.c',
    include_directories : [include_directories('../include'),
                           include_directories('../src')],
    link_args : composition_e2e_plugin_link_args,
    install : false,
  )

  composition_e2e_xml_so = shared_library('composition_e2e_xml',
    'composition_e2e_fixtures/xml_plugin.c',
    include_directories : [include_directories('../include'),
                           include_directories('../src')],
    link_args : composition_e2e_plugin_link_args,
    install : false,
  )

  test_composition_e2e = executable('test_composition_e2e',
    'test_composition_e2e.c',
    dependencies : lime_parser_dep,
    include_directories : include_directories('../src'),
    export_dynamic : true,
  )

  test('composition_e2e', test_composition_e2e,
    args : [
      composition_e2e_sql_so.full_path(),
      composition_e2e_json_so.full_path(),
      composition_e2e_xml_so.full_path(),
    ],
    depends : [
      composition_e2e_sql_so,
      composition_e2e_json_so,
      composition_e2e_xml_so,
    ],
    timeout : 60,
  )

  # ==================================================================
  # v0.5.2: parse-manager(1) integration test
  # ==================================================================
  #
  # Spawns the meson-built parse-manager CLI as a subprocess and
  # asserts each documented command (version, help, list, load,
  # unknown-command) round-trips through the tool's argv parser
  # and ParserManager backend cleanly.  Drives it against the
  # built sql_plugin.so from examples/plugin_template -- if either
  # the CLI surface or the plugin ABI regresses, this test fails.
  #
  # Depends on the parse_manager_exe + sql_plugin_lib targets,
  # both declared in subdir('tools') and subdir('examples/plugin_template')
  # respectively, which the root meson.build wires before
  # subdir('tests').
  test_parse_manager_bin = executable('test_parse_manager',
    'test_parse_manager.c',
    dependencies : lime_parser_dep,
    include_directories : include_directories('../src'),
  )

  test('parse_manager_cli', test_parse_manager_bin,
    args : [
      parse_manager_exe.full_path(),
      sql_plugin_lib.full_path(),
    ],
    depends : [parse_manager_exe, sql_plugin_lib],
    timeout : 60,
  )
endif

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

# Bayesian strategy unit tests
test_strategy_bayesian = executable('test_strategy_bayesian',
  'test_strategy_bayesian.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('strategy_bayesian', test_strategy_bayesian)

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

# Context-switch trigger registry: unit tests for
# src/context_switch.c (register_trigger / classify_lexeme /
# needed / detect_exit).
test_context_switch_unit = executable('test_context_switch_unit',
  'test_context_switch_unit.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('context_switch_unit', test_context_switch_unit)

# Context-switch end-to-end test -- drives the
# examples/multi_grammar_sql_json driver through C and asserts on
# the produced AST shape.  Covers clean trigger + exit, multiple
# triggers in one input, and the no-triggers fast path.
test_context_switch_e2e = executable('test_context_switch_e2e',
  'test_context_switch_e2e.c',
  dependencies : [lime_parser_dep, multi_dep],
  include_directories : include_directories('../src'),
)

test('context_switch_e2e', test_context_switch_e2e)

# Context-switch error-path test -- exercises lex/parse failure
# paths and registry edge cases (unterminated json region,
# malformed json body, NULL embedded snapshot).
test_context_switch_error = executable('test_context_switch_error',
  'test_context_switch_error.c',
  dependencies : [lime_parser_dep, multi_dep],
  include_directories : include_directories('../src'),
)

test('context_switch_error', test_context_switch_error)

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
  c_args : nounusedparam,
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
  c_args : nounusedparam,
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
  c_args : nounusedparam,
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
  c_args : nounusedparam,
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
  c_args : nounusedparam,
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
  c_args : nounusedparam,
)

test('yylloc_action_assign', test_yylloc_action_assign)

# P0-NEW-11: action-body letter-label substitution must respect
# string literals, char literals, and comments.  The pre-fix
# walk in lime.c:translate_code rewrote bare alphabetic
# identifiers blindly, so PG's contrib/cube saw `(A)` LHS aliases
# rewritten inside `errdetail("A cube cannot have more than %d
# ...")` strings to `errdetail("yylhsminor.yy0 cube ...")`.
# This test exercises every state-machine boundary: "...", '...',
# // ..., /* ... */, escape sequences, and adjacent strings.  The
# driver verifies (a) runtime: captured literal bytes equal the
# grammar-source bytes, and (b) source: the generated .c contains
# the literals verbatim AND substituted the bare-A in code
# position to a yymsp[...] reference.
test_action_string_literal_gen = custom_target('test_action_string_literal_parser',
  input   : ['test_action_string_literal_grammar.y'],
  output  : ['test_action_string_literal_grammar.c',
             'test_action_string_literal_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_action_string_literal_gen_c = meson.current_build_dir() / \
  'test_action_string_literal_grammar.c'

test_action_string_literal = executable('test_action_string_literal',
  'test_action_string_literal.c',
  test_action_string_literal_gen,
  dependencies : lime_parser_dep,
  c_args : nounusedparam + ['-DACTION_STRLIT_GEN_C="' + test_action_string_literal_gen_c + '"'],
)

test('action_string_literal', test_action_string_literal)

# M1 phase 1 of the lexer subsystem: .lex source-language
# tokenizer.  No DFA, no parser yet -- just the byte-level
# token stream that the M1 parser will consume.
test_lex_tokenize = executable('test_lex_tokenize',
  'test_lex_tokenize.c',
  dependencies : lime_lex_compiler_dep,
  c_args : nounusedparam,
)

test('lex_tokenize', test_lex_tokenize)

# M1.2 of the lexer subsystem: .lex parser (token stream -> AST).
# Pattern resolution and block desugaring are M1.3/M1.4.
test_lex_parse = executable('test_lex_parse',
  'test_lex_parse.c',
  dependencies : lime_lex_compiler_dep,
  c_args : nounusedparam,
)

test('lex_parse', test_lex_parse)

# M1.3 of the lexer subsystem: %pattern fragment resolution with
# cycle detection.
test_lex_resolve = executable('test_lex_resolve',
  'test_lex_resolve.c',
  dependencies : lime_lex_compiler_dep,
  c_args : nounusedparam,
)

test('lex_resolve', test_lex_resolve)

# M1.5 of the lexer subsystem: pretty-printer round-trip.
# parse(text) -> A; pretty(A) -> text'; parse(text') -> A';
# pretty(A') -> text''.  Assert text' == text'' (idempotent)
# AND A == A' (structural).  M1 ship gate.
test_lex_round_trip = executable('test_lex_round_trip',
  'test_lex_round_trip.c',
  dependencies : lime_lex_compiler_dep,
  c_args : nounusedparam,
)

test('lex_round_trip', test_lex_round_trip)

# M2.1 of the lexer subsystem: regex parser.  Builds an AST
# from regex source strings (POSIX-extended subset).  Input to
# the M2.2 NFA construction (Thompson's algorithm).
test_lex_regex = executable('test_lex_regex',
  'test_lex_regex.c',
  dependencies : lime_lex_compiler_dep,
  c_args : nounusedparam,
)

test('lex_regex', test_lex_regex)

# M2.2 of the lexer subsystem: Thompson's NFA construction +
# bounded NFA simulator (testing only).  Inputs are regex ASTs
# from M2.1; output is a fragment NFA fed to M2.3 subset
# construction.
test_lex_nfa = executable('test_lex_nfa',
  'test_lex_nfa.c',
  dependencies : lime_lex_compiler_dep,
  c_args : nounusedparam,
)

test('lex_nfa', test_lex_nfa)

# M2.3 of the lexer subsystem: subset construction (NFA -> DFA).
test_lex_dfa = executable('test_lex_dfa',
  'test_lex_dfa.c',
  dependencies : lime_lex_compiler_dep,
  c_args : nounusedparam,
)

test('lex_dfa', test_lex_dfa)

# M2.4 of the lexer subsystem: DFA minimization (Moore's
# algorithm).  Asserts language preservation, no state growth,
# and strict reduction on redundant patterns.
test_lex_dfa_min = executable('test_lex_dfa_min',
  'test_lex_dfa_min.c',
  dependencies : lime_lex_compiler_dep,
  c_args : nounusedparam,
)

test('lex_dfa_min', test_lex_dfa_min)

# M2.5a of the lexer subsystem: multi-rule NFA combiner +
# accept-rule tagging.  Tests the NFA combine + subset
# construction's lowest-rule-id resolution on overlap.
test_lex_combine = executable('test_lex_combine',
  'test_lex_combine.c',
  dependencies : lime_lex_compiler_dep,
  c_args : nounusedparam,
)

test('lex_combine', test_lex_combine)

# M2.5b of the lexer subsystem: per-state spec compilation.
# End-to-end pipeline: spec source -> parse -> resolve ->
# compile per-state DFAs.
test_lex_compile = executable('test_lex_compile',
  'test_lex_compile.c',
  dependencies : lime_lex_compiler_dep,
  c_args : nounusedparam,
)

test('lex_compile', test_lex_compile)

# M4.1 of the lexer subsystem: %lexer_include filtering.
# When %lexer_include is present, only the named rulesets
# contribute rules to the compiled DFA, in include-list order.
# When absent, every ruleset's rules are pulled (legacy).  An
# undefined ruleset name in %lexer_include bumps error_count.
# Skipped on Windows: relies on POSIX pipe() to capture stderr,
# which has no direct MSVC equivalent (would need _pipe() rework).
if host_machine.system() != 'windows'
  test_lex_lexer_include = executable('test_lex_lexer_include',
    'test_lex_lexer_include.c',
    dependencies : lime_lex_compiler_dep,
    c_args : nounusedparam,
  )

  test('lex_lexer_include', test_lex_lexer_include)
endif

# M4.2 of the lexer subsystem: lime_lex_compiled_to_text
# introspection serializer.  Round-trippable .lex dump of a
# compiled lexer paired with its source spec, prefixed by a
# comment block of per-state DFA stats.
test_lex_introspect = executable('test_lex_introspect',
  'test_lex_introspect.c',
  dependencies : lime_lex_compiler_dep,
  c_args : nounusedparam,
)

test('lex_introspect', test_lex_introspect)

# M3.1 of the lexer subsystem: emit DFA tables + longest-match
# driver as compilable C source.  Foundation for the M3.2+
# runtime API layer.
# runtime API layer.
# Skipped on Windows: relies on POSIX open_memstream() which has
# no direct MSVC equivalent.  Could be rewritten with a tmpfile
# round-trip; tracked as a follow-up.
if host_machine.system() != 'windows'
  test_lex_emit = executable('test_lex_emit',
    'test_lex_emit.c',
    dependencies : lime_lex_compiler_dep,
    c_args : nounusedparam,
  )

  test('lex_emit', test_lex_emit)
endif

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
  c_args : nounusedparam,
)

test('lex_runtime', test_lex_runtime)

# M3.4 of the lexer subsystem: action-body inlining + LEX_*
# macros.  Same end-to-end shape as test_lex_runtime, but the
# generated FeedBytes inlines each rule's action body inside a
# switch and exposes LEX_EMIT/LEX_SKIP/LEX_TRANSITION/
# LEX_TERMINATE/LEX_ERROR_AT to user code.  Empty action bodies
# still auto-emit -- preserves the M3.3 contract.
test_lex_actions_gen = custom_target('test_lex_actions_lex',
  input   : ['test_lex_actions_grammar.lex'],
  output  : ['test_lex_actions_grammar_lex.c',
             'test_lex_actions_grammar_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_lex_actions = executable('test_lex_actions',
  'test_lex_actions.c',
  test_lex_actions_gen,
  c_args : nounusedparam,
)

test('lex_actions', test_lex_actions)

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
  c_args : nounusedparam,
)

test('lex_include', test_lex_include)

# M3.4 + M3.5 merge bridge: cross-feature integration test
# verifying LEX_PUSHBACK in an action body (M3.4 macro contract)
# correctly delegates to LexPushback (M3.5 runtime) and the
# rewound buffer-stack frame is re-matched on the next
# iteration.  Also verifies caller-driven LexInclude composes
# with LexFeedBytes when the include sits on top of the stack
# before the feed begins.
test_lex_m3_integration_gen = custom_target('test_lex_m3_integration_lex',
  input   : ['test_lex_m3_integration_grammar.lex'],
  output  : ['test_lex_m3_integration_grammar_lex.c',
             'test_lex_m3_integration_grammar_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_lex_m3_integration = executable('test_lex_m3_integration',
  'test_lex_m3_integration.c',
  test_lex_m3_integration_gen,
  c_args : nounusedparam,
)

test('lex_m3_integration', test_lex_m3_integration)

# M3.7 + P0-NEW-9 of the lexer subsystem: %literal_buffer
# runtime emission and %include block emission.  Same generator
# pipeline as test_lex_runtime: lime -X compiles the .lex grammar
# to .c + .h, the test driver compiles against the generated
# header and exercises:
#   - LEX_BUF_START / APPEND / APPEND_CH / TAKE / LEN / PEEK
#     macros under both *N (multiplicative) and +N (additive)
#     growth policies
#   - the %include block's static helper called from an action
#     body (the compile would fail if include emission missed)
#   - two %literal_buffer declarations operating independently
#   - LexFree cleanup of a dangling buffer (caller never
#     invoked LEX_BUF_TAKE)
test_lex_buf_gen = custom_target('test_lex_buf_lex',
  input   : ['test_lex_buf_grammar.lex'],
  output  : ['test_lex_buf_grammar_lex.c',
             'test_lex_buf_grammar_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_lex_buf = executable('test_lex_buf',
  'test_lex_buf.c',
  test_lex_buf_gen,
  c_args : nounusedparam,
)

test('lex_buf', test_lex_buf)

# M3.6 of the lexer subsystem: <<EOF>> rule dispatch in the
# LexFeedBytes auto-pop branch.  Two grammars feed one driver:
#   - test_lex_eof_grammar.lex declares both an unqualified
#     <<EOF>> rule (firing in INITIAL) and an <XQ><<EOF>> rule
#     (firing only in the exclusive XQ state); the driver
#     verifies per-state dispatch, LEX_EMIT and LEX_ERROR_AT
#     inside an EOF action, and EOF firing on every
#     buffer-stack frame's auto-pop including LexInclude'd
#     frames.
#   - test_lex_eof_none_grammar.lex declares zero EOF rules and
#     pins the regression that LexFeedBytes still returns
#     LEX_OK on end-of-input when the codegen omits the EOF
#     dispatch entirely (the `any_eof == 0` branch).
test_lex_eof_gen = custom_target('test_lex_eof_lex',
  input   : ['test_lex_eof_grammar.lex'],
  output  : ['test_lex_eof_grammar_lex.c',
             'test_lex_eof_grammar_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_lex_eof_none_gen = custom_target('test_lex_eof_none_lex',
  input   : ['test_lex_eof_none_grammar.lex'],
  output  : ['test_lex_eof_none_grammar_lex.c',
             'test_lex_eof_none_grammar_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_lex_eof = executable('test_lex_eof',
  'test_lex_eof.c',
  test_lex_eof_gen,
  test_lex_eof_none_gen,
  c_args : nounusedparam,
)

test('lex_eof', test_lex_eof)

# M4.3 of the lexer subsystem: per-rule test entry points.
# The generator emits a thin wrapper per non-EOF rule:
#   FooLexResult Foo_test_rule_<name>(input, n, &consumed);
# Wrappers invoke Foo_match in the rule's first declared state
# qualifier (or INITIAL when unqualified) and report LEX_OK
# only when that specific rule wins the longest-match.  EOF
# rules are excluded -- they aren't reachable through Foo_match
# (M3.6 dispatches them from LexFeedBytes' auto-pop branch).
#
# The driver also reads back the generated .c file (path passed
# via -DPER_RULE_GEN_C) to assert the EOF rule's wrapper is
# absent.  meson.current_build_dir() pins the path stably across
# build configurations.
test_lex_per_rule_gen = custom_target('test_lex_per_rule_lex',
  input   : ['test_lex_per_rule_grammar.lex'],
  output  : ['test_lex_per_rule_grammar_lex.c',
             'test_lex_per_rule_grammar_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_lex_per_rule_gen_c = meson.current_build_dir() / 'test_lex_per_rule_grammar_lex.c'

test_lex_per_rule = executable('test_lex_per_rule',
  'test_lex_per_rule.c',
  test_lex_per_rule_gen,
  c_args : nounusedparam + ['-DPER_RULE_GEN_C="' + test_lex_per_rule_gen_c + '"'],
)

test('lex_per_rule', test_lex_per_rule)

# P0-NEW-13: LexFeedBytes must pop frames pushed during the call
# on every return path -- normal completion, LEX_TERMINATE,
# LEX_ERROR_AT, unmatched input.  Without this, an action body
# that fires LEX_TERMINATE leaks the bottom frame each call;
# after FOO_LEX_MAX_INCLUDE_DEPTH iterations every subsequent
# LexFeedBytes returns LEX_ERROR("include depth exceeded").
# Reproduces ecpg's pgc.c failure mode in isolation: 200
# back-to-back LexFeedBytes calls on the same input, each
# action LEX_TERMINATEs after the first token, and depth must
# stay at 0 between calls.
test_lex_terminate_frame_gen = custom_target('test_lex_terminate_frame_lex',
  input   : ['test_lex_terminate_frame_grammar.lex'],
  output  : ['test_lex_terminate_frame_grammar_lex.c',
             'test_lex_terminate_frame_grammar_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_lex_terminate_frame = executable('test_lex_terminate_frame',
  'test_lex_terminate_frame.c',
  test_lex_terminate_frame_gen,
  c_args : nounusedparam,
)

test('lex_terminate_frame', test_lex_terminate_frame)

# P0-NEW-12: %lexer_extra_argument threads a user-declared
# parameter through LexFeedBytes() into action bodies.  Replaces
# the file-scope-extern workaround the PG migration team used
# (loses thread-safety + per-instance isolation).  The parameter
# lands at the END of the LexFeedBytes argument list, after
# user_ctx.  Two sub-tests: basic threading (action body reads
# and mutates extra), and per-instance isolation (two lexers
# each with their own extra struct).
test_lex_extra_arg_gen = custom_target('test_lex_extra_arg_lex',
  input   : ['test_lex_extra_arg_grammar.lex'],
  output  : ['test_lex_extra_arg_grammar_lex.c',
             'test_lex_extra_arg_grammar_lex.h'],
  command : [
    lime_exe,
    '-X',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_lex_extra_arg = executable('test_lex_extra_arg',
  'test_lex_extra_arg.c',
  test_lex_extra_arg_gen,
  c_args : nounusedparam,
)

test('lex_extra_arg', test_lex_extra_arg)

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
  c_args : nounusedparam,
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
  c_args : nounusedparam,
)

test('default_reduce', test_default_reduce)

# v0.6.0: per-rule reduce action callbacks replace the historical
# yy_reduce switch.  The test (a) audits the generated .c file
# for the presence of per-rule yy_rule_<N> functions and the
# yy_rule_reduce_fn[] dispatch table, (b) drives the parser with
# several token streams and asserts the result + rule-firing
# trace match a hand-evaluated oracle, (c) checks the dispatch
# array is `static const` so composition can concatenate it
# safely, (d) verifies the function naming schema is
# `yy_rule_<N>` so a future profile-guided pass can splice in
# __attribute__((hot)) by name.
test_per_rule_reduce_gen = custom_target('test_per_rule_reduce_parser',
  input   : ['test_per_rule_reduce_grammar.y'],
  output  : ['test_per_rule_reduce_grammar.c',
             'test_per_rule_reduce_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_per_rule_reduce = executable('test_per_rule_reduce',
  'test_per_rule_reduce.c',
  test_per_rule_reduce_gen,
  dependencies : lime_parser_dep,
  c_args : nounusedparam,
)

test('per_rule_reduce', test_per_rule_reduce,
  args : [
    meson.current_build_dir() / 'test_per_rule_reduce_grammar.c',
  ],
)

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
  c_args : nounusedparam,
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
  c_args : nounusedparam,
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

# Forensic harness that probes runtime extensibility and JIT claims
# against the actual code paths.  Always exits 0; verdicts in stdout.
test_claims_proof = executable('test_claims_proof',
  'test_claims_proof.c',
  include_directories : include_directories('../src'),
  dependencies : lime_parser_dep,
)

test('claims_proof', test_claims_proof)

# Generate an arithmetic parser locally for the runtime parse test
# (separate from bench/, since meson custom_target outputs are scoped
# to their declaring subdir).
test_runtime_arith_gen = custom_target('test_runtime_arith_parser',
  input   : ['../bench/bench_arith_grammar.y'],
  output  : ['bench_arith_grammar.c', 'bench_arith_grammar.h',
             'bench_arith_grammar_snapshot.c'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-n',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

# AOT codegen output for the same arith grammar -- generates
# bench_arith_grammar_aot.c via the `lime -j` flag.  Used by the
# AOT regression test below to check that the AOT path's
# yy_find_shift_action_aot() agrees with the table-driven
# yy_find_shift_action() across every (state, lookahead) pair.
test_runtime_arith_aot_gen = custom_target('test_runtime_arith_aot',
  input   : ['../bench/bench_arith_grammar.y'],
  output  : ['bench_arith_grammar_aot.c'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-j',  # emit *_aot.c
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

# End-to-end: take a real lime-generated parser, build a runtime
# snapshot from it via <Prefix>BuildSnapshot, then drive parse_token
# against it.  Proves Claim C3 in test_claims_proof.
test_runtime_parse = executable('test_runtime_parse',
  'test_runtime_parse.c',
  test_runtime_arith_gen,
  include_directories : include_directories('../src'),
  dependencies : lime_parser_dep,
)

test('runtime_parse', test_runtime_parse)

# Direct unit-test coverage of the LALR runtime hot path
# (src/parse_engine.c).  Reuses the same arith-grammar snapshot as
# test_runtime_parse but adds synthetic snapshots, NULL-safety,
# stack-growth, fallback-retry, and threaded refcount tests that
# integration tests miss.
test_parse_engine = executable('test_parse_engine',
  'test_parse_engine.c',
  test_runtime_arith_gen,
  include_directories : include_directories('../src'),
  dependencies : lime_parser_dep,
)

test('parse_engine', test_parse_engine, timeout : 60)

# GLR end-to-end tests.  Reuse test_runtime_arith_gen above (same
# arith grammar both the bench and the runtime tests use).
test_glr_no_conflict = executable('test_glr_no_conflict',
  'test_glr_no_conflict.c',
  test_runtime_arith_gen,
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('glr_no_conflict', test_glr_no_conflict)

test_glr_ambiguous = executable('test_glr_ambiguous',
  'test_glr_ambiguous.c',
  test_runtime_arith_gen,
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('glr_ambiguous', test_glr_ambiguous)


# End-to-end extension exercise: drives create_modified_snapshot
# against a real generated parser snapshot and confirms parse_token
# still works on the result.
test_extension_e2e = executable('test_extension_e2e',
  'test_extension_e2e.c',
  test_runtime_arith_gen,
  include_directories : include_directories('../src'),
  dependencies : lime_parser_dep,
)

test('extension_e2e', test_extension_e2e)

# End-to-end test of lime_snapshot_create() -- the runtime
# subprocess pipeline that takes a .y file, runs lime + cc on it,
# dlopens the result, and returns a ParserSnapshot.  Skipped at
# runtime when lime/cc are not available.  Skipped at configure
# time on Windows: the underlying fork/exec/dlopen path is
# stubbed (returns NULL with a "not yet supported" error) on
# this platform, so the test would always skip-at-runtime; no
# value in compiling it.
if host_machine.system() != 'windows'
  test_snapshot_create = executable('test_snapshot_create',
    'test_snapshot_create.c',
    include_directories : include_directories('../src'),
    dependencies : lime_parser_dep,
    # Export executable symbols so the .so produced by
    # lime_snapshot_create at runtime can resolve snapshot_release /
    # create_base_snapshot / etc. via dlopen against the parent
    # binary.  Required on FreeBSD (where the default is hidden);
    # harmless on Linux / macOS where the default is already export.
    export_dynamic : true,
  )

  test('snapshot_create', test_snapshot_create,
    workdir : meson.project_source_root(),
    is_parallel : false,
  )
endif

# v0.6.x: cross-platform regression for lime_snapshot_create routed
# through the in-process API.  On Windows this exercises the path that
# was previously stubbed (no fork/dlopen); on Unix it confirms the
# in-process fast path and the LIME_FORCE_SUBPROCESS=1 fallback both
# work.  Links lime_compiler_dep so the weak
# lime_compile_grammar_in_process reference in src/snapshot_create.c
# resolves; without it the fast path is silently skipped.
test_windows_snapshot_create = executable('test_windows_snapshot_create',
  'test_windows_snapshot_create.c',
  include_directories : include_directories('../src'),
  dependencies : [lime_parser_dep, lime_compiler_dep],
  export_dynamic : true,
)

test('windows_snapshot_create', test_windows_snapshot_create,
  workdir : meson.project_source_root(),
  is_parallel : false,
)

# End-to-end exercise of runtime LALR(1) automaton rebuild via the
# subprocess pipeline.  Adds a postfix-factorial extension to the
# arithmetic grammar, calls publish_modified_snapshot which
# concatenates the original grammar source with the serialised
# modifications and reruns lime+cc, then parses input that exercises
# the new rule.  Skipped at runtime when lime or cc is unavailable.
# Same Windows-skip rationale as test_snapshot_create above.
if host_machine.system() != 'windows'
  test_extension_rebuild = executable('test_extension_rebuild',
    'test_extension_rebuild.c',
    test_runtime_arith_gen,
    include_directories : include_directories('../src'),
    dependencies : lime_parser_dep,
    export_dynamic : true,
  )

  test('extension_rebuild', test_extension_rebuild,
    workdir : meson.project_source_root(),
    is_parallel : false,
  )
endif

# Equivalence test: the JIT-armed parse_token path produces the same
# accept/reject outcomes as the table-driven path on a battery of
# well-formed and malformed inputs.  Skipped at runtime when LLVM is
# unavailable (lime_jit_compile returns non-zero).
test_jit_parse_equivalence = executable('test_jit_parse_equivalence',
  'test_jit_parse_equivalence.c',
  test_runtime_arith_gen,
  include_directories : include_directories('../src'),
  dependencies : lime_parser_dep,
)

test('jit_parse_equivalence', test_jit_parse_equivalence)

# v0.6.x: per-rule reducer inlining policy classification test.
# Exercises jit_can_inline_rule against a corpus of representative rule
# bodies (empty, passthrough, simple expression, function call,
# control flow) to lock the conservative-when-in-doubt policy.
# Skipped at runtime when LLVM is disabled (LIME_NO_JIT) -- the
# inlining policy doesn't matter when there's no JIT to inline into.
test_jit_inline = executable('test_jit_inline',
  'test_jit_inline.c',
  dependencies : lime_parser_dep,
  include_directories : include_directories('../src'),
)

test('jit_inline', test_jit_inline)

# Letter-15 regression: AOT path must agree with the
# table-driven path for every (state, lookahead) pair.
test_aot_equivalence = executable('test_aot_equivalence',
  'test_aot_equivalence.c',
  test_runtime_arith_gen,
  test_runtime_arith_aot_gen,
  include_directories : include_directories('../src'),
  dependencies : lime_parser_dep,
)

test('aot_equivalence', test_aot_equivalence)

# Letter 16 (May 2026) regression: AOT codegen used to silently
# drop explicit ERROR action-table entries from the per-state
# action loop.  This test uses a tiny grammar with %nonassoc on
# a comparison operator -- the conflict resolver promotes the
# shift action to ERROR when same-precedence shift/reduce can't
# break the tie via associativity, producing explicit ERROR
# action entries that the AOT path must emit.  Pre-fix the
# AOT-vs-table sweep diverges on ~3 states; post-fix silent.
test_aot_error_gen = custom_target('aot_error_gen',
  input   : ['aot_error_grammar.y'],
  output  : ['aot_error_grammar.c', 'aot_error_grammar.h',
             'aot_error_grammar_snapshot.c'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-n',  # emit *_snapshot.c with <Prefix>BuildSnapshot
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_aot_error_aot_gen = custom_target('aot_error_aot_gen',
  input   : ['aot_error_grammar.y'],
  output  : ['aot_error_grammar_aot.c'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-j',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_aot_error_actions = executable('test_aot_error_actions',
  'test_aot_error_actions.c',
  test_aot_error_gen,
  test_aot_error_aot_gen,
  include_directories : include_directories('../src'),
  dependencies : lime_parser_dep,
  c_args : nounusedparam,
)

test('aot_error_actions', test_aot_error_actions)

# %symbol_prefix collision test: two grammars with different
# %symbol_prefix values must coexist in a single translation unit
# without internal-name collision.  Without the directive, both .c
# files emit yy_action / yyParser / YYNTOKEN / etc. -- preprocessor
# would not even compile them together.  See lime.c::emit_symbol_prefix_block.
test_symbol_prefix_a_gen = custom_target('test_symbol_prefix_a_parser',
  input   : ['test_symbol_prefix_a_grammar.y'],
  output  : ['test_symbol_prefix_a_grammar.c', 'test_symbol_prefix_a_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_symbol_prefix_b_gen = custom_target('test_symbol_prefix_b_parser',
  input   : ['test_symbol_prefix_b_grammar.y'],
  output  : ['test_symbol_prefix_b_grammar.c', 'test_symbol_prefix_b_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_symbol_prefix = executable('test_symbol_prefix',
  'test_symbol_prefix.c',
  test_symbol_prefix_a_gen,
  test_symbol_prefix_b_gen,
  # The include_directories('../src') we'd otherwise add is unneeded
  # here -- meson auto-adds the per-target build dir for custom_target
  # outputs.  Adding ../src actually shadows that; without it the
  # generated .h files resolve correctly via the implicit build-dir
  # search path.
  dependencies : lime_parser_dep,
)

test('symbol_prefix', test_symbol_prefix)

# %destructor smoke test: drives a tiny grammar with %destructor
# directives, exercises both happy-path and error-recovery code paths,
# and asserts every allocation has a matching free (proving the
# destructor actually runs on stack pops, the README's "modern memory
# safety" claim).
test_destructor_gen = custom_target('test_destructor_parser',
  input   : ['test_destructor_grammar.y'],
  output  : ['test_destructor_grammar.c', 'test_destructor_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_destructor = executable('test_destructor',
  'test_destructor.c',
  test_destructor_gen,
  include_directories : include_directories('../src'),
  dependencies : lime_parser_dep,
)

test('destructor', test_destructor)

# ParseReset() smoke test: exercises parser context reuse without
# free+alloc churn, covering: smoke (alloc→parse→reset→parse→free),
# pointer stability, destructor firing on reset, user-arg preservation,
# error-state clear, and stack depth tracking.  Added v0.6.1 for PG's
# pgbench tight-loop use case.
test_parse_reset_gen = custom_target('test_parse_reset_parser',
  input   : ['test_parse_reset_grammar.y'],
  output  : ['test_parse_reset_grammar.c', 'test_parse_reset_grammar.h'],
  command : [
    lime_exe,
    '-T' + meson.project_source_root() / 'limpar.c',
    '-d@OUTDIR@',
    '@INPUT@',
  ],
)

test_parse_reset = executable('test_parse_reset',
  'test_parse_reset.c',
  test_parse_reset_gen,
  include_directories : include_directories('../src'),
  dependencies : lime_parser_dep,
)

test('parse_reset', test_parse_reset)

# ====================================================================
# PostgreSQL grammar regression test (gated by -Dpg_grammar=true)
# ====================================================================
# The PG SQL grammar is the largest LR(1) machine Lime hosts:
#   - 3,842 states, 3,584 rules, 557 terminals
#   - 145,227-entry action table (which exposes int16_t overflow on
#     yy_shift_ofst / yy_reduce_ofst -- the int32 widening lives in
#     src/snapshot.h)
#   - 1,665 conflicts resolved by precedence + default-shift
#
# Regenerating gram_snapshot.c takes ~3.8 s, so this test is opt-in.
# Enable it in CI sweeps that need to exercise stress-test corners:
#     meson configure builddir -Dpg_grammar=true
if get_option('pg_grammar')
  pg_gram_snapshot_gen = custom_target('pg_gram_snapshot',
    input   : meson.project_source_root() / 'examples/pg/gram.lime',
    output  : ['gram.c', 'gram.h', 'gram_snapshot.c'],
    command : [
      lime_exe,
      '-T' + meson.project_source_root() / 'limpar.c',
      '-n',
      '-d@OUTDIR@',
      '@INPUT@',
    ],
  )

  # Compile only the snapshot file (not gram.c, which depends on
  # the PostgreSQL header tree).  Use a wrapper static library so
  # we can pass the necessary -Wno-* flags without polluting the
  # whole project.
  pg_gram_snapshot_lib = static_library('pg_gram_snapshot',
    pg_gram_snapshot_gen[2],   # gram_snapshot.c is the third output
    c_args : [
      '-Wno-shorten-64-to-32',
      '-Wno-constant-conversion',
      '-Wno-unused-variable',
    ],
    include_directories : include_directories('../include', '../src'),
    dependencies : lime_parser_dep,
  )

  test_pg_grammar = executable('test_pg_grammar',
    'test_pg_grammar.c',
    pg_gram_snapshot_gen[1],   # gram.h: pull into the build graph so it's
                               # generated before this target compiles, and
                               # so meson auto-adds its dir to the include
                               # path -- the test C file does #include "gram.h".
    include_directories : include_directories('../src'),
    link_with    : pg_gram_snapshot_lib,
    dependencies : lime_parser_dep,
  )

  test('pg_grammar', test_pg_grammar, timeout : 120)
endif

# ====================================================================
# Property-based tests via Hegel (gated on libhegel availability)
# ====================================================================
#
# Hegel is a Hypothesis-backed property-based testing protocol; the
# C99 binding ships at https://github.com/gburd/hegel-c.  When
# present, four PBTs land here, exercising:
#
#   1. parser-table roundtrip (test_runtime_pbt)
#   2. snapshot acquire/release refcount stateful test
#      (test_snapshot_pbt)
#   3. JIT vs interpreter equivalence on (state, lookahead)
#      (test_jit_pbt)
#   4. JSON allocator-mode equivalence (test_json_pbt)
#
# The targets and methodology live in
# .agent/skills/hegel/references/c/reference.md.  Each test exits
# 77 (meson skip code) when hegel_session_new() returns NULL so
# the rest of the suite still runs on hosts without the hegel
# server binary on PATH.
hegel_dep = dependency('hegel', required : false)

if hegel_dep.found()
  message('Hegel property-based tests: ENABLED (hegel ' +
          hegel_dep.version() + ')')

  # Reuse the runtime arith snapshot generated above by the
  # existing runtime-engine tests.  No new lime invocations.
  test_runtime_pbt = executable('test_runtime_pbt',
    'test_runtime_pbt.c',
    test_runtime_arith_gen,
    include_directories : include_directories('../src'),
    dependencies : [lime_parser_dep, hegel_dep],
  )
  test('runtime_pbt', test_runtime_pbt, timeout : 120)

  test_snapshot_pbt = executable('test_snapshot_pbt',
    'test_snapshot_pbt.c',
    test_runtime_arith_gen,
    include_directories : include_directories('../src'),
    dependencies : [lime_parser_dep, hegel_dep],
  )
  test('snapshot_pbt', test_snapshot_pbt, timeout : 60)

  test_jit_pbt = executable('test_jit_pbt',
    'test_jit_pbt.c',
    test_runtime_arith_gen,
    include_directories : include_directories('../src'),
    dependencies : [lime_parser_dep, hegel_dep],
  )
  test('jit_pbt', test_jit_pbt, timeout : 120)

  # JSON allocator-mode PBT consumes the JSON example's parser
  # sources directly (json.h, json_helpers.c, json_tokenize.c) and
  # the bench-shared json_grammar custom_target.  Reuse the bench
  # custom_target by referencing its outputs.
  if get_option('pg_grammar') or true
    # json_grammar source lives under bench/; generate it here
    # on its own custom_target so this test stands alone if the
    # bench is disabled.
    json_pbt_gen = custom_target('json_pbt_parser',
      input   : meson.project_source_root() / 'examples/json/json_grammar.lime',
      output  : ['json_grammar.c', 'json_grammar.h', 'json_grammar_snapshot.c'],
      command : [
        lime_exe,
        '-T' + meson.project_source_root() / 'limpar.c',
        '-n',
        '-d@OUTDIR@',
        '@INPUT@',
      ],
    )

    test_json_pbt = executable('test_json_pbt',
      'test_json_pbt.c',
      json_pbt_gen,
      meson.project_source_root() / 'examples/json/json_helpers.c',
      meson.project_source_root() / 'examples/json/json_tokenize.c',
      include_directories : [
        include_directories('../src'),
        include_directories('../examples/json'),
      ],
      dependencies : [lime_parser_dep, hegel_dep],
    )
    test('json_pbt', test_json_pbt, timeout : 120)
  endif
else
  message('Hegel property-based tests: SKIPPED ' +
          '(install hegel-c to enable; see ' +
          '.agent/skills/hegel/references/c/reference.md)')
endif