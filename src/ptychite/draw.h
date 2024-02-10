#ifndef PTYCHITE_DRAW_H
#define PTYCHITE_DRAW_H

#include <cairo.h>
#include <pango/pangocairo.h>
#include <stdbool.h>

void cairo_draw_rounded_rect(cairo_t *cairo, double x, double y, double width, double height, double corner_radius);

PangoLayout *cairo_get_pango_layout(
		cairo_t *cairo, PangoFontDescription *font, const char *text, double scale, bool markup);

int cairo_draw_text(cairo_t *cairo, PangoFontDescription *font, const char *text, float foreground[4],
		float background[4], double scale, bool markup, int *width, int *height);

int cairo_get_text_size(cairo_t *cairo, PangoFontDescription *font, const char *text, double scale, bool markup,
		int *width, int *height);

int cairo_draw_text_center(cairo_t *cairo, int y, int geom_x, int geom_width, int *x_out, PangoFontDescription *font,
		const char *text, float foreground[4], float background[4], double scale, bool markup, int *width, int *height);

int cairo_draw_text_right(cairo_t *cairo, int y, int right_x, int *x_out, PangoFontDescription *font, const char *text,
		float foreground[4], float background[4], double scale, bool markup, int *width, int *height);

#endif
