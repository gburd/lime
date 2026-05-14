project('lime-parser', 'c', 'cpp',
  version : '0.1.0',
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

# Expose POSIX and GNU extensions (needed for pthread_rwlock_t, etc.)
add_project_arguments('-D_GNU_SOURCE', language : 'c')

# Project-wide include directory
inc = include_directories('include')

# Build the lime parser generator tool from root lime.c
lime_exe = executable('lime',
  'lime.c',
  install : true,
)

# Install man pages
install_man('man/lime.1', 'man/lime_grammar.5')

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
  'include/glr.h',
  'include/grammar_context.h',
  'include/jit_context.h',
  'include/jit_llvm_compat.h',
  'include/jit_policy.h',
  'include/jit_tokenizer.h',
  'include/lime_ast.h',
  'include/lime_error.h',
  'include/lime_location.h',
  'include/parse_context.h',
  'include/parser_fork.h',
  'include/parser.h',
  'include/parser_manager.h',
  'include/parser_operations.h',
  'include/tokenize.h',
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

# Build tests
subdir('tests')

# Build benchmarks
subdir('bench')

# API documentation via Doxygen
doxygen = find_program('doxygen', required : false)
if doxygen.found()
  custom_target('doxygen',
    output : 'html',
    command : [doxygen, meson.project_source_root() / 'docs' / 'Doxyfile'],
    build_by_default : false,
  )
endif
