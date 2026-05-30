# tools/meson.build -- replaces the old hand-rolled tools/Makefile.
#
# parse-manager is the CLI front-end for ParserManager.  Pre-v0.5.2 it
# was built by tools/Makefile, which defaulted to USE_STUBS=1 so the
# binary linked against a 64-LOC tools/stubs.c shim that returned
# placeholder values for parse_begin / parse_token / parse_end / JIT
# query.  USE_STUBS=0 mode never worked past v0.2.x because the
# Makefile's hand-maintained dep list rotted.
#
# Migrating to meson (v0.5.2):
#   - Real link against lime_parser_dep -- every parse path is live.
#   - tools/stubs.c removed entirely (no replacement; it lied about
#     functions that have working implementations in the parser lib).
#   - parse-manager becomes installable (`meson install` -> $prefix/bin).
#   - The man page tools/parse-manager.1 is wired into install_man so
#     `man parse-manager` works on installed systems.
#
# The integration test (tests/test_parse_manager.c) exercises the
# tool against a real loaded plugin (examples/plugin_template/sql_plugin.so),
# proving the CLI round-trips load -> list -> info -> unload against
# the actual ParserManager surface.

# parse-manager links lime_parser_dep so the plugin loader has live
# dlopen/RTLD_NOW machinery.  -ldl is required on Linux glibc/musl
# for dlopen; harmless elsewhere.
parse_manager_link_args = []
parse_manager_dl_dep = cc.find_library('dl', required : false)
if parse_manager_dl_dep.found()
  parse_manager_link_args += ['-ldl']
endif

# Skipped on Windows: parse-manager uses getopt(3) and dlopen
# (POSIX-only); a Win32 port via getopt_long_w + LoadLibraryA is
# feasible but unfunded.  See tests/meson.build for the matching
# test_parse_manager gate.
if host_machine.system() != 'windows'
  parse_manager_exe = executable('parse-manager',
    'parse-manager.c',
    include_directories : [inc, include_directories('../src')],
    dependencies : [lime_parser_dep],
    link_args : parse_manager_link_args,
    # export_dynamic : true so plugins loaded via parser_manager_load()
    # can resolve back into the host's lime_parser symbols (matches the
    # pattern in tests/test_snapshot_create and examples/plugin_template).
    export_dynamic : true,
    install : true,
  )

  # Install the man page alongside the binary.
  install_man('parse-manager.1')
endif
