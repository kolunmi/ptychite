#include <librsvg/rsvg.h>
#include <wayland-util.h>

#include "../compositor.h"
#include "../config.h"
#include "../draw.h"
#include "../icon.h"
#include "../macros.h"
#include "../monitor.h"
#include "../server.h"
#include "../util.h"
#include "../view.h"
#include "../windows.h"
#include "cairo.h"
#include "src/ptychite/applications.h"

static const uint32_t ptychite_svg[] = {
		1836597052,
		1702240364,
		1869181810,
		824327534,
		539111470,
		1868787301,
		1735289188,
		1414865469,
		574106950,
		1933327935,
		1998612342,
		1752458345,
		959652413,
		942946094,
		539127149,
		1734960488,
		574452840,
		858665011,
		577596724,
		1919252000,
		1852795251,
		774971965,
		1981817393,
		1115121001,
		574453871,
		540024880,
		925776179,
		857749556,
		875769392,
		1836589090,
		1030975084,
		1953785890,
		791624304,
		779581303,
		1865298807,
		841967474,
		791687216,
		577205875,
		543636542,
		1851880052,
		1919903347,
		1948401005,
		1936613746,
		1702125932,
		926035240,
		925906478,
		775302432,
		859060019,
		1010704937,
		1882996321,
		543716449,
		1830960484,
		858665524,
		775238176,
		1664692535,
		808463920,
		840970802,
		1697921070,
		757085229,
		876097078,
		540290405,
		808463920,
		942486070,
		1698116910,
		807416877,
		909127726,
		774909237,
		808792368,
		774905906,
		808661552,
		774909240,
		909194544,
		774909235,
		842084400,
		808333357,
		842412598,
		808333357,
		943207473,
		808333357,
		858928694,
		808333357,
		959656504,
		808333344,
		876163378,
		825110573,
		909456441,
		808333344,
		842216761,
		841887789,
		959787571,
		841887776,
		875836210,
		825110573,
		540554545,
		909389360,
		540162352,
		909127216,
		540424243,
		909454896,
		540162098,
		909258288,
		540553523,
		825241136,
		540161078,
		858795568,
		540160306,
		858795568,
		540423990,
		942681648,
		540227127,
		875572784,
		807416886,
		842150190,
		807416121,
		875835694,
		807416116,
		942814510,
		808269363,
		959591214,
		824194864,
		925970478,
		774909236,
		959525176,
		774971448,
		909653041,
		808333613,
		807416629,
		909652526,
		825046321,
		892877102,
		908996653,
		943011123,
		825111085,
		758199864,
		808857137,
		808268341,
		876032814,
		825046576,
		858798126,
		774971446,
		859191601,
		858665773,
		540619315,
		926428722,
		858599732,
		842020398,
		775036981,
		959526456,
		875442221,
		842018870,
		841889056,
		540293425,
		858795570,
		891303985,
		909456686,
		925774880,
		540227121,
		859123248,
		540096055,
		942812724,
		858601016,
		892351022,
		925775648,
		758659380,
		892481079,
		941634101,
		892612910,
		775302449,
		540488753,
		959786544,
		758462777,
		808333617,
		875377459,
		825767726,
		825306424,
		842478638,
		774910253,
		758722870,
		859188784,
		758658873,
		808791608,
		908079410,
		808663086,
		892415283,
		842543406,
		775172384,
		758264889,
		908997937,
		824193844,
		842280497,
		774909233,
		876164150,
		808591411,
		892875566,
		875444512,
		540358705,
		925773874,
		840971830,
		909323824,
		774905905,
		808728368,
		775036979,
		875901489,
		925775149,
		758723635,
		926297650,
		825045816,
		808988210,
		774909235,
		909195313,
		1713381941,
		1030515817,
		1852796450,
		1931485797,
		1802465908,
		589446501,
		577136230,
		1920234272,
		761621359,
		1701734764,
		1030775139,
		1970237986,
		539124846,
		1869771891,
		1999463787,
		1752458345,
		775168573,
		790770993,
		1630485566,
		1731148862,
		1932475454,
		171861878,
};

