#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <fnmatch.h>
#include <glob.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <cairo/cairo.h>

#include "notification.h"
#include "icon.h"
#include "util.h"
#include "windows.h"
#include "server.h"
#include "monitor.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

static cairo_surface_t *create_cairo_surface_from_gdk_pixbuf(const GdkPixbuf *gdkbuf) {
	int chan = gdk_pixbuf_get_n_channels(gdkbuf);
	if (chan < 3) {
		return NULL;
	}

	const guint8* gdkpix = gdk_pixbuf_read_pixels(gdkbuf);
	if (!gdkpix) {
		return NULL;
	}
	gint w = gdk_pixbuf_get_width(gdkbuf);
	gint h = gdk_pixbuf_get_height(gdkbuf);
	int stride = gdk_pixbuf_get_rowstride(gdkbuf);

	cairo_format_t fmt = (chan == 3) ? CAIRO_FORMAT_RGB24 : CAIRO_FORMAT_ARGB32;
	cairo_surface_t * cs = cairo_image_surface_create(fmt, w, h);
	cairo_surface_flush(cs);
	if (!cs || cairo_surface_status(cs) != CAIRO_STATUS_SUCCESS) {
		return NULL;
	}

	int cstride = cairo_image_surface_get_stride(cs);
	unsigned char *cpix = cairo_image_surface_get_data(cs);

	if (chan == 3) {
		for (int i = h; i; --i) {
			const guint8 *gp = gdkpix;
			unsigned char *cp = cpix;
			const guint8* end = gp + 3*w;
			while (gp < end) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
				cp[0] = gp[2];
				cp[1] = gp[1];
				cp[2] = gp[0];
#else
				cp[1] = gp[0];
				cp[2] = gp[1];
				cp[3] = gp[2];
#endif
				gp += 3;
				cp += 4;
			}
			gdkpix += stride;
			cpix += cstride;
		}
	} else {
		/* premul-color = alpha/255 * color/255 * 255 = (alpha*color)/255
		 * (z/255) = z/256 * 256/255     = z/256 (1 + 1/255)
		 *         = z/256 + (z/256)/255 = (z + z/255)/256
		 *         # recurse once
		 *         = (z + (z + z/255)/256)/256
		 *         = (z + z/256 + z/256/255) / 256
		 *         # only use 16bit uint operations, loose some precision,
		 *         # result is floored.
		 *       ->  (z + z>>8)>>8
		 *         # add 0x80/255 = 0.5 to convert floor to round
		 *       =>  (z+0x80 + (z+0x80)>>8 ) >> 8
		 * ------
		 * tested as equal to lround(z/255.0) for uint z in [0..0xfe02]
		 */
#define PREMUL_ALPHA(x,a,b,z) { z = a * b + 0x80; x = (z + (z >> 8)) >> 8; }
		for (int i = h; i; --i) {
			const guint8 *gp = gdkpix;
			unsigned char *cp = cpix;
			const guint8* end = gp + 4*w;
			guint z1, z2, z3;
			while (gp < end) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
				PREMUL_ALPHA(cp[0], gp[2], gp[3], z1);
				PREMUL_ALPHA(cp[1], gp[1], gp[3], z2);
				PREMUL_ALPHA(cp[2], gp[0], gp[3], z3);
				cp[3] = gp[3];
#else
				PREMUL_ALPHA(cp[1], gp[0], gp[3], z1);
				PREMUL_ALPHA(cp[2], gp[1], gp[3], z2);
				PREMUL_ALPHA(cp[3], gp[2], gp[3], z3);
				cp[0] = gp[3];
#endif
				gp += 4;
				cp += 4;
			}
			gdkpix += stride;
			cpix += cstride;
		}
#undef PREMUL_ALPHA
	}
	cairo_surface_mark_dirty(cs);
	return cs;
}

static bool validate_icon_name(const char* icon_name) {
	int icon_len = strlen(icon_name);
	if (icon_len > 1024) {
		return false;
	}
	int index;
	for (index = 0; index < icon_len; index ++) {
		bool is_number = icon_name[index] >= '0' && icon_name[index] <= '9';
		bool is_abc = (icon_name[index] >= 'A' && icon_name[index] <= 'Z') ||
				(icon_name[index] >= 'a' && icon_name[index] <= 'z');
		bool is_other = icon_name[index] == '-'
				|| icon_name[index] == '.' || icon_name[index] == '_';

		bool is_legal = is_number || is_abc || is_other;
		if (!is_legal) {
			return false;
		}
	}
	return true;
}

