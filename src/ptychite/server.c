#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <cairo.h>
#include <drm_fourcc.h>

#include <linux/input-event-codes.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "compositor.h"
#include "config.h"
#include "json.h"
#include "macros.h"
#include "ptychite-message-unstable-v1-protocol.h"
#include "server.h"
#include "windows/windows.h"
#include "draw.h"

static int protocol_json_get_mode_convert_to_native(
		enum zptychite_message_v1_json_get_mode mode, enum ptychite_json_get_mode *mode_out) {
	enum ptychite_json_get_mode get_mode;
	switch (mode) {
	case ZPTYCHITE_MESSAGE_V1_JSON_GET_MODE_PRETTY:
		get_mode = PTYCHITE_JSON_GET_PRETTY;
		break;
	case ZPTYCHITE_MESSAGE_V1_JSON_GET_MODE_COMPACT:
		get_mode = PTYCHITE_JSON_GET_COMPACT;
		break;
	default:
		return -1;
	}

	*mode_out = get_mode;
	return 0;
}

#define CALLBACK_SUCCESS_SEND_AND_DESTROY(callback, data) \
	zptychite_message_callback_v1_send_success(callback, data); \
	wl_resource_destroy(callback)

#define CALLBACK_FAILURE_SEND_AND_DESTROY(callback, message) \
	zptychite_message_callback_v1_send_failure(callback, message); \
	wl_resource_destroy(callback)

static void message_set_property(struct wl_client *client, struct wl_resource *resource, const char *path,
		const char *string, uint32_t mode, uint32_t id) {
	struct wl_resource *callback =
			wl_resource_create(client, &zptychite_message_callback_v1_interface, wl_resource_get_version(resource), id);
	if (!callback) {
		return;
	}

	struct ptychite_server *server = wl_resource_get_user_data(resource);

	enum ptychite_property_set_mode set_mode;
	switch (mode) {
	case ZPTYCHITE_MESSAGE_V1_PROPERTY_SET_MODE_APPEND:
		set_mode = PTYCHITE_PROPERTY_SET_APPEND;
		break;
	case ZPTYCHITE_MESSAGE_V1_PROPERTY_SET_MODE_OVERWRITE:
		set_mode = PTYCHITE_PROPERTY_SET_OVERWRITE;
		break;
	default:
		CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "invalid setting mode");
		return;
	}

	char *error;
	if (!ptychite_config_set_property_from_string(server->compositor->config, path, string, set_mode, &error)) {
		zptychite_message_callback_v1_send_success(callback, "");
	} else {
		zptychite_message_callback_v1_send_failure(callback, error);
	}

	wl_resource_destroy(callback);
}

static void message_get_property(
		struct wl_client *client, struct wl_resource *resource, const char *path, uint32_t mode, uint32_t id) {
	struct wl_resource *callback =
			wl_resource_create(client, &zptychite_message_callback_v1_interface, wl_resource_get_version(resource), id);
	if (!callback) {
		return;
	}

	enum ptychite_json_get_mode get_mode;
	if (protocol_json_get_mode_convert_to_native(mode, &get_mode)) {
		CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "invalid getting mode");
		return;
	}

	struct ptychite_server *server = wl_resource_get_user_data(resource);

	char *error;
	char *string = ptychite_config_get_property(server->compositor->config, path, get_mode, &error);
	if (!string) {
		CALLBACK_FAILURE_SEND_AND_DESTROY(callback, error);
		return;
	}

	CALLBACK_SUCCESS_SEND_AND_DESTROY(callback, string);
	free(string);
}

static struct json_object *view_describe(struct ptychite_view *view) {
	struct json_object *description = json_object_new_object();
	if (!description) {
		return NULL;
	}

#define JSON_OBJECT_ADD_MEMBER_OR_RETURN(object, member, key, type, value) \
	if (!(member = json_object_new_##type(value))) { \
		json_object_put(object); \
		return NULL; \
	} \
	if (json_object_object_add(object, key, member)) { \
		json_object_put(member); \
		json_object_put(object); \
		return NULL; \
	}

	struct json_object *member;
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "appid", string, view->xdg_toplevel->app_id)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "title", string, view->xdg_toplevel->title)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "x", int, view->element.scene_tree->node.x)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "y", int, view->element.scene_tree->node.y)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "width", int, view->element.width)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "height", int, view->element.height)

#undef JSON_OBJECT_ADD_MEMBER_OR_RETURN

	return description;
}

static void message_dump_views(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *output_resource, uint32_t mode, uint32_t id) {
	struct wl_resource *callback =
			wl_resource_create(client, &zptychite_message_callback_v1_interface, wl_resource_get_version(resource), id);
	if (!callback) {
		return;
	}

	struct ptychite_server *server = wl_resource_get_user_data(resource);

	enum ptychite_json_get_mode get_mode;
	if (protocol_json_get_mode_convert_to_native(mode, &get_mode)) {
		CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "invalid getting mode");
		return;
	}

	struct json_object *array;
	if (output_resource) {
		struct wlr_output *output = wlr_output_from_resource(output_resource);
		if (!output) {
			CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "unable to obtain wlr_output from resource");
			return;
		}

		struct ptychite_monitor *monitor = output->data;
		if (!(array = json_object_new_array_ext(wl_list_length(&monitor->views)))) {
			CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "memory error");
			return;
		}

		size_t idx = 0;
		struct ptychite_view *view;
		wl_list_for_each(view, &monitor->views, monitor_link) {
			struct json_object *description = view_describe(view);
			if (!description) {
				json_object_put(array);
				CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "memory error");
				return;
			}
			json_object_array_put_idx(array, idx, description);
			idx++;
		}
	} else {
		if (!(array = json_object_new_array_ext(wl_list_length(&server->views)))) {
			CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "memory error");
		}

		size_t idx = 0;
		struct ptychite_view *view;
		wl_list_for_each(view, &server->views, server_link) {
			struct json_object *description = view_describe(view);
			if (!description) {
				json_object_put(array);
				CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "memory error");
				return;
			}
			json_object_array_put_idx(array, idx, description);
			idx++;
		}
	}

	char *error;
	const char *string = ptychite_json_object_convert_to_string(array, get_mode, &error);
	if (!string) {
		json_object_put(array);
		CALLBACK_FAILURE_SEND_AND_DESTROY(callback, error);
		return;
	}

	CALLBACK_SUCCESS_SEND_AND_DESTROY(callback, string);
	json_object_put(array);
}

static void message_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zptychite_message_v1_interface ptychite_message_impl = {
		.set_property = message_set_property,
		.get_property = message_get_property,
		.dump_views = message_dump_views,
		.destroy = message_destroy,
};

static void message_handle_server_destroy(struct wl_resource *resource) {
}

static void message_handle_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	struct wl_resource *resource = wl_resource_create(client, &zptychite_message_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &ptychite_message_impl, data, message_handle_server_destroy);
}

static struct ptychite_view *element_get_view(struct ptychite_element *element) {
	assert(element->type == PTYCHITE_ELEMENT_VIEW);

	struct ptychite_view *view = wl_container_of(element, view, element);

	return view;
}

static struct ptychite_window *element_get_window(struct ptychite_element *element) {
	assert(element->type == PTYCHITE_ELEMENT_WINDOW);

	struct ptychite_window *window = wl_container_of(element, window, element);

	return window;
}


/* returns whether a change has been made */
bool mouse_region_update_state(struct ptychite_mouse_region *region, double x, double y) {
	if (wlr_box_contains_point(&region->box, x, y)) {
		if (region->entered) {
			return false;
		}
		region->entered = true;
		return true;
	}
	if (region->entered) {
		region->entered = false;
		return true;
	}

	return false;
}



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

