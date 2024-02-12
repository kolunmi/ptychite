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

wlroots        = dependency('wlroots', version: '>=0.17.1')
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

server_protoxmls = [
  join_paths(wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'),
  'protocols/wlr-layer-shell-unstable-v1.xml',
  'protocols/wlr-output-power-management-unstable-v1.xml',
  'protocols/ptychite-message-unstable-v1.xml',
]

server_protosrcs = []
foreach xml : server_protoxmls
  header = wayland_scanner_server_header.process(xml)
  code = wayland_scanner_code.process(xml)

  server_protosrcs += [header, code]
endforeach
server_protos = declare_dependency(
  sources: server_protosrcs,
)

client_protoxmls = [
  'protocols/ptychite-message-unstable-v1.xml',
]

client_protosrcs = []
foreach xml : client_protoxmls
  header = wayland_scanner_client_header.process(xml)
  code = wayland_scanner_code.process(xml)

  client_protosrcs += [header, code]
endforeach
client_protos = declare_dependency(
  sources: client_protosrcs,
)

executable(
  'ptychite',
  [
    # .h
    'src/ptychite/compositor.h',
    'src/ptychite/server.h',
    'src/ptychite/message.h',
    'src/ptychite/action.h',
    'src/ptychite/monitor.h',
    'src/ptychite/buffer.h',
    'src/ptychite/element.h',
    'src/ptychite/view.h',
    'src/ptychite/keyboard.h',
    'src/ptychite/draw.h',
    'src/ptychite/windows.h',
    'src/ptychite/chord.h',
    'src/ptychite/config.h',
    'src/ptychite/json.h',
    'src/ptychite/macros.h',
    'src/ptychite/util.h',

    # .c
    'src/ptychite/ptychite.c',
    'src/ptychite/compositor.c',
    
    'src/ptychite/server.c',
    'src/ptychite/message.c',
    'src/ptychite/action.c',
    'src/ptychite/monitor.c',
    'src/ptychite/buffer.c',
    'src/ptychite/view.c',
    'src/ptychite/keyboard.c',
    'src/ptychite/draw.c',
    'src/ptychite/windows.c',
    
    'src/ptychite/windows/wallpaper.c',
    'src/ptychite/windows/panel.c',
    'src/ptychite/windows/control.c',
    'src/ptychite/windows/titlebar.c',
    
    'src/ptychite/chord.c',
    'src/ptychite/config.c',
    'src/ptychite/json.c',
    'src/ptychite/util.c',
  ],
  include_directories: [include_directories('.')],
  dependencies: [
    server_protos,
    wayland_server,
    wlroots,
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
    client_protos,
    wayland_client,
    json,
  ],
)
