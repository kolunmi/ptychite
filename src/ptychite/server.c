#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <cairo.h>
#include <drm_fourcc.h>

#include <linux/input-event-codes.h>
#include <math.h>
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
#include "macros.h"
#include "server.h"
#include "windows.h"
#include "message.h"
#include "view.h"
#include "monitor.h"
#include "keyboard.h"

static void server_activate_monitor(struct ptychite_server *server, struct ptychite_monitor *monitor) {
	server->active_monitor = monitor;
}

struct ptychite_element *server_identify_element_at(struct ptychite_server *server, double lx, double ly, double *sx,
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

	ptychite_view_resize(view, new_right - new_left, new_bottom - new_top);
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
				ptychite_window_relay_pointer_leave(server->hovered_window);
				server->hovered_window = NULL;
			}
			break;
		}
		case PTYCHITE_ELEMENT_WINDOW: {
			struct ptychite_window *window = ptychite_element_get_window(element);
			if (window != server->hovered_window) {
				if (server->hovered_window) {
					ptychite_window_relay_pointer_leave(server->hovered_window);
				}
				server->hovered_window = window;
				ptychite_window_relay_pointer_enter(window);
			}
			ptychite_window_relay_pointer_move(window, sx, sy);
			wlr_seat_pointer_clear_focus(server->seat);
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
			break;
		}
		}
	} else {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
		wlr_seat_pointer_clear_focus(server->seat);
		if (server->hovered_window) {
			ptychite_window_relay_pointer_leave(server->hovered_window);
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

	ptychite_keyboard_rig(p_keyboard, device);

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
				ptychite_window_relay_draw_same_size(&monitor->panel->base);
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
				ptychite_control_draw_auto(server->control);
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
		ptychite_monitor_disable(monitor);
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
			ptychite_wallpaper_draw_auto(monitor->wallpaper);
		}

		if (monitor->panel) {
			wlr_scene_node_set_position(
					&monitor->panel->base.element.scene_tree->node, monitor->geometry.x, monitor->geometry.y);
			if (monitor->panel->base.element.scene_tree->node.enabled) {
				ptychite_panel_draw_auto(monitor->panel);
			}
		}

		ptychite_monitor_tile(monitor);

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
		ptychite_control_draw_auto(server->control);
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
	if (!(monitor->current_workspace = ptychite_monitor_add_workspace(monitor))) {
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

	ptychite_monitor_rig(monitor);

	wl_list_insert(&server->monitors, &monitor->link);
	if (!server->active_monitor) {
		server->active_monitor = monitor;
	}

	if ((monitor->wallpaper = calloc(1, sizeof(struct ptychite_wallpaper)))) {
		if (!ptychite_window_init(&monitor->wallpaper->base, server, &ptychite_wallpaper_window_impl, server->layers.bottom, output)) {
			monitor->wallpaper->monitor = monitor;
		} else {
			free(monitor->wallpaper);
			monitor->wallpaper = NULL;
		}
	}

	if ((monitor->panel = calloc(1, sizeof(struct ptychite_panel)))) {
		if (!ptychite_window_init(&monitor->panel->base, server, &ptychite_panel_window_impl, server->layers.bottom, output)) {
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
		if (!ptychite_window_init(&view->title_bar->base, server, &ptychite_title_bar_window_impl, view->element.scene_tree, NULL)) {
			view->title_bar->view = view;
			wlr_scene_node_set_enabled(&view->title_bar->base.element.scene_tree->node,
					server->compositor->config->views.title_bar.enabled);
			wlr_scene_node_set_enabled(&view->border.top->node, !server->compositor->config->views.title_bar.enabled);
		} else {
			free(view->title_bar);
			view->title_bar = NULL;
		}
	}

	ptychite_view_rig(view, xdg_surface);
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
				struct ptychite_view *view = ptychite_element_get_view(element);
				struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
				ptychite_view_focus(view, scene_surface->surface);

				struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
				if (!keyboard) {
					break;
				}

				uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard);
				if (modifiers == WLR_MODIFIER_LOGO) {
					if (event->button == BTN_LEFT) {
						ptychite_view_begin_interactive(view, PTYCHITE_CURSOR_MOVE);
						return;
					} else if (event->button == BTN_RIGHT) {
						ptychite_view_begin_interactive(view, PTYCHITE_CURSOR_RESIZE);
						return;
					}
				}
			}
			break;
		case PTYCHITE_ELEMENT_WINDOW: {
			struct ptychite_window *window = ptychite_element_get_window(element);
			ptychite_window_relay_pointer_button(window, sx, sy, event);
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

struct ptychite_view *ptychite_server_get_top_view(struct ptychite_server *server) {
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

struct ptychite_view *ptychite_server_get_front_view(struct ptychite_server *server) {
	if (!server->active_monitor) {
		return NULL;
	}

	struct ptychite_view *view;
	wl_list_for_each(view, &server->active_monitor->current_workspace->views_order, workspace_order_link) {
		return view;
	}

	return NULL;
}

struct ptychite_view *ptychite_server_get_focused_view(struct ptychite_server *server) {
	struct wlr_surface *surface = server->seat->keyboard_state.focused_surface;
	if (!surface) {
		return NULL;
	}

	struct ptychite_view *view = ptychite_server_get_top_view(server);
	if (!view) {
		return NULL;
	}

	if (view->xdg_toplevel->base->surface != surface) {
		return NULL;
	}

	return view;
}

void ptychite_server_tiling_change_views_in_master(struct ptychite_server *server, int delta) {
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
	ptychite_monitor_tile(monitor);
}

void ptychite_server_tiling_change_master_factor(struct ptychite_server *server, double delta) {
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
	ptychite_monitor_tile(monitor);
}

void ptychite_server_focus_any(struct ptychite_server *server) {
	struct ptychite_monitor *monitor = server->active_monitor;
	if (monitor && !wl_list_empty(&monitor->current_workspace->views_focus)) {
		struct ptychite_view *view = wl_container_of(monitor->current_workspace->views_focus.next, view, workspace_focus_link);
		ptychite_view_focus(view, view->xdg_toplevel->base->surface);
	}
}

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

	ptychite_setup_message_proto(server);

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
	if (ptychite_window_init(&server->control->base, server, &ptychite_control_window_impl, server->layers.overlay, NULL)) {
		return -1;
	}
	ptychite_control_hide(server->control);

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
			ptychite_panel_draw_auto(monitor->panel);
		} else {
			monitor->window_geometry = monitor->geometry;
		}
		ptychite_monitor_tile(monitor);
	}

	if (server->control->base.element.scene_tree->node.enabled) {
		ptychite_control_draw_auto(server->control);
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
		ptychite_view_resize(view, view->element.width, view->element.height);
	}

	ptychite_server_check_cursor(server);
}

void ptychite_server_refresh_wallpapers(struct ptychite_server *server) {
	struct ptychite_monitor *monitor;
	wl_list_for_each(monitor, &server->monitors, link) {
		if (!monitor->wallpaper) {
			continue;
		}

		ptychite_wallpaper_draw_auto(monitor->wallpaper);
	}
}

void ptychite_server_retile(struct ptychite_server *server) {
	struct ptychite_monitor *monitor;
	wl_list_for_each(monitor, &server->monitors, link) {
		ptychite_monitor_tile(monitor);
	}
}

void ptychite_server_check_cursor(struct ptychite_server *server) {
	server_process_cursor_motion(server, 0);
}
