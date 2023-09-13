#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-util.h>

#include "ptychite-message-unstable-v1-client-protocol.h"

struct ptymsg_state {
	struct wl_display *display;
	struct zptychite_message_v1 *ptychite_message;
	struct wl_list monitors;
	struct wl_list callback_datas;
	int exit_code;
};

struct monitor {
	struct wl_list link;
	uint32_t registry_name;
	struct wl_output *output;
	char *name;
};

struct callback_data {
	struct wl_list link;
	struct ptymsg_state *state;
	const char *error_prefix;
};

static void output_handle_description(
		void *data, struct wl_output *wl_output, const char *description) {
}

static void output_handle_done(void *data, struct wl_output *wl_output) {
}

static void output_handle_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y,
		int32_t physical_width, int32_t physical_height, int32_t subpixel, const char *make,
		const char *model, int32_t transform) {
}

static void output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags,
		int32_t width, int32_t height, int32_t refresh) {
}

static void output_handle_name(void *data, struct wl_output *wl_output, const char *name) {
	struct monitor *monitor = data;

	monitor->name = strdup(name);
}

static void output_handle_scale(void *data, struct wl_output *wl_output, int32_t factor) {
}

static const struct wl_output_listener output_listener = {
		.description = output_handle_description,
		.done = output_handle_done,
		.geometry = output_handle_geometry,
		.mode = output_handle_mode,
		.name = output_handle_name,
		.scale = output_handle_scale,
};

static void ptychite_message_callback_handle_success(
		void *data, struct zptychite_message_callback_v1 *callback, const char *message) {
	struct callback_data *callback_data = data;

	if (*message) {
		fprintf(stdout, "%s\n", message);
	}

	wl_list_remove(&callback_data->link);
	free(callback_data);
}

static void ptychite_message_callback_handle_failure(
		void *data, struct zptychite_message_callback_v1 *callback, const char *error) {
	struct callback_data *callback_data = data;

	if (callback_data->error_prefix) {
		fprintf(stderr, "%s: %s\n", callback_data->error_prefix, error);
	} else {
		fprintf(stderr, "%s", error);
	}

	callback_data->state->exit_code = 1;
	wl_list_remove(&callback_data->link);
	free(callback_data);
}