static void view_resize(struct ptychite_view *view, int width, int height) {
	view->element.width = width;
	view->element.height = height;

	int border_thickness = view->server->compositor->config->views.border.thickness;
	int top_thickness;
	if (view->title_bar && view->title_bar->base.element.scene_tree->node.enabled) {
		struct ptychite_font *font = &view->server->compositor->config->panel.font;
		top_thickness = font->height + font->height / 6;
	} else {
		top_thickness = border_thickness;
	}

	if (view->xdg_toplevel->base->client->shell->version >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION &&
			view->element.width >= 0 && view->element.height >= 0) {
		wlr_xdg_toplevel_set_bounds(view->xdg_toplevel, view->element.width, view->element.height);
	}

	struct wlr_xdg_toplevel_state *state = &view->xdg_toplevel->current;
	int max_width = state->max_width;
	int max_height = state->max_height;
	int min_width = state->min_width;
	int min_height = state->min_height;

	view->element.width = fmax(min_width + (2 * border_thickness), view->element.width);
	view->element.height = fmax(min_height + (top_thickness + border_thickness), view->element.height);

	if (max_width > 0 && !(2 * border_thickness > INT_MAX - max_width)) {
		view->element.width = fmin(max_width + (2 * border_thickness), view->element.width);
	}
	if (max_height > 0 && !(top_thickness + border_thickness > INT_MAX - max_height)) {
		view->element.height = fmin(max_height + (top_thickness + border_thickness), view->element.height);
	}

	if (view->title_bar && view->title_bar->base.element.scene_tree->node.enabled &&
			(view->title_bar->base.element.width != view->element.width ||
					view->title_bar->base.element.height != top_thickness)) {
		if (view->monitor) {
			view->title_bar->base.output = view->monitor->output;
		} else {
			view->title_bar->base.output = NULL;
		}
		window_relay_draw(&view->title_bar->base, view->element.width, top_thickness);
	}

	wlr_scene_node_set_position(&view->scene_tree_surface->node, border_thickness, top_thickness);
	view->resize_serial = wlr_xdg_toplevel_set_size(view->xdg_toplevel, view->element.width - 2 * border_thickness,
			view->element.height - (top_thickness + border_thickness));

	if (view->border.top->node.enabled) {
		wlr_scene_rect_set_size(view->border.top, view->element.width, border_thickness);
	}
	wlr_scene_node_set_position(&view->border.right->node, view->element.width - border_thickness, top_thickness);
	wlr_scene_rect_set_size(
			view->border.right, border_thickness, view->element.height - (top_thickness + border_thickness));
	wlr_scene_node_set_position(&view->border.bottom->node, 0, view->element.height - border_thickness);
	wlr_scene_rect_set_size(view->border.bottom, view->element.width, border_thickness);
	wlr_scene_node_set_position(&view->border.left->node, 0, top_thickness);
	wlr_scene_rect_set_size(
			view->border.left, border_thickness, view->element.height - (top_thickness + border_thickness));
}

static void surface_unfocus(struct wlr_surface *surface) {
	struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(surface);
	assert(xdg_surface && xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	wlr_xdg_toplevel_set_activated(xdg_surface->toplevel, false);

	struct wlr_scene_tree *scene_tree = xdg_surface->data;
	struct ptychite_element *element = scene_tree->node.data;
	if (element) {
		struct ptychite_view *view = element_get_view(element);
		struct ptychite_config *config = view->server->compositor->config;
		view->focused = false;
		wlr_scene_rect_set_color(view->border.top, config->views.border.colors.inactive);
		wlr_scene_rect_set_color(view->border.right, config->views.border.colors.inactive);
		wlr_scene_rect_set_color(view->border.bottom, config->views.border.colors.inactive);
		wlr_scene_rect_set_color(view->border.left, config->views.border.colors.inactive);
		if (view->title_bar && view->title_bar->base.element.scene_tree->node.enabled) {
			window_relay_draw_same_size(&view->title_bar->base);
		}
	}
}

void view_focus(struct ptychite_view *view, struct wlr_surface *surface) {
	struct ptychite_server *server = view->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct ptychite_config *config = server->compositor->config;

	if (prev_surface == surface) {
		return;
	}
	if (prev_surface) {
		surface_unfocus(prev_surface);
	}

	view->focused = true;
	wlr_scene_node_raise_to_top(&view->element.scene_tree->node);
	wl_list_remove(&view->server_link);
	wl_list_insert(&server->views, &view->server_link);
	if (view->workspace) {
		wl_list_remove(&view->workspace_focus_link);
		wl_list_insert(&view->workspace->views_focus, &view->workspace_focus_link);
	}
	wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);

	wlr_scene_rect_set_color(view->border.top, config->views.border.colors.active);
	wlr_scene_rect_set_color(view->border.right, config->views.border.colors.active);
	wlr_scene_rect_set_color(view->border.bottom, config->views.border.colors.active);
	wlr_scene_rect_set_color(view->border.left, config->views.border.colors.active);
	if (view->title_bar && view->title_bar->base.element.scene_tree->node.enabled) {
		window_relay_draw_same_size(&view->title_bar->base);
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(seat, view->xdg_toplevel->base->surface, keyboard->keycodes,
				keyboard->num_keycodes, &keyboard->modifiers);
	}
}

static struct ptychite_workspace *monitor_add_workspace(struct ptychite_monitor *monitor) {
	struct ptychite_workspace *workspace = calloc(1, sizeof(struct ptychite_workspace));
	if (!workspace) {
		return NULL;
	}

	wl_list_init(&workspace->views_order);
	wl_list_init(&workspace->views_focus);
	workspace->tiling.traditional.views_in_master = 1;
	workspace->tiling.traditional.master_factor = 0.55;
	workspace->tiling.traditional.right_master = false;

	wl_list_insert(monitor->workspaces.prev, &workspace->link);

	return workspace;
}

static void monitor_tile(struct ptychite_monitor *monitor) {
	struct ptychite_workspace *workspace = monitor->current_workspace;
	if (wl_list_empty(&workspace->views_order)) {
		return;
	}

	struct ptychite_config *config = monitor->server->compositor->config;
	int gaps = config->tiling.gaps;

	switch (config->tiling.mode) {
	case PTYCHITE_TILING_NONE:
		break;
	case PTYCHITE_TILING_TRADITIONAL: {
		int views_len = wl_list_length(&workspace->views_order);
		int views_in_master = workspace->tiling.traditional.views_in_master;
		double master_factor = workspace->tiling.traditional.master_factor;
		bool right_master = workspace->tiling.traditional.right_master;

		int master_width;
		if (views_len > views_in_master) {
			master_width = views_in_master ? (monitor->window_geometry.width + gaps) * master_factor : 0;
		} else {
			master_width = monitor->window_geometry.width - gaps;
		}

		int master_x =
				monitor->window_geometry.x + (right_master ? monitor->window_geometry.width - master_width : gaps);
		int stack_x = monitor->window_geometry.x + (right_master ? gaps : master_width + gaps);
		int master_y = gaps;
		int stack_y = gaps;
		int i = 0;
		struct ptychite_view *view;
		wl_list_for_each(view, &workspace->views_order, workspace_order_link) {
			if (i < views_in_master) {
				int r = fmin(views_len, views_in_master) - i;
				int height = (monitor->window_geometry.height - master_y - gaps * r) / r;
				wlr_scene_node_set_position(
						&view->element.scene_tree->node, master_x, monitor->window_geometry.y + master_y);
				view_resize(view, master_width - gaps, height);
				master_y += view->element.height + gaps;
			} else {
				int r = views_len - i;
				int height = (monitor->window_geometry.height - stack_y - gaps * r) / r;
				wlr_scene_node_set_position(
						&view->element.scene_tree->node, stack_x, monitor->window_geometry.y + stack_y);
				view_resize(view, monitor->window_geometry.width - master_width - 2 * gaps, height);
				stack_y += view->element.height + gaps;
			}
			i++;
		}

		break;
	}
	}

	ptychite_server_check_cursor(monitor->server);
}

static void monitor_fix_workspaces(struct ptychite_monitor *monitor) {
	struct ptychite_workspace *end_workspace = wl_container_of(monitor->workspaces.prev, end_workspace, link);

	struct ptychite_workspace *workspace, *workspace_tmp;
	wl_list_for_each_safe(workspace, workspace_tmp, &monitor->workspaces, link) {
		if (workspace == end_workspace) {
			break;
		}
		if (workspace == monitor->current_workspace) {
			continue;
		}
		if (wl_list_empty(&workspace->views_order)) {
			wl_list_remove(&workspace->link);
			free(workspace);
		}
	}
}

