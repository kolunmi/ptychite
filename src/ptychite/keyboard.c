#include <wlr/backend/session.h>
#include <wlr/types/wlr_keyboard.h>

#include "keyboard.h"
#include "server.h"
#include "compositor.h"
#include "config.h"
#include "monitor.h"
#include "windows/windows.h"

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct ptychite_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct wlr_keyboard_key_event *event = data;
	struct ptychite_server *server = keyboard->server;
	struct wlr_seat *seat = server->seat;

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->keyboard);

	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		size_t old_keys_size = server->keys.size;

		int i;
		for (i = 0; i < nsyms; i++) {
			if (server->keys.size &&
					(syms[i] == XKB_KEY_Super_L || syms[i] == XKB_KEY_Super_R || syms[i] == XKB_KEY_Alt_L ||
							syms[i] == XKB_KEY_Alt_R || syms[i] == XKB_KEY_Shift_L || syms[i] == XKB_KEY_Shift_R ||
							syms[i] == XKB_KEY_Control_L || syms[i] == XKB_KEY_Control_R ||
							syms[i] == XKB_KEY_Caps_Lock)) {
				handled = true;
				continue;
			}

			bool match = false;
			struct ptychite_chord_binding *chord_binding;
			wl_array_for_each(chord_binding, &server->compositor->config->keyboard.chords) {
				if (!chord_binding->active) {
					continue;
				}

				size_t length = chord_binding->chord.keys_l;
				size_t progress = server->keys.size / sizeof(struct ptychite_key);
				if (length <= progress) {
					continue;
				}

				bool pass = false;
				size_t j;
				for (j = 0; j < progress; j++) {
					struct ptychite_key *key_binding = &chord_binding->chord.keys[j];
					struct ptychite_key *key_current = &((struct ptychite_key *)server->keys.data)[j];
					if (key_binding->sym != key_current->sym || key_binding->modifiers != key_current->modifiers) {
						pass = true;
						break;
					}
				}
				if (pass) {
					continue;
				}

				struct ptychite_key *key = &chord_binding->chord.keys[progress];
				if (syms[i] != key->sym || modifiers != key->modifiers) {
					continue;
				}

				handled = match = true;
				if (length == progress + 1) {
					ptychite_server_execute_action(server, chord_binding->action);
					server->keys.size = 0;
				} else {
					struct ptychite_key *append = wl_array_add(&server->keys, sizeof(struct ptychite_key));
					if (!append) {
						server->keys.size = 0;
						break;
					}
					*append = (struct ptychite_key){.sym = syms[i], .modifiers = modifiers};
				}

				break;
			}

			if (!match) {
				if (server->keys.size) {
					server->keys.size = 0;
					handled = true;
				} else {
					handled = false;
				}
			}

			if (!handled && server->session && modifiers == (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT)) {
				unsigned int vt = 0;
				switch (syms[i]) {
				case XKB_KEY_XF86Switch_VT_1:
					vt = 1;
					break;
				case XKB_KEY_XF86Switch_VT_2:
					vt = 2;
					break;
				case XKB_KEY_XF86Switch_VT_3:
					vt = 3;
					break;
				case XKB_KEY_XF86Switch_VT_4:
					vt = 4;
					break;
				case XKB_KEY_XF86Switch_VT_5:
					vt = 5;
					break;
				case XKB_KEY_XF86Switch_VT_6:
					vt = 6;
					break;
				case XKB_KEY_XF86Switch_VT_7:
					vt = 7;
					break;
				case XKB_KEY_XF86Switch_VT_8:
					vt = 8;
					break;
				case XKB_KEY_XF86Switch_VT_9:
					vt = 9;
					break;
				case XKB_KEY_XF86Switch_VT_10:
					vt = 10;
					break;
				case XKB_KEY_XF86Switch_VT_11:
					vt = 11;
					break;
				case XKB_KEY_XF86Switch_VT_12:
					vt = 12;
					break;
				}
				if (vt) {
					handled = true;
					wlr_session_change_vt(server->session, vt);
				}
			}
		}

		if (server->keys.size != old_keys_size) {
			struct ptychite_monitor *monitor;
			wl_list_for_each(monitor, &server->monitors, link) {
				if (monitor->panel && monitor->panel->base.element.scene_tree->node.enabled) {
					window_relay_draw_same_size(&monitor->panel->base);
				}
			}
		}
	}

	if (!handled) {
		wlr_seat_set_keyboard(seat, keyboard->keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	struct ptychite_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);

	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);

	free(keyboard);
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
	struct ptychite_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);

	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->keyboard->modifiers);
}

void keyboard_rig(struct ptychite_keyboard *keyboard, struct wlr_input_device *device) {
  	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&keyboard->keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&keyboard->keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);
}
