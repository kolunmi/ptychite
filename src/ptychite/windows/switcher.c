#include <wayland-util.h>

#include "../applications.h"
#include "../compositor.h"
#include "../config.h"
#include "../draw.h"
#include "../icon.h"
#include "../monitor.h"
#include "../server.h"
#include "../view.h"
#include "../windows.h"

static void switcher_draw(
		struct ptychite_window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct ptychite_switcher *switcher = wl_container_of(window, switcher, base);
	struct ptychite_server *server = switcher->base.server;
	struct ptychite_config *config = server->compositor->config;

	float *accent = config->panel.colors.accent;
	float *border = config->panel.colors.border;
	float *foreground = config->panel.colors.foreground;
	float *gray1 = config->panel.colors.gray1;
	struct ptychite_font *font = &config->panel.font;

	ptychite_cairo_draw_rounded_rect(
			cairo, 10 * scale, 10 * scale, surface_width - 20 * scale, surface_height - 20 * scale, 10 * scale);
	cairo_set_source_rgba(cairo, accent[0], accent[1], accent[2], accent[3]);
	cairo_fill_preserve(cairo);
	cairo_set_source_rgba(cairo, border[0], border[1], border[2], border[3]);
	cairo_set_line_width(cairo, 2);
	cairo_stroke(cairo);

	int x = 30;
	int i = 0;

	struct ptychite_switcher_app *sapp;
	wl_array_for_each(sapp, &switcher->apps) {
		struct ptychite_icon *icon = ptychite_hash_map_get(&server->icons, sapp->app->resolved_icon);
		if (!icon) {
			continue;
		}

		struct wlr_box box = {
				.x = x * scale,
				.y = 30 * scale,
				.width = 64 * scale,
				.height = 64 * scale,
		};

		if (i == switcher->idx) {
			ptychite_cairo_draw_rounded_rect(cairo, box.x - 10 * scale, box.y - 10 * scale, box.width + 20 * scale,
					box.height + 45 * scale, 5 * scale);
			cairo_set_source_rgba(cairo, gray1[0], gray1[1], gray1[2], gray1[3]);
			cairo_fill(cairo);
		}

		draw_icon(cairo, icon, box);
		ptychite_cairo_draw_text_center(cairo, box.y + box.height + 5 * scale, box.x, box.width, NULL, font->font,
				sapp->app->name, foreground, NULL, scale * 0.6, false, NULL, NULL);

		x += 64 + 30;
		i++;
	}
}

static void switcher_destroy(struct ptychite_window *window) {
	struct ptychite_switcher *switcher = wl_container_of(window, switcher, base);

	free(switcher);
}

const struct ptychite_window_impl ptychite_switcher_window_impl = {
		.draw = switcher_draw,
		.handle_pointer_enter = NULL,
		.handle_pointer_leave = NULL,
		.handle_pointer_move = NULL,
		.handle_pointer_button = NULL,
		.destroy = switcher_destroy,
};

void ptychite_switcher_draw_auto(struct ptychite_switcher *switcher) {
	struct ptychite_server *server = switcher->base.server;
	struct ptychite_monitor *monitor = server->active_monitor;

	if (!monitor) {
		return;
	}

	wl_array_release(&switcher->apps);
	wl_array_init(&switcher->apps);

	struct ptychite_view *view;
	wl_list_for_each(view, &server->views, server_link) {
		struct ptychite_application *app = ptychite_hash_map_get(&server->applications, view->xdg_toplevel->app_id);
		if (!app) {
			continue;
		}

		bool found = false;
		struct ptychite_switcher_app *iter;
		wl_array_for_each(iter, &switcher->apps) {
			if (iter->app == app) {
				found = true;
				break;
			}
		}
		if (found) {
			continue;
		}

		struct ptychite_switcher_app *append = wl_array_add(&switcher->apps, sizeof(struct ptychite_switcher_app));
		if (!append) {
			continue;
		}
		*append = (struct ptychite_switcher_app){
				.app = app,
				.view = view,
		};
	}

	size_t len = switcher->apps.size / sizeof(struct ptychite_switcher_app);
	if (!len) {
		return;
	}

	if (switcher->idx < 0) {
		switcher->idx = len > 1 ? 1 : 0;
	}

	int width = (64 + 30) * len + 60 - 30;
	int height = 64 + 85;
	wlr_scene_node_set_position(&switcher->base.element.scene_tree->node,
			monitor->window_geometry.x + (monitor->window_geometry.width - width) / 2,
			monitor->window_geometry.y + (monitor->window_geometry.height - height) / 2);

	switcher->base.output = monitor->output;
	ptychite_window_relay_draw(&switcher->base, width, height);
	wlr_scene_node_set_enabled(&switcher->base.element.scene_tree->node, true);
}