void monitor_switch_workspace(struct ptychite_monitor *monitor, struct ptychite_workspace *workspace) {
	struct ptychite_workspace *last_workspace = monitor->current_workspace;
	monitor->current_workspace = workspace;

	struct ptychite_view *view;
	wl_list_for_each(view, &last_workspace->views_order, workspace_order_link) {
		wlr_scene_node_set_enabled(&view->element.scene_tree->node, false);
	}
	wl_list_for_each(view, &monitor->current_workspace->views_order, workspace_order_link) {
		wlr_scene_node_set_enabled(&view->element.scene_tree->node, true);
	}

	monitor_fix_workspaces(monitor);
	if (monitor->panel && monitor->panel->base.element.scene_tree->node.enabled) {
		window_relay_draw_same_size(&monitor->panel->base);
	}

	if (wl_list_empty(&monitor->current_workspace->views_focus)) {
		struct wlr_surface *focused_surface = monitor->server->seat->keyboard_state.focused_surface;
		if (focused_surface) {
			surface_unfocus(focused_surface);
			wlr_seat_keyboard_notify_clear_focus(monitor->server->seat);
		}
	} else {
		struct ptychite_view *new_view =
				wl_container_of(monitor->current_workspace->views_focus.next, new_view, workspace_focus_link);
		view_focus(new_view, new_view->xdg_toplevel->base->surface);
	}

	ptychite_server_check_cursor(monitor->server);
}

static void monitor_disable(struct ptychite_monitor *monitor) {
	struct ptychite_server *server = monitor->server;

	if (monitor == server->active_monitor) {
		server->active_monitor = NULL;
		struct ptychite_monitor *iter;
		wl_list_for_each(iter, &server->monitors, link) {
			if (iter->output->enabled) {
				server->active_monitor = iter;
				break;
			}
		}
	}

	if (server->active_monitor) {
		struct ptychite_workspace *end_workspace = wl_container_of(monitor->workspaces.prev, end_workspace, link);
		wl_list_init(&end_workspace->views_order);
		wl_list_init(&end_workspace->views_focus);

		struct ptychite_workspace *workspace;
		wl_list_for_each(workspace, &monitor->workspaces, link) {
			if (workspace == end_workspace) {
				break;
			}
			wl_list_remove(&workspace->link);
			free(workspace);
		}

		struct ptychite_view *view, *view_tmp;
		wl_list_for_each_safe(view, view_tmp, &monitor->views, monitor_link) {
			wl_list_insert(&server->active_monitor->views, &view->monitor_link);
			wl_list_insert(&server->active_monitor->current_workspace->views_order, &view->workspace_order_link);
			wl_list_insert(server->active_monitor->current_workspace->views_focus.prev, &view->workspace_focus_link);
		}
		wl_list_init(&monitor->views);

		monitor_tile(server->active_monitor);
	}
}

static void monitor_handle_frame(struct wl_listener *listener, void *data) {
	struct ptychite_monitor *monitor = wl_container_of(listener, monitor, frame);

	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(monitor->server->scene, monitor->output);

	struct ptychite_monitor *monitor_iter;
	wl_list_for_each(monitor_iter, &monitor->server->monitors, link) {
		struct ptychite_view *view;
		wl_list_for_each(view, &monitor_iter->current_workspace->views_order, workspace_order_link) {
			if (!view->resize_serial || view == monitor->server->grabbed_view ||
					!view->element.scene_tree->node.enabled) {
				continue;
			}

			struct wlr_surface_output *surface_output;
			wl_list_for_each(surface_output, &view->xdg_toplevel->base->surface->current_outputs, link) {
				if (surface_output->output == monitor->output) {
					goto skip;
				}
			}
		}
	}

	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
skip:
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void monitor_handle_request_state(struct wl_listener *listener, void *data) {
	struct ptychite_monitor *monitor = wl_container_of(listener, monitor, request_state);
	const struct wlr_output_event_request_state *event = data;

	wlr_output_commit_state(monitor->output, event->state);
}

static void monitor_handle_destroy(struct wl_listener *listener, void *data) {
	struct ptychite_monitor *monitor = wl_container_of(listener, monitor, destroy);

	wl_list_remove(&monitor->frame.link);
	wl_list_remove(&monitor->request_state.link);
	wl_list_remove(&monitor->destroy.link);
	wl_list_remove(&monitor->link);

	if (!monitor->server->terminated) {
		if (monitor->wallpaper) {
			wlr_scene_node_destroy(&monitor->wallpaper->base.scene_buffer->node);
		}
		if (monitor->panel) {
			wlr_scene_node_destroy(&monitor->panel->base.scene_buffer->node);
		}
	}

	monitor_disable(monitor);
	struct ptychite_workspace *workspace;
	wl_list_for_each(workspace, &monitor->workspaces, link) {
		free(workspace);
	}

	free(monitor);
}

void view_begin_interactive(struct ptychite_view *view, enum ptychite_cursor_mode mode) {
	struct ptychite_server *server = view->server;
	struct wlr_surface *focused_surface = server->seat->pointer_state.focused_surface;

	if (focused_surface && view->xdg_toplevel->base->surface != wlr_surface_get_root_surface(focused_surface)) {
		return;
	}
	server->grabbed_view = view;
	server->cursor_mode = mode;

	if (mode == PTYCHITE_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - view->element.scene_tree->node.x;
		server->grab_y = server->cursor->y - view->element.scene_tree->node.y;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "fleur");
	} else {
		double border_x = view->element.scene_tree->node.x + view->element.width;
		double border_y = view->element.scene_tree->node.y + view->element.height;
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "bottom_right_corner");
	}
}

static void view_handle_commit(struct wl_listener *listener, void *data) {
	struct ptychite_view *view = wl_container_of(listener, view, commit);

	if (view->resize_serial && view->resize_serial <= view->xdg_toplevel->base->current.configure_serial) {
		view->resize_serial = 0;
	}
}

static void view_handle_set_title(struct wl_listener *listener, void *data) {
	struct ptychite_view *view = wl_container_of(listener, view, set_title);

	if (view->title_bar && view->title_bar->base.element.scene_tree->node.enabled) {
		window_relay_draw_same_size(&view->title_bar->base);
	}
}

static void view_handle_map(struct wl_listener *listener, void *data) {
	struct ptychite_view *view = wl_container_of(listener, view, map);
	struct ptychite_config *config = view->server->compositor->config;

	wl_list_insert(&view->server->views, &view->server_link);
	if (view->server->active_monitor) {
		view->monitor = view->server->active_monitor;
		view->workspace = view->monitor->current_workspace;

		wl_list_insert(&view->monitor->views, &view->monitor_link);
		wl_list_insert(config->views.map_to_front ? &view->workspace->views_order : view->workspace->views_order.prev,
				&view->workspace_order_link);
		wl_list_insert(&view->workspace->views_focus, &view->workspace_focus_link);

		struct ptychite_workspace *end_workspace = wl_container_of(view->monitor->workspaces.prev, end_workspace, link);
		if (view->workspace == end_workspace && monitor_add_workspace(view->monitor) && view->monitor->panel &&
				view->monitor->panel->base.scene_buffer->node.enabled) {
			window_relay_draw_same_size(&view->monitor->panel->base);
		}
	}
	view->commit.notify = view_handle_commit;
	wl_signal_add(&view->xdg_toplevel->base->surface->events.commit, &view->commit);
	view->set_title.notify = view_handle_set_title;
	wl_signal_add(&view->xdg_toplevel->events.set_title, &view->set_title);

	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geometry);
	view->initial_width = geometry.width + 2 * config->views.border.thickness;
	view->initial_height = geometry.height + 2 * config->views.border.thickness;

	if (!view->monitor || config->tiling.mode == PTYCHITE_TILING_NONE) {
		struct wlr_box box;
		if (view->monitor) {
			box = view->monitor->window_geometry;
		} else {
			wlr_output_layout_get_box(view->server->output_layout, NULL, &box);
		}

		wlr_scene_node_set_position(&view->element.scene_tree->node, box.x + (box.width - view->initial_width) / 2,
				box.y + (box.height - view->initial_height) / 2);
		view_resize(view, view->initial_width, view->initial_height);
	} else {
		monitor_tile(view->monitor);
	}

	wlr_scene_node_set_enabled(&view->element.scene_tree->node, true);
	view_focus(view, view->xdg_toplevel->base->surface);
}

