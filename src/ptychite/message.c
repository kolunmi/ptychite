#include "compositor.h"
#include "config.h"
#include "json.h"
#include "monitor.h"
#include "ptychite-message-unstable-v1-protocol.h"
#include "server.h"
#include "view.h"

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

static struct json_object *view_describe(struct ptychite_view *view) {
	struct json_object *description = json_object_new_object();
	if (!description) {
		return NULL;
	}

	struct json_object *member;
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "appid", string, view->xdg_toplevel->app_id)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "title", string, view->xdg_toplevel->title)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "x", int, view->element.scene_tree->node.x)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "y", int, view->element.scene_tree->node.y)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "width", int, view->element.width)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "height", int, view->element.height)

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

void ptychite_setup_message_proto(struct ptychite_server *server) {
	wl_global_create(server->display, &zptychite_message_v1_interface, 1, server, message_handle_bind);
}