static int get_section_width(cairo_t *cairo, struct ptychite_panel *panel, struct ptychite_panel_section *section,
		int surface_height, float scale) {
	struct ptychite_server *server = panel->base.server;
	struct ptychite_config *config = server->compositor->config;
	struct ptychite_font *font = &config->panel.font;

	int font_height = font->height * scale;
	int x = 0;
	int y = (surface_height - font_height) / 2;

	int i;
	for (i = 0; i < section->modules_l; i++) {
		switch (section->modules[i].type) {
		case PTYCHITE_PANEL_MODULE_LOGO: {
			x += surface_height + font_height;
			break;
		}
		case PTYCHITE_PANEL_MODULE_WINDOWICON: {
			int size = surface_height - y * 2;
			x += size;
			break;
		}
		case PTYCHITE_PANEL_MODULE_WORKSPACES: {
			x += font_height;
			struct ptychite_workspace *workspace;
			wl_list_for_each(workspace, &panel->monitor->workspaces, link) {
				x += font_height;
			}
			break;
		}
		case PTYCHITE_PANEL_MODULE_DATE: {
			if (*server->panel_date) {
				int width;
				if (!ptychite_cairo_get_text_size(cairo, font->font, server->panel_date, scale, false, &width, NULL)) {
					x += width;
				}
				x += font_height;
			}
			break;
		}
		case PTYCHITE_PANEL_MODULE_CHORD: {
			if (server->keys.size) {
				struct ptychite_chord chord = {
						.keys = server->keys.data,
						.keys_l = server->keys.size / sizeof(struct ptychite_key),
				};
				/* FIXME this is done twice */
				char *chord_string = ptychite_chord_get_pattern(&chord);
				if (chord_string) {
					int width;
					if (!ptychite_cairo_get_text_size(cairo, font->font, chord_string, scale, false, &width, NULL)) {
						x += width;
					}
					free(chord_string);
				}
				x += font_height;
			}
			break;
		}
		case PTYCHITE_PANEL_MODULE_NETWORK: {
			if (server->dbus_active) {
				int width;
				if (!ptychite_cairo_get_text_size(
							cairo, font->font, server->internet ? "Net Up" : "Net Down", scale, false, &width, NULL)) {
					x += width;
				}
			}
			break;
		}
		case PTYCHITE_PANEL_MODULE_BATTERY: {
			if (server->dbus_active && server->battery.enabled) {
				char buf[32];
				snprintf(buf, sizeof(buf), "BAT %d%%", (int)server->battery.percent);

				int width;
				if (!ptychite_cairo_get_text_size(cairo, font->font, buf, scale, false, &width, NULL)) {
					x += width;
				}
			}
			break;
		}
		default:
			break;
		}
		x += font_height / 2;
	}

	return x - font_height / 2;
}