static void view_handle_unmap(struct wl_listener *listener, void *data) {
	struct ptychite_view *view = wl_container_of(listener, view, unmap);

	if (view == view->server->grabbed_view) {
		view->server->cursor_mode = PTYCHITE_CURSOR_PASSTHROUGH;
		view->server->grabbed_view = NULL;
	}

	wl_list_remove(&view->workspace_order_link);
	wl_list_remove(&view->workspace_focus_link);
	wl_list_remove(&view->monitor_link);
	wl_list_remove(&view->server_link);
	wl_list_remove(&view->set_title.link);

	wlr_scene_node_set_enabled(&view->element.scene_tree->node, false);

	if (view->monitor) {
		monitor_tile(view->monitor);
		if (view->focused && !wl_list_empty(&view->monitor->current_workspace->views_order)) {
			struct ptychite_view *new_view =
					wl_container_of(view->monitor->current_workspace->views_focus.next, new_view, workspace_focus_link);
			view_focus(new_view, new_view->xdg_toplevel->base->surface);
		}
	}
}

static void view_handle_destroy(struct wl_listener *listener, void *data) {
	struct ptychite_view *view = wl_container_of(listener, view, destroy);

	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);

	wlr_scene_node_destroy(&view->element.scene_tree->node);

	free(view);
}

static void view_handle_request_maximize(struct wl_listener *listener, void *data) {
	struct ptychite_view *view = wl_container_of(listener, view, request_maximize);

	wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void view_handle_request_fullscreen(struct wl_listener *listener, void *data) {
	struct ptychite_view *view = wl_container_of(listener, view, request_fullscreen);

	wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}


static void server_activate_monitor(struct ptychite_server *server, struct ptychite_monitor *monitor) {
	server->active_monitor = monitor;
}

static struct ptychite_element *server_identify_element_at(struct ptychite_server *server, double lx, double ly, double *sx,
		double *sy, struct wlr_scene_buffer **scene_buffer) {
	struct wlr_scene_node *scene_node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);

	if (!scene_node || scene_node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}

	if (scene_buffer) {
		*scene_buffer = wlr_scene_buffer_from_node(scene_node);
	}

	struct wlr_scene_tree *scene_tree = scene_node->parent;
	while (scene_tree && !scene_tree->node.data) {
		scene_tree = scene_tree->node.parent;
	}

	return scene_tree->node.data;
}

static void server_process_cursor_move(struct ptychite_server *server, uint32_t time) {
	struct ptychite_view *view = server->grabbed_view;

	wlr_scene_node_set_position(
			&view->element.scene_tree->node, server->cursor->x - server->grab_x, server->cursor->y - server->grab_y);
}

static void server_process_cursor_resize(struct ptychite_server *server, uint32_t time) {
	struct ptychite_view *view = server->grabbed_view;

	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;

	int new_left = view->element.scene_tree->node.x;
	int new_right = view->element.scene_tree->node.x + view->element.width;
	int new_top = view->element.scene_tree->node.y;
	int new_bottom = view->element.scene_tree->node.y + view->element.height;

	new_right = border_x;
	if (new_right <= new_left) {
		new_right = new_left + 1;
	}
	new_bottom = border_y;
	if (new_bottom <= new_top) {
		new_bottom = new_top + 1;
	}

	view_resize(view, new_right - new_left, new_bottom - new_top);
}

static void server_process_cursor_motion(struct ptychite_server *server, uint32_t time) {
	struct wlr_output *output =
			wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
	if (output && output->data != server->active_monitor) {
		server_activate_monitor(server, output->data);
	}

	if (server->cursor_mode == PTYCHITE_CURSOR_MOVE) {
		server_process_cursor_move(server, time);
		return;
	} else if (server->cursor_mode == PTYCHITE_CURSOR_RESIZE) {
		server_process_cursor_resize(server, time);
		return;
	}

	double sx, sy;
	struct wlr_scene_buffer *scene_buffer;
	struct ptychite_element *element =
			server_identify_element_at(server, server->cursor->x, server->cursor->y, &sx, &sy, &scene_buffer);
	if (element) {
		switch (element->type) {
		case PTYCHITE_ELEMENT_VIEW: {
			struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
			if (scene_surface) {
				wlr_seat_pointer_notify_enter(server->seat, scene_surface->surface, sx, sy);
				wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
			} else {
				wlr_seat_pointer_clear_focus(server->seat);
			}
			if (server->hovered_window) {
				window_relay_pointer_leave(server->hovered_window);
				server->hovered_window = NULL;
			}
			break;
		}
		case PTYCHITE_ELEMENT_WINDOW: {
			struct ptychite_window *window = element_get_window(element);
			if (window != server->hovered_window) {
				if (server->hovered_window) {
					window_relay_pointer_leave(server->hovered_window);
				}
				server->hovered_window = window;
				window_relay_pointer_enter(window);
			}
			window_relay_pointer_move(window, sx, sy);
			wlr_seat_pointer_clear_focus(server->seat);
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
			break;
		}
		}
	} else {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
		wlr_seat_pointer_clear_focus(server->seat);
		if (server->hovered_window) {
			window_relay_pointer_leave(server->hovered_window);
			server->hovered_window = NULL;
		}
	}
}

static void server_new_pointer(struct ptychite_server *server, struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_keyboard(struct ptychite_server *server, struct wlr_input_device *device) {
	struct ptychite_keyboard *p_keyboard = calloc(1, sizeof(struct ptychite_keyboard));
	if (!p_keyboard) {
		wlr_log(WLR_ERROR, "Could not initialize keyboard: insufficent memory");
		return;
	}

	struct wlr_keyboard *keyboard = wlr_keyboard_from_input_device(device);

	p_keyboard->server = server;
	p_keyboard->keyboard = keyboard;

	wlr_keyboard_set_repeat_info(keyboard, server->compositor->config->keyboard.repeat.rate,
			server->compositor->config->keyboard.repeat.delay);

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context) {
		struct xkb_rule_names rules = (struct xkb_rule_names){
				.options = server->compositor->config->keyboard.xkb.options,
				.rules = NULL,
				.layout = NULL,
				.model = NULL,
				.variant = NULL,
		};
		struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
		wlr_keyboard_set_keymap(keyboard, keymap);
		xkb_keymap_unref(keymap);
		xkb_context_unref(context);
	} else {
		wlr_keyboard_set_keymap(keyboard, NULL);
	}

	p_keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&p_keyboard->keyboard->events.modifiers, &p_keyboard->modifiers);
	p_keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&p_keyboard->keyboard->events.key, &p_keyboard->key);
	p_keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &p_keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, p_keyboard->keyboard);

	wl_list_insert(&server->keyboards, &p_keyboard->link);
}

static int server_time_tick_update(void *data) {
	struct ptychite_server *server = data;

	time_t t;
	time(&t);
	struct tm *info = localtime(&t);

	if (info) {
		if (!*server->panel_date || !info->tm_sec) {
			strftime(server->panel_date, sizeof(server->panel_date), "%b %-d %-H:%M", info);
			struct ptychite_monitor *monitor;
			wl_list_for_each(monitor, &server->monitors, link) {
				if (!monitor->panel || !monitor->panel->base.element.scene_tree->node.enabled) {
					continue;
				}
				window_relay_draw_same_size(&monitor->panel->base);
			}
		}

		if (!server->control_greeting || !info->tm_sec) {
			char *greeting;
			if (info->tm_hour >= 18) {
				greeting = "Good Evening";
			} else if (info->tm_hour >= 12) {
				greeting = "Good Afternoon";
			} else {
				greeting = "Good Morning";
			}
			if (server->control_greeting != greeting) {
				server->control_greeting = greeting;
				control_draw_auto(server->control);
			}
		}
	}

	wl_event_source_timer_update(server->time_tick, 1000);

	return 0;
}

