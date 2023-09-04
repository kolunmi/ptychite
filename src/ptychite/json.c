#define _POSIX_C_SOURCE 200809L

#include "json.h"

const char *ptychite_json_object_convert_to_string(
		struct json_object *object, enum ptychite_json_get_mode mode, char **error) {
	int flags;
	switch (mode) {
	case PTYCHITE_JSON_GET_COMPACT:
		flags = JSON_C_TO_STRING_PLAIN;
		break;
	case PTYCHITE_JSON_GET_PRETTY:
		flags = JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_PRETTY_TAB;
		break;
	default:
		*error = "internal error: invalid getting mode";
		return NULL;
	}

	const char *string = json_object_to_json_string_ext(object, flags);
	if (!string) {
		*error = "json string could not be retrieved";
	}
	return string;
}
