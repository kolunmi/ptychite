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

	struct ptychite_switcher_app *sapp;
	wl_list_for_each(sapp, &switcher->sapps, link) {
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

		if (sapp == switcher->cur) {
			ptychite_cairo_draw_rounded_rect(cairo, box.x - 10 * scale, box.y - 10 * scale, box.width + 20 * scale,
					box.height + 45 * scale, 5 * scale);
			cairo_set_source_rgba(cairo, gray1[0], gray1[1], gray1[2], gray1[3]);
			cairo_fill(cairo);
		}

		draw_icon(cairo, icon, box);
		ptychite_cairo_draw_text_center(cairo, box.y + box.height + 5 * scale, box.x, box.width, NULL, font->font,
				sapp->app->name, foreground, NULL, scale * 0.6, false, NULL, NULL);

		x += 64 + 30;
	}
}

const struct ptychite_window_impl ptychite_switcher_window_impl = {
		.draw = switcher_draw,
		.handle_pointer_enter = NULL,
		.handle_pointer_leave = NULL,
		.handle_pointer_move = NULL,
		.handle_pointer_button = NULL,
		.destroy = NULL,
};

static void sub_switcher_draw(
		struct ptychite_window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct ptychite_switcher *switcher = wl_container_of(window, switcher, sub_switcher);
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

	int y = 20;
	int idx = 0;

	struct ptychite_view *view;
	wl_list_for_each(view, &switcher->cur->views, switcher_link) {
		ptychite_cairo_draw_text_center(cairo, y * scale, 20 * scale, surface_width - 40 * scale, NULL, font->font,
				view->xdg_toplevel->title, foreground, idx == switcher->cur->idx ? gray1 : NULL, scale * 0.75, false,
				NULL, NULL);
		y += (float)font->height * 0.75;
		idx++;
	}
}

const struct ptychite_window_impl ptychite_sub_switcher_window_impl = {
		.draw = sub_switcher_draw,
		.handle_pointer_enter = NULL,
		.handle_pointer_leave = NULL,
		.handle_pointer_move = NULL,
		.handle_pointer_button = NULL,
		.destroy = NULL,
};

void ptychite_switcher_draw_auto(struct ptychite_switcher *switcher, bool sub_switcher) {
	struct ptychite_server *server = switcher->base.server;
	struct ptychite_monitor *monitor = server->active_monitor;

	if (!monitor) {
		return;
	}

	/* swap the old head */
	struct wl_list old;
	wl_list_insert(&switcher->sapps, &old);
	wl_list_remove(&switcher->sapps);

	/* zero out the list */
	wl_list_init(&switcher->sapps);

	int len = 0;

	struct ptychite_view *view;
	wl_list_for_each(view, &server->views, server_link) {
		struct ptychite_application *app = ptychite_hash_map_get(&server->applications, view->xdg_toplevel->app_id);
		if (!app) {
			view->in_switcher = false;
			continue;
		}

		bool found = false;
		struct ptychite_switcher_app *iter, *tmp;
		wl_list_for_each_safe(iter, tmp, &old, link) {
			if (iter->app == app) {
				wl_list_remove(&iter->link);
				wl_list_insert(switcher->sapps.prev, &iter->link);

				wl_list_init(&iter->views);
				wl_list_insert(&iter->views, &view->switcher_link);
				view->in_switcher = true;

				if (!switcher->cur) {
					iter->idx = 0;
				}

				len++;

				found = true;
				break;
			}
		}
		if (found) {
			continue;
		}

		wl_list_for_each(iter, &switcher->sapps, link) {
			if (iter->app == app) {
				wl_list_insert(iter->views.prev, &view->switcher_link);

				found = true;
				break;
			}
		}
		if (found) {
			continue;
		}

		struct ptychite_switcher_app *sapp = calloc(1, sizeof(struct ptychite_switcher_app));
		if (!sapp) {
			continue;
		}
		wl_list_insert(switcher->sapps.prev, &sapp->link);

		sapp->app = app;

		wl_list_init(&sapp->views);
		wl_list_insert(&sapp->views, &view->switcher_link);
		view->in_switcher = true;

		len++;
	}

	struct ptychite_switcher_app *iter, *tmp;
	wl_list_for_each_safe(iter, tmp, &old, link) {
		if (iter == switcher->cur) {
			switcher->cur = NULL;
		}
		free(iter);
	}

	if (!len) {
		return;
	}

	if (!switcher->cur) {
		switcher->cur = (len > 1 && !sub_switcher) ? wl_container_of(switcher->sapps.next->next, switcher->cur, link)
												   : wl_container_of(switcher->sapps.next, switcher->cur, link);

		if (sub_switcher) {
			switcher->cur->idx = 1;
		}
	}

	int width = (64 + 30) * len + 60 - 30;
	int height = 64 + 85;
	int x = monitor->window_geometry.x + (monitor->window_geometry.width - width) / 2;
	int y = monitor->window_geometry.y + (monitor->window_geometry.height - height) / 2;

	wlr_scene_node_set_position(&switcher->base.element.scene_tree->node, x, y);

	switcher->base.output = monitor->output;
	ptychite_window_relay_draw(&switcher->base, width, height);
	wlr_scene_node_set_enabled(&switcher->base.element.scene_tree->node, true);

	if (sub_switcher) {
		int app_idx = 0;
		wl_list_for_each(iter, &switcher->sapps, link) {
			if (iter == switcher->cur) {
				break;
			}
			app_idx++;
		}

		int views_l = wl_list_length(&switcher->cur->views);
		if (switcher->cur->idx >= views_l) {
			switcher->cur->idx = 0;
		}

		int sub_width = 64 + 30 + 30 + 40 * 2;
		int sub_font_height = (float)server->compositor->config->panel.font.height * 0.75;
		int sub_height = views_l * sub_font_height + 40;
		/* these are relative to the main switcher */
		int sub_x = (64 + 30) * app_idx - 40;
		int sub_y = height;

		wlr_scene_node_set_position(&switcher->sub_switcher.element.scene_tree->node, sub_x, sub_y);

		switcher->sub_switcher.output = monitor->output;
		ptychite_window_relay_draw(&switcher->sub_switcher, sub_width, sub_height);
		wlr_scene_node_set_enabled(&switcher->sub_switcher.element.scene_tree->node, true);

	} else {
		wlr_scene_node_set_enabled(&switcher->sub_switcher.element.scene_tree->node, false);
	}
}
