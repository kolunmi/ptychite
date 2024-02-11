#include "windows.h"
#include "../config.h"
#include "../compositor.h"
#include "../macros.h"
#include "../draw.h"
#include "../view.h"

static void title_bar_draw(struct ptychite_window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct ptychite_title_bar *title_bar = wl_container_of(window, title_bar, base);

	struct ptychite_server *server = title_bar->view->server;
	struct ptychite_config *config = server->compositor->config;

	float *background =
			title_bar->view->focused ? config->views.border.colors.active : config->views.border.colors.inactive;
	float *foreground = config->panel.colors.foreground;
	float *close = config->views.title_bar.colors.close;

	cairo_set_source_rgba(cairo, background[0], background[1], background[2], background[3]);
	cairo_rectangle(cairo, 0, 0, surface_width, surface_height);
	cairo_fill(cairo);

	struct ptychite_font *font = &config->panel.font;
	int font_height = font->height * scale;

	int x = font_height / 2;
	int y = (surface_height - font_height) / 2;

	cairo_move_to(cairo, x, y);
	cairo_draw_text(
			cairo, font->font, title_bar->view->xdg_toplevel->title, foreground, NULL, scale, false, NULL, NULL);

	title_bar->regions.close.box = (struct wlr_box){
			.x = surface_width - font_height / 2 - font_height,
			.y = 0,
			.width = font_height / 2 + font_height,
			.height = surface_height,
	};

	struct wlr_box close_draw_box = {
			.x = surface_width - font_height / 2 - font_height / 2,
			.y = (surface_height - font_height / 2) / 2,
			.width = font_height / 2,
			.height = font_height / 2,
	};

	cairo_rectangle(cairo, title_bar->regions.close.box.x, title_bar->regions.close.box.y,
			title_bar->regions.close.box.width, title_bar->regions.close.box.height);
	cairo_set_source_rgba(cairo, background[0], background[1], background[2], background[3]);
	cairo_fill(cairo);

	if (title_bar->regions.close.entered) {
		cairo_arc(cairo, close_draw_box.x + close_draw_box.width / 2.0, close_draw_box.y + close_draw_box.height / 2.0,
				close_draw_box.width, 0, PI * 2);
		cairo_set_source_rgba(cairo, close[0], close[1], close[2], close[3]);
		cairo_fill(cairo);
	}

	cairo_move_to(cairo, close_draw_box.x, close_draw_box.y);
	cairo_line_to(cairo, close_draw_box.x + close_draw_box.width, close_draw_box.y + close_draw_box.height);
	cairo_move_to(cairo, close_draw_box.x, close_draw_box.y + close_draw_box.height);
	cairo_line_to(cairo, close_draw_box.x + close_draw_box.width, close_draw_box.y);

	cairo_set_line_width(cairo, font_height / 10.0);
	cairo_set_source_rgba(cairo, foreground[0], foreground[1], foreground[2], foreground[3]);
	cairo_stroke(cairo);
}

static void title_bar_handle_pointer_enter(struct ptychite_window *window) {
}

static void title_bar_handle_pointer_leave(struct ptychite_window *window) {
	struct ptychite_title_bar *title_bar = wl_container_of(window, title_bar, base);

	bool redraw = false;
	redraw |= title_bar->regions.hide.entered;
	title_bar->regions.hide.entered = false;
	redraw |= title_bar->regions.close.entered;
	title_bar->regions.close.entered = false;

	if (redraw) {
		window_relay_draw_same_size(window);
	}
}

static void title_bar_handle_pointer_move(struct ptychite_window *window, double x, double y) {
	struct ptychite_title_bar *title_bar = wl_container_of(window, title_bar, base);

	bool redraw = false;
	redraw |= mouse_region_update_state(&title_bar->regions.hide, x, y);
	redraw |= mouse_region_update_state(&title_bar->regions.close, x, y);

	if (redraw) {
		window_relay_draw_same_size(window);
	}
}

static void title_bar_handle_pointer_button(
		struct ptychite_window *window, double x, double y, struct wlr_pointer_button_event *event) {
	struct ptychite_title_bar *title_bar = wl_container_of(window, title_bar, base);

	if (event->state != WLR_BUTTON_PRESSED) {
		return;
	}

	if (title_bar->regions.close.entered) {
		wlr_xdg_toplevel_send_close(title_bar->view->xdg_toplevel);
		return;
	}

	view_focus(title_bar->view, title_bar->view->xdg_toplevel->base->surface);
	view_begin_interactive(title_bar->view, PTYCHITE_CURSOR_MOVE);
}

static void title_bar_destroy(struct ptychite_window *window) {
	struct ptychite_title_bar *title_bar = wl_container_of(window, title_bar, base);

	free(title_bar);
}

const struct ptychite_window_impl title_bar_window_impl = {
		.draw = title_bar_draw,
		.handle_pointer_enter = title_bar_handle_pointer_enter,
		.handle_pointer_leave = title_bar_handle_pointer_leave,
		.handle_pointer_move = title_bar_handle_pointer_move,
		.handle_pointer_button = title_bar_handle_pointer_button,
		.destroy = title_bar_destroy,
};
