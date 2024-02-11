#ifndef PTYCHITE_WINDOWS_H
#define PTYCHITE_WINDOWS_H

#include <wlr/types/wlr_pointer.h>
#include <cairo.h>

#include "../util.h"
#include "../element.h"

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

struct ptychite_window *ptychite_element_get_window(struct ptychite_element *element);

int ptychite_window_init(struct ptychite_window *window, struct ptychite_server *server, const struct ptychite_window_impl *impl,
		struct wlr_scene_tree *parent, struct wlr_output *output);
int ptychite_window_relay_draw(struct ptychite_window *window, int width, int height);
void ptychite_window_relay_draw_same_size(struct ptychite_window *window);
void ptychite_window_relay_pointer_enter(struct ptychite_window *window);
void ptychite_window_relay_pointer_leave(struct ptychite_window *window);
void ptychite_window_relay_pointer_move(struct ptychite_window *window, double x, double y);
void ptychite_window_relay_pointer_button(
		struct ptychite_window *window, double x, double y, struct wlr_pointer_button_event *event);

/* Wallpaper */
struct ptychite_wallpaper {
	struct ptychite_window base;
	struct ptychite_monitor *monitor;
};

extern const struct ptychite_window_impl ptychite_wallpaper_window_impl;

void ptychite_wallpaper_draw_auto(struct ptychite_wallpaper *wallpaper);

/* Panel */
struct ptychite_panel {
	struct ptychite_window base;
	struct ptychite_monitor *monitor;

	struct {
		struct ptychite_mouse_region shell;
		struct ptychite_mouse_region time;
	} regions;
};

extern const struct ptychite_window_impl ptychite_panel_window_impl;

void ptychite_panel_draw_auto(struct ptychite_panel *panel);

/* Control */
struct ptychite_control {
	struct ptychite_window base;

	struct {
		struct wlr_box box;
	} time;
};

extern const struct ptychite_window_impl ptychite_control_window_impl;

void ptychite_control_draw_auto(struct ptychite_control *control);
void ptychite_control_show(struct ptychite_control *control);
void ptychite_control_hide(struct ptychite_control *control);

/* Title Bar */
struct ptychite_title_bar {
	struct ptychite_window base;
	struct ptychite_view *view;

	struct {
		struct ptychite_mouse_region hide;
		struct ptychite_mouse_region close;
	} regions;
};

extern const struct ptychite_window_impl ptychite_title_bar_window_impl;

#endif
