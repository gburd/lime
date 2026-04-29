project('lime-parser', 'c', 'cpp',
  version : '0.1.0',
  default_options : [
    'c_std=c11',
    'warning_level=2',
    'buildtype=debugoptimized',
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

# Build the library components from src/
subdir('src')

# Build tests
subdir('tests')

# Build benchmarks
subdir('bench')

# API documentation via Doxygen
doxygen = find_program('doxygen', required : false)
if doxygen.found()
  custom_target('doxygen',
    output : 'html',
    command : [doxygen, meson.project_source_root() / 'Doxyfile'],
    build_by_default : false,
  )
endif