static void draw_section(cairo_t *cairo, struct ptychite_panel *panel, struct ptychite_panel_section *section, int x,
		int surface_width, int surface_height, float scale) {
	struct ptychite_server *server = panel->base.server;
	struct ptychite_config *config = server->compositor->config;

	float *foreground = config->panel.colors.foreground;
	float *accent = config->panel.colors.accent;
	float *chord_color = config->panel.colors.chord;

	struct ptychite_font *font = &config->panel.font;
	int font_height = font->height * scale;
	int y = (surface_height - font_height) / 2;

	cairo_set_source_rgba(cairo, foreground[0], foreground[1], foreground[2], foreground[3]);

	int i;
	for (i = 0; i < section->modules_l; i++) {
		switch (section->modules[i].type) {
		case PTYCHITE_PANEL_MODULE_LOGO: {
			GError *error = NULL;
			RsvgHandle *svg_handle = rsvg_handle_new_from_data((guint8 *)ptychite_svg, sizeof(ptychite_svg), &error);
			if (svg_handle) {
				RsvgRectangle viewport = {
						.x = x + font_height / 2.0,
						.y = y,
						.width = surface_height,
						.height = font_height,
				};

				panel->regions.shell.box = (struct wlr_box){
						.x = x,
						.y = 0,
						.width = viewport.width + font_height,
						.height = surface_height,
				};
				if (panel->regions.shell.entered) {
					cairo_set_source_rgba(cairo, accent[0], accent[1], accent[2], accent[3]);
					cairo_rectangle(cairo, panel->regions.shell.box.x, panel->regions.shell.box.y,
							panel->regions.shell.box.width, panel->regions.shell.box.height);
					cairo_fill(cairo);
				}

				rsvg_handle_render_document(svg_handle, cairo, &viewport, &error);
				g_object_unref(svg_handle);

				x += panel->regions.shell.box.width;
			} else {
				g_error_free(error);
			}
			break;
		}
		case PTYCHITE_PANEL_MODULE_WINDOWICON: {
			int size = surface_height - y * 2;
			do {
				struct ptychite_view *view = ptychite_server_get_focused_view(server);
				if (!view) {
					break;
				}

				struct ptychite_icon *icon = ptychite_view_get_icon(view);
				if (!icon) {
					break;
				}

				struct wlr_box box = {
						.x = x,
						.y = y,
						.width = size,
						.height = size,
				};
				draw_icon(cairo, icon, box);

			} while (0);
			x += size;
			break;
		}
		case PTYCHITE_PANEL_MODULE_WORKSPACES: {
			x += font_height / 2;
			struct ptychite_workspace *workspace;
			wl_list_for_each(workspace, &panel->monitor->workspaces, link) {
				workspace->region.box = (struct wlr_box){
						.x = x - font_height / 2,
						.y = 0,
						.width = font_height,
						.height = surface_height,
				};

				if (workspace->region.entered) {
					cairo_rectangle(cairo, workspace->region.box.x, workspace->region.box.y,
							workspace->region.box.width, workspace->region.box.height);
					cairo_set_source_rgba(cairo, accent[0], accent[1], accent[2], accent[3]);
					cairo_fill(cairo);
				}
				int radius = font_height / (workspace == panel->monitor->current_workspace ? 4 : 8);
				cairo_arc(cairo, x, surface_height / 2.0, radius, 0, PI * 2);
				cairo_set_source_rgba(cairo, foreground[0], foreground[1], foreground[2], foreground[3]);
				cairo_fill(cairo);

				x += font_height;
			}
			x -= font_height / 2;
			break;
		}
		case PTYCHITE_PANEL_MODULE_DATE: {
			if (*server->panel_date) {
				float *bg = ((server->active_monitor == panel->monitor &&
									 server->control->base.element.scene_tree->node.enabled) ||
									panel->regions.time.entered)
						? accent
						: NULL;
				cairo_move_to(cairo, x, y);
				int width;
				if (!ptychite_cairo_draw_text(
							cairo, font->font, server->panel_date, foreground, bg, scale, false, &width, NULL)) {
					panel->regions.time.box = (struct wlr_box){
							.x = x,
							.y = 0,
							.width = width,
							.height = surface_height,
					};
					x += width;
				}
			}
			break;
		}
		case PTYCHITE_PANEL_MODULE_CHORD: {
			if (server->keys.size) {
				x += font_height / 2;
				struct ptychite_chord chord = {
						.keys = server->keys.data,
						.keys_l = server->keys.size / sizeof(struct ptychite_key),
				};
				char *chord_string = ptychite_chord_get_pattern(&chord);
				if (chord_string) {
					cairo_move_to(cairo, x, y);
					int width;
					ptychite_cairo_draw_text(
							cairo, font->font, chord_string, foreground, chord_color, scale, false, &width, NULL);
					free(chord_string);
					x += width;
				}
				x += font_height / 2;
			}
			break;
		}
		case PTYCHITE_PANEL_MODULE_NETWORK: {
			if (server->dbus_active) {
				cairo_move_to(cairo, x, y);
				int width;
				if (!ptychite_cairo_draw_text(cairo, font->font, server->internet ? "Net Up" : "Net Down", foreground,
							NULL, scale, false, &width, NULL)) {
					x += width;
				}
			}
			break;
		}
		case PTYCHITE_PANEL_MODULE_BATTERY: {
			if (server->battery.enabled) {
				char buf[32];
				snprintf(buf, sizeof(buf), "BAT %d%%", (int)server->battery.percent);

				cairo_move_to(cairo, x, y);
				int width;
				if (!ptychite_cairo_draw_text(cairo, font->font, buf, foreground, NULL, scale, false, &width, NULL)) {
					x += width;
				}
			}
			break;
		}
		default:
			break;
		}
		x += font_height / 2;
	}
}

