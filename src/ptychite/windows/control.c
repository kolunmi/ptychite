#include "windows.h"
#include "../config.h"
#include "../compositor.h"
#include "../draw.h"
#include "../server.h"
#include "../monitor.h"

static void control_draw(struct ptychite_window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct ptychite_control *control = wl_container_of(window, control, base);

	struct wlr_box box = {
			.x = 2,
			.y = 2,
			.width = surface_width - 4,
			.height = surface_height - 4,
	};

	struct ptychite_server *server = control->base.server;
	struct ptychite_config *config = server->compositor->config;

	float *accent = config->panel.colors.accent;
	float *gray1 = config->panel.colors.gray1;
	float *gray2 = config->panel.colors.gray2;
	float *border = config->panel.colors.border;
	float *seperator = config->panel.colors.seperator;

	int rect_radius = fmin(box.width, box.height) / 20;
	ptychite_cairo_draw_rounded_rect(cairo, box.x, box.y, box.width, box.height, rect_radius);
	cairo_set_source_rgba(cairo, accent[0], accent[1], accent[2], accent[3]);
	cairo_fill_preserve(cairo);
	cairo_set_source_rgba(cairo, border[0], border[1], border[2], border[3]);
	cairo_set_line_width(cairo, 2);
	cairo_stroke(cairo);

	box.x += rect_radius;
	box.y += rect_radius;
	box.width -= 2 * rect_radius;
	box.height -= 2 * rect_radius;

	int y = box.y;
	struct ptychite_font *font = &config->panel.font;

	if (server->control_greeting) {
		int height;
		if (!ptychite_cairo_draw_text_center(cairo, y, box.x, box.width, NULL, font->font, server->control_greeting, gray2, NULL,
					scale * 0.8, false, NULL, &height)) {
			y += height + rect_radius;
			cairo_move_to(cairo, box.x, y);
			cairo_line_to(cairo, box.x + box.width, y);
			cairo_set_source_rgba(cairo, seperator[0], seperator[1], seperator[2], seperator[3]);
			cairo_set_line_width(cairo, 2);
			cairo_stroke(cairo);
			y += rect_radius;
		}
	}

	int font_height = font->height * scale;
	size_t i;
	for (i = 0; i < 4; i++) {
		ptychite_cairo_draw_rounded_rect(cairo, box.x, y, box.width, font_height * 3, font_height);
		cairo_set_source_rgba(cairo, gray1[0], gray1[1], gray1[2], gray1[3]);
		cairo_fill(cairo);
		y += font_height * 3 + rect_radius;
	}
}

static void control_handle_pointer_move(struct ptychite_window *window, double x, double y) {
}

static void control_handle_pointer_button(
		struct ptychite_window *window, double x, double y, struct wlr_pointer_button_event *event) {
}

static void control_destroy(struct ptychite_window *window) {
	struct ptychite_control *control = wl_container_of(window, control, base);

	free(control);
}

const struct ptychite_window_impl ptychite_control_window_impl = {
		.draw = control_draw,
		.handle_pointer_enter = NULL,
		.handle_pointer_leave = NULL,
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