static GdkPixbuf *load_image(const char *path) {
	if (strlen(path) == 0) {
		return NULL;
	}
	GError *err = NULL;
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &err);
	if (!pixbuf) {
		fprintf(stderr, "Failed to load icon (%s)\n", err->message);
		g_error_free(err);
		return NULL;
	}
	return pixbuf;
}

static GdkPixbuf *load_image_data(struct ptychite_image_data *image_data) {
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(image_data->data, GDK_COLORSPACE_RGB,
			image_data->has_alpha, image_data->bits_per_sample, image_data->width,
			image_data->height, image_data->rowstride, NULL, NULL);
	if (!pixbuf) {
		fprintf(stderr, "Failed to load icon\n");
		return NULL;
	}
	return pixbuf;
}

static double fit_to_square(int width, int height, int square_size) {
	double longest = width > height ? width : height;
	return longest > square_size ? square_size/longest : 1.0;
}

static char hex_val(char digit) {
	assert(isxdigit(digit));
	if (digit >= 'a') {
		return digit - 'a' + 10;
	} else if (digit >= 'A') {
		return digit - 'A' + 10;
	} else {
		return digit - '0';
	}
}

static void url_decode(char *dst, const char *src) {
	while (src[0]) {
		if (src[0] == '%' && isxdigit(src[1]) && isxdigit(src[2])) {
			dst[0] = 16*hex_val(src[1]) + hex_val(src[2]);
			dst++; src += 3;
		} else {
			dst[0] = src[0];
			dst++; src++;
		}
	}
	dst[0] = '\0';
}