static const struct zptychite_message_callback_v1_listener ptychite_message_callback_listener = {
		.success = ptychite_message_callback_handle_success,
		.failure = ptychite_message_callback_handle_failure,
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version) {
	struct ptymsg_state *state = data;

	if (!strcmp(interface, zptychite_message_v1_interface.name)) {
		state->ptychite_message =
				wl_registry_bind(registry, name, &zptychite_message_v1_interface, 1);
	} else if (!strcmp(interface, wl_output_interface.name)) {
		struct monitor *monitor = calloc(1, sizeof(struct monitor));
		if (!monitor) {
			return;
		}
		monitor->registry_name = name;
		monitor->output = wl_registry_bind(registry, name, &wl_output_interface, 4);
		wl_output_add_listener(monitor->output, &output_listener, monitor);
		wl_list_insert(&state->monitors, &monitor->link);
	}
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = {
		.global = registry_handle_global,
		.global_remove = registry_handle_global_remove,
};

int main(int argc, char **argv) {
	struct ptymsg_state state = {0};
	wl_list_init(&state.monitors);
	wl_list_init(&state.callback_datas);
	state.exit_code = 0;

	if (!(state.display = wl_display_connect(NULL))) {
		fprintf(stderr, "could not connect to display\n");
		return 1;
	}

	struct wl_registry *registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);
	if (!state.ptychite_message) {
		fprintf(stderr, "compositor did not advertise all needed protocols\n");
		return 1;
	}

	wl_display_roundtrip(state.display);

	int i;
	for (i = 1; i < argc; i++) {
		struct zptychite_message_callback_v1 *callback = NULL;
		struct callback_data *callback_data = calloc(1, sizeof(struct callback_data));
		if (!callback_data) {
			fprintf(stderr, "a memory error occurred\n");
			state.exit_code = 1;
			goto done;
		}
		wl_list_insert(&state.callback_datas, &callback_data->link);
		callback_data->state = &state;

		if (!strcmp(argv[i], "set")) {
			enum zptychite_message_v1_property_set_mode mode =
					ZPTYCHITE_MESSAGE_V1_PROPERTY_SET_MODE_APPEND;
			if (i + 1 < argc && !strcmp(argv[i + 1], "--overwrite")) {
				mode = ZPTYCHITE_MESSAGE_V1_PROPERTY_SET_MODE_OVERWRITE;
				i++;
			}
			if (i + 2 >= argc) {
				fprintf(stderr, "command set requires two arguments\n");
				state.exit_code = 1;
				goto done;
			}
			char *path = argv[++i];
			char *string = argv[++i];
			callback =
					zptychite_message_v1_set_property(state.ptychite_message, path, string, mode);
			callback_data->error_prefix = "failed to set property";

		} else if (!strcmp(argv[i], "get")) {
			enum zptychite_message_v1_json_get_mode mode =
					ZPTYCHITE_MESSAGE_V1_JSON_GET_MODE_PRETTY;
			if (i + 1 < argc && !strcmp(argv[i + 1], "--compact")) {
				mode = ZPTYCHITE_MESSAGE_V1_JSON_GET_MODE_COMPACT;
				i++;
			}
			if (i + 1 >= argc) {
				fprintf(stderr, "command get requires an argument\n");
				state.exit_code = 1;
				goto done;
			}
			char *path = argv[++i];
			callback = zptychite_message_v1_get_property(state.ptychite_message, path, mode);
			callback_data->error_prefix = "failed to get property";

		} else if (!strcmp(argv[i], "dump-views")) {
			enum zptychite_message_v1_json_get_mode mode =
					ZPTYCHITE_MESSAGE_V1_JSON_GET_MODE_PRETTY;
			if (i + 1 < argc && !strcmp(argv[i + 1], "--compact")) {
				mode = ZPTYCHITE_MESSAGE_V1_JSON_GET_MODE_COMPACT;
				i++;
			}
			if (i + 1 >= argc) {
				fprintf(stderr, "command dump-views requires an argument\n");
				state.exit_code = 1;
				goto done;
			}
			char *output_name = argv[++i];
			struct wl_output *output = NULL;
			if (strcmp(output_name, "all")) {
				struct monitor *monitor;
				wl_list_for_each(monitor, &state.monitors, link) {
					if (!strcmp(monitor->name, output_name)) {
						output = monitor->output;
						break;
					}
				}
				if (!output) {
					fprintf(stderr,
							"command dump-views did not recieve an available output name\n");
					state.exit_code = 1;
					goto done;
				}
			}
			callback = zptychite_message_v1_dump_views(state.ptychite_message, output, mode);
			callback_data->error_prefix = "failed to dump view info";
		}

		if (callback) {
			zptychite_message_callback_v1_add_listener(
					callback, &ptychite_message_callback_listener, callback_data);
		} else {
			fprintf(stderr, "unrecognized command '%s'\n", argv[i]);
			state.exit_code = 1;
			goto done;
		}
	}

	while (!wl_list_empty(&state.callback_datas)) {
		if (wl_display_dispatch(state.display)) {
			break;
		}
	}

	struct callback_data *callback_data, *callback_data_tmp;
	struct monitor *monitor, *monitor_tmp;
done:
	wl_list_for_each_safe(callback_data, callback_data_tmp, &state.callback_datas, link) {
		free(callback_data);
	}
	wl_list_for_each_safe(monitor, monitor_tmp, &state.monitors, link) {
		free(monitor->name);
		free(monitor);
	}
	zptychite_message_v1_destroy(state.ptychite_message);
	wl_registry_destroy(registry);
	wl_display_disconnect(state.display);

	return state.exit_code;
}
