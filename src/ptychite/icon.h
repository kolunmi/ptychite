#ifndef PTYCHITE_ICON_H
#define PTYCHITE_ICON_H

#include <cairo.h>
#include <stdint.h>
#include <wlr/util/box.h>

struct ptychite_server;
struct ptychite_notification;

struct ptychite_icon {
	double width;
	double height;
	double scale;
	cairo_surface_t *image;
};

struct ptychite_image_data {
	int32_t width;
	int32_t height;
	int32_t rowstride;
	uint32_t has_alpha;
	int32_t bits_per_sample;
	int32_t channels;
	uint8_t *data;
};

struct ptychite_icon *ptychite_icon_create(struct ptychite_server *server, char *name, char **path_out);
struct ptychite_icon *ptychite_icon_create_for_notification(struct ptychite_notification *notif);

void destroy_icon(struct ptychite_icon *icon);
void draw_icon(cairo_t *cairo, struct ptychite_icon *icon, struct wlr_box box);

#endif
