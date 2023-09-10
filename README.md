<div align="center">
<img src="https://git.sr.ht/~kolunmi/ptychite/blob/main/ptychite.png" width=50% height=50%>
</div>

# Ptychite
A flexible, approachable, and powerful wayland compositor. This project is still in its early stages; it's not currently usable as a daily driver, but will be soon!

![screenshot1](https://git.sr.ht/~kolunmi/ptychite/blob/main/screenshot1.png "screenshot 1")
![screenshot2](https://git.sr.ht/~kolunmi/ptychite/blob/main/screenshot2.png "screenshot 2")

## Project Goals
* performant
* easy to use and configure
* built-in panel, wallpaper, control center, screenshotting-tool, etc
* optional mosaic and traditional window tiling
* a consistent and comfortable aesthetic design
* basic animations, rounded corners, and blur
* gnome-like workspace management, with the zoom out effect
* Xwayland support
* active application icon support in panel
* NetworkManager and libupower integration in panel
* notifications over dbus
* possibly switch config format from json to yaml

ptychite should have a robust base-level functionality and be completely usable out of the box without the need for external clients.

## Current Dependencies
* [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots)
* wayland-protocols
* wayland-server
* wayland-client
* xkbcommon
* [json-c](https://github.com/json-c/json-c)
* [cairo](https://www.cairographics.org/)
* [pango](https://pango.gnome.org/)
* pangocairo
* [librsvg](https://gitlab.gnome.org/GNOME/librsvg)

## Building
```bash
git clone https://git.sr.ht/~kolunmi/ptychite
cd ptychite
meson setup build/
ninja -C build/
```

## Configuration
ptychite is configured in json; all data you send to and recieve from the compositor will be in this format. Upon startup ptychite looks for the file `~/.config/ptychite/ptychite.json` (see `sample_config.json`). This file will instruct ptychite to build a configuration with the given property nodes. 

### ptymsg
ptymsg is a client program used to configure and query information from ptychite at runtime. The currently supported commands are:
* `ptymsg set (--overwrite) <property path> <json object>` set the specified property; for example, `ptymsg set monitors:wallpaper:filepath '"background.png"'`
* `ptymsg get (--compact) <property path>`: get the specified property as a json object
* `ptymsg dump-views (--compact) <output name>`: dump information about the current views on the given output or `all` for every view regardless of output

You can dump the current config in its entirety to stdout using `ptymsg get :`
