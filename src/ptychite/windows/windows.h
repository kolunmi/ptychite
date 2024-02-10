#ifndef PTYCHITE_WINDOWS_H
#define PTYCHITE_WINDOWS_H

#include <cairo.h>

#include "../server.h"

struct ptychite_window {
	struct ptychite_element element;
	struct ptychite_server *server;
	struct wlr_output *output;
	struct wlr_scene_buffer *scene_buffer;
	const struct ptychite_window_impl *impl;

	struct wl_listener destroy;
};

struct ptychite_window_impl {
	void (*draw)(struct ptychite_window *window, cairo_t *cairo, int surface_width, int surface_height, float scale);
	void (*handle_pointer_move)(struct ptychite_window *window, double x, double y);
	void (*handle_pointer_button)(
			struct ptychite_window *window, double x, double y, struct wlr_pointer_button_event *event);
	void (*handle_pointer_enter)(struct ptychite_window *window);
	void (*handle_pointer_leave)(struct ptychite_window *window);
	void (*destroy)(struct ptychite_window *window);
};

int window_init(struct ptychite_window *window, struct ptychite_server *server, const struct ptychite_window_impl *impl,
		struct wlr_scene_tree *parent, struct wlr_output *output);
int window_relay_draw(struct ptychite_window *window, int width, int height);
void window_relay_draw_same_size(struct ptychite_window *window);
void window_relay_pointer_enter(struct ptychite_window *window);
void window_relay_pointer_leave(struct ptychite_window *window);
void window_relay_pointer_move(struct ptychite_window *window, double x, double y);
void window_relay_pointer_button(
		struct ptychite_window *window, double x, double y, struct wlr_pointer_button_event *event);

/* Wallpaper */
struct ptychite_wallpaper {
	struct ptychite_window base;
	struct ptychite_monitor *monitor;
};

extern const struct ptychite_window_impl wallpaper_window_impl;

void wallpaper_draw_auto(struct ptychite_wallpaper *wallpaper);

/* Panel */
struct ptychite_panel {
	struct ptychite_window base;
	struct ptychite_monitor *monitor;

	struct {
		struct ptychite_mouse_region shell;
		struct ptychite_mouse_region time;
	} regions;
};

extern const struct ptychite_window_impl panel_window_impl;

void panel_draw_auto(struct ptychite_panel *panel);

/* Control */
struct ptychite_control {
	struct ptychite_window base;

	struct {
		struct wlr_box box;
	} time;
};

extern const struct ptychite_window_impl control_window_impl;

void control_draw_auto(struct ptychite_control *control);
void control_show(struct ptychite_control *control);
void control_hide(struct ptychite_control *control);

/* Title Bar */
struct ptychite_title_bar {
	struct ptychite_window base;
	struct ptychite_view *view;

	struct {
		struct ptychite_mouse_region hide;
		struct ptychite_mouse_region close;
	} regions;
};

extern const struct ptychite_window_impl title_bar_window_impl;

#endif
