#ifndef PTYCHITE_KEYBOARD_H
#define PTYCHITE_KEYBOARD_H

#include <wayland-server-core.h>

struct ptychite_keyboard {
	struct wl_list link;
	struct ptychite_server *server;
	struct wlr_keyboard *keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

void ptychite_keyboard_rig(struct ptychite_keyboard *keyboard, struct wlr_input_device *device);

#endif
