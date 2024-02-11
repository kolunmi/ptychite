#ifndef PTYCHITE_BUFFER_H
#define PTYCHITE_BUFFER_H

#include <cairo.h>
#include <wlr/interfaces/wlr_buffer.h>

struct ptychite_buffer {
	struct wlr_buffer base;
	cairo_surface_t *surface;
	cairo_t *cairo;
};

extern const struct wlr_buffer_impl ptychite_buffer_buffer_impl;

#endif
