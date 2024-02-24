#include <wayland-util.h>

#include "../compositor.h"
#include "../config.h"
#include "../draw.h"
#include "../icon.h"
#include "../monitor.h"
#include "../server.h"
#include "../windows.h"

static void control_draw(
		struct ptychite_window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct ptychite_control *control = wl_container_of(window, control, base);

	struct wlr_box content_box = {
			.x = 2,
			.y = 2,
			.width = surface_width - 4,
			.height = surface_height - 4,
	};

	struct ptychite_server *server = control->base.server;
	struct ptychite_config *config = server->compositor->config;

	float *accent = config->panel.colors.accent;
	float *foreground = config->panel.colors.foreground;
	float *gray1 = config->panel.colors.gray1;
	float *gray2 = config->panel.colors.gray2;
	float *gray3 = config->panel.colors.gray3;
	float *border = config->panel.colors.border;
	float *separator = config->panel.colors.separator;

	int rect_radius = fmin(content_box.width, content_box.height) / 20;
	ptychite_cairo_draw_rounded_rect(
			cairo, content_box.x, content_box.y, content_box.width, content_box.height, rect_radius);
	cairo_set_source_rgba(cairo, accent[0], accent[1], accent[2], accent[3]);
	cairo_fill_preserve(cairo);
	cairo_set_source_rgba(cairo, border[0], border[1], border[2], border[3]);
	cairo_set_line_width(cairo, 2);
	cairo_stroke(cairo);

	content_box.x += rect_radius;
	content_box.y += rect_radius;
	content_box.width -= 2 * rect_radius;
	content_box.height -= 2 * rect_radius;

	int y = content_box.y;
	struct ptychite_font *font = &config->panel.font;
	int font_height = font->height * scale;

	if (server->control_greeting) {
		int height;
		if (!ptychite_cairo_draw_text_center(cairo, y, content_box.x, content_box.width, NULL, font->font,
					server->control_greeting, gray3, NULL, scale * 0.8, false, NULL, &height)) {
			y += height + rect_radius;
			cairo_move_to(cairo, content_box.x, y);
			cairo_line_to(cairo, content_box.x + content_box.width, y);
			cairo_set_source_rgba(cairo, separator[0], separator[1], separator[2], separator[3]);
			cairo_set_line_width(cairo, 2);
			cairo_stroke(cairo);
			y += rect_radius;
		}
	}

	if (!server->dbus.active || wl_list_empty(&server->notifications.history)) {
		ptychite_cairo_draw_text_center(cairo, content_box.y + (content_box.height - font_height) / 2, content_box.x,
				content_box.width, NULL, font->font, "No Notifications", gray3, NULL, scale * 1.25, false, NULL, NULL);
	} else {
		struct wlr_box notif_box = {
				.x = content_box.x,
				.y = y,
				.width = content_box.width,
				.height = fmax((content_box.height - y + content_box.y) / 5.0 - (10 - 10 / 5.0) * scale,
						font->height * scale * 2.5),
		};

		int x;

		struct ptychite_notification *notif;
		wl_list_for_each(notif, &server->notifications.history, link) {
			if (y + notif_box.height > surface_height) {
				notif->control_regions.region = (struct ptychite_mouse_region){0};
				notif->control_regions.close = (struct ptychite_mouse_region){0};
				continue;
			}

			notif->control_regions.region.box = notif_box;

			ptychite_cairo_draw_rounded_rect(
					cairo, notif_box.x, notif_box.y, notif_box.width, notif_box.height, notif_box.height / 10.0);
			if (notif->control_regions.region.entered) {
				cairo_set_source_rgba(cairo, gray2[0], gray2[1], gray2[2], gray2[3]);
			} else {
				cairo_set_source_rgba(cairo, gray1[0], gray1[1], gray1[2], gray1[3]);
			}
			cairo_fill(cairo);

			x = notif_box.x;
			x += notif_box.height / 10.0;
			y = notif_box.y;
			y += notif_box.height / 10.0;

			if (notif->icon) {
				struct wlr_box box = {
						.x = x,
						.y = y,
						.width = notif_box.height - 2 * (notif_box.height / 10.0),
						.height = notif_box.height - 2 * (notif_box.height / 10.0),
				};
				draw_icon(cairo, notif->icon, box);
				x += box.width + 10 * scale;
			}

			cairo_move_to(cairo, x, y);
			int height;
			if (!ptychite_cairo_draw_text(
						cairo, font->font, notif->summary, foreground, NULL, scale, false, NULL, &height)) {
				y += height + 5 * scale;
			}

			cairo_move_to(cairo, x, y);
			if (!ptychite_cairo_draw_text(
						cairo, font->font, notif->body, foreground, NULL, scale * 0.75, false, NULL, &height)) {
				y += height + 5 * scale;
			}

			struct wlr_box close_draw_box = {
					.x = notif_box.x + notif_box.width - font_height - 2 * scale,
					.y = notif_box.y + font_height / 2.0 + 2 * scale,
					.width = font_height / 2,
					.height = font_height / 2,
			};

			notif->control_regions.close.box = (struct wlr_box){
					.x = close_draw_box.x - font_height / 4,
					.y = close_draw_box.y - font_height / 4,
					.width = close_draw_box.width + font_height / 2,
					.height = close_draw_box.height + font_height / 2,
			};

			ptychite_cairo_draw_x(cairo, close_draw_box, foreground,
					notif->control_regions.close.entered ? gray3 : NULL, font_height / 7);

			notif_box.y += notif_box.height + 10 * scale;
		}
	}
}

