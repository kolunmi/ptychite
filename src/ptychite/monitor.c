#include "compositor.h"
#include "config.h"
#include "monitor.h"
#include "server.h"
#include "view.h"
#include "windows.h"

struct ptychite_workspace *ptychite_monitor_add_workspace(struct ptychite_monitor *monitor) {
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

void ptychite_monitor_tile(struct ptychite_monitor *monitor) {
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
				ptychite_view_resize(view, master_width - gaps, height);
				master_y += view->element.height + gaps;
			} else {
				int r = views_len - i;
				int height = (monitor->window_geometry.height - stack_y - gaps * r) / r;
				wlr_scene_node_set_position(
						&view->element.scene_tree->node, stack_x, monitor->window_geometry.y + stack_y);
				ptychite_view_resize(view, monitor->window_geometry.width - master_width - 2 * gaps, height);
				stack_y += view->element.height + gaps;
			}
			i++;
		}

		break;
	}
	}

	ptychite_server_check_cursor(monitor->server);
}

void ptychite_monitor_fix_workspaces(struct ptychite_monitor *monitor) {
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

void ptychite_monitor_switch_workspace(struct ptychite_monitor *monitor, struct ptychite_workspace *workspace) {
	struct ptychite_workspace *last_workspace = monitor->current_workspace;
	monitor->current_workspace = workspace;

	struct ptychite_view *view;
	wl_list_for_each(view, &last_workspace->views_order, workspace_order_link) {
		wlr_scene_node_set_enabled(&view->element.scene_tree->node, false);
	}
	wl_list_for_each(view, &monitor->current_workspace->views_order, workspace_order_link) {
		wlr_scene_node_set_enabled(&view->element.scene_tree->node, true);
	}

	ptychite_monitor_fix_workspaces(monitor);
	if (monitor->panel && monitor->panel->base.element.scene_tree->node.enabled) {
		ptychite_window_relay_draw_same_size(&monitor->panel->base);
	}

	if (wl_list_empty(&monitor->current_workspace->views_focus)) {
		struct wlr_surface *focused_surface = monitor->server->seat->keyboard_state.focused_surface;
		if (focused_surface) {
			ptychite_surface_unfocus(focused_surface);
			wlr_seat_keyboard_notify_clear_focus(monitor->server->seat);
		}
	} else {
		struct ptychite_view *new_view =
				wl_container_of(monitor->current_workspace->views_focus.next, new_view, workspace_focus_link);
		ptychite_view_focus(new_view, new_view->xdg_toplevel->base->surface);
	}

	ptychite_server_check_cursor(monitor->server);
}

void ptychite_monitor_disable(struct ptychite_monitor *monitor) {
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

		ptychite_monitor_tile(server->active_monitor);
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

	ptychite_monitor_disable(monitor);
	struct ptychite_workspace *workspace;
	wl_list_for_each(workspace, &monitor->workspaces, link) {
		free(workspace);
	}

	free(monitor);
}

void ptychite_monitor_rig(struct ptychite_monitor *monitor) {
	monitor->frame.notify = monitor_handle_frame;
	wl_signal_add(&monitor->output->events.frame, &monitor->frame);
	monitor->request_state.notify = monitor_handle_request_state;
	wl_signal_add(&monitor->output->events.request_state, &monitor->request_state);
	monitor->destroy.notify = monitor_handle_destroy;
	wl_signal_add(&monitor->output->events.destroy, &monitor->destroy);
}