static void server_update_monitors(struct ptychite_server *server) {
	struct wlr_output_configuration_v1 *output_config = wlr_output_configuration_v1_create();

	struct ptychite_monitor *monitor;
	wl_list_for_each(monitor, &server->monitors, link) {
		if (monitor->output->enabled) {
			continue;
		}

		if (output_config) {
			struct wlr_output_configuration_head_v1 *head =
					wlr_output_configuration_head_v1_create(output_config, monitor->output);
			if (head) {
				head->state.enabled = false;
			}
		}
		wlr_output_layout_remove(server->output_layout, monitor->output);
		monitor_disable(monitor);
	}

	wl_list_for_each(monitor, &server->monitors, link) {
		if (monitor->output->enabled && !wlr_output_layout_get(server->output_layout, monitor->output)) {
			wlr_output_layout_add_auto(server->output_layout, monitor->output);
		}
	}

	wl_list_for_each(monitor, &server->monitors, link) {
		if (!monitor->output->enabled) {
			continue;
		}

		wlr_output_layout_get_box(server->output_layout, monitor->output, &monitor->geometry);
		if (monitor->panel && monitor->panel->base.element.scene_tree->node.enabled) {
			monitor->window_geometry = (struct wlr_box){
					.x = monitor->geometry.x,
					.y = monitor->geometry.y + monitor->panel->base.element.height,
					.width = monitor->geometry.width,
					.height = monitor->geometry.height - monitor->panel->base.element.height,
			};
		} else {
			monitor->window_geometry = monitor->geometry;
		}

		if (monitor->wallpaper) {
			wlr_scene_node_set_position(
					&monitor->wallpaper->base.element.scene_tree->node, monitor->geometry.x, monitor->geometry.y);
			wallpaper_draw_auto(monitor->wallpaper);
		}

		if (monitor->panel) {
			wlr_scene_node_set_position(
					&monitor->panel->base.element.scene_tree->node, monitor->geometry.x, monitor->geometry.y);
			if (monitor->panel->base.element.scene_tree->node.enabled) {
				panel_draw_auto(monitor->panel);
			}
		}

		monitor_tile(monitor);

		if (output_config) {
			struct wlr_output_configuration_head_v1 *head =
					wlr_output_configuration_head_v1_create(output_config, monitor->output);
			if (head) {
				head->state.enabled = true;
				head->state.mode = monitor->output->current_mode;
				head->state.x = monitor->geometry.x;
				head->state.y = monitor->geometry.y;
			}
		}
	}

	if (server->control->base.element.scene_tree->node.enabled) {
		control_draw_auto(server->control);
	}

	wlr_output_manager_v1_set_configuration(server->output_mgr, output_config);
}

static void server_apply_output_config(
		struct ptychite_server *server, struct wlr_output_configuration_v1 *output_config, bool test) {
	bool ok = true;

	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &output_config->heads, link) {
		struct wlr_output *output = head->state.output;
		struct ptychite_monitor *monitor = output->data;

		wlr_output_enable(output, head->state.enabled);
		if (head->state.enabled) {
			if (head->state.mode) {
				wlr_output_set_mode(output, head->state.mode);
			} else {
				wlr_output_set_custom_mode(output, head->state.custom_mode.width, head->state.custom_mode.height,
						head->state.custom_mode.refresh);
			}

			if (monitor->geometry.x != head->state.x || monitor->geometry.y != head->state.y) {
				wlr_output_layout_add(server->output_layout, output, head->state.x, head->state.y);
			}
			wlr_output_set_transform(output, head->state.transform);
			wlr_output_set_scale(output, head->state.scale);
			wlr_xcursor_manager_load(server->cursor_mgr, head->state.scale);
			wlr_output_enable_adaptive_sync(output, head->state.adaptive_sync_enabled);
		}

		if (test) {
			ok &= wlr_output_test(output);
			wlr_output_rollback(output);
		} else {
			ok &= wlr_output_commit(output);
		}
	}

	if (ok) {
		wlr_output_configuration_v1_send_succeeded(output_config);
	} else {
		wlr_output_configuration_v1_send_failed(output_config);
	}

	server_update_monitors(server);
}

static void server_handle_output_mgr_apply(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, output_mgr_apply);
	struct wlr_output_configuration_v1 *output_config = data;

	server_apply_output_config(server, output_config, false);
	wlr_output_configuration_v1_destroy(output_config);
}

static void server_handle_output_mgr_test(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, output_mgr_test);
	struct wlr_output_configuration_v1 *output_config = data;

	server_apply_output_config(server, output_config, true);
	wlr_output_configuration_v1_destroy(output_config);
}

static void server_handle_layout_change(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, layout_change);

	server_update_monitors(server);
}

static void server_handle_new_output(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *output = data;

	struct ptychite_monitor *monitor = calloc(1, sizeof(struct ptychite_monitor));
	if (!monitor) {
		wlr_log(WLR_ERROR, "Could not initialize output: insufficent memory");
		return;
	}

	wl_list_init(&monitor->workspaces);
	if (!(monitor->current_workspace = monitor_add_workspace(monitor))) {
		wlr_log(WLR_ERROR, "Could not initialize output: insufficent memory");
		free(monitor);
		return;
	}

	if (!wlr_output_init_render(output, server->allocator, server->renderer)) {
		wlr_log(WLR_ERROR, "Could not initialize output render");
		free(monitor->current_workspace);
		free(monitor);
		return;
	}

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_state_set_scale(&state, server->compositor->config->monitors.default_scale);

	wlr_output_commit_state(output, &state);
	wlr_output_state_finish(&state);

	output->data = monitor;
	monitor->output = output;
	monitor->server = server;
	wl_list_init(&monitor->views);

	monitor->frame.notify = monitor_handle_frame;
	wl_signal_add(&monitor->output->events.frame, &monitor->frame);
	monitor->request_state.notify = monitor_handle_request_state;
	wl_signal_add(&monitor->output->events.request_state, &monitor->request_state);
	monitor->destroy.notify = monitor_handle_destroy;
	wl_signal_add(&monitor->output->events.destroy, &monitor->destroy);

	wl_list_insert(&server->monitors, &monitor->link);
	if (!server->active_monitor) {
		server->active_monitor = monitor;
	}

	if ((monitor->wallpaper = calloc(1, sizeof(struct ptychite_wallpaper)))) {
		if (!window_init(&monitor->wallpaper->base, server, &wallpaper_window_impl, server->layers.bottom, output)) {
			monitor->wallpaper->monitor = monitor;
		} else {
			free(monitor->wallpaper);
			monitor->wallpaper = NULL;
		}
	}

	if ((monitor->panel = calloc(1, sizeof(struct ptychite_panel)))) {
		if (!window_init(&monitor->panel->base, server, &panel_window_impl, server->layers.bottom, output)) {
			monitor->panel->monitor = monitor;
		} else {
			free(monitor->panel);
			monitor->panel = NULL;
		}
	}

	struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout, output);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
}

static void server_handle_new_xdg_surface(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;

	if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_surface->popup->parent);
		assert(parent);
		struct wlr_scene_tree *parent_tree = parent->data;
		struct wlr_scene_tree *scene_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_surface);
		xdg_surface->data = scene_tree;
		return;
	}
	assert(xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

	struct ptychite_view *view = calloc(1, sizeof(struct ptychite_view));
	if (!view) {
		wlr_log(WLR_ERROR, "Could not initialize view: insufficent memory");
		return;
	}

	view->element.type = PTYCHITE_ELEMENT_VIEW;
	view->server = server;
	view->xdg_toplevel = xdg_surface->toplevel;
	view->element.scene_tree = wlr_scene_tree_create(server->layers.tiled);
	xdg_surface->data = view->element.scene_tree;
	view->scene_tree_surface = wlr_scene_xdg_surface_create(view->element.scene_tree, view->xdg_toplevel->base);
	view->element.scene_tree->node.data = view->scene_tree_surface->node.data = &view->element;

	float *colors = server->compositor->config->views.border.colors.inactive;
	struct wlr_scene_rect **borders[] = {
			&view->border.top, &view->border.right, &view->border.bottom, &view->border.left};
	size_t i;
	for (i = 0; i < LENGTH(borders); i++) {
		*borders[i] = wlr_scene_rect_create(view->element.scene_tree, 0, 0, colors);
		(*borders[i])->node.data = view;
	}

	if ((view->title_bar = calloc(1, sizeof(struct ptychite_title_bar)))) {
		if (!window_init(&view->title_bar->base, server, &title_bar_window_impl, view->element.scene_tree, NULL)) {
			view->title_bar->view = view;
			wlr_scene_node_set_enabled(&view->title_bar->base.element.scene_tree->node,
					server->compositor->config->views.title_bar.enabled);
			wlr_scene_node_set_enabled(&view->border.top->node, !server->compositor->config->views.title_bar.enabled);
		} else {
			free(view->title_bar);
			view->title_bar = NULL;
		}
	}

	view->map.notify = view_handle_map;
	wl_signal_add(&xdg_surface->surface->events.map, &view->map);
	view->unmap.notify = view_handle_unmap;
	wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
	view->destroy.notify = view_handle_destroy;
	wl_signal_add(&xdg_surface->surface->events.destroy, &view->destroy);

	struct wlr_xdg_toplevel *toplevel = xdg_surface->toplevel;
	view->request_maximize.notify = view_handle_request_maximize;
	wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);
	view->request_fullscreen.notify = view_handle_request_fullscreen;
	wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);
}

