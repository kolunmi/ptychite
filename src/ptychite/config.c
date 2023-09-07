#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <pixman.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

#include "compositor.h"
#include "config.h"
#include "json.h"
#include "macros.h"
#include "server.h"

#define JSON_ARRAY_FOREACH(arr, i, iter) \
	for (iter = json_object_array_get_idx(arr, (i = 0)); i < json_object_array_length(arr); \
			iter = json_object_array_get_idx(arr, ++i))

typedef int (*ptychite_property_set_func_t)(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error);

typedef struct json_object *(*ptychite_property_get_func_t)(struct ptychite_config *config);

struct property_entry {
	char **path;
	ptychite_property_set_func_t set_func;
	ptychite_property_get_func_t get_func;
};

struct property_entry_reference {
	struct property_entry *entry;
	size_t path_l;
};

struct property_path_node {
	size_t parent_id;
	const char *name;
	struct json_object *object;
	size_t depth;
	bool visited;
};

static int uint_parse_from_hexcolor_string(uint32_t *dest, const char *string, int *len_out) {
	if (*string == '#') {
		string++;
	}
	int len = strlen(string);

	if ((len != 6 && len != 8) || !isxdigit(string[0]) || !isxdigit(string[1])) {
		return -1;
	}

	char *ptr;
	uint32_t parsed = strtoul(string, &ptr, 16);
	if (*ptr) {
		return -1;
	}

	*dest = parsed;
	if (len_out) {
		*len_out = len;
	}

	return 0;
}

static int color_parse_from_string(pixman_color_t *color, const char *string) {
	uint32_t parsed;
	int len;
	if (uint_parse_from_hexcolor_string(&parsed, string, &len)) {
		return -1;
	}

	if (len == 8) {
		color->alpha = (parsed & 0xff) * 0x101;
		parsed >>= 8;
	} else {
		color->alpha = 0xffff;
	}
	color->red = ((parsed >> 16) & 0xff) * 0x101;
	color->green = ((parsed >> 8) & 0xff) * 0x101;
	color->blue = ((parsed >> 0) & 0xff) * 0x101;

	return 0;
}

static int arrcolor_parse_from_string(float color[4], const char *string) {
	uint32_t parsed;
	int len;
	if (uint_parse_from_hexcolor_string(&parsed, string, &len)) {
		return -1;
	}

	if (len == 8) {
		color[3] = (parsed & 0xff) / 255.0f;
		parsed >>= 8;
	} else {
		color[3] = 1.0f;
	}
	color[0] = ((parsed >> 16) & 0xff) / 255.0f;
	color[1] = ((parsed >> 8) & 0xff) / 255.0f;
	color[2] = ((parsed >> 0) & 0xff) / 255.0f;

	return 0;
}

static json_object *color_convert_to_json(pixman_color_t *color) {
	char buf[10];
	snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", (uint8_t)color->red, (uint8_t)color->green,
			(uint8_t)color->blue, (uint8_t)color->alpha);

	return json_object_new_string(buf);
}

static json_object *arrcolor_convert_to_json(float color[4]) {
	char buf[10];
	snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", (uint8_t)(color[0] * 255.0f),
			(uint8_t)(color[1] * 255.0f), (uint8_t)(color[2] * 255.0f),
			(uint8_t)(color[3] * 255.0f));

	return json_object_new_string(buf);
}

static struct json_object *json_object_get_and_ensure_type(
		struct json_object *object, const char *key, enum json_type type) {
	struct json_object *member;
	if (!json_object_object_get_ex(object, key, &member)) {
		return NULL;
	}

	if (!json_object_is_type(member, type)) {
		return NULL;
	}

	return member;
}

static int font_fill_from_string(struct ptychite_font *p_font, const char *string, char **error) {
	PangoFontDescription *font = pango_font_description_from_string(string);
	if (!font) {
		*error = "could not parse font string";
		return -1;
	}

	if (!pango_font_description_get_family(font)) {
		*error = "could not detect font family";
		goto err_parse_font;
	}
	if (!pango_font_description_get_size(font)) {
		*error = "could not detect font size";
		goto err_parse_font;
	}

	char *font_string = strdup(string);
	if (!font_string) {
		*error = "memory error";
		goto err_parse_font;
	}

	cairo_t *cairo = cairo_create(NULL);
	if (!cairo) {
		*error = "memory error";
		goto err_create_cairo;
	}
	PangoContext *pango = pango_cairo_create_context(cairo);
	if (!pango) {
		*error = "memory error";
		goto err_create_pango;
	}
	PangoFontMetrics *metrics = pango_context_get_metrics(pango, font, NULL);
	if (!metrics) {
		*error = "could not obtain font metrics";
		goto err_rest;
	}

	if (p_font->font) {
		pango_font_description_free(p_font->font);
	}
	p_font->font = font;
	if (p_font->string) {
		free(p_font->string);
	}
	p_font->string = font_string;
	p_font->baseline = pango_font_metrics_get_ascent(metrics) / PANGO_SCALE;
	p_font->height = p_font->baseline + pango_font_metrics_get_descent(metrics) / PANGO_SCALE;
	p_font->markup = false;

	pango_font_metrics_unref(metrics);
	g_object_unref(pango);
	cairo_destroy(cairo);
	return 0;

err_rest:
	g_object_unref(pango);
err_create_pango:
	cairo_destroy(cairo);
err_create_cairo:
	free(font_string);
err_parse_font:
	pango_font_description_free(font);
	return -1;
}

