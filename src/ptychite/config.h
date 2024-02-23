#ifndef PTYCHITE_CONFIG_H
#define PTYCHITE_CONFIG_H

#include <cairo.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-util.h>

#include "action.h"
#include "chord.h"
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

enum ptychite_panel_module_type {
	PTYCHITE_PANEL_MODULE_LOGO,
	PTYCHITE_PANEL_MODULE_WINDOWICON,
	PTYCHITE_PANEL_MODULE_WORKSPACES,
	PTYCHITE_PANEL_MODULE_DATE,
	PTYCHITE_PANEL_MODULE_CHORD,
	PTYCHITE_PANEL_MODULE_BATTERY,
	PTYCHITE_PANEL_MODULE_NETWORK,
	PTYCHITE_PANEL_MODULE_USER,
};

struct ptychite_panel_module {
	enum ptychite_panel_module_type type;
	struct {
		char **cmd_args;
		int interval;
		struct ptychite_action *action;
	} user;
};

struct ptychite_panel_section {
	struct ptychite_panel_module *modules;
	int modules_l;
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
			struct ptychite_panel_section left;
			struct ptychite_panel_section center;
			struct ptychite_panel_section right;
		} sections;
		struct {
			float foreground[4];
			float background[4];
			float accent[4];
			float gray1[4];
			float gray2[4];
			float gray3[4];
			float border[4];
			float separator[4];
			float chord[4];
		} colors;
	} panel;

	struct {
		bool map_to_front;
		struct {
			bool enabled;
			struct {
				float close[4];
			} colors;
		} title_bar;
		struct {
			int thickness;
			struct {
				float active[4];
				float inactive[4];
			} colors;
		} border;
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
		struct ptychite_config *config, const char *pattern, const char **args, int args_l, char **error);

int ptychite_config_parse_config(struct ptychite_config *config, char **error);

int ptychite_config_set_property_from_string(struct ptychite_config *config, const char *path, const char *string,
		enum ptychite_property_set_mode mode, char **error);

int ptychite_config_set_property_from_file(struct ptychite_config *config, const char *path, const char *filepath,
		enum ptychite_property_set_mode mode, char **error);

char *ptychite_config_get_property(
		struct ptychite_config *config, const char *path, enum ptychite_json_get_mode mode, char **error);

#endif
