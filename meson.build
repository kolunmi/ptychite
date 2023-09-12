project(
  'ptychite',
  'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
)

add_project_arguments(
  [
    '-DWLR_USE_UNSTABLE',

    '-Wno-unused-parameter',
  ],
  language: 'c',
)

cc = meson.get_compiler('c')

wlroots        = subproject('wlroots', default_options: ['examples=false'])
# scenefx        = subproject('scenefx')
wayland_protos = dependency('wayland-protocols', version: '>=1.18')
wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
xkbcommon      = dependency('xkbcommon')
json           = dependency('json-c')
cairo          = dependency('cairo')
pango          = dependency('pango')
pangocairo     = dependency('pangocairo')
librsvg        = dependency('librsvg-2.0')
# libup          = dependency('upower-glib')
# libnm          = dependency('libnm')
math           = cc.find_library('m')

wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')
wayland_scanner = find_program('wayland-scanner')

wayland_scanner_server_header = generator(
  wayland_scanner,
  output: '@BASENAME@-protocol.h',
  arguments: ['server-header', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client_header = generator(
  wayland_scanner,
  output: '@BASENAME@-client-protocol.h',
  arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_code = generator(
  wayland_scanner,
  output: '@BASENAME@-protocol.c',
  arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

ptychite_protocols = [
  [wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
  ['protocols/wlr-layer-shell-unstable-v1.xml'],
  ['protocols/wlr-output-power-management-unstable-v1.xml'],
  ['protocols/ptychite-message-unstable-v1.xml'],
]

ptyclient_protocols = [  
  ['protocols/ptychite-message-unstable-v1.xml'],
]

ptychite_protos = []
foreach p : ptychite_protocols
  xml = join_paths(p)
  header = wayland_scanner_server_header.process(xml)
  code = wayland_scanner_code.process(xml)

  ptychite_protos += [header, code]
endforeach
ptychite_protos = declare_dependency(
  sources: ptychite_protos,
)

ptyclient_protos = []
foreach p : ptyclient_protocols
  xml = join_paths(p)
  header = wayland_scanner_client_header.process(xml)
  code = wayland_scanner_code.process(xml)

  ptyclient_protos += [header, code]
endforeach
ptyclient_protos = declare_dependency(
  sources: ptyclient_protos,
)

executable(
  'ptychite',
  [
    'src/ptychite/ptychite.c',
    'src/ptychite/compositor.c',
    'src/ptychite/server.c',
    'src/ptychite/config.c',
    'src/ptychite/json.c',
  ],
  include_directories: [include_directories('.')],
  dependencies: [
    ptychite_protos,
    wayland_server,
    wlroots.get_variable('wlroots'),
    # scenefx.get_variable('scenefx'),
    xkbcommon,
    json,
    cairo,
    pango,
    pangocairo,
    librsvg,
    # libup,
    # libnm,
    math,
  ],
)

executable(
  'ptymsg',
  [
    'src/ptymsg/ptymsg.c',
  ],
  include_directories: [include_directories('.')],
  dependencies: [
    ptyclient_protos,
    wayland_client,
    json,
  ],
)
