#define _POSIX_C_SOURCE 200809L
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>

#include <wlr/util/log.h>

#include "compositor.h"
#include "config.h"
#include "macros.h"
#include "server.h"
#include "util.h"

enum ptychite_action_func_data_mode {
	PTYCHITE_ACTION_FUNC_DATA_NONE,
	PTYCHITE_ACTION_FUNC_DATA_INT,
	PTYCHITE_ACTION_FUNC_DATA_STRING,
	PTYCHITE_ACTION_FUNC_DATA_ARGV,
};

typedef void (*ptychite_action_func_t)(struct ptychite_compositor *compositor, void *data);

struct ptychite_action {
	struct wl_list link;
	ptychite_action_func_t action_func;
	void *data;
};

static void compositor_action_terminate(struct ptychite_compositor *compositor, void *data) {
	ptychite_server_terminate(compositor->server);
}

static void compositor_action_close(struct ptychite_compositor *compositor, void *data) {
	ptychite_server_close_focused_client(compositor->server);
}

static void compositor_action_toggle_control(struct ptychite_compositor *compositor, void *data) {
	ptychite_server_toggle_control(compositor->server);
}

static void compositor_action_spawn(struct ptychite_compositor *compositor, void *data) {
	char **args = data;

	util_spawn(args);
}

static void compositor_action_shell(struct ptychite_compositor *compositor, void *data) {
	char *command = data;

	char *args[] = {"/bin/sh", "-c", command, NULL};

	util_spawn(args);
}

static void compositor_action_inc_master(struct ptychite_compositor *compositor, void *data) {
	ptychite_server_tiling_increase_views_in_master(compositor->server);
}

static void compositor_action_dec_master(struct ptychite_compositor *compositor, void *data) {
	ptychite_server_tiling_decrease_views_in_master(compositor->server);
}

static void compositor_action_inc_mfact(struct ptychite_compositor *compositor, void *data) {
	ptychite_server_tiling_increase_master_factor(compositor->server);
}

static void compositor_action_dec_mfact(struct ptychite_compositor *compositor, void *data) {
	ptychite_server_tiling_decrease_master_factor(compositor->server);
}

static void compositor_action_toggle_rmaster(struct ptychite_compositor *compositor, void *data) {
	ptychite_server_tiling_toggle_right_master(compositor->server);
}

static const struct {
	char *name;
	ptychite_action_func_t action_func;
	enum ptychite_action_func_data_mode data_mode;
} ptychite_action_name_table[] = {
		{"terminate", compositor_action_terminate, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"close", compositor_action_close, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"control", compositor_action_toggle_control, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"spawn", compositor_action_spawn, PTYCHITE_ACTION_FUNC_DATA_ARGV},
		{"shell", compositor_action_shell, PTYCHITE_ACTION_FUNC_DATA_STRING},
		{"inc_master", compositor_action_inc_master, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"dec_master", compositor_action_dec_master, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"inc_mfact", compositor_action_inc_mfact, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"dec_mfact", compositor_action_dec_mfact, PTYCHITE_ACTION_FUNC_DATA_NONE},
		{"toggle_rmaster", compositor_action_toggle_rmaster, PTYCHITE_ACTION_FUNC_DATA_NONE},
};

int ptychite_compositor_init(struct ptychite_compositor *compositor) {
	if (!(compositor->config = calloc(1, sizeof(struct ptychite_config)))) {
		return -1;
	}
	if (ptychite_config_init(compositor->config, compositor)) {
		goto err_config_init;
	}

	if (!(compositor->server = ptychite_server_create())) {
		goto err_rest;
	}

	return 0;

err_rest:
	ptychite_config_deinit(compositor->config);
err_config_init:
	free(compositor->config);
	return -1;
}

int ptychite_compositor_run(struct ptychite_compositor *compositor) {
	compositor->config->compositor = NULL;
	char *error;
	if (ptychite_config_parse_config(compositor->config, &error)) {
		wlr_log(WLR_ERROR, "Failed to parse config: %s", error);
	}

	wlr_log(WLR_INFO, "Starting server");
	compositor->config->compositor = compositor;
	return ptychite_server_init_and_run(compositor->server, compositor);
}

void ptychite_compositor_deinit(struct ptychite_compositor *compositor) {
	ptychite_config_deinit(compositor->config);
	free(compositor->config);
	free(compositor->server);
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

void ptychite_compositor_execute_action(
		struct ptychite_compositor *compositor, struct ptychite_action *action) {
	action->action_func(compositor, action->data);
}

bool ptychite_compositor_evaluate_key(struct ptychite_compositor *compositor, uint32_t sym,
		uint32_t modifiers, struct wl_array *history) {
	struct ptychite_chord_binding *chord_binding;
	wl_array_for_each(chord_binding, &compositor->config->keyboard.chords) {
		if (!chord_binding->active) {
			continue;
		}

		size_t length = chord_binding->chord.keys_l;
		size_t progress = history->size / sizeof(struct ptychite_key);
		if (length <= progress) {
			continue;
		}

		bool pass = false;
		size_t i;
		for (i = 0; i < progress; i++) {
			struct ptychite_key *key_binding = &chord_binding->chord.keys[i];
			struct ptychite_key *key_current = &((struct ptychite_key *)history->data)[i];
			if (key_binding->sym != key_current->sym ||
					key_binding->modifiers != key_current->modifiers) {
				pass = true;
				break;
			}
		}
		if (pass) {
			continue;
		}

		struct ptychite_key *key = &chord_binding->chord.keys[progress];
		if (sym != key->sym || modifiers != key->modifiers) {
			continue;
		}

		if (length == progress + 1) {
			ptychite_compositor_execute_action(compositor, chord_binding->action);
			history->size = 0;
		} else {
			struct ptychite_key *append = wl_array_add(history, sizeof(struct ptychite_key));
			if (!append) {
				history->size = 0;
				return true;
			}
			*append = (struct ptychite_key){.sym = sym, .modifiers = modifiers};
		}

		return true;
	}

	if (history->size) {
		history->size = 0;
		return true;
	}

	return false;
}
