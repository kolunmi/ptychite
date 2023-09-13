<div align="center">
<img src="https://git.sr.ht/~kolunmi/ptychite/blob/main/ptychite.png" width=50% height=50%>
</div>

[![builds.sr.ht status](https://builds.sr.ht/~kolunmi/ptychite/.svg)](https://builds.sr.ht/~kolunmi/ptychite/?)

# Ptychite
A flexible, approachable, and powerful wayland compositor. This project is still in its early stages; it's not currently usable as a daily driver, but will be soon!

Note: ptychite is developed on [sr.ht](https://git.sr.ht/~kolunmi/ptychite).

![screenshot](https://git.sr.ht/~kolunmi/ptychite/blob/main/screenshot.png "screenshot")

## Project Goals
* performant
* easy to use and configure
* built-in panel, wallpaper, control center, screenshotting-tool, etc
* optional mosaic and traditional window tiling
* optional server-side decorations
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
```sh
meson setup build/
ninja -C build/
```

## Configuration
ptychite is configured in json; all data you send to and recieve from the compositor will be in this format. Upon startup ptychite looks for the file `~/.config/ptychite/ptychite.json` (see `sample_config.json`). This file will instruct ptychite to build a configuration with the given property nodes. Properties are expressed as paths delimited by `:` characters.

This simple configuration will accept the default values for all properties except `keyboard:repeat:rate`, `keyboard:repeat:delay`, and `keyboard:xkb:options`, for which custom values are provided:
```json
{
	"keyboard":{
		"repeat":{
			"rate":60,
			"delay":250
		},
		"xkb":{
			"options":"ctrl:swapcaps"
		}
	}
}
```

### ptymsg
ptymsg is a client program used to configure and query information from ptychite at runtime. It utilizes the ptychite-message protocol to communicate with the compositor.

#### Setting Properties
The `set` command takes two arguments: a property path and json data.
```sh
ptymsg set monitors:wallpaper:filepath '"background.png"'
```
It also takes an optional flag, `--overwrite`, to overwrite lists instead of appending to them.
```sh
# delete all chords before adding
ptymsg set --overwrite keyboard:chords '[{"pattern":"S-x i","action":["spawn","firefox"]}]'
```
Note that multiple properties can be set at once by passing an incomplete property path.
```sh
ptymsg set tiling '{"mode":"traditional","gaps":20}'
# is equivalent to
ptymsg set tiling:mode '"traditional"' && ptymsg set tiling:gaps 20
```

#### Getting Properties
The `get` command outputs the corresponding json data with a property path. By default, the json is pretty-printed; whitespace will be removed if the `--compact` option is passed.
```sh
ptymsg get --compact panel
```
To output the entire configuration:
```sh
ptymsg get :
```

#### View Data
The `dump-views` command will dump information about the current views on the given output or `all` for every view regardless of output.
```sh
ptymsg dump-views eDP-1
ptymsg dump-views --compact all
```

## Contributing
Thank you! Patches should be sent to [~kolunmi/ptychite@lists.sr.ht](https://lists.sr.ht/~kolunmi/ptychite).
