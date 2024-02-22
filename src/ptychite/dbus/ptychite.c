#define _POSIX_C_SOURCE 200809L
#include <asm-generic/errno-base.h>

#include "../dbus.h"
#include "../icon.h"
#include "../json.h"
#include "../notification.h"
#include "../server.h"

static const char *service_path = "/am/kolunmi/Ptychite";
static const char *service_interface = "am.kolunmi.Ptychite";

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

static struct json_object *application_describe(const struct ptychite_application *app) {
	struct json_object *description = json_object_new_object();
	if (!description) {
		return NULL;
	}

	struct json_object *member;
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "name", string, app->name)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "desktop_file", string, app->df)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "desktop_file_base", string, app->df_basename)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "wmclass", string, app->wmclass ? app->wmclass : "")
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "requested_icon", string, app->icon ? app->icon : "")
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(
			description, member, "resolved_icon", string, app->resolved_icon ? app->resolved_icon : "")

	return description;
}

static bool count_hash_map_len(const void *data, void *user_data) {
	int *i = user_data;
	(*i)++;

	return false;
}

struct add_application_iterator_data {
	struct json_object *array;
	int idx;
};

static bool add_application(const void *data, void *user_data) {
	const struct ptychite_application *app = data;
	struct add_application_iterator_data *iterator_data = user_data;

	struct json_object *description = application_describe(app);
	if (!app) {
		return true;
	}

	json_object_array_put_idx(iterator_data->array, iterator_data->idx, description);
	iterator_data->idx++;

	return false;
}

static int handle_dump_applications(sd_bus_message *msg, void *data, sd_bus_error *ret_error) {
	struct ptychite_server *server = data;

	int len = 0;
	ptychite_hash_map_iterate(&server->applications, &len, count_hash_map_len);

	struct json_object *array = json_object_new_array_ext(len);
	if (!array) {
		return -ENOMEM;
	}

	struct add_application_iterator_data iterator_data = {
			.array = array,
			.idx = 0,
	};
	if (!ptychite_hash_map_iterate(&server->applications, &iterator_data, add_application)) {
		json_object_put(array);
		return -ENOMEM;
	}

	char *error;
	const char *string = ptychite_json_object_convert_to_string(array, PTYCHITE_JSON_GET_PRETTY, &error);
	if (!string) {
		json_object_put(array);
		return -ENOMEM;
	}

	sd_bus_message *reply = NULL;
	int ret = sd_bus_message_new_method_return(msg, &reply);
	if (ret < 0) {
		json_object_put(array);
		return ret;
	}

	/* ret = sd_bus_message_open_container(reply, 'a', "s"); */
	/* if (ret < 0) { */
	/* 	json_object_put(array); */
	/* 	return ret; */
	/* } */

	ret = sd_bus_message_append(reply, "s", string);
	if (ret < 0) {
		json_object_put(array);
		return ret;
	}

	/* ret = sd_bus_message_close_container(reply); */
	/* if (ret < 0) { */
	/* 	json_object_put(array); */
	/* 	return ret; */
	/* } */

	ret = sd_bus_send(NULL, reply, NULL);
	if (ret < 0) {
		json_object_put(array);
		return ret;
	}

	json_object_put(array);
	sd_bus_message_unref(reply);
	return 0;
}

static const sd_bus_vtable service_vtable[] = {SD_BUS_VTABLE_START(0),
		SD_BUS_METHOD("DumpApplications", "", "s", handle_dump_applications, SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_VTABLE_END};

int init_dbus_ptychite(struct ptychite_server *server) {
	return sd_bus_add_object_vtable(
			server->bus, &server->ptychite_slot, service_path, service_interface, service_vtable, server);
}