static void server_handle_idle_inhibitor_create(struct wl_listener *listener, void *data) {
	struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;

	// TODO
}

static void server_handle_new_xdg_decoration(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *xdg_toplevel_decoration = data;

	wlr_xdg_toplevel_decoration_v1_set_mode(xdg_toplevel_decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void server_handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	server_process_cursor_motion(server, event->time_msec);
}

static void server_handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;

	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	server_process_cursor_motion(server, event->time_msec);
}

static void server_handle_cursor_button(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	if (event->state == WLR_BUTTON_RELEASED) {
		if (server->cursor_mode != PTYCHITE_CURSOR_PASSTHROUGH) {
			server->cursor_mode = PTYCHITE_CURSOR_PASSTHROUGH;
			server->grabbed_view = NULL;
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
		}
	}

	double sx, sy;
	struct wlr_scene_buffer *scene_buffer;
	struct ptychite_element *element =
			server_identify_element_at(server, server->cursor->x, server->cursor->y, &sx, &sy, &scene_buffer);
	if (element) {
		switch (element->type) {
		case PTYCHITE_ELEMENT_VIEW:
			if (event->state == WLR_BUTTON_PRESSED) {
				struct ptychite_view *view = element_get_view(element);
				struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
				view_focus(view, scene_surface->surface);

				struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
				if (!keyboard) {
					break;
				}

				uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard);
				if (modifiers == WLR_MODIFIER_LOGO) {
					if (event->button == BTN_LEFT) {
						view_begin_interactive(view, PTYCHITE_CURSOR_MOVE);
						return;
					} else if (event->button == BTN_RIGHT) {
						view_begin_interactive(view, PTYCHITE_CURSOR_RESIZE);
						return;
					}
				}
			}
			break;
		case PTYCHITE_ELEMENT_WINDOW: {
			struct ptychite_window *window = element_get_window(element);
			window_relay_pointer_button(window, sx, sy, event);
			return;
		}
		}
	}

	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

static void server_handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	wlr_seat_pointer_notify_axis(
			server->seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source);
}

static void server_handle_cursor_frame(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, cursor_frame);

	wlr_seat_pointer_notify_frame(server->seat);
}

