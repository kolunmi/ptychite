#include "draw.h"
#include "macros.h"

void cairo_draw_rounded_rect(cairo_t *cairo, double x, double y, double width, double height, double corner_radius) {
	cairo_new_sub_path(cairo);

	cairo_arc(cairo, x + width - corner_radius, y + corner_radius, corner_radius, -PI / 2, 0);
	cairo_arc(cairo, x + width - corner_radius, y + height - corner_radius, corner_radius, 0, PI / 2);
	cairo_arc(cairo, x + corner_radius, y + height - corner_radius, corner_radius, PI / 2, PI);
	cairo_arc(cairo, x + corner_radius, y + corner_radius, corner_radius, PI, 3 * PI / 2);

	cairo_close_path(cairo);
}

PangoLayout *cairo_get_pango_layout(
		cairo_t *cairo, PangoFontDescription *font, const char *text, double scale, bool markup) {
	PangoLayout *layout = pango_cairo_create_layout(cairo);
	if (!layout) {
		return NULL;
	}

	PangoAttrList *attrs;
	if (markup) {
		char *buf;
		GError *error = NULL;
		if (pango_parse_markup(text, -1, 0, &attrs, &buf, NULL, &error)) {
			pango_layout_set_text(layout, buf, -1);
			free(buf);
		} else {
			g_error_free(error);
			markup = false;
		}
	}
	if (!markup) {
		if (!(attrs = pango_attr_list_new())) {
			g_object_unref(layout);
			return NULL;
		}
		pango_layout_set_text(layout, text, -1);
	}

	pango_attr_list_insert(attrs, pango_attr_scale_new(scale));
	pango_layout_set_font_description(layout, font);
	pango_layout_set_single_paragraph_mode(layout, 1);
	pango_layout_set_attributes(layout, attrs);
	pango_attr_list_unref(attrs);
	return layout;
}

int cairo_draw_text(cairo_t *cairo, PangoFontDescription *font, const char *text, float foreground[4],
		float background[4], double scale, bool markup, int *width, int *height) {
	PangoLayout *layout = cairo_get_pango_layout(cairo, font, text, scale, markup);
	if (!layout) {
		return -1;
	}

	cairo_font_options_t *font_options = cairo_font_options_create();
	if (!font_options) {
		g_object_unref(layout);
		return -1;
	}

	cairo_get_font_options(cairo, font_options);
	pango_cairo_context_set_font_options(pango_layout_get_context(layout), font_options);
	cairo_font_options_destroy(font_options);
	pango_cairo_update_layout(cairo, layout);

	double x, y;
	cairo_get_current_point(cairo, &x, &y);

	if (background || width || height) {
		int w, h;
		pango_layout_get_pixel_size(layout, &w, &h);
		if (background) {
			cairo_set_source_rgba(cairo, background[0], background[1], background[2], background[3]);
			cairo_draw_rounded_rect(cairo, x - h / 2.0, y, w + h, h, h / 2.0);
			cairo_fill(cairo);
			cairo_move_to(cairo, x, y);
		}
		if (width) {
			*width = w;
		}
		if (height) {
			*height = h;
		}
	}

	cairo_set_source_rgba(cairo, foreground[0], foreground[1], foreground[2], foreground[3]);
	pango_cairo_show_layout(cairo, layout);

	g_object_unref(layout);
	return 0;
}

int cairo_get_text_size(cairo_t *cairo, PangoFontDescription *font, const char *text, double scale, bool markup,
		int *width, int *height) {
	PangoLayout *layout = cairo_get_pango_layout(cairo, font, text, scale, markup);
	if (!layout) {
		return -1;
	}

	pango_cairo_update_layout(cairo, layout);

	int w, h;
	pango_layout_get_pixel_size(layout, &w, &h);
	if (width) {
		*width = w;
	}
	if (height) {
		*height = h;
	}

	g_object_unref(layout);
	return 0;
}

int cairo_draw_text_center(cairo_t *cairo, int y, int geom_x, int geom_width, int *x_out, PangoFontDescription *font,
		const char *text, float foreground[4], float background[4], double scale, bool markup, int *width,
		int *height) {
	int w, h;
	if (!cairo_get_text_size(cairo, font, text, scale, markup, &w, &h)) {
		if (width) {
			*width = w;
		}
		if (height) {
			*height = h;
		}

		int x = geom_x + (geom_width - w) / 2;
		cairo_move_to(cairo, x, y);
		if (x_out) {
			*x_out = x;
		}
		return cairo_draw_text(cairo, font, text, foreground, background, scale, markup, NULL, NULL);
	}

	return -1;
}

int cairo_draw_text_right(cairo_t *cairo, int y, int right_x, int *x_out, PangoFontDescription *font, const char *text,
		float foreground[4], float background[4], double scale, bool markup, int *width, int *height) {
	int w, h;
	if (!cairo_get_text_size(cairo, font, text, scale, markup, &w, &h)) {
		if (width) {
			*width = w;
		}
		if (height) {
			*height = h;
		}

		int x = right_x - w;
		cairo_move_to(cairo, x, y);
		if (x_out) {
			*x_out = x;
		}
		return cairo_draw_text(cairo, font, text, foreground, background, scale, markup, NULL, NULL);
	}

	return -1;
}
