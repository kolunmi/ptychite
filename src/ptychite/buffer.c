#include <stdlib.h>
#include <drm_fourcc.h>

#include "buffer.h"

static void buffer_destroy(struct wlr_buffer *buffer) {
	struct ptychite_buffer *p_buffer = wl_container_of(buffer, p_buffer, base);

	cairo_surface_destroy(p_buffer->surface);
	cairo_destroy(p_buffer->cairo);
	free(p_buffer);
}

static bool buffer_begin_data_ptr_access(
		struct wlr_buffer *buffer, uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct ptychite_buffer *p_buffer = wl_container_of(buffer, p_buffer, base);

	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) {
		return false;
	}

	*data = cairo_image_surface_get_data(p_buffer->surface);
	*stride = cairo_image_surface_get_stride(p_buffer->surface);
	*format = DRM_FORMAT_ARGB8888;

	return true;
}

static void buffer_end_data_ptr_access(struct wlr_buffer *buffer) {
}

const struct wlr_buffer_impl ptychite_buffer_buffer_impl = {
		.destroy = buffer_destroy,
		.begin_data_ptr_access = buffer_begin_data_ptr_access,
		.end_data_ptr_access = buffer_end_data_ptr_access,
};
