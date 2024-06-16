project(
  'c-scheme',
  'c',
  version: '0.1',
  default_options: ['warning_level=3', 'default_library=static'],
)

s7_dep = dependency('s7')

scheme_pp = executable('scheme-pp', 'scheme-pp.c', dependencies: [s7_dep])
scheme_pp_gen = generator(
  scheme_pp,
  arguments: ['@INPUT@'],
  capture: true,
  output: '@PLAINNAME@.c',
)

test_src = scheme_pp_gen.process('test.cscm')

executable('c-scheme', test_src)
