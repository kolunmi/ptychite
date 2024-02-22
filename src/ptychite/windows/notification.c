#include <wlr/util/box.h>

#include "../applications.h"
#include "../compositor.h"
#include "../config.h"
#include "../draw.h"
#include "../icon.h"
#include "../monitor.h"
#include "../server.h"
#include "../windows.h"
#include "src/ptychite/notification.h"

static void notification_draw(
		struct ptychite_window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct ptychite_notification *notif = wl_container_of(window, notif, base);
	struct ptychite_server *server = notif->base.server;
	struct ptychite_config *config = server->compositor->config;

	float *accent = config->panel.colors.accent;
	float *border = config->panel.colors.border;
	float *foreground = config->panel.colors.foreground;
	struct ptychite_font *font = &config->panel.font;

	ptychite_cairo_draw_rounded_rect(
			cairo, 2 * scale, 2 * scale, surface_width - 4 * scale, surface_height - 4 * scale, 10 * scale);
	cairo_set_source_rgba(cairo, accent[0], accent[1], accent[2], accent[3]);
	cairo_fill_preserve(cairo);
	cairo_set_source_rgba(cairo, border[0], border[1], border[2], border[3]);
	cairo_set_line_width(cairo, 2 * scale);
	cairo_stroke(cairo);

	int font_height = font->height * scale;
	int x = font_height / 2.0 + 2 * scale;
	int y = x;

	if (notif->icon) {
		struct wlr_box box = {
				.x = x,
				.y = y,
				.width = 32 * scale,
				.height = 32 * scale,
		};

		draw_icon(cairo, notif->icon, box);
		x += box.width + 10;
	}

	cairo_move_to(cairo, x, y);

	int height;
	if (!ptychite_cairo_draw_text(cairo, font->font, notif->summary, foreground, NULL, scale, false, NULL, &height)) {
		y += height * 1.5;
	}

	x = font_height / 2.0 + 2 * scale;
	cairo_move_to(cairo, x, y);

	if (!ptychite_cairo_draw_text(
				cairo, font->font, notif->body, foreground, NULL, scale * 0.75, false, NULL, &height)) {
		y += height + 1.2;
	}

	struct wlr_box close_draw_box = {
			.x = surface_width - font_height - 2 * scale,
			.y = font_height / 2.0 + 2 * scale,
			.width = font_height / 2,
			.height = font_height / 2,
	};

	notif->regions.close.box.x = close_draw_box.x - font_height / 4;
	notif->regions.close.box.y = 0;
	notif->regions.close.box.width = surface_width - notif->regions.close.box.x;
	notif->regions.close.box.height = close_draw_box.y + close_draw_box.height + font_height / 4;

	ptychite_cairo_draw_x(
			cairo, close_draw_box, foreground, notif->regions.close.entered ? border : NULL, font_height / 7);
}

static void notification_handle_pointer_leave(struct ptychite_window *window) {
	struct ptychite_notification *notif = wl_container_of(window, notif, base);

	bool redraw = false;
	redraw |= notif->regions.close.entered;
	notif->regions.close.entered = false;

	if (redraw) {
		ptychite_window_relay_draw_same_size(window);
	}
}

static void notification_handle_pointer_move(struct ptychite_window *window, double x, double y) {
	struct ptychite_notification *notif = wl_container_of(window, notif, base);

	bool redraw = false;
	redraw |= ptychite_mouse_region_update_state(&notif->regions.close, x, y);

	if (redraw) {
		ptychite_window_relay_draw_same_size(window);
	}
}

static void notification_handle_pointer_button(
		struct ptychite_window *window, double x, double y, struct wlr_pointer_button_event *event) {
	struct ptychite_notification *notif = wl_container_of(window, notif, base);
	struct ptychite_server *server = notif->server;

	if (event->state != WLR_BUTTON_PRESSED) {
		return;
	}

	if (notif->regions.close.entered) {
		ptychite_notification_close(notif, PTYCHITE_NOTIFICATION_CLOSE_REQUEST, false);
		ptychite_server_arrange_notifications(server);
	}
}

const struct ptychite_window_impl ptychite_notification_window_impl = {
		.draw = notification_draw,
		.handle_pointer_enter = NULL,
		.handle_pointer_leave = notification_handle_pointer_leave,
		.handle_pointer_move = notification_handle_pointer_move,
		.handle_pointer_button = notification_handle_pointer_button,
		.destroy = NULL,
};

void ptychite_notification_draw_auto(struct ptychite_notification *notif) {
	if (notif->server->active_monitor) {
		notif->base.output = notif->server->active_monitor->output;
	}

	ptychite_window_relay_draw(&notif->base, 300, 100);
}
