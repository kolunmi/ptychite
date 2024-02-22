#ifndef PTYCHITE_WINDOWS_H
#define PTYCHITE_WINDOWS_H

#include <cairo.h>
#include <wayland-util.h>

#include <wlr/types/wlr_pointer.h>

#include "applications.h"
#include "element.h"
#include "notification.h"
#include "util.h"

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

int ptychite_window_init(struct ptychite_window *window, struct ptychite_server *server,
		const struct ptychite_window_impl *impl, struct wlr_scene_tree *parent, struct wlr_output *output);
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

/* Switcher */
struct ptychite_switcher_app {
	struct wl_list link;

	struct ptychite_application *app;

	struct wl_list views;
	int idx;
};

struct ptychite_switcher {
	struct ptychite_window base;

	struct wl_list sapps;
	struct ptychite_switcher_app *cur;

	struct ptychite_window sub_switcher;
};

extern const struct ptychite_window_impl ptychite_switcher_window_impl;
extern const struct ptychite_window_impl ptychite_sub_switcher_window_impl;

void ptychite_switcher_draw_auto(struct ptychite_switcher *switcher, bool sub_switcher);

/* Notification */
struct ptychite_notification {
	struct ptychite_window base;

	struct wl_list link;
	struct ptychite_server *server;

	struct ptychite_icon *icon;

	uint32_t id;
	int group_index;
	int group_count;
	bool hidden;

	bool markup_enabled;
	char *app_name;
	char *app_icon;
	char *summary;
	char *body;
	int32_t requested_timeout;
	bool actions_enabled;
	struct wl_list actions; // ptychite_action::link

	enum ptychite_notification_urgency urgency;
	char *category;
	char *desktop_entry;
	char *tag;
	int32_t progress;
	struct ptychite_image_data *image_data;

	struct wl_event_source *timer;

	struct {
		struct ptychite_mouse_region close;
	} regions;

	struct {
		struct ptychite_mouse_region region;
		struct ptychite_mouse_region close;
	} control_regions;
};

extern const struct ptychite_window_impl ptychite_notification_window_impl;

void ptychite_notification_draw_auto(struct ptychite_notification *notif);

#endif
