project('s7', 'c', default_options: ['c_std=c11'])

cc = meson.get_compiler('c')
flags = ['-w', '-DWITH_C_LOADER=0']
if cc.get_id() == 'clang' or cc.get_id() == 'clang-cl'
  flags += ['-Wno-int-conversion']
endif

s7_lib = library('s7', 's7.c', c_args: flags)
s7_dep = declare_dependency(
  link_with: s7_lib,
  include_directories: include_directories('.'),
)