static void server_handle_new_input(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}

	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void server_handle_seat_request_cursor(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;

	if (focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

static void server_handle_seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;

	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static struct ptychite_view *server_get_top_view(struct ptychite_server *server) {
	if (server->active_monitor) {
		struct ptychite_view *view;
		wl_list_for_each(view, &server->active_monitor->current_workspace->views_focus, workspace_focus_link) {
			return view;
		}

		return NULL;
	}

	struct ptychite_view *view;
	wl_list_for_each(view, &server->views, server_link) {
		return view;
	}

	return NULL;
}

static struct ptychite_view *server_get_front_view(struct ptychite_server *server) {
	if (!server->active_monitor) {
		return NULL;
	}

	struct ptychite_view *view;
	wl_list_for_each(view, &server->active_monitor->current_workspace->views_order, workspace_order_link) {
		return view;
	}

	return NULL;
}

static struct ptychite_view *server_get_focused_view(struct ptychite_server *server) {
	struct wlr_surface *surface = server->seat->keyboard_state.focused_surface;
	if (!surface) {
		return NULL;
	}

	struct ptychite_view *view = server_get_top_view(server);
	if (!view) {
		return NULL;
	}

	if (view->xdg_toplevel->base->surface != surface) {
		return NULL;
	}

	return view;
}

static void server_tiling_change_views_in_master(struct ptychite_server *server, int delta) {
	struct ptychite_monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct ptychite_workspace *workspace = monitor->current_workspace;
	int views_in_master = workspace->tiling.traditional.views_in_master + delta;
	if (views_in_master > 100) {
		views_in_master = 100;
	} else if (views_in_master < 0) {
		views_in_master = 0;
	}

	if (views_in_master == workspace->tiling.traditional.views_in_master) {
		return;
	}

	workspace->tiling.traditional.views_in_master = views_in_master;
	monitor_tile(monitor);
}

static void server_tiling_change_master_factor(struct ptychite_server *server, double delta) {
	struct ptychite_monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct ptychite_workspace *workspace = monitor->current_workspace;
	double master_factor = workspace->tiling.traditional.master_factor + delta;
	if (master_factor > 0.95) {
		master_factor = 0.95;
	} else if (master_factor < 0.05) {
		master_factor = 0.05;
	}

	if (master_factor == workspace->tiling.traditional.master_factor) {
		return;
	}

	workspace->tiling.traditional.master_factor = master_factor;
	monitor_tile(monitor);
}

static void server_focus_any(struct ptychite_server *server) {
	struct ptychite_monitor *monitor = server->active_monitor;
	if (monitor && !wl_list_empty(&monitor->current_workspace->views_focus)) {
		struct ptychite_view *view = wl_container_of(monitor->current_workspace->views_focus.next, view, workspace_focus_link);
		view_focus(view, view->xdg_toplevel->base->surface);
	}
}

static void spawn(char **args) {
	if (!fork()) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(args[0], args);
		exit(EXIT_SUCCESS);
	}
}

static void server_action_terminate(struct ptychite_server *server, void *data) {
	wl_display_terminate(server->display);
}

static void server_action_close(struct ptychite_server *server, void *data) {
	struct ptychite_view *view = server_get_focused_view(server);
	if (!view) {
		return;
	}

	wlr_xdg_toplevel_send_close(view->xdg_toplevel);
}

static void server_action_toggle_control(struct ptychite_server *server, void *data) {
	if (server->control->base.element.scene_tree->node.enabled) {
		control_hide(server->control);
	} else {
		control_show(server->control);
	}
}

static void server_action_spawn(struct ptychite_server *server, void *data) {
	char **args = data;

	spawn(args);
}

static void server_action_shell(struct ptychite_server *server, void *data) {
	char *command = data;
	char *args[] = {"/bin/sh", "-c", command, NULL};

	spawn(args);
}

static void server_action_inc_master(struct ptychite_server *server, void *data) {
	server_tiling_change_views_in_master(server, 1);
}

static void server_action_dec_master(struct ptychite_server *server, void *data) {
	server_tiling_change_views_in_master(server, -1);
}

static void server_action_inc_mfact(struct ptychite_server *server, void *data) {
	server_tiling_change_master_factor(server, 0.05);
}

static void server_action_dec_mfact(struct ptychite_server *server, void *data) {
	server_tiling_change_master_factor(server, -0.05);
}

static void server_action_toggle_rmaster(struct ptychite_server *server, void *data) {
	struct ptychite_monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct ptychite_workspace *workspace = monitor->current_workspace;
	workspace->tiling.traditional.right_master = !workspace->tiling.traditional.right_master;

	monitor_tile(monitor);
}

static void server_action_goto_next_workspace(struct ptychite_server *server, void *data) {
	struct ptychite_monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct wl_list *list = monitor->current_workspace->link.next == &monitor->workspaces
			? monitor->current_workspace->link.next->next
			: monitor->current_workspace->link.next;
	struct ptychite_workspace *workspace = wl_container_of(list, workspace, link);

	monitor_switch_workspace(monitor, workspace);
}

static void server_action_goto_previous_workspace(struct ptychite_server *server, void *data) {
	struct ptychite_monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct wl_list *list = monitor->current_workspace->link.prev == &monitor->workspaces
			? monitor->current_workspace->link.prev->prev
			: monitor->current_workspace->link.prev;
	struct ptychite_workspace *workspace = wl_container_of(list, workspace, link);

	monitor_switch_workspace(monitor, workspace);
}

static void server_action_focus_next_view(struct ptychite_server *server, void *data) {
	struct ptychite_view *old_view = server_get_focused_view(server);
	if (!old_view) {
		server_focus_any(server);
		return;
	}

	struct wl_list *list = old_view->workspace_order_link.next == &old_view->workspace->views_order
			? old_view->workspace_order_link.next->next
			: old_view->workspace_order_link.next;
	struct ptychite_view *new_view = wl_container_of(list, new_view, workspace_order_link);

	view_focus(new_view, new_view->xdg_toplevel->base->surface);
}

static void server_action_focus_previous_view(struct ptychite_server *server, void *data) {
	struct ptychite_view *old_view = server_get_focused_view(server);
	if (!old_view) {
		server_focus_any(server);
		return;
	}

	struct wl_list *list = old_view->workspace_order_link.prev == &old_view->workspace->views_order
			? old_view->workspace_order_link.prev->prev
			: old_view->workspace_order_link.prev;
	struct ptychite_view *new_view = wl_container_of(list, new_view, workspace_order_link);

	view_focus(new_view, new_view->xdg_toplevel->base->surface);
}

static void server_action_swap_front(struct ptychite_server *server, void *data) {
	struct ptychite_view *view = server_get_focused_view(server);
	if (!view) {
		return;
	}

	struct ptychite_view *front = server_get_front_view(server);
	assert(front);

	struct ptychite_view *new_front;
	if (view == front) {
		if (view->workspace_order_link.next == &view->workspace->views_order) {
			return;
		} else {
			new_front = wl_container_of(view->workspace_order_link.next, new_front, workspace_order_link);
		}
	} else {
		new_front = view;
	}

	wl_list_remove(&new_front->workspace_order_link);
	wl_list_insert(&new_front->workspace->views_order, &new_front->workspace_order_link);
	monitor_tile(new_front->monitor);
}

static const struct {
	char *name;
	ptychite_action_func_t action_func;
	enum ptychite_action_func_data_mode data_mode;
} ptychite_action_name_table[] = {
		{"terminate", server_action_terminate, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"close", server_action_close, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"control", server_action_toggle_control, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"spawn", server_action_spawn, PTYCHITE_ACTION_FUNC_DATA_ARGV},
		{"shell", server_action_shell, PTYCHITE_ACTION_FUNC_DATA_STRING},
		{"inc_master", server_action_inc_master, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"dec_master", server_action_dec_master, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"inc_mfact", server_action_inc_mfact, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"dec_mfact", server_action_dec_mfact, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"toggle_rmaster", server_action_toggle_rmaster, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"next_workspace", server_action_goto_next_workspace, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"prev_workspace", server_action_goto_previous_workspace, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"next_view", server_action_focus_next_view, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"prev_view", server_action_focus_previous_view, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"swap_front", server_action_swap_front, PTYCHITE_ACTION_FUNC_DATA_NONE},
};

struct ptychite_server *ptychite_server_create(void) {
	return calloc(1, sizeof(struct ptychite_server));
}

int ptychite_server_init_and_run(struct ptychite_server *server, struct ptychite_compositor *compositor) {
	server->compositor = compositor;
	wl_array_init(&server->keys);
	server->terminated = false;

	if (!(server->display = wl_display_create())) {
		return -1;
	};

	if (!(server->backend = wlr_backend_autocreate(server->display, &server->session))) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		return -1;
	}

	if (!(server->renderer = wlr_renderer_autocreate(server->backend))) {
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		return -1;
	}

	if (!wlr_renderer_init_wl_display(server->renderer, server->display)) {
		return -1;
	}

	if (!(server->allocator = wlr_allocator_autocreate(server->backend, server->renderer))) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		return -1;
	}

	wlr_compositor_create(server->display, 5, server->renderer);
	wlr_subcompositor_create(server->display);
	wlr_data_device_manager_create(server->display);

	if (!(server->output_layout = wlr_output_layout_create())) {
		return -1;
	}
	server->layout_change.notify = server_handle_layout_change;
	wl_signal_add(&server->output_layout->events.change, &server->layout_change);
	wlr_xdg_output_manager_v1_create(server->display, server->output_layout);

	if (!(server->output_mgr = wlr_output_manager_v1_create(server->display))) {
		return -1;
	}
	server->output_mgr_apply.notify = server_handle_output_mgr_apply;
	wl_signal_add(&server->output_mgr->events.apply, &server->output_mgr_apply);
	server->output_mgr_test.notify = server_handle_output_mgr_test;
	wl_signal_add(&server->output_mgr->events.test, &server->output_mgr_test);

	wl_list_init(&server->monitors);
	server->new_output.notify = server_handle_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);

	if (!(server->scene = wlr_scene_create())) {
		return -1;
	}
	if (!(server->scene_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout))) {
		return -1;
	}

	struct wlr_presentation *presentation = wlr_presentation_create(server->display, server->backend);
	if (!presentation) {
		return -1;
	}
	wlr_scene_set_presentation(server->scene, presentation);
	struct wlr_scene_tree **layers[] = {&server->layers.background, &server->layers.bottom, &server->layers.tiled,
			&server->layers.floating, &server->layers.fullscreen, &server->layers.top, &server->layers.overlay,
			&server->layers.block};
	size_t i;
	for (i = 0; i < LENGTH(layers); i++) {
		struct wlr_scene_tree **layer = layers[i];
		if (!(*layer = wlr_scene_tree_create(&server->scene->tree))) {
			return -1;
		}
	}
	if (!wlr_scene_attach_output_layout(server->scene, server->output_layout)) {
		return -1;
	}

	/* server->idle = wlr_idle_create(server->display); */
	/* server->idle_notifier = wlr_idle_notifier_v1_create(server->display); */
	/* server->idle_inhibit_mgr = wlr_idle_inhibit_v1_create(server->display); */
	/* server->idle_inhibitor_create.notify = server_handle_idle_inhibitor_create; */
	/* wl_signal_add(&server->idle_inhibit_mgr->events.new_inhibitor, &server->idle_inhibitor_create); */

	wl_list_init(&server->views);
	server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
	server->new_xdg_surface.notify = server_handle_new_xdg_surface;
	wl_signal_add(&server->xdg_shell->events.new_surface, &server->new_xdg_surface);

	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

	server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	server->cursor_mode = PTYCHITE_CURSOR_PASSTHROUGH;
	server->cursor_motion.notify = server_handle_cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = server_handle_cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
	server->cursor_button.notify = server_handle_cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = server_handle_cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = server_handle_cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");

	wl_list_init(&server->keyboards);
	server->new_input.notify = server_handle_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);
	server->seat = wlr_seat_create(server->display, "seat0");
	server->request_cursor.notify = server_handle_seat_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
	server->request_set_selection.notify = server_handle_seat_request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);

	wl_global_create(server->display, &zptychite_message_v1_interface, 1, server, message_handle_bind);

	wlr_viewporter_create(server->display);
	wlr_single_pixel_buffer_manager_v1_create(server->display);
	wlr_gamma_control_manager_v1_create(server->display);
	wlr_screencopy_manager_v1_create(server->display);
	wlr_export_dmabuf_manager_v1_create(server->display);
	wlr_fractional_scale_manager_v1_create(server->display, 1);
	wlr_data_control_manager_v1_create(server->display);

	struct wlr_server_decoration_manager *server_decoration_manager =
			wlr_server_decoration_manager_create(server->display);
	if (!server_decoration_manager) {
		return -1;
	}
	wlr_server_decoration_manager_set_default_mode(
			server_decoration_manager, WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager =
			wlr_xdg_decoration_manager_v1_create(server->display);
	if (!xdg_decoration_manager) {
		return -1;
	}
	server->new_xdg_decoration.notify = server_handle_new_xdg_decoration;
	wl_signal_add(&xdg_decoration_manager->events.new_toplevel_decoration, &server->new_xdg_decoration);

	if (!(server->control = calloc(1, sizeof(struct ptychite_control)))) {
		return -1;
	}
	if (window_init(&server->control->base, server, &control_window_impl, server->layers.overlay, NULL)) {
		return -1;
	}
	control_hide(server->control);

	if (!(server->time_tick = wl_event_loop_add_timer(
				  wl_display_get_event_loop(server->display), server_time_tick_update, server))) {
		return -1;
	}
	server_time_tick_update(server);

	const char *socket = wl_display_add_socket_auto(server->display);
	if (!socket) {
		wlr_backend_destroy(server->backend);
		return -1;
	}

	if (!wlr_backend_start(server->backend)) {
		wlr_backend_destroy(server->backend);
		wl_display_destroy(server->display);
		return -1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);

	wlr_log(WLR_INFO, "Running ptychite on WAYLAND_DISPLAY=%s", socket);
	wl_display_run(server->display);
	server->terminated = true;

	wl_display_destroy_clients(server->display);
	wlr_scene_node_destroy(&server->scene->tree.node);
	wlr_xcursor_manager_destroy(server->cursor_mgr);
	wlr_output_layout_destroy(server->output_layout);
	wl_display_destroy(server->display);

	return 0;
}

