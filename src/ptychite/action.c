#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "action.h"
#include "applications.h"
#include "macros.h"
#include "monitor.h"
#include "server.h"
#include "view.h"
#include "windows.h"

static void server_action_terminate(struct ptychite_server *server, void *data) {
	wl_display_terminate(server->display);
}

static void server_action_close(struct ptychite_server *server, void *data) {
	struct ptychite_view *view = ptychite_server_get_focused_view(server);
	if (!view) {
		return;
	}

	wlr_xdg_toplevel_send_close(view->xdg_toplevel);
}

static void server_action_toggle_control(struct ptychite_server *server, void *data) {
	if (server->control->base.element.scene_tree->node.enabled) {
		ptychite_control_hide(server->control);
	} else {
		ptychite_control_show(server->control);
	}
}

static void server_action_spawn(struct ptychite_server *server, void *data) {
	char **args = data;

	ptychite_spawn(args);
}

static void server_action_shell(struct ptychite_server *server, void *data) {
	char *command = data;
	char *args[] = {"/bin/sh", "-c", command, NULL};

	ptychite_spawn(args);
}

static void server_action_inc_master(struct ptychite_server *server, void *data) {
	ptychite_server_tiling_change_views_in_master(server, 1);
}

static void server_action_dec_master(struct ptychite_server *server, void *data) {
	ptychite_server_tiling_change_views_in_master(server, -1);
}

static void server_action_inc_mfact(struct ptychite_server *server, void *data) {
	ptychite_server_tiling_change_master_factor(server, 0.05);
}

static void server_action_dec_mfact(struct ptychite_server *server, void *data) {
	ptychite_server_tiling_change_master_factor(server, -0.05);
}

static void server_action_toggle_rmaster(struct ptychite_server *server, void *data) {
	struct ptychite_monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct ptychite_workspace *workspace = monitor->current_workspace;
	workspace->tiling.traditional.right_master = !workspace->tiling.traditional.right_master;

	ptychite_monitor_tile(monitor);
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

	ptychite_monitor_switch_workspace(monitor, workspace);
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

	ptychite_monitor_switch_workspace(monitor, workspace);
}

static void server_action_focus_next_view(struct ptychite_server *server, void *data) {
	struct ptychite_view *old_view = ptychite_server_get_focused_view(server);
	if (!old_view) {
		ptychite_server_focus_any(server);
		return;
	}

	struct wl_list *list = old_view->workspace_order_link.next == &old_view->workspace->views_order
			? old_view->workspace_order_link.next->next
			: old_view->workspace_order_link.next;
	struct ptychite_view *new_view = wl_container_of(list, new_view, workspace_order_link);

	ptychite_view_focus(new_view, new_view->xdg_toplevel->base->surface);
}

static void server_action_focus_previous_view(struct ptychite_server *server, void *data) {
	struct ptychite_view *old_view = ptychite_server_get_focused_view(server);
	if (!old_view) {
		ptychite_server_focus_any(server);
		return;
	}

	struct wl_list *list = old_view->workspace_order_link.prev == &old_view->workspace->views_order
			? old_view->workspace_order_link.prev->prev
			: old_view->workspace_order_link.prev;
	struct ptychite_view *new_view = wl_container_of(list, new_view, workspace_order_link);

	ptychite_view_focus(new_view, new_view->xdg_toplevel->base->surface);
}

static void server_action_swap_front(struct ptychite_server *server, void *data) {
	struct ptychite_view *view = ptychite_server_get_focused_view(server);
	if (!view) {
		return;
	}

	struct ptychite_view *front = ptychite_server_get_front_view(server);
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
	ptychite_monitor_tile(new_front->monitor);
}

static void server_action_switch_app(struct ptychite_server *server, void *data) {
	struct ptychite_switcher *switcher = &server->switcher;

	if (switcher->base.element.scene_tree->node.enabled) {
		struct wl_list *list = switcher->cur->link.next == &switcher->sapps ? switcher->cur->link.next->next
																			: switcher->cur->link.next;
		switcher->cur = wl_container_of(list, switcher->cur, link);
	} else {
		switcher->cur = NULL;
	}

	ptychite_switcher_draw_auto(switcher, false);
}

static void server_action_switch_app_instance(struct ptychite_server *server, void *data) {
	struct ptychite_switcher *switcher = &server->switcher;

	if (switcher->base.element.scene_tree->node.enabled) {
		if (switcher->sub_switcher.element.scene_tree->node.enabled) {
			switcher->cur->idx++;
		}
	} else {
		switcher->cur = NULL;
	}

	ptychite_switcher_draw_auto(switcher, true);
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
		{"switch_app", server_action_switch_app, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"switch_app_instance", server_action_switch_app_instance, PTYCHITE_ACTION_FUNC_DATA_NONE},
};

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
