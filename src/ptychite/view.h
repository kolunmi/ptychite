#ifndef PTYCHITE_VIEW_H
#define PTYCHITE_VIEW_H

#include "server.h"
#include "element.h"

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

struct ptychite_view *ptychite_element_get_view(struct ptychite_element *element);

void ptychite_view_resize(struct ptychite_view *view, int width, int height);
void ptychite_surface_unfocus(struct wlr_surface *surface);
void ptychite_view_focus(struct ptychite_view *view, struct wlr_surface *surface);
void ptychite_view_begin_interactive(struct ptychite_view *view, enum ptychite_cursor_mode mode);
void ptychite_view_rig(struct ptychite_view *view, struct wlr_xdg_surface *xdg_surface);

#endif
