#include <assert.h>
#include <wayland-util.h>

#include <wlr/types/wlr_cursor.h>

#include "applications.h"
#include "compositor.h"
#include "config.h"
#include "icon.h"
#include "monitor.h"
#include "server.h"
#include "view.h"
#include "windows.h"

struct ptychite_view *ptychite_element_get_view(struct ptychite_element *element) {
	assert(element->type == PTYCHITE_ELEMENT_VIEW);

	struct ptychite_view *view = wl_container_of(element, view, element);

	return view;
}

void ptychite_view_resize(struct ptychite_view *view, int width, int height) {
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
		ptychite_window_relay_draw(&view->title_bar->base, view->element.width, top_thickness);
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

void ptychite_surface_unfocus(struct wlr_surface *surface) {
	struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(surface);
	assert(xdg_surface && xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	wlr_xdg_toplevel_set_activated(xdg_surface->toplevel, false);

	struct wlr_scene_tree *scene_tree = xdg_surface->data;
	struct ptychite_element *element = scene_tree->node.data;
	if (element) {
		struct ptychite_view *view = ptychite_element_get_view(element);
		struct ptychite_config *config = view->server->compositor->config;
		view->focused = false;
		wlr_scene_rect_set_color(view->border.top, config->views.border.colors.inactive);
		wlr_scene_rect_set_color(view->border.right, config->views.border.colors.inactive);
		wlr_scene_rect_set_color(view->border.bottom, config->views.border.colors.inactive);
		wlr_scene_rect_set_color(view->border.left, config->views.border.colors.inactive);
		if (view->title_bar && view->title_bar->base.element.scene_tree->node.enabled) {
			ptychite_window_relay_draw_same_size(&view->title_bar->base);
		}
	}
}

void ptychite_view_focus(struct ptychite_view *view, struct wlr_surface *surface) {
	struct ptychite_server *server = view->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct ptychite_config *config = server->compositor->config;

	if (prev_surface == surface) {
		return;
	}
	if (prev_surface) {
		ptychite_surface_unfocus(prev_surface);
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
		ptychite_window_relay_draw_same_size(&view->title_bar->base);
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(seat, view->xdg_toplevel->base->surface, keyboard->keycodes,
				keyboard->num_keycodes, &keyboard->modifiers);
	}

	if (view->monitor && view->workspace != view->monitor->current_workspace) {
		ptychite_monitor_switch_workspace(view->monitor, view->workspace);
	}

	server->active_monitor = view->monitor;

	struct ptychite_monitor *monitor;
	wl_list_for_each(monitor, &view->server->monitors, link) {
		if (!monitor->panel) {
			continue;
		}
		ptychite_panel_draw_auto(monitor->panel);
	}
}

void ptychite_view_begin_interactive(struct ptychite_view *view, enum ptychite_cursor_mode mode) {
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
		ptychite_window_relay_draw_same_size(&view->title_bar->base);
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
		if (view->workspace == end_workspace && ptychite_monitor_add_workspace(view->monitor) && view->monitor->panel &&
				view->monitor->panel->base.scene_buffer->node.enabled) {
			ptychite_window_relay_draw_same_size(&view->monitor->panel->base);
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
		ptychite_view_resize(view, view->initial_width, view->initial_height);
	} else {
		ptychite_monitor_tile(view->monitor);
	}

	wlr_scene_node_set_enabled(&view->element.scene_tree->node, true);
	ptychite_view_focus(view, view->xdg_toplevel->base->surface);
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
	if (view->in_switcher) {
		wl_list_remove(&view->switcher_link);
	}
	wl_list_remove(&view->set_title.link);

	wlr_scene_node_set_enabled(&view->element.scene_tree->node, false);

	if (view->monitor) {
		ptychite_monitor_tile(view->monitor);
		if (view->focused && !wl_list_empty(&view->monitor->current_workspace->views_order)) {
			struct ptychite_view *new_view =
					wl_container_of(view->monitor->current_workspace->views_focus.next, new_view, workspace_focus_link);
			ptychite_view_focus(new_view, new_view->xdg_toplevel->base->surface);
		} else {
			struct ptychite_monitor *monitor;
			wl_list_for_each(monitor, &view->server->monitors, link) {
				if (!monitor->panel) {
					continue;
				}
				ptychite_panel_draw_auto(monitor->panel);
			}
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

void ptychite_view_rig(struct ptychite_view *view, struct wlr_xdg_surface *xdg_surface) {
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

struct ptychite_icon *ptychite_view_get_icon(struct ptychite_view *view) {
	struct ptychite_application *application =
			ptychite_hash_map_get(&view->server->applications, view->xdg_toplevel->app_id);
	if (!application) {
		return NULL;
	}

	return ptychite_hash_map_get(&view->server->icons, application->resolved_icon);
}
