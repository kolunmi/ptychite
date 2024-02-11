#ifndef PTYCHITE_MONITOR_H
#define PTYCHITE_MONITOR_H

#include "util.h"

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

struct ptychite_workspace *ptychite_monitor_add_workspace(struct ptychite_monitor *monitor);
void ptychite_monitor_tile(struct ptychite_monitor *monitor);
void ptychite_monitor_switch_workspace(struct ptychite_monitor *monitor, struct ptychite_workspace *workspace);
void ptychite_monitor_fix_workspaces(struct ptychite_monitor *monitor);
void ptychite_monitor_disable(struct ptychite_monitor *monitor);
void ptychite_monitor_rig(struct ptychite_monitor *monitor);

#endif
