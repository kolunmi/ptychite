#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <assert.h>
#include <pango/pangocairo.h>

#include "windows.h"
#include "../buffer.h"
#include "../server.h"

struct ptychite_window *element_get_window(struct ptychite_element *element) {
	assert(element->type == PTYCHITE_ELEMENT_WINDOW);

	struct ptychite_window *window = wl_container_of(element, window, element);

	return window;
}

static void window_handle_destroy(struct wl_listener *listener, void *data) {
	struct ptychite_window *window = wl_container_of(listener, window, destroy);

	wl_list_remove(&window->destroy.link);

	if (window->server->hovered_window == window) {
		window->server->hovered_window = NULL;
	}

	window->impl->destroy(window);
}

int window_init(struct ptychite_window *window, struct ptychite_server *server, const struct ptychite_window_impl *impl,
		struct wlr_scene_tree *parent, struct wlr_output *output) {
	if (!(window->element.scene_tree = wlr_scene_tree_create(parent))) {
		return -1;
	}

	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_create(window->element.scene_tree, NULL);
	if (!scene_buffer) {
		wlr_scene_node_destroy(&window->element.scene_tree->node);
		return -1;
	}

	window->element.type = PTYCHITE_ELEMENT_WINDOW;
	window->element.scene_tree->node.data = &window->element;
	window->scene_buffer = scene_buffer;
	window->server = server;
	window->impl = impl;
	window->output = output;

	window->destroy.notify = window_handle_destroy;
	wl_signal_add(&scene_buffer->node.events.destroy, &window->destroy);

	return 0;
}

int window_relay_draw(struct ptychite_window *window, int width, int height) {
	if (!window->impl || !window->impl->draw) {
		return -1;
	}

	float scale;
	if (window->output) {
		scale = window->output->scale;
	} else {
		scale = 1.0;
	}
	int scaled_width = ceil(width * scale);
	int scaled_height = ceil(height * scale);

	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, scaled_width, scaled_height);
	if (!surface) {
		return -1;
	}
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		goto err_create_cairo;
	}

	cairo_t *cairo = cairo_create(surface);
	if (!cairo) {
		goto err_create_cairo;
	}
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);

	cairo_font_options_t *font_options = cairo_font_options_create();
	if (!font_options) {
		goto err_create_font_options;
	}
	cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_FULL);
	cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_GRAY);
	cairo_set_font_options(cairo, font_options);

	PangoContext *pango = pango_cairo_create_context(cairo);
	if (!pango) {
		goto err_create_pango;
	}

	struct ptychite_buffer *buffer = calloc(1, sizeof(struct ptychite_buffer));
	if (!buffer) {
		goto err_rest;
	}

	window->element.width = width;
	window->element.height = height;
	window->impl->draw(window, cairo, scaled_width, scaled_height, scale);
	cairo_surface_flush(surface);

	buffer->cairo = cairo;
	buffer->surface = surface;
	wlr_buffer_init(&buffer->base, &buffer_buffer_impl, scaled_width, scaled_height);

	wlr_scene_buffer_set_dest_size(window->scene_buffer, width, height);
	wlr_scene_buffer_set_buffer(window->scene_buffer, &buffer->base);
	wlr_buffer_drop(&buffer->base);

	return 0;

err_rest:
	g_object_unref(pango);
err_create_pango:
	cairo_font_options_destroy(font_options);
err_create_font_options:
	cairo_destroy(cairo);
err_create_cairo:
	cairo_surface_destroy(surface);
	return -1;
}

void window_relay_draw_same_size(struct ptychite_window *window) {
	window_relay_draw(window, window->element.width, window->element.height);
}

void window_relay_pointer_enter(struct ptychite_window *window) {
	if (!window->impl || !window->impl->handle_pointer_enter) {
		return;
	}

	window->impl->handle_pointer_enter(window);
}

void window_relay_pointer_leave(struct ptychite_window *window) {
	if (!window->impl || !window->impl->handle_pointer_leave) {
		return;
	}

	window->impl->handle_pointer_leave(window);
}

void window_relay_pointer_move(struct ptychite_window *window, double x, double y) {
	if (!window->impl || !window->impl->handle_pointer_move) {
		return;
	}

	float scale = window->output->scale;
	double scale_x = scale * x;
	double scale_y = scale * y;

	window->impl->handle_pointer_move(window, scale_x, scale_y);
}

void window_relay_pointer_button(
		struct ptychite_window *window, double x, double y, struct wlr_pointer_button_event *event) {
	if (!window->impl || !window->impl->handle_pointer_button) {
		return;
	}

	float scale = window->output->scale;
	double scale_x = scale * x;
	double scale_y = scale * y;

	window->impl->handle_pointer_button(window, scale_x, scale_y, event);
}