// Attempt to find a full path for a notification's icon_name, which may be:
// - An absolute path, which will simply be returned (as a new string)
// - A file:// URI, which will be converted to an absolute path
// - A Freedesktop icon name, which will be resolved within the configured
//   `icon-path` using something that looks vaguely like the algorithm defined
//   in the icon theme spec (https://standards.freedesktop.org/icon-theme-spec/icon-theme-spec-latest.html)
//
// Returns the resolved path, or NULL if it was unable to find an icon. The
// return value must be freed by the caller.
static char *resolve_icon(struct ptychite_notification *notif) {
	char *icon_name = notif->app_icon;
	if (icon_name[0] == '\0') {
		return NULL;
	}
	if (icon_name[0] == '/') {
		return strdup(icon_name);
	}
	if (strstr(icon_name, "file://") == icon_name) {
		// Chop off the scheme and URL decode
		char *icon_path = malloc(strlen(icon_name) + 1 - strlen("file://"));
		if (icon_path == NULL) {
			return icon_path;
		}

		url_decode(icon_path, icon_name + strlen("file://"));
		return icon_path;
	}

	// Determine the largest scale factor of any attached output.
	int32_t max_scale = 1;
	struct ptychite_monitor *monitor = NULL;
	wl_list_for_each(monitor, &notif->server->monitors, link) {
		if (monitor->output->scale > max_scale) {
			max_scale = monitor->output->scale;
		}
	}

	static const char fallback[] = "/usr/share/icons/hicolor";
	char *search = strdup(fallback);

	char *saveptr = NULL;
	char *theme_path = strtok_r(search, ":", &saveptr);

	// Match all icon files underneath of the theme_path followed by any icon
	// size and category subdirectories. This pattern assumes that all the
	// files in the icon path are valid icon types.
	static const char pattern_fmt[] = "%s/*/*/%s.*";

	char *icon_path = NULL;
	int32_t last_icon_size = 0;

	if (!validate_icon_name(icon_name)) {
		return NULL;
	}

	while (theme_path) {
		if (strlen(theme_path) == 0) {
			continue;
		}

		glob_t icon_glob = {0};
		char *pattern = ptychite_asprintf(pattern_fmt, theme_path, icon_name);

		// Disable sorting because we're going to do our own anyway.
		int found = glob(pattern, GLOB_NOSORT, NULL, &icon_glob);
		size_t found_count = 0;
		if (found == 0) {
			// The value of gl_pathc isn't guaranteed to be usable if glob
			// returns non-zero.
			found_count = icon_glob.gl_pathc;
		}

		for (size_t i = 0; i < found_count; ++i) {
			char *relative_path = icon_glob.gl_pathv[i];

			// Find the end of the current search path and walk to the next
			// path component. Hopefully this will be the icon resolution
			// subdirectory.
			relative_path += strlen(theme_path);
			while (relative_path[0] == '/') {
				++relative_path;
			}

			errno = 0;
			int32_t icon_size = strtol(relative_path, NULL, 10);
			if (errno || icon_size == 0) {
				// Try second level subdirectory if failed.
				errno = 0;
				while (relative_path[0] != '/') {
					++relative_path;
				}
				++relative_path;
				icon_size = strtol(relative_path, NULL, 10);
				if (errno || icon_size == 0) {
					continue;
				}
			}

			int32_t icon_scale = 1;
			char *scale_str = strchr(relative_path, '@');
			if (scale_str != NULL) {
				icon_scale = strtol(scale_str + 1, NULL, 10);
			}

			if (icon_size == 64 &&
					icon_scale == max_scale) {
				// If we find an exact match, we're done.
				free(icon_path);
				icon_path = strdup(icon_glob.gl_pathv[i]);
				break;
			} else if (icon_size < 64 * max_scale &&
					icon_size > last_icon_size) {
				// Otherwise, if this icon is small enough to fit but bigger
				// than the last best match, choose it on a provisional basis.
				// We multiply by max_scale to increase the odds of finding an
				// icon which looks sharp on the highest-scale output.
				free(icon_path);
				icon_path = strdup(icon_glob.gl_pathv[i]);
				last_icon_size = icon_size;
			}
		}

		free(pattern);
		globfree(&icon_glob);

		if (icon_path) {
			// The spec says that if we find any match whatsoever in a theme,
			// we should stop there to avoid mixing icons from different
			// themes even if one is a better size.
			break;
		}
		theme_path = strtok_r(NULL, ":", &saveptr);
	}

	if (icon_path == NULL) {
		// Finally, fall back to looking in /usr/share/pixmaps. These are
		// unsized icons, which may lead to downscaling, but some apps are
		// still using it.
		static const char pixmaps_fmt[] = "/usr/share/pixmaps/%s.*";

		char *pattern = ptychite_asprintf(pixmaps_fmt, icon_name);

		glob_t icon_glob = {0};
		int found = glob(pattern, GLOB_NOSORT, NULL, &icon_glob);

		if (found == 0 && icon_glob.gl_pathc > 0) {
			icon_path = strdup(icon_glob.gl_pathv[0]);
		}
		free(pattern);
		globfree(&icon_glob);
	}

	free(search);
	return icon_path;
}

struct ptychite_icon *create_icon(struct ptychite_notification *notif) {
	GdkPixbuf *image = NULL;
	if (notif->image_data != NULL) {
		image = load_image_data(notif->image_data);
	}

	if (image == NULL) {
		char *path = resolve_icon(notif);
		if (path == NULL) {
			return NULL;
		}

		image = load_image(path);
		free(path);
		if (image == NULL) {
			return NULL;
		}
	}

	int image_width = gdk_pixbuf_get_width(image);
	int image_height = gdk_pixbuf_get_height(image);

	struct ptychite_icon *icon = calloc(1, sizeof(struct ptychite_icon));
	icon->scale = fit_to_square(
			image_width, image_height, 64);
	icon->width = image_width * icon->scale;
	icon->height = image_height * icon->scale;

	icon->image = create_cairo_surface_from_gdk_pixbuf(image);
	g_object_unref(image);
	if (icon->image == NULL) {
		free(icon);
		return NULL;
	}

	return icon;
}

void draw_icon(cairo_t *cairo, struct ptychite_icon *icon,
		double xpos, double ypos, double scale) {
	cairo_save(cairo);
	cairo_scale(cairo, scale*icon->scale, scale*icon->scale);
	cairo_set_source_surface(cairo, icon->image, xpos/icon->scale, ypos/icon->scale);
	cairo_paint(cairo);
	cairo_restore(cairo);
}

void destroy_icon(struct ptychite_icon *icon) {
	if (icon != NULL) {
		if (icon->image != NULL) {
			cairo_surface_destroy(icon->image);
		}
		free(icon);
	}
}