static void panel_draw(
		struct ptychite_window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct ptychite_panel *panel = wl_container_of(window, panel, base);
	struct ptychite_server *server = panel->monitor->server;
	struct ptychite_config *config = server->compositor->config;
	float *background = config->panel.colors.background;

	cairo_set_source_rgba(cairo, background[0], background[1], background[2], background[3]);
	cairo_rectangle(cairo, 0, 0, surface_width, surface_height);
	cairo_fill(cairo);

	struct ptychite_font *font = &config->panel.font;
	int font_height = font->height * scale;

	draw_section(cairo, panel, &config->panel.sections.left, font_height / 2, surface_width, surface_height, scale);
	draw_section(cairo, panel, &config->panel.sections.center,
			(surface_width - get_section_width(cairo, panel, &config->panel.sections.center, surface_height, scale)) /
					2,
			surface_width, surface_height, scale);
	draw_section(cairo, panel, &config->panel.sections.right,
			surface_width - font_height / 2 -
					get_section_width(cairo, panel, &config->panel.sections.right, surface_height, scale),
			surface_width, surface_height, scale);
}

static void panel_handle_pointer_enter(struct ptychite_window *window) {
}

static void panel_handle_pointer_leave(struct ptychite_window *window) {
	struct ptychite_panel *panel = wl_container_of(window, panel, base);
	bool redraw = false;

	redraw |= panel->regions.time.entered;
	panel->regions.time.entered = false;
	redraw |= panel->regions.shell.entered;
	panel->regions.shell.entered = false;

	struct ptychite_workspace *workspace;
	wl_list_for_each(workspace, &panel->monitor->workspaces, link) {
		redraw |= workspace->region.entered;
		workspace->region.entered = false;
	}

	if (redraw) {
		ptychite_window_relay_draw_same_size(window);
	}
}

static void panel_handle_pointer_move(struct ptychite_window *window, double x, double y) {
	struct ptychite_panel *panel = wl_container_of(window, panel, base);

	bool redraw = false;
	redraw |= ptychite_mouse_region_update_state(&panel->regions.shell, x, y);
	redraw |= ptychite_mouse_region_update_state(&panel->regions.time, x, y);

	struct ptychite_workspace *workspace;
	wl_list_for_each(workspace, &panel->monitor->workspaces, link) {
		redraw |= ptychite_mouse_region_update_state(&workspace->region, x, y);
	}

	if (redraw) {
		ptychite_window_relay_draw_same_size(window);
	}
}

static void panel_handle_pointer_button(
		struct ptychite_window *window, double x, double y, struct wlr_pointer_button_event *event) {
	struct ptychite_panel *panel = wl_container_of(window, panel, base);

	if (event->state != WLR_BUTTON_PRESSED) {
		return;
	}

	if (panel->regions.shell.entered) {
	}
	if (panel->regions.time.entered) {
		struct ptychite_server *server = panel->monitor->server;
		if (server->control->base.element.scene_tree->node.enabled) {
			ptychite_control_hide(server->control);
		} else {
			ptychite_control_show(server->control);
		}
	}

	struct ptychite_workspace *workspace;
	wl_list_for_each(workspace, &panel->monitor->workspaces, link) {
		if (workspace->region.entered) {
			if (workspace != panel->monitor->current_workspace) {
				ptychite_monitor_switch_workspace(panel->monitor, workspace);
			}
			break;
		}
	}
}

static void panel_destroy(struct ptychite_window *window) {
	struct ptychite_panel *panel = wl_container_of(window, panel, base);

	free(panel);
}

const struct ptychite_window_impl ptychite_panel_window_impl = {
		.draw = panel_draw,
		.handle_pointer_enter = panel_handle_pointer_enter,
		.handle_pointer_leave = panel_handle_pointer_leave,
		.handle_pointer_move = panel_handle_pointer_move,
		.handle_pointer_button = panel_handle_pointer_button,
		.destroy = panel_destroy,
};

void ptychite_panel_draw_auto(struct ptychite_panel *panel) {
	struct ptychite_font *font = &panel->monitor->server->compositor->config->panel.font;
	int height = font->height + font->height / 2;

	panel->monitor->window_geometry.y = panel->monitor->geometry.y + height;
	panel->monitor->window_geometry.height = panel->monitor->geometry.height - height;

	ptychite_window_relay_draw(&panel->base, panel->monitor->geometry.width, height);
}