void ptychite_server_configure_keyboards(struct ptychite_server *server) {
	struct ptychite_config *config = server->compositor->config;

	struct ptychite_keyboard *keyboard;
	wl_list_for_each(keyboard, &server->keyboards, link) {
		wlr_keyboard_set_repeat_info(keyboard->keyboard, config->keyboard.repeat.rate, config->keyboard.repeat.delay);

		struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (!context) {
			continue;
		}
		const struct xkb_rule_names rule_names = {
				.options = config->keyboard.xkb.options,
				.rules = NULL,
				.layout = NULL,
				.model = NULL,
				.variant = NULL,
		};
		struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &rule_names, XKB_KEYMAP_COMPILE_NO_FLAGS);
		if (!keymap) {
			xkb_context_unref(context);
			continue;
		}
		wlr_keyboard_set_keymap(keyboard->keyboard, keymap);
		xkb_keymap_unref(keymap);
		xkb_context_unref(context);
	}
}

void ptychite_server_configure_panels(struct ptychite_server *server) {
	struct ptychite_monitor *monitor;
	wl_list_for_each(monitor, &server->monitors, link) {
		if (!monitor->panel) {
			continue;
		}

		wlr_scene_node_set_enabled(
				&monitor->panel->base.element.scene_tree->node, server->compositor->config->panel.enabled);
		if (monitor->panel->base.element.scene_tree->node.enabled) {
			panel_draw_auto(monitor->panel);
		} else {
			monitor->window_geometry = monitor->geometry;
		}
		monitor_tile(monitor);
	}

	if (server->control->base.element.scene_tree->node.enabled) {
		control_draw_auto(server->control);
	}
}

void ptychite_server_configure_views(struct ptychite_server *server) {
	struct ptychite_view *view;
	wl_list_for_each(view, &server->views, server_link) {
		if (view->title_bar) {
			wlr_scene_node_set_enabled(&view->title_bar->base.element.scene_tree->node,
					server->compositor->config->views.title_bar.enabled);
			wlr_scene_node_set_enabled(&view->border.top->node, !server->compositor->config->views.title_bar.enabled);
		}
		view_resize(view, view->element.width, view->element.height);
	}

	ptychite_server_check_cursor(server);
}

void ptychite_server_refresh_wallpapers(struct ptychite_server *server) {
	struct ptychite_monitor *monitor;
	wl_list_for_each(monitor, &server->monitors, link) {
		if (!monitor->wallpaper) {
			continue;
		}

		wallpaper_draw_auto(monitor->wallpaper);
	}
}

void ptychite_server_retile(struct ptychite_server *server) {
	struct ptychite_monitor *monitor;
	wl_list_for_each(monitor, &server->monitors, link) {
		monitor_tile(monitor);
	}
}

void ptychite_server_check_cursor(struct ptychite_server *server) {
	server_process_cursor_motion(server, 0);
}

void ptychite_server_execute_action(struct ptychite_server *server, struct ptychite_action *action) {
	action->action_func(server, action->data);
}

struct ptychite_action *ptychite_action_create(const char **args, int args_l, char **error) {
	if (args_l < 1) {
		*error = "action arguments must be non-zero in length";
		return NULL;
	}

	size_t i;
	for (i = 0; i < LENGTH(ptychite_action_name_table); i++) {
		if (strcmp(args[0], ptychite_action_name_table[i].name)) {
			continue;
		}

		struct ptychite_action *action = calloc(1, sizeof(struct ptychite_action));
		if (!action) {
			*error = "memory error";
			return NULL;
		}

		switch (ptychite_action_name_table[i].data_mode) {
		case PTYCHITE_ACTION_FUNC_DATA_NONE:
			if (args_l != 1) {
				*error = "actions with data type \"none\" require one argument";
				goto err;
			}

			action->data = NULL;
			break;

		case PTYCHITE_ACTION_FUNC_DATA_INT:
			if (args_l != 2) {
				*error = "actions with data type \"int\" require two arguments";
				goto err;
			}

			if (!(action->data = malloc(sizeof(int)))) {
				*error = "memory error";
				goto err;
			}
			*(int *)action->data = atoi(args[1]);
			break;

		case PTYCHITE_ACTION_FUNC_DATA_STRING:
			if (args_l != 2) {
				*error = "actions with data type \"string\" require two arguments";
				goto err;
			}

			if (!(action->data = strdup(args[1]))) {
				*error = "memory error";
				goto err;
			}
			break;

		case PTYCHITE_ACTION_FUNC_DATA_ARGV:
			if (args_l < 2) {
				*error = "actions with data type \"argv\" require at least two arguments";
				goto err;
			}

			int argc = args_l - 1;
			char **argv;
			if (!(argv = malloc(sizeof(char *) * (argc + 1)))) {
				*error = "memory error";
				goto err;
			}

			int j;
			for (j = 0; j < argc; j++) {
				if (!(argv[j] = strdup(args[j + 1]))) {
					int k;
					for (k = 0; k < j; k++) {
						free(argv[k]);
					}
					free(argv);
					*error = "memory error";
					goto err;
				}
			}
			argv[argc] = NULL;

			action->data = argv;
			break;

		default:
			*error = "something has gone very wrong";
			goto err;
		}

		action->action_func = ptychite_action_name_table[i].action_func;
		return action;

err:
		free(action);
		break;
	}

	return NULL;
}

int ptychite_action_get_args(struct ptychite_action *action, char ***args_out, int *args_l_out) {
	size_t i;
	for (i = 0; i < LENGTH(ptychite_action_name_table); i++) {
		if (ptychite_action_name_table[i].action_func != action->action_func) {
			continue;
		}

		int args_l;
		enum ptychite_action_func_data_mode data_mode = ptychite_action_name_table[i].data_mode;
		switch (data_mode) {
		case PTYCHITE_ACTION_FUNC_DATA_NONE:
			args_l = 1;
			break;

		case PTYCHITE_ACTION_FUNC_DATA_INT:
		case PTYCHITE_ACTION_FUNC_DATA_STRING:
			args_l = 2;
			break;

		case PTYCHITE_ACTION_FUNC_DATA_ARGV:
			for (args_l = 0; ((char **)action->data)[args_l]; args_l++) {
				;
			}
			args_l++;
			break;

		default:
			return -1;
		}

		char **args = malloc(args_l * sizeof(char *));
		if (!args) {
			return -1;
		}

		char *name = strdup(ptychite_action_name_table[i].name);
		if (!name) {
			free(args);
			return -1;
		}
		args[0] = name;

		switch (data_mode) {
		case PTYCHITE_ACTION_FUNC_DATA_NONE:
			break;

		case PTYCHITE_ACTION_FUNC_DATA_INT: {
			char buffer[64];
			snprintf(buffer, sizeof(buffer), "%d", *(int *)action->data);
			if (!(args[1] = strdup(buffer))) {
				free(name);
				free(args);
				return -1;
			}
			break;
		}

		case PTYCHITE_ACTION_FUNC_DATA_STRING:
			if (!(args[1] = strdup(action->data))) {
				free(name);
				free(args);
				return -1;
			}
			break;

		case PTYCHITE_ACTION_FUNC_DATA_ARGV: {
			int j;
			for (j = 1; j < args_l; j++) {
				if (!(args[j] = strdup(((char **)action->data)[j - 1]))) {
					free(name);
					int k;
					for (k = 1; k < j; k++) {
						free(args[k]);
					}
					free(args);
					return -1;
				}
			}
			break;
		}
		}

		*args_out = args;
		*args_l_out = args_l;

		return 0;
	}

	return -1;
}

void ptychite_action_destroy(struct ptychite_action *action) {
	size_t i;
	for (i = 0; i < LENGTH(ptychite_action_name_table); i++) {
		if (ptychite_action_name_table[i].action_func != action->action_func) {
			continue;
		}
		switch (ptychite_action_name_table[i].data_mode) {
		case PTYCHITE_ACTION_FUNC_DATA_INT:
		case PTYCHITE_ACTION_FUNC_DATA_STRING:
			free(action->data);
			break;

		case PTYCHITE_ACTION_FUNC_DATA_ARGV: {
			char **p;
			for (p = action->data; *p; p++) {
				free(*p);
			}
			free(action->data);
			break;
		}

		case PTYCHITE_ACTION_FUNC_DATA_NONE:
		default:
			break;
		}
		break;
	}

	free(action);
}
