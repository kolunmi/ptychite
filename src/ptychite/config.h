#ifndef PTYCHITE_CONFIG_H
#define PTYCHITE_CONFIG_H

#include <cairo.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-util.h>

#include "json.h"

enum ptychite_property_set_mode {
	PTYCHITE_PROPERTY_SET_OVERWRITE,
	PTYCHITE_PROPERTY_SET_APPEND,
};

enum ptychite_wallpaper_mode {
	PTYCHITE_WALLPAPER_FIT,
	PTYCHITE_WALLPAPER_STRETCH,
};

enum ptychite_tiling_mode {
	PTYCHITE_TILING_NONE,
	PTYCHITE_TILING_TRADITIONAL,
};

struct ptychite_key {
	uint32_t modifiers;
	uint32_t sym;
};

struct ptychite_chord {
	struct ptychite_key *keys;
	size_t keys_l;
};

struct ptychite_chord_binding {
	struct ptychite_chord chord;
	struct ptychite_action *action;
	bool active;
};

struct ptychite_font {
	PangoFontDescription *font;
	char *string;
	int height;
	int baseline;
	bool markup;
};

struct ptychite_config {
	struct ptychite_compositor *compositor;

	struct {
		struct {
			int32_t rate;
			int32_t delay;
		} repeat;
		struct {
			char *options;
		} xkb;
		struct wl_array chords;
	} keyboard;

	struct {
		bool enabled;
		struct ptychite_font font;
		struct {
			float foreground[4];
			float background[4];
			float accent[4];
			float gray1[4];
			float gray2[4];
			float border[4];
			float seperator[4];
			float chord[4];
		} colors;
	} panel;

	struct {
		struct {
			int thickness;
			struct {
				float active[4];
				float inactive[4];
			} colors;
		} bar;
	} views;

	struct {
		float default_scale;
		struct {
			char *path;
			enum ptychite_wallpaper_mode mode;
			cairo_surface_t *surface;
		} wallpaper;
	} monitors;

	struct {
		enum ptychite_tiling_mode mode;
		int gaps;
	} tiling;
};

int ptychite_config_init(struct ptychite_config *config, struct ptychite_compositor *compositor);

void ptychite_chord_binding_deinit(struct ptychite_chord_binding *chord_binding);

void ptychite_config_wipe_chord_bindings(struct ptychite_config *config);

void ptychite_config_deinit(struct ptychite_config *config);

struct ptychite_chord_binding *ptychite_config_add_chord_binding(struct ptychite_config *config);

struct ptychite_chord_binding *ptychite_config_scan_into_chord_binding(
		struct ptychite_config *config, const char *pattern, const char **args, int args_l,
		char **error);

int ptychite_config_parse_config(struct ptychite_config *config, char **error);

int ptychite_config_set_property_from_string(struct ptychite_config *config, const char *path,
		const char *string, enum ptychite_property_set_mode mode, char **error);

int ptychite_config_set_property_from_file(struct ptychite_config *config, const char *path,
		const char *filepath, enum ptychite_property_set_mode mode, char **error);

char *ptychite_config_get_property(struct ptychite_config *config, const char *path,
		enum ptychite_json_get_mode mode, char **error);

int ptychite_chord_parse_pattern(struct ptychite_chord *chord, const char *pattern, char **error);

void ptychite_chord_deinit(struct ptychite_chord *chord);

char *ptychite_chord_get_pattern(struct ptychite_chord *chord);

#endif
