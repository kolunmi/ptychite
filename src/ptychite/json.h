#ifndef PTYCHITE_JSON_H
#define PTYCHITE_JSON_H

#include <json.h>

enum ptychite_json_get_mode {
	PTYCHITE_JSON_GET_COMPACT,
	PTYCHITE_JSON_GET_PRETTY,
};

const char *ptychite_json_object_convert_to_string(
		struct json_object *object, enum ptychite_json_get_mode mode, char **error);

#endif
