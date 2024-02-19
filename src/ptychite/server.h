#ifndef PTYCHITE_SERVER_H
#define PTYCHITE_SERVER_H

#include <systemd/sd-bus.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "util.h"
#include "windows.h"

struct ptychite_compositor;
struct ptychite_server;
struct ptychite_action;

enum ptychite_cursor_mode {
	PTYCHITE_CURSOR_PASSTHROUGH,
	PTYCHITE_CURSOR_MOVE,
	PTYCHITE_CURSOR_RESIZE,
};

struct ptychite_server {
	struct ptychite_compositor *compositor;
	bool terminated;

	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_session *session;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;

	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct {
		struct wlr_scene_tree *background;
		struct wlr_scene_tree *bottom;
		struct wlr_scene_tree *tiled;
		struct wlr_scene_tree *floating;
		struct wlr_scene_tree *fullscreen;
		struct wlr_scene_tree *top;
		struct wlr_scene_tree *overlay;
		struct wlr_scene_tree *block;
	} layers;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_surface;
	struct wl_listener new_xdg_decoration;
	struct wl_list views;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	enum ptychite_cursor_mode cursor_mode;
	struct ptychite_view *grabbed_view;
	struct ptychite_window *hovered_window;
	double grab_x, grab_y;
	struct wl_array keys;

	struct wlr_output_layout *output_layout;
	struct wl_list monitors;
	struct ptychite_monitor *active_monitor;
	struct wl_listener new_output;
	struct wl_listener layout_change;

	struct wlr_output_manager_v1 *output_mgr;
	struct wl_listener output_mgr_apply;
	struct wl_listener output_mgr_test;

	/* struct wlr_idle *idle; */
	/* struct wlr_idle_notifier_v1 *idle_notifier; */
	/* struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr; */
	/* struct wl_listener idle_inhibitor_create; */

	struct wl_event_source *time_tick;

	char panel_date[128];
	struct ptychite_control *control;
	const char *control_greeting;

	bool dbus_active;
	sd_bus *bus;
	sd_bus_slot *xdg_slot;

	uint32_t last_id;
	struct wl_list notifications;
	struct wl_list history;

	struct ptychite_hash_map applications;
	struct ptychite_hash_map icons;

	struct ptychite_switcher switcher;
};

struct ptychite_view *ptychite_server_get_top_view(struct ptychite_server *server);
struct ptychite_view *ptychite_server_get_front_view(struct ptychite_server *server);
struct ptychite_view *ptychite_server_get_focused_view(struct ptychite_server *server);

void ptychite_server_tiling_change_views_in_master(struct ptychite_server *server, int delta);
void ptychite_server_tiling_change_master_factor(struct ptychite_server *server, double delta);
void ptychite_server_focus_any(struct ptychite_server *server);

struct ptychite_server *ptychite_server_create(void);

int ptychite_server_init_and_run(struct ptychite_server *server, struct ptychite_compositor *compositor);

void ptychite_server_configure_keyboards(struct ptychite_server *server);

void ptychite_server_configure_panels(struct ptychite_server *server);

void ptychite_server_configure_views(struct ptychite_server *server);

void ptychite_server_refresh_wallpapers(struct ptychite_server *server);

void ptychite_server_retile(struct ptychite_server *server);

void ptychite_server_check_cursor(struct ptychite_server *server);

void ptychite_server_execute_action(struct ptychite_server *server, struct ptychite_action *action);

#endif
