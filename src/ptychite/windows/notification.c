#include "../applications.h"
#include "../compositor.h"
#include "../config.h"
#include "../draw.h"
#include "../icon.h"
#include "../monitor.h"
#include "../server.h"
#include "../windows.h"

static void notification_draw(
		struct ptychite_window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct ptychite_notification *notif = wl_container_of(window, notif, base);
	struct ptychite_server *server = notif->base.server;
	struct ptychite_config *config = server->compositor->config;

	float *accent = config->panel.colors.accent;
	float *border = config->panel.colors.border;
	float *foreground = config->panel.colors.foreground;
	struct ptychite_font *font = &config->panel.font;

	ptychite_cairo_draw_rounded_rect(cairo, 10, 10, surface_width - 20, surface_height - 20, 10 * scale);
	cairo_set_source_rgba(cairo, accent[0], accent[1], accent[2], accent[3]);
	cairo_fill_preserve(cairo);
	cairo_set_source_rgba(cairo, border[0], border[1], border[2], border[3]);
	cairo_set_line_width(cairo, 2);
	cairo_stroke(cairo);

	int x = 15 * scale;
	int y = 15 * scale;

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
		y += height + 5;
	}

	x = 15 * scale;
	cairo_move_to(cairo, x, y);

	if (!ptychite_cairo_draw_text(
				cairo, font->font, notif->body, foreground, NULL, scale * 0.75, false, NULL, &height)) {
		y += height + 5;
	}
}

const struct ptychite_window_impl ptychite_notification_window_impl = {
		.draw = notification_draw,
		.handle_pointer_enter = NULL,
		.handle_pointer_leave = NULL,
		.handle_pointer_move = NULL,
		.handle_pointer_button = NULL,
		.destroy = NULL,
};

void ptychite_notification_draw_auto(struct ptychite_notification *notif) {
	if (notif->server->active_monitor) {
		notif->base.output = notif->server->active_monitor->output;
	}

	ptychite_window_relay_draw(&notif->base, 300, 100);
}
