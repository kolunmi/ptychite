#include "windows.h"
#include "../config.h"
#include "../compositor.h"
#include "../monitor.h"
#include "../server.h"

static void wallpaper_draw(struct ptychite_window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct ptychite_panel *wallpaper = wl_container_of(window, wallpaper, base);
	struct ptychite_config *config = wallpaper->monitor->server->compositor->config;

	if (!config->monitors.wallpaper.surface) {
		cairo_set_source_rgba(cairo, 0.2, 0.2, 0.3, 1.0);
		cairo_rectangle(cairo, 0, 0, surface_width, surface_height);
		cairo_fill(cairo);
		return;
	}

	cairo_surface_t *image_surface = config->monitors.wallpaper.surface;
	double image_width = cairo_image_surface_get_width(image_surface);
	double image_height = cairo_image_surface_get_height(image_surface);

	switch (config->monitors.wallpaper.mode) {
	case PTYCHITE_WALLPAPER_FIT: {
		cairo_rectangle(cairo, 0, 0, surface_width, surface_height);
		cairo_clip(cairo);
		double width_ratio = (double)surface_width / image_width;
		if (width_ratio * image_height >= surface_height) {
			cairo_scale(cairo, width_ratio, width_ratio);
		} else {
			double height_ratio = (double)surface_height / image_height;
			cairo_translate(cairo, -(image_width * height_ratio - (double)surface_width) / 2, 0);
			cairo_scale(cairo, height_ratio, height_ratio);
		}
		break;
	}
	case PTYCHITE_WALLPAPER_STRETCH:
		cairo_scale(cairo, (double)surface_width / image_width, (double)surface_height / image_height);
		break;
	}

	cairo_set_source_surface(cairo, image_surface, 0, 0);
	cairo_paint(cairo);
	cairo_restore(cairo);
}

static void wallpaper_destroy(struct ptychite_window *window) {
	struct ptychite_wallpaper *wallpaper = wl_container_of(window, wallpaper, base);

	free(wallpaper);
}

const struct ptychite_window_impl ptychite_wallpaper_window_impl = {
		.draw = wallpaper_draw,
		.handle_pointer_enter = NULL,
		.handle_pointer_leave = NULL,
		.handle_pointer_move = NULL,
		.handle_pointer_button = NULL,
		.destroy = wallpaper_destroy,
};

void ptychite_wallpaper_draw_auto(struct ptychite_wallpaper *wallpaper) {
	ptychite_window_relay_draw(&wallpaper->base, wallpaper->monitor->geometry.width, wallpaper->monitor->geometry.height);
}
