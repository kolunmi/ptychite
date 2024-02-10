#ifndef PTYCHITE_SERVER_H
#define PTYCHITE_SERVER_H

#include <wayland-server-core.h>
#include <wlr/util/box.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_pointer.h>
#include <cairo.h>

struct ptychite_compositor;
struct ptychite_server;
struct ptychite_action;

enum ptychite_cursor_mode {
	PTYCHITE_CURSOR_PASSTHROUGH,
	PTYCHITE_CURSOR_MOVE,
	PTYCHITE_CURSOR_RESIZE,
};

enum ptychite_element_type {
	PTYCHITE_ELEMENT_VIEW,
	PTYCHITE_ELEMENT_WINDOW,
};

enum ptychite_action_func_data_mode {
	PTYCHITE_ACTION_FUNC_DATA_NONE,
	PTYCHITE_ACTION_FUNC_DATA_INT,
	PTYCHITE_ACTION_FUNC_DATA_STRING,
	PTYCHITE_ACTION_FUNC_DATA_ARGV,
};

typedef void (*ptychite_action_func_t)(struct ptychite_server *compositor, void *data);

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
};

struct ptychite_action {
	struct wl_list link;
	ptychite_action_func_t action_func;
	void *data;
};

struct ptychite_monitor {
	struct wl_list link;
	struct ptychite_server *server;
	struct wlr_output *output;
	struct wlr_box geometry;
	struct wlr_box window_geometry;
	struct wl_list views;
	struct wl_list workspaces;
	struct ptychite_workspace *current_workspace;
	struct ptychite_wallpaper *wallpaper;
	struct ptychite_panel *panel;

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct ptychite_keyboard {
	struct wl_list link;
	struct ptychite_server *server;
	struct wlr_keyboard *keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

struct ptychite_mouse_region {
	struct wlr_box box;
	bool entered;
};

struct ptychite_workspace {
	struct wl_list link;
	struct wl_list views_order;
	struct wl_list views_focus;
	struct {
		struct {
			int views_in_master;
			double master_factor;
			bool right_master;
		} traditional;
	} tiling;
	struct ptychite_mouse_region region;
};

struct ptychite_element {
	enum ptychite_element_type type;
	struct wlr_scene_tree *scene_tree;
	int width, height;
};

struct ptychite_view {
	struct ptychite_element element;
	struct wl_list server_link;
	struct wl_list monitor_link;
	struct wl_list workspace_order_link;
	struct wl_list workspace_focus_link;

	struct ptychite_server *server;
	struct ptychite_monitor *monitor;
	struct ptychite_workspace *workspace;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree_surface;
	struct ptychite_title_bar *title_bar;
	struct {
		struct wlr_scene_rect *top;
		struct wlr_scene_rect *right;
		struct wlr_scene_rect *bottom;
		struct wlr_scene_rect *left;
	} border;

	int initial_width;
	int initial_height;
	uint32_t resize_serial;
	bool focused;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
};


struct ptychite_server *ptychite_server_create(void);

int ptychite_server_init_and_run(struct ptychite_server *server, struct ptychite_compositor *compositor);

void ptychite_server_configure_keyboards(struct ptychite_server *server);

void ptychite_server_configure_panels(struct ptychite_server *server);

void ptychite_server_configure_views(struct ptychite_server *server);

void ptychite_server_refresh_wallpapers(struct ptychite_server *server);

void ptychite_server_retile(struct ptychite_server *server);

void ptychite_server_check_cursor(struct ptychite_server *server);

void ptychite_server_execute_action(struct ptychite_server *server, struct ptychite_action *action);

struct ptychite_action *ptychite_action_create(const char **args, int args_l, char **error);

int ptychite_action_get_args(struct ptychite_action *action, char ***args_out, int *args_l_out);

void ptychite_action_destroy(struct ptychite_action *action);

#endif
