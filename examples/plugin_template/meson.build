# Build rules for the plugin template example.
#
# Builds two targets:
#   1. sql_plugin.so   - The example plugin as a shared library
#   2. plugin_host     - The example host application (links statically)
#
# Usage:
#   meson setup builddir
#   meson compile -C builddir
#
#   # Dynamic loading:
#   ./builddir/examples/plugin_template/plugin_host \
#       --plugin ./builddir/examples/plugin_template/sql_plugin.so \
#       grammar.y
#
#   # Static linking:
#   ./builddir/examples/plugin_template/plugin_host grammar.y

# Shared library plugin
sql_plugin_lib = shared_library('sql_plugin',
  'sql_plugin.c',
  include_directories : inc,
  dependencies : [lime_parser_dep],
  install : false,
)

# Host application that links the plugin statically
plugin_host_exe = executable('plugin_host',
  'plugin_host.c',
  'sql_plugin.c',
  include_directories : inc,
  dependencies : [lime_parser_dep],
  link_args : ['-ldl'],
  install : false,
)