static int config_set_keyboard_repeat_rate(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_int)) {
		*error = "keyboard repeat rate must be an integer";
		return -1;
	}
	int rate = json_object_get_int(value);

	config->keyboard.repeat.rate = rate;

	if (config->compositor) {
		ptychite_server_configure_keyboards(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_keyboard_repeat_rate(struct ptychite_config *config) {
	return json_object_new_int(config->keyboard.repeat.rate);
}

static int config_set_keyboard_repeat_delay(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_int)) {
		*error = "keyboard repeat delay must be an integer";
		return -1;
	}
	int delay = json_object_get_int(value);

	config->keyboard.repeat.delay = delay;

	if (config->compositor) {
		ptychite_server_configure_keyboards(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_keyboard_repeat_delay(struct ptychite_config *config) {
	return json_object_new_int(config->keyboard.repeat.delay);
}

static int config_set_keyboard_xkb_options(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "keyboard xkb options must be a string";
		return -1;
	}
	const char *options = json_object_get_string(value);

	char *options_dup = strdup(options);
	if (!options_dup) {
		*error = "memory error";
		return -1;
	}

	free(config->keyboard.xkb.options);
	config->keyboard.xkb.options = options_dup;

	if (config->compositor) {
		ptychite_server_configure_keyboards(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_keyboard_xkb_options(struct ptychite_config *config) {
	return json_object_new_string(config->keyboard.xkb.options ? config->keyboard.xkb.options : "");
}

static int config_set_keyboard_chords(struct ptychite_config *config, struct json_object *value,
		enum ptychite_property_set_mode mode, char **error) {
	if (mode == PTYCHITE_PROPERTY_SET_OVERWRITE) {
		ptychite_config_wipe_chord_bindings(config);
	}

	if (!json_object_is_type(value, json_type_array)) {
		*error = "keyboard chords must be an array";
		return -1;
	}

	size_t i;
	struct json_object *binding;
	JSON_ARRAY_FOREACH(value, i, binding) {
		if (!json_object_is_type(binding, json_type_object)) {
			*error = "each keyboard chord binding must be an object";
			return -1;
		}
		if (json_object_object_length(binding) != 2) {
			*error = "each keyboard chord binding object must have two members";
			return -1;
		}

		struct json_object *pattern =
				json_object_get_and_ensure_type(binding, "pattern", json_type_string);
		if (!pattern) {
			*error = "each keyboard chord binding object must have a member \"pattern\" of type "
					 "string";
			return -1;
		}
		const char *pattern_string = json_object_get_string(pattern);

		struct json_object *action =
				json_object_get_and_ensure_type(binding, "action", json_type_array);
		if (!action) {
			*error = "each keyboard chord binding object must have a member \"action\" of type "
					 "array";
			return -1;
		}
		size_t args_l = json_object_array_length(action);
		if (!args_l) {
			*error = "each keyboard chord binding object must have at least one action argument";
			return -1;
		}
		const char **args = malloc(sizeof(char *) * args_l);
		if (!args) {
			*error = "memory error";
			return -1;
		}
		size_t j;
		struct json_object *arg;
		JSON_ARRAY_FOREACH(action, j, arg) {
			if (!json_object_is_type(arg, json_type_string)) {
				*error = "each keyboard chord binding object requires all action arguments to be "
						 "of type string";
				free(args);
				return -1;
			}
			args[j] = json_object_get_string(arg);
		}

		struct ptychite_chord_binding *chord_binding = ptychite_config_scan_into_chord_binding(
				config, pattern_string, args, args_l, error);
		free(args);
		if (!chord_binding) {
			return -1;
		}
	}

	return 0;
}

static struct json_object *config_get_keyboard_chords(struct ptychite_config *config) {
	size_t len = 0;
	struct ptychite_chord_binding *chord_binding;
	wl_array_for_each(chord_binding, &config->keyboard.chords) {
		if (chord_binding->active) {
			len++;
		}
	}

	struct json_object *array = json_object_new_array_ext(len);
	if (!array) {
		return NULL;
	}
	if (!len) {
		return array;
	}

	size_t idx = 0;
	wl_array_for_each(chord_binding, &config->keyboard.chords) {
		if (!chord_binding->active) {
			continue;
		}

		struct json_object *binding = json_object_new_object();
		if (!binding) {
			json_object_put(array);
			return NULL;
		}
		json_object_array_put_idx(array, idx, binding);

		char *pattern_string = ptychite_chord_get_pattern(&chord_binding->chord);
		if (!pattern_string) {
			json_object_put(array);
			return NULL;
		}
		struct json_object *pattern = json_object_new_string(pattern_string);
		free(pattern_string);
		if (!pattern) {
			json_object_put(array);
			return NULL;
		}
		json_object_object_add(binding, "pattern", pattern);

		char **args;
		int args_l;
		if (ptychite_action_get_args(chord_binding->action, &args, &args_l)) {
			json_object_put(array);
			return NULL;
		}
		struct json_object *action = json_object_new_array_ext(args_l);
		if (!action) {
			int i;
			for (i = 0; i < args_l; i++) {
				free(args[i]);
			}
			free(args);
			json_object_put(array);
			return NULL;
		}
		json_object_object_add(binding, "action", action);
		int i;
		for (i = 0; i < args_l; i++) {
			struct json_object *argument = json_object_new_string(args[i]);
			free(args[i]);
			if (!argument) {
				int j;
				for (j = i + 1; j < args_l; j++) {
					free(args[j]);
				}
				free(args);
				json_object_put(array);
				return NULL;
			}
			json_object_array_put_idx(action, i, argument);
		}
		free(args);

		idx++;
	}

	return array;
}

static int config_set_panel_enabled(struct ptychite_config *config, struct json_object *value,
		enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_boolean)) {
		*error = "panel switch must be a boolean";
		return -1;
	}
	bool enabled = json_object_get_boolean(value);

	config->panel.enabled = enabled;

	if (config->compositor) {
		ptychite_server_configure_panels(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_panel_enabled(struct ptychite_config *config) {
	return json_object_new_boolean(config->panel.enabled);
}

static int config_set_panel_font(struct ptychite_config *config, struct json_object *value,
		enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "panel font must be a string";
		return -1;
	}
	const char *font_string = json_object_get_string(value);

	if (font_fill_from_string(&config->panel.font, font_string, error)) {
		return -1;
	}

	if (config->compositor) {
		ptychite_server_configure_panels(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_panel_font(struct ptychite_config *config) {
	return json_object_new_string(config->panel.font.string);
}

static int config_set_panel_colors_foreground(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "panel color must be a string";
		return -1;
	}
	const char *color = json_object_get_string(value);

	if (arrcolor_parse_from_string(config->panel.colors.foreground, color)) {
		*error = "panel color is malformed";
		return -1;
	}

	if (config->compositor) {
		ptychite_server_configure_panels(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_panel_colors_foreground(struct ptychite_config *config) {
	return arrcolor_convert_to_json(config->panel.colors.foreground);
}

static int config_set_panel_colors_background(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "panel color must be a string";
		return -1;
	}
	const char *color = json_object_get_string(value);

	if (arrcolor_parse_from_string(config->panel.colors.background, color)) {
		*error = "panel color is malformed";
		return -1;
	}

	if (config->compositor) {
		ptychite_server_configure_panels(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_panel_colors_background(struct ptychite_config *config) {
	return arrcolor_convert_to_json(config->panel.colors.background);
}

static int config_set_panel_colors_accent(struct ptychite_config *config, struct json_object *value,
		enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "panel color must be a string";
		return -1;
	}
	const char *color = json_object_get_string(value);

	if (arrcolor_parse_from_string(config->panel.colors.accent, color)) {
		*error = "panel color is malformed";
		return -1;
	}

	if (config->compositor) {
		ptychite_server_configure_panels(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_panel_colors_accent(struct ptychite_config *config) {
	return arrcolor_convert_to_json(config->panel.colors.accent);
}

static int config_set_panel_colors_gray1(struct ptychite_config *config, struct json_object *value,
		enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "panel color must be a string";
		return -1;
	}
	const char *color = json_object_get_string(value);

	if (arrcolor_parse_from_string(config->panel.colors.gray1, color)) {
		*error = "panel color is malformed";
		return -1;
	}

	if (config->compositor) {
		ptychite_server_configure_panels(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_panel_colors_gray1(struct ptychite_config *config) {
	return arrcolor_convert_to_json(config->panel.colors.gray1);
}

static int config_set_panel_colors_gray2(struct ptychite_config *config, struct json_object *value,
		enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "panel color must be a string";
		return -1;
	}
	const char *color = json_object_get_string(value);

	if (arrcolor_parse_from_string(config->panel.colors.gray2, color)) {
		*error = "panel color is malformed";
		return -1;
	}

	if (config->compositor) {
		ptychite_server_configure_panels(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_panel_colors_gray2(struct ptychite_config *config) {
	return arrcolor_convert_to_json(config->panel.colors.gray2);
}

static int config_set_panel_colors_border(struct ptychite_config *config, struct json_object *value,
		enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "panel color must be a string";
		return -1;
	}
	const char *color = json_object_get_string(value);

	if (arrcolor_parse_from_string(config->panel.colors.border, color)) {
		*error = "panel color is malformed";
		return -1;
	}

	if (config->compositor) {
		ptychite_server_configure_panels(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_panel_colors_border(struct ptychite_config *config) {
	return arrcolor_convert_to_json(config->panel.colors.border);
}

static int config_set_panel_colors_seperator(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "panel color must be a string";
		return -1;
	}
	const char *color = json_object_get_string(value);

	if (arrcolor_parse_from_string(config->panel.colors.seperator, color)) {
		*error = "panel color is malformed";
		return -1;
	}

	if (config->compositor) {
		ptychite_server_configure_panels(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_panel_colors_seperator(struct ptychite_config *config) {
	return arrcolor_convert_to_json(config->panel.colors.seperator);
}

static int config_set_panel_colors_chord(struct ptychite_config *config, struct json_object *value,
		enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "panel color must be a string";
		return -1;
	}
	const char *color = json_object_get_string(value);

	if (arrcolor_parse_from_string(config->panel.colors.chord, color)) {
		*error = "panel color is malformed";
		return -1;
	}

	if (config->compositor) {
		ptychite_server_configure_panels(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_panel_colors_chord(struct ptychite_config *config) {
	return arrcolor_convert_to_json(config->panel.colors.chord);
}

static int config_set_views_map_to_front(struct ptychite_config *config, struct json_object *value,
		enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_boolean)) {
		*error = "property map_to_front must be a boolean";
		return -1;
	}
	bool map_to_front = json_object_get_boolean(value);

	config->views.map_to_front = map_to_front;

	return 0;
}

static struct json_object *config_get_views_map_to_front(struct ptychite_config *config) {
	return json_object_new_boolean(config->views.map_to_front);
}

static int config_set_views_title_bar_enabled(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_boolean)) {
		*error = "title bar switch must be a boolean";
		return -1;
	}
	bool enabled = json_object_get_boolean(value);

	config->views.title_bar.enabled = enabled;

	if (config->compositor) {
		ptychite_server_configure_views(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_views_title_bar_enabled(struct ptychite_config *config) {
	return json_object_new_boolean(config->views.title_bar.enabled);
}

static int config_set_views_border_thickness(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_int)) {
		*error = "view border thickness must be an integer";
		return -1;
	}
	int thickness = json_object_get_int(value);

	if (thickness < 0) {
		*error = "view border thickness must not be negative";
		return -1;
	} else if (thickness > 50) {
		*error = "view border thickness must be less than or equal to 50";
		return -1;
	}

	config->views.border.thickness = thickness;

	if (config->compositor) {
		ptychite_server_configure_views(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_views_border_thickness(struct ptychite_config *config) {
	return json_object_new_int(config->views.border.thickness);
}

static int config_set_views_border_colors_active(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "view border active color must be a string";
		return -1;
	}
	const char *color = json_object_get_string(value);

	if (arrcolor_parse_from_string(config->views.border.colors.active, color)) {
		*error = "view border active color is malformed";
		return -1;
	}

	return 0;
}

static struct json_object *config_get_views_border_colors_active(struct ptychite_config *config) {
	return arrcolor_convert_to_json(config->views.border.colors.active);
}

static int config_set_views_border_colors_inactive(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "view border inactive color must be a string";
		return -1;
	}
	const char *color = json_object_get_string(value);

	if (arrcolor_parse_from_string(config->views.border.colors.inactive, color)) {
		*error = "view border inactive color is malformed";
		return -1;
	}

	return 0;
}

static struct json_object *config_get_views_border_colors_inactive(struct ptychite_config *config) {
	return arrcolor_convert_to_json(config->views.border.colors.inactive);
}

static int config_set_monitors_default_scale(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_double)) {
		*error = "monitor scale must be a double";
		return -1;
	}
	double default_scale = json_object_get_double(value);

	if (default_scale <= 0) {
		*error = "monitor default scale must be greater than zero";
		return -1;
	}

	config->monitors.default_scale = default_scale;

	return 0;
}

static struct json_object *config_get_monitors_default_scale(struct ptychite_config *config) {
	return json_object_new_double(config->monitors.default_scale);
}

static int config_set_monitors_wallpaper_filepath(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "wallpaper path must be a string";
		return -1;
	}
	const char *path = json_object_get_string(value);

	char *path_dup;
	cairo_surface_t *surface;
	if (*path) {
		if (!(path_dup = strdup(path))) {
			*error = "memory error";
			return -1;
		}

		if (!(surface = cairo_image_surface_create_from_png(path))) {
			free(path_dup);
			*error = "memory error";
			return -1;
		}
		cairo_status_t status = cairo_surface_status(surface);
		if (status != CAIRO_STATUS_SUCCESS) {
			free(path_dup);
			cairo_surface_destroy(surface);
			switch (status) {
			case CAIRO_STATUS_FILE_NOT_FOUND:
				*error = "png file was not found";
				break;
			case CAIRO_STATUS_READ_ERROR:
				*error = "could not read png file";
				break;
			case CAIRO_STATUS_PNG_ERROR:
				*error = "could not load png data";
				break;
			default:
				*error = "memory error";
				break;
			}
			return -1;
		}
	} else {
		path_dup = NULL;
		surface = NULL;
	}

	free(config->monitors.wallpaper.path);
	config->monitors.wallpaper.path = path_dup;

	if (config->monitors.wallpaper.surface) {
		cairo_surface_destroy(config->monitors.wallpaper.surface);
	}
	config->monitors.wallpaper.surface = surface;

	if (config->compositor) {
		ptychite_server_refresh_wallpapers(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_monitors_wallpaper_filepath(struct ptychite_config *config) {
	return json_object_new_string(
			config->monitors.wallpaper.path ? config->monitors.wallpaper.path : "");
}

static int config_set_monitors_wallpaper_mode(struct ptychite_config *config,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "wallpaper mode must be a string";
		return -1;
	}
	const char *string = json_object_get_string(value);

	enum ptychite_wallpaper_mode wallpaper_mode;
	if (!strcmp(string, "fit")) {
		wallpaper_mode = PTYCHITE_WALLPAPER_FIT;
	} else if (!strcmp(string, "stretch")) {
		wallpaper_mode = PTYCHITE_WALLPAPER_STRETCH;
	} else {
		*error = "invalid wallpaper mode, available values are 'fit' and 'stretch'";
		return -1;
	}

	config->monitors.wallpaper.mode = wallpaper_mode;

	if (config->compositor) {
		ptychite_server_refresh_wallpapers(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_monitors_wallpaper_mode(struct ptychite_config *config) {
	char *string;
	switch (config->monitors.wallpaper.mode) {
	case PTYCHITE_WALLPAPER_FIT:
		string = "fit";
		break;
	case PTYCHITE_WALLPAPER_STRETCH:
		string = "stretch";
		break;
	default:
		return NULL;
	}

	return json_object_new_string(string);
}

static int config_set_tiling_mode(struct ptychite_config *config, struct json_object *value,
		enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_string)) {
		*error = "tiling mode must be a string";
		return -1;
	}
	const char *string = json_object_get_string(value);

	enum ptychite_tiling_mode tiling_mode;
	if (!strcmp(string, "none")) {
		tiling_mode = PTYCHITE_TILING_NONE;
	} else if (!strcmp(string, "traditional")) {
		tiling_mode = PTYCHITE_TILING_TRADITIONAL;
	} else {
		*error = "invalid tiling mode, available values are 'none' and 'traditional'";
		return -1;
	}

	config->tiling.mode = tiling_mode;

	if (config->compositor) {
		ptychite_server_retile(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_tiling_mode(struct ptychite_config *config) {
	char *string;
	switch (config->tiling.mode) {
	case PTYCHITE_TILING_NONE:
		string = "none";
		break;
	case PTYCHITE_TILING_TRADITIONAL:
		string = "traditional";
		break;
	default:
		return NULL;
	}

	return json_object_new_string(string);
}

static int config_set_tiling_gaps(struct ptychite_config *config, struct json_object *value,
		enum ptychite_property_set_mode mode, char **error) {
	if (!json_object_is_type(value, json_type_int)) {
		*error = "tiling gaps must be an integer";
		return -1;
	}
	int gaps = json_object_get_int(value);

	config->tiling.gaps = gaps;

	if (config->compositor) {
		ptychite_server_retile(config->compositor->server);
	}

	return 0;
}

static struct json_object *config_get_tiling_gaps(struct ptychite_config *config) {
	return json_object_new_int(config->tiling.gaps);
}

struct property_entry config_property_table[] = {
		{(char *[]){"keyboard", "repeat", "rate", NULL}, config_set_keyboard_repeat_rate,
				config_get_keyboard_repeat_rate},
		{(char *[]){"keyboard", "repeat", "delay", NULL}, config_set_keyboard_repeat_delay,
				config_get_keyboard_repeat_delay},
		{(char *[]){"keyboard", "xkb", "options", NULL}, config_set_keyboard_xkb_options,
				config_get_keyboard_xkb_options},
		{(char *[]){"keyboard", "chords", NULL}, config_set_keyboard_chords,
				config_get_keyboard_chords},

		{(char *[]){"panel", "enabled", NULL}, config_set_panel_enabled, config_get_panel_enabled},
		{(char *[]){"panel", "font", NULL}, config_set_panel_font, config_get_panel_font},
		{(char *[]){"panel", "colors", "foreground", NULL}, config_set_panel_colors_foreground,
				config_get_panel_colors_foreground},
		{(char *[]){"panel", "colors", "background", NULL}, config_set_panel_colors_background,
				config_get_panel_colors_background},
		{(char *[]){"panel", "colors", "accent", NULL}, config_set_panel_colors_accent,
				config_get_panel_colors_accent},
		{(char *[]){"panel", "colors", "gray1", NULL}, config_set_panel_colors_gray1,
				config_get_panel_colors_gray1},
		{(char *[]){"panel", "colors", "gray2", NULL}, config_set_panel_colors_gray2,
				config_get_panel_colors_gray2},
		{(char *[]){"panel", "colors", "border", NULL}, config_set_panel_colors_border,
				config_get_panel_colors_border},
		{(char *[]){"panel", "colors", "seperator", NULL}, config_set_panel_colors_seperator,
				config_get_panel_colors_seperator},
		{(char *[]){"panel", "colors", "chord", NULL}, config_set_panel_colors_chord,
				config_get_panel_colors_chord},

		{(char *[]){"views", "map_to_front", NULL}, config_set_views_map_to_front,
				config_get_views_map_to_front},
		{(char *[]){"views", "title_bar", "enabled", NULL}, config_set_views_title_bar_enabled,
				config_get_views_title_bar_enabled},
		{(char *[]){"views", "border", "thickness", NULL}, config_set_views_border_thickness,
				config_get_views_border_thickness},
		{(char *[]){"views", "border", "colors", "active", NULL},
				config_set_views_border_colors_active, config_get_views_border_colors_active},
		{(char *[]){"views", "border", "colors", "inactive", NULL},
				config_set_views_border_colors_inactive, config_get_views_border_colors_inactive},

		{(char *[]){"monitors", "default_scale", NULL}, config_set_monitors_default_scale,
				config_get_monitors_default_scale},
		{(char *[]){"monitors", "wallpaper", "filepath", NULL},
				config_set_monitors_wallpaper_filepath, config_get_monitors_wallpaper_filepath},
		{(char *[]){"monitors", "wallpaper", "mode", NULL}, config_set_monitors_wallpaper_mode,
				config_get_monitors_wallpaper_mode},

		{(char *[]){"tiling", "mode", NULL}, config_set_tiling_mode, config_get_tiling_mode},
		{(char *[]){"tiling", "gaps", NULL}, config_set_tiling_gaps, config_get_tiling_gaps},
};

static int property_path_gather_entry_refs(const char *path, size_t *path_l_out,
		struct wl_array *entry_refs_out, size_t *entry_refs_l_out, char **error) {
	wl_array_init(entry_refs_out);

	size_t path_l = 0, *path_token_lens = NULL;
	if (strcmp(path, ":")) {
		char *beg, *end;
		bool on_delim = true;
		for (beg = (char *)path; *beg; beg++) {
			bool is_delim = *beg == ':';
			if (on_delim && !is_delim) {
				path_l++;
			} else if (on_delim && is_delim) {
				*error = "invalid property path";
				return -1;
			}
			on_delim = is_delim;
		}
		if (!path_l || on_delim) {
			*error = "invalid property path";
			return -1;
		}
		end = beg;

		path_token_lens = malloc(sizeof(size_t) * path_l);
		if (!path_token_lens) {
			*error = "memory error";
			return -1;
		}

		char *seek;
		size_t i = 0;
		beg = (char *)path;
		while (true) {
			if (!(seek = strchr(beg, ':'))) {
				seek = end;
			}
			size_t token_len = seek - beg;
			path_token_lens[i++] = token_len;
			if (i >= path_l) {
				break;
			}
			beg += token_len + 1;
		}
	}

	size_t i, entry_refs_l = 0;
	for (i = 0; i < LENGTH(config_property_table); i++) {
		size_t entry_path_l;
		for (entry_path_l = 0; config_property_table[i].path[entry_path_l]; entry_path_l++) {
			;
		}
		if (entry_path_l < path_l) {
			continue;
		}

		bool match = true;
		size_t j;
		char *pos;
		for (j = 0, pos = (char *)path; j < path_l; pos += path_token_lens[j++] + 1) {
			if (strlen(config_property_table[i].path[j]) != path_token_lens[j] ||
					strncmp(pos, config_property_table[i].path[j], path_token_lens[j])) {
				match = false;
				break;
			}
		}
		if (!match) {
			continue;
		}

		struct property_entry_reference *append =
				wl_array_add(entry_refs_out, sizeof(struct property_entry_reference));
		if (!append) {
			wl_array_release(entry_refs_out);
			free(path_token_lens);
			*error = "memory error";
			return -1;
		}
		*append = (struct property_entry_reference){
				.entry = &config_property_table[i],
				.path_l = entry_path_l,
		};

		entry_refs_l++;
	}

	free(path_token_lens);

	if (!entry_refs_l) {
		*error = "unknown property path";
		wl_array_release(entry_refs_out);
		return -1;
	}

	*path_l_out = path_l;
	*entry_refs_l_out = entry_refs_l;

	return 0;
}

static int config_set_property_inner(struct ptychite_config *config, const char *path,
		struct json_object *value, enum ptychite_property_set_mode mode, char **error) {
	struct wl_array entry_refs;
	size_t path_l, entry_refs_l;
	if (property_path_gather_entry_refs(path, &path_l, &entry_refs, &entry_refs_l, error)) {
		return -1;
	}

	struct property_entry_reference *front = entry_refs.data;
	if (entry_refs_l == 1 && front->path_l == path_l) {
		int rv = front->entry->set_func(config, value, mode, error);
		wl_array_release(&entry_refs);
		return rv;
	}

	if (!json_object_is_type(value, json_type_object)) {
		wl_array_release(&entry_refs);
		*error = "setting value is not a json object";
		return -1;
	}

	struct wl_array path_nodes;
	wl_array_init(&path_nodes);
	struct property_path_node *root_node =
			wl_array_add(&path_nodes, sizeof(struct property_path_node));
	if (!root_node) {
		*error = "memory error";
		goto err;
	}
	*root_node = (struct property_path_node){
			.name = NULL,
			.object = value,
			.depth = path_l,
			.visited = false,
	};

	while (true) {
		struct property_path_node *node = NULL, *iter;
		size_t node_id = 0;
		wl_array_for_each(iter, &path_nodes) {
			if (!iter->visited) {
				node = iter;
				break;
			}
			node_id++;
		}
		if (!node) {
			break;
		}
		node->visited = true;
		size_t child_depth = node->depth + 1;

		struct json_object_iter child;
		json_object_object_foreachC(node->object, child) {
			if (!json_object_is_type(child.val, json_type_object)) {
				bool success = false;
				struct property_entry_reference *entry_ref;
				wl_array_for_each(entry_ref, &entry_refs) {
					if (entry_ref->path_l != child_depth) {
						continue;
					}
					if (strcmp(entry_ref->entry->path[entry_ref->path_l - 1], child.key)) {
						continue;
					}

					bool match = true;
					size_t i, id = node_id;
					for (i = 1; i < entry_ref->path_l - path_l; i++) {
						struct property_path_node *parent =
								&((struct property_path_node *)path_nodes.data)[id];
						if (strcmp(parent->name,
									entry_ref->entry->path[entry_ref->path_l - 1 - i])) {
							match = false;
							break;
						}
						id = parent->parent_id;
					}
					if (!match) {
						continue;
					}

					if (!entry_ref->entry->set_func(config, child.val, mode, error)) {
						success = true;
					} else {
						goto err;
					}
					break;
				}
				if (!success) {
					*error = "json object contains an unknown member";
					goto err;
				}

				continue;
			}

			struct property_path_node *append =
					wl_array_add(&path_nodes, sizeof(struct property_path_node));
			if (!append) {
				*error = "memory error";
				goto err;
			}
			*append = (struct property_path_node){
					.parent_id = node_id,
					.name = child.key,
					.object = child.val,
					.depth = child_depth,
					.visited = false,
			};
		}
	}

	wl_array_release(&entry_refs);
	wl_array_release(&path_nodes);
	return 0;

err:
	wl_array_release(&entry_refs);
	wl_array_release(&path_nodes);
	return -1;
}

static const struct {
	char *name;
	uint32_t value;
} modifier_name_table[] = {
		{"Sh", WLR_MODIFIER_SHIFT},
		{"Cp", WLR_MODIFIER_CAPS},
		{"C", WLR_MODIFIER_CTRL},
		{"M", WLR_MODIFIER_ALT},
		{"M1", WLR_MODIFIER_ALT},
		{"A", WLR_MODIFIER_ALT},
		{"M2", WLR_MODIFIER_MOD2},
		{"M3", WLR_MODIFIER_MOD3},
		{"S", WLR_MODIFIER_LOGO},
		{"M4", WLR_MODIFIER_LOGO},
		{"M5", WLR_MODIFIER_MOD5},
};

int ptychite_config_init(struct ptychite_config *config, struct ptychite_compositor *compositor) {
	config->compositor = compositor;

	config->keyboard.repeat.rate = 25;
	config->keyboard.repeat.delay = 600;
	config->keyboard.xkb.options = NULL;
	wl_array_init(&config->keyboard.chords);

	config->panel.enabled = true;
	char *error;
	if (font_fill_from_string(&config->panel.font, "monospace bold 15", &error)) {
		return -1;
	}
	config->panel.colors.background[0] = 0.0;
	config->panel.colors.background[1] = 0.0;
	config->panel.colors.background[2] = 0.0;
	config->panel.colors.background[3] = 1.0;
	config->panel.colors.foreground[0] = 0.9;
	config->panel.colors.foreground[1] = 0.95;
	config->panel.colors.foreground[2] = 1.0;
	config->panel.colors.foreground[3] = 1.0;
	config->panel.colors.accent[0] = 0.208;
	config->panel.colors.accent[1] = 0.208;
	config->panel.colors.accent[2] = 0.208;
	config->panel.colors.accent[3] = 1.0;
	config->panel.colors.gray1[0] = 0.278;
	config->panel.colors.gray1[1] = 0.278;
	config->panel.colors.gray1[2] = 0.278;
	config->panel.colors.gray1[3] = 1.0;
	config->panel.colors.gray2[0] = 0.604;
	config->panel.colors.gray2[1] = 0.604;
	config->panel.colors.gray2[2] = 0.604;
	config->panel.colors.gray2[3] = 1.0;
	config->panel.colors.border[0] = 0.6;
	config->panel.colors.border[1] = 0.6;
	config->panel.colors.border[2] = 0.6;
	config->panel.colors.border[3] = 1.0;
	config->panel.colors.seperator[0] = 0.5;
	config->panel.colors.seperator[1] = 0.5;
	config->panel.colors.seperator[2] = 0.5;
	config->panel.colors.seperator[3] = 1.0;
	config->panel.colors.chord[0] = 0.8;
	config->panel.colors.chord[1] = 0.6;
	config->panel.colors.chord[2] = 0.2;
	config->panel.colors.chord[3] = 1.0;

	config->views.map_to_front = true;
	config->views.title_bar.enabled = true;
	config->views.border.thickness = 2;
	config->views.border.colors.active[0] = 0.2;
	config->views.border.colors.active[1] = 0.3;
	config->views.border.colors.active[2] = 0.8;
	config->views.border.colors.active[3] = 0.5;
	config->views.border.colors.inactive[0] = 0.5;
	config->views.border.colors.inactive[1] = 0.5;
	config->views.border.colors.inactive[2] = 0.5;
	config->views.border.colors.inactive[3] = 1.0;

	config->monitors.default_scale = 1.0;
	config->monitors.wallpaper.path = NULL;
	config->monitors.wallpaper.mode = PTYCHITE_WALLPAPER_FIT;
	config->monitors.wallpaper.surface = NULL;

	config->tiling.mode = PTYCHITE_TILING_TRADITIONAL;
	config->tiling.gaps = 10;

	return 0;
}

void ptychite_chord_binding_deinit(struct ptychite_chord_binding *chord_binding) {
	ptychite_chord_deinit(&chord_binding->chord);
	ptychite_action_destroy(chord_binding->action);
	chord_binding->active = false;
}

void ptychite_config_wipe_chord_bindings(struct ptychite_config *config) {
	struct ptychite_chord_binding *chord_binding;
	wl_array_for_each(chord_binding, &config->keyboard.chords) {
		if (!chord_binding->active) {
			continue;
		}

		ptychite_chord_binding_deinit(chord_binding);
	}
}

void ptychite_config_deinit(struct ptychite_config *config) {
	ptychite_config_wipe_chord_bindings(config);
	wl_array_release(&config->keyboard.chords);
	pango_font_description_free(config->panel.font.font);
	free(config->panel.font.string);
}

struct ptychite_chord_binding *ptychite_config_add_chord_binding(struct ptychite_config *config) {
	struct ptychite_chord_binding *chord_binding;
	wl_array_for_each(chord_binding, &config->keyboard.chords) {
		if (!chord_binding->active) {
			return chord_binding;
		}
	}

	return wl_array_add(&config->keyboard.chords, sizeof(struct ptychite_chord_binding));
}

struct ptychite_chord_binding *ptychite_config_scan_into_chord_binding(
		struct ptychite_config *config, const char *pattern, const char **args, int args_l,
		char **error) {
	struct ptychite_chord_binding *chord_binding = ptychite_config_add_chord_binding(config);
	if (!chord_binding) {
		*error = "memory error";
		return NULL;
	}

	struct ptychite_chord chord;
	if (ptychite_chord_parse_pattern(&chord, pattern, error)) {
		chord_binding->active = false;
		return NULL;
	}

	struct ptychite_action *action = ptychite_action_create(args, args_l, error);
	if (!action) {
		ptychite_chord_deinit(&chord);
		chord_binding->active = false;
		return NULL;
	}

	*chord_binding = (struct ptychite_chord_binding){
			.chord = chord,
			.action = action,
			.active = true,
	};

	return chord_binding;
}

int ptychite_config_parse_config(struct ptychite_config *config, char **error) {
	char filepath[256];
	const char *env_home = getenv("HOME");
	const char *env_xdg_config_home = getenv("XDG_CONFIG_HOME");

	if (env_xdg_config_home) {
		snprintf(filepath, sizeof(filepath), "%s/ptychite/ptychite.json", env_xdg_config_home);
	} else if (env_home) {
		snprintf(filepath, sizeof(filepath), "%s/.config/ptychite/ptychite.json", env_home);
	} else {
		*error = "environment variables 'HOME' or 'XDG_CONFIG_HOME' were not found";
		return -1;
	}

	wlr_log(WLR_INFO, "Parsing config file at %s", filepath);
	return ptychite_config_set_property_from_file(
			config, ":", filepath, PTYCHITE_PROPERTY_SET_OVERWRITE, error);
}

int ptychite_config_set_property_from_string(struct ptychite_config *config, const char *path,
		const char *string, enum ptychite_property_set_mode mode, char **error) {
	struct json_object *value = json_tokener_parse(string);
	if (!value) {
		*error = "json data could not be parsed";
		return -1;
	}

	int rv = config_set_property_inner(config, path, value, mode, error);
	json_object_put(value);
	return rv;
}

int ptychite_config_set_property_from_file(struct ptychite_config *config, const char *path,
		const char *filepath, enum ptychite_property_set_mode mode, char **error) {
	struct json_object *value = json_object_from_file(filepath);
	if (!value) {
		*error = "json data could not be parsed";
		return -1;
	}

	int rv = config_set_property_inner(config, path, value, mode, error);
	json_object_put(value);
	return rv;
}

char *ptychite_config_get_property(struct ptychite_config *config, const char *path,
		enum ptychite_json_get_mode mode, char **error) {
	struct wl_array entries;
	size_t entries_l, path_l;
	if (property_path_gather_entry_refs(path, &path_l, &entries, &entries_l, error)) {
		return NULL;
	}

	struct json_object *output;

	struct property_entry_reference *front = entries.data;
	if (entries_l == 1 && front->path_l == path_l) {
		if (!(output = front->entry->get_func(config))) {
			wl_array_release(&entries);
			*error = "most likely a memory error";
			return NULL;
		}
	} else {
		output = json_object_new_object();
		if (!output) {
			wl_array_release(&entries);
			*error = "memory error";
			return NULL;
		}

		struct property_entry_reference *entry_ref;
		wl_array_for_each(entry_ref, &entries) {
			struct json_object *object = output;

			char **token;
			for (token = entry_ref->entry->path + path_l; *(token + 1); token++) {
				struct json_object *child;
				if (!json_object_object_get_ex(object, *token, &child)) {
					if (!(child = json_object_new_object())) {
						json_object_put(output);
						wl_array_release(&entries);
						*error = "memory error";
						return NULL;
					}
					json_object_object_add(object, *token, child);
				}
				object = child;
			}

			struct json_object *value = entry_ref->entry->get_func(config);
			if (!value) {
				json_object_put(output);
				wl_array_release(&entries);
				*error = "most likely a memory error";
				return NULL;
			}
			json_object_object_add(object, *token, value);
		}
	}

	wl_array_release(&entries);

	const char *string = ptychite_json_object_convert_to_string(output, mode, error);
	if (!string) {
		json_object_put(output);
		return NULL;
	}

	char *string_dup = strdup(string);
	if (!string_dup) {
		*error = "memory error";
	}

	json_object_put(output);
	return string_dup;
}

int ptychite_chord_parse_pattern(struct ptychite_chord *chord, const char *pattern, char **error) {
	char *pos;
	bool on_space = true;
	size_t tokens = 0;
	for (pos = (char *)pattern; *pos; pos++) {
		bool is_space = *pos == ' ';
		if (on_space && !is_space) {
			tokens++;
		}
		on_space = is_space;
	}
	if (!tokens) {
		*error = "chord pattern is empty";
		return -1;
	}

	char *dup_pattern = malloc(pos - pattern + 1);
	if (!dup_pattern) {
		*error = "memory error";
		return -1;
	}
	strcpy(dup_pattern, pattern);

	struct ptychite_key *keys = calloc(tokens, sizeof(struct ptychite_key));
	if (!keys) {
		*error = "memory error";
		goto err_alloc_keys;
	}

	size_t keys_l;
	for (pos = dup_pattern, keys_l = 0; *pos; pos++) {
		if (*pos == ' ') {
			continue;
		}
		if (*pos == '-') {
			*error = "chord pattern has an unexpected character";
			goto err_rest;
		}

		char *end;
		for (end = pos; *end && *end != ' '; end++) {
			;
		}

		bool done = false;
		if (*end) {
			*end = '\0';
		} else {
			done = true;
		}

		char *stay, *seek;
		for (stay = pos; (seek = strchr(stay, '-')); stay = seek) {
			*seek++ = '\0';
			if (!*seek || *seek == ' ' || *seek == '-') {
				*error = "chord pattern has an unexpected character";
				goto err_rest;
			}

			size_t i;
			bool found = false;
			for (i = 0; i < LENGTH(modifier_name_table); i++) {
				if (!strcmp(stay, modifier_name_table[i].name)) {
					keys[keys_l].modifiers |= modifier_name_table[i].value;
					found = true;
					break;
				}
			}
			if (!found) {
				*error = "chord pattern contains an unknown modifier";
				goto err_rest;
			}
		}
		if (!*stay) {
			goto err_rest;
		}

		uint32_t sym = xkb_keysym_from_name(stay, XKB_KEYSYM_NO_FLAGS);
		if (sym == XKB_KEY_NoSymbol) {
			*error = "chord pattern contains an unknown sym name";
			goto err_rest;
		}
		keys[keys_l].sym = sym;

		if (done) {
			break;
		}

		pos = end;
		keys_l++;
	}

	chord->keys = keys;
	chord->keys_l = tokens;

	free(dup_pattern);
	return 0;

err_rest:
	free(keys);
err_alloc_keys:
	free(dup_pattern);
	return -1;
}

void ptychite_chord_deinit(struct ptychite_chord *chord) {
	free(chord->keys);
}

char *ptychite_chord_get_pattern(struct ptychite_chord *chord) {
	if (!chord->keys_l) {
		return NULL;
	}

	struct wl_array output;
	wl_array_init(&output);

	char *append;
	size_t i;
	for (i = 0; i < chord->keys_l; i++) {
		int j;
		for (j = 0; j <= 7; j++) {
			uint32_t modifier = 1 << j;
			if (!(chord->keys[i].modifiers & modifier)) {
				continue;
			}

			char *name = NULL;
			size_t k;
			for (k = 0; k < LENGTH(modifier_name_table); k++) {
				if (modifier_name_table[k].value == modifier) {
					name = modifier_name_table[k].name;
					break;
				}
			}
			if (!name) {
				goto err;
			}

			size_t name_l = strlen(name);
			if (!(append = wl_array_add(&output, name_l + 1))) {
				goto err;
			}
			memcpy(append, name, name_l);
			append[name_l] = '-';
		}

		char buffer[128];
		int sym_l = xkb_keysym_get_name(chord->keys[i].sym, buffer, sizeof(buffer));
		if (sym_l < 0) {
			goto err;
		}
		if (!(append = wl_array_add(&output, sym_l + 1))) {
			goto err;
		}
		memcpy(append, buffer, sym_l);
		append[sym_l] = ' ';
	}

	char *term = (char *)output.data + output.size - 1;
	*term = '\0';

	char *pattern = strdup(output.data);
	wl_array_release(&output);
	return pattern;

err:
	wl_array_release(&output);
	return NULL;
}
