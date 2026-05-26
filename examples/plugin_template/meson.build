# Build rules for the plugin template example.
#
# Builds three targets:
#
#   1. sql_plugin.so   -- The example plugin packaged as a shared
#                          library.  A host application loads it at
#                          runtime via dlopen() (driven through
#                          ParserManager) and calls lime_plugin_entry().
#
#   2. plugin_host     -- The example host application.  Demonstrates
#                          both the static-linking and dynamic-loading
#                          paths through ParserManager.  Built so other
#                          callers can study the worked example; not
#                          run as a meson test (needs a real grammar).
#
#   3. plugin_smoke    -- Narrow smoke-test driver: dlopen the built
#                          sql_plugin.so, validate identity + capability
#                          metadata, unload cleanly, exit 0.  Wired as
#                          a meson test.  Load-bearing guardrail for
#                          the runtime plugin-loading path that
#                          compose_snapshots() depends on.
#
# Both demo binaries are install : false -- they are example code,
# not part of the shipped library surface.
#
# Skipped on Windows: the manager's plugin loader is dlopen()-based
# (POSIX dl).  Mirrors tests/test_snapshot_create's skip rationale.

if host_machine.system() == 'windows'
  subdir_done()
endif

# Use libdl on platforms where the linker needs it explicitly.
# Linux glibc / musl require -ldl; macOS / FreeBSD do not.
dl_dep = cc.find_library('dl', required : false)

# The shared library plugin deliberately does NOT link against
# lime_parser_dep.  Its references to runtime helpers
# (lime_snapshot_create, parser_manager_register) resolve at dlopen()
# time against the host process, which exports its symbols via
# export_dynamic : true on the host targets.  This matches the
# pattern src/snapshot_create.c documents and keeps a single copy
# of every static-library global in the process image (g_registry,
# g_next_fork_id, g_glr_bindings).
#
# The project default LDFLAGS include -Wl,--no-undefined; we have to
# undo that for the plugin .so or the linker rejects the dlopen-time
# resolution model.  GNU ld accepts --unresolved-symbols and
# --allow-shlib-undefined; macOS ld wants -undefined dynamic_lookup.
plugin_link_args = []
if host_machine.system() == 'darwin'
  plugin_link_args += ['-Wl,-undefined,dynamic_lookup']
else
  plugin_link_args += ['-Wl,--unresolved-symbols=ignore-in-object-files']
endif

sql_plugin_lib = shared_library('sql_plugin',
  'sql_plugin.c',
  include_directories : inc,
  link_args : plugin_link_args,
  install : false,
)

# Host link flags shared by plugin_host and plugin_smoke.
host_link_args = []
if dl_dep.found()
  host_link_args += ['-ldl']
endif

# Host application.  Links the plugin source statically too so the
# binary demonstrates both the static-registration and dynamic-load
# paths against the same plugin code.  export_dynamic : true so the
# dlopened sql_plugin.so resolves snapshot/manager symbols against
# the host process.
plugin_host_exe = executable('plugin_host',
  'plugin_host.c',
  'sql_plugin.c',
  include_directories : inc,
  dependencies : [lime_parser_dep],
  link_args : host_link_args,
  export_dynamic : true,
  install : false,
)

# Smoke-test driver.  Lives in this directory because it only makes
# sense alongside the example plugin it loads.
plugin_smoke_exe = executable('plugin_smoke',
  'plugin_smoke.c',
  include_directories : inc,
  dependencies : [lime_parser_dep],
  link_args : host_link_args,
  export_dynamic : true,
  install : false,
)

# End-to-end smoke test: dlopen the built sql_plugin.so through the
# smoke driver, assert identity + capability metadata round-trip,
# verify clean unload.  If the plugin ABI ever drifts (a new required
# callback added without bumping LIME_PLUGIN_ABI_VERSION_MAJOR, the
# entry-point symbol renamed, etc.) this test fails immediately.
test('plugin_host_smoke',
  plugin_smoke_exe,
  args : [sql_plugin_lib.full_path()],
  depends : [sql_plugin_lib],
)