static void control_handle_pointer_leave(struct ptychite_window *window) {
	struct ptychite_control *control = wl_container_of(window, control, base);
	struct ptychite_server *server = control->base.server;

	bool redraw = false;

	struct ptychite_notification *notif;
	wl_list_for_each(notif, &server->notifications.history, link) {
		redraw |= notif->control_regions.region.entered;
		notif->control_regions.region.entered = false;

		redraw |= notif->control_regions.close.entered;
		notif->control_regions.close.entered = false;
	}

	if (redraw) {
		ptychite_window_relay_draw_same_size(window);
	}
}

static void control_handle_pointer_move(struct ptychite_window *window, double x, double y) {
	struct ptychite_control *control = wl_container_of(window, control, base);
	struct ptychite_server *server = control->base.server;

	bool redraw = false;

	struct ptychite_notification *notif;
	wl_list_for_each(notif, &server->notifications.history, link) {
		redraw |= ptychite_mouse_region_update_state(&notif->control_regions.region, x, y);
		redraw |= ptychite_mouse_region_update_state(&notif->control_regions.close, x, y);
	}

	if (redraw) {
		ptychite_window_relay_draw_same_size(window);
	}
}

static void control_handle_pointer_button(
		struct ptychite_window *window, double x, double y, struct wlr_pointer_button_event *event) {
	struct ptychite_control *control = wl_container_of(window, control, base);
	struct ptychite_server *server = control->base.server;

	if (event->state != WLR_BUTTON_PRESSED) {
		return;
	}

	bool redraw = false;

	struct ptychite_notification *notif, *tmp;
	wl_list_for_each_safe(notif, tmp, &server->notifications.history, link) {
		if (notif->control_regions.region.entered) {
		}
		if (notif->control_regions.close.entered) {
			ptychite_notification_destroy(notif);
			redraw = true;
		}
	}

	if (redraw) {
		ptychite_window_relay_draw_same_size(window);
		ptychite_server_check_cursor(server);
	}
}

static void control_destroy(struct ptychite_window *window) {
	struct ptychite_control *control = wl_container_of(window, control, base);

	free(control);
}

const struct ptychite_window_impl ptychite_control_window_impl = {
		.draw = control_draw,
		.handle_pointer_enter = NULL,
		.handle_pointer_leave = control_handle_pointer_leave,
		.handle_pointer_move = control_handle_pointer_move,
		.handle_pointer_button = control_handle_pointer_button,
		.destroy = control_destroy,
};

void ptychite_control_draw_auto(struct ptychite_control *control) {
	struct ptychite_monitor *monitor = control->base.server->active_monitor;
	if (!monitor) {
		return;
	}

	struct ptychite_config *config = control->base.server->compositor->config;
	struct ptychite_font *font = &config->panel.font;

	int margin = monitor->window_geometry.height / 60;
	int width = fmin(font->height * 25, monitor->window_geometry.width - margin * 2);
	int height = fmin(font->height * 20, monitor->window_geometry.height - margin * 2);

	wlr_scene_node_set_position(&control->base.element.scene_tree->node,
			monitor->window_geometry.x + (monitor->window_geometry.width - width) / 2,
			monitor->window_geometry.y + margin);

	control->base.output = monitor->output;
	ptychite_window_relay_draw(&control->base, width, height);
}

void ptychite_control_show(struct ptychite_control *control) {
	if (control->base.server->dbus.active) {
		ptychite_server_close_all_notifications(control->base.server, PTYCHITE_NOTIFICATION_CLOSE_EXPIRED);
	}

	ptychite_control_draw_auto(control);
	wlr_scene_node_set_enabled(&control->base.element.scene_tree->node, true);

	struct ptychite_monitor *monitor = control->base.server->active_monitor;
	if (monitor && monitor->panel && monitor->panel->base.element.scene_tree->node.enabled) {
		ptychite_window_relay_draw_same_size(&monitor->panel->base);
	}
}

void ptychite_control_hide(struct ptychite_control *control) {
	wlr_scene_node_set_enabled(&control->base.element.scene_tree->node, false);

	struct ptychite_monitor *monitor = control->base.server->active_monitor;
	if (monitor && monitor->panel && monitor->panel->base.element.scene_tree->node.enabled) {
		ptychite_window_relay_draw_same_size(&monitor->panel->base);
	}
}
