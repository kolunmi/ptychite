#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <cairo.h>
#include <drm_fourcc.h>
#include <librsvg/rsvg.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "compositor.h"
#include "config.h"
#include "json.h"
#include "macros.h"
#include "ptychite-message-unstable-v1-protocol.h"
#include "server.h"
#include "util.h"

enum cursor_mode {
	CURSOR_PASSTHROUGH,
	CURSOR_MOVE,
	CURSOR_RESIZE,
};

enum element_type {
	ELEMENT_VIEW,
	ELEMENT_WINDOW,
};

typedef int (*ptychite_server_for_each_layer_func_t)(
		struct ptychite_server *server, struct wlr_scene_tree **layer);

typedef void (*ptychite_server_reverse_for_each_layer_func_t)(
		struct ptychite_server *server, struct wlr_scene_tree **layer);

struct ptychite_server {
	struct ptychite_compositor *compositor;
	bool terminated;

	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_session *session;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;

	struct wlr_scene *scene;
	struct {
		struct wlr_scene_tree *background;
		struct wlr_scene_tree *bottom;
		struct wlr_scene_tree *tiled;
		struct wlr_scene_tree *floating;
		struct wlr_scene_tree *fullscreen;
		struct wlr_scene_tree *top;
		struct wlr_scene_tree *overlay;
		struct wlr_scene_tree *block;
	} layers;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_surface;
	struct wl_listener new_xdg_decoration;
	struct wl_list views;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	enum cursor_mode cursor_mode;
	struct view *grabbed_view;
	struct window *hovered_window;
	double grab_x, grab_y;
	struct wl_array keys;

	struct wlr_output_layout *output_layout;
	struct wl_list monitors;
	struct monitor *active_monitor;
	struct wl_listener new_output;
	struct wl_listener layout_change;

	struct wlr_output_manager_v1 *output_mgr;
	struct wl_listener output_mgr_apply;
	struct wl_listener output_mgr_test;

	struct wlr_idle *idle;
	struct wlr_idle_notifier_v1 *idle_notifier;
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
	struct wl_listener idle_inhibitor_create;

	struct wl_event_source *time_tick;

	char panel_date[128];
	struct control *control;
	const char *control_greeting;
};

struct workspace {
	struct wl_list link;
	struct wl_list views;
	struct {
		struct {
			int views_in_master;
			double master_factor;
			bool right_master;
		} traditional;
	} tiling;
};

struct monitor {
	struct wl_list link;
	struct ptychite_server *server;
	struct wlr_output *output;
	struct wlr_box geometry;
	struct wlr_box window_geometry;
	struct wl_list views;
	struct wl_list workspaces;
	struct workspace *current_workspace;
	struct wallpaper *wallpaper;
	struct panel *panel;

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct keyboard {
	struct wl_list link;
	struct ptychite_server *server;
	struct wlr_keyboard *keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

struct element {
	enum element_type type;
	struct wlr_scene_tree *scene_tree;
	int width, height;
};

struct view {
	struct element element;
	struct wl_list link;
	struct wl_list monitor_link;
	struct wl_list workspace_link;

	struct ptychite_server *server;
	struct monitor *monitor;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree_surface;
	struct title_bar *title_bar;
	struct {
		struct wlr_scene_rect *top;
		struct wlr_scene_rect *right;
		struct wlr_scene_rect *bottom;
		struct wlr_scene_rect *left;
	} border;

	int initial_width;
	int initial_height;
	bool focused;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
};

struct buffer {
	struct wlr_buffer base;
	cairo_surface_t *surface;
	cairo_t *cairo;
};

struct window {
	struct element element;
	struct ptychite_server *server;
	struct wlr_output *output;
	struct wlr_scene_buffer *scene_buffer;
	const struct window_impl *impl;

	struct wl_listener destroy;
};

struct window_impl {
	void (*draw)(struct window *window, cairo_t *cairo, int surface_width, int surface_height,
			float scale);
	void (*handle_pointer_move)(struct window *window, double x, double y);
	void (*handle_pointer_button)(
			struct window *window, double x, double y, struct wlr_pointer_button_event *event);
	void (*handle_pointer_enter)(struct window *window);
	void (*handle_pointer_leave)(struct window *window);
	void (*destroy)(struct window *window);
};

struct wallpaper {
	struct window base;
	struct monitor *monitor;
};

struct mouse_region {
	struct wlr_box box;
	bool entered;
};

struct panel {
	struct window base;
	struct monitor *monitor;

	struct {
		struct mouse_region shell;
		struct mouse_region time;
	} regions;
};

struct control {
	struct window base;

	struct {
		struct wlr_box box;
	} time;
};

struct title_bar {
	struct window base;
	struct view *view;

	struct {
		struct mouse_region hide;
		struct mouse_region close;
	} regions;
};

static int protocol_json_get_mode_convert_to_native(
		enum zptychite_message_v1_json_get_mode mode, enum ptychite_json_get_mode *mode_out) {
	enum ptychite_json_get_mode get_mode;
	switch (mode) {
	case ZPTYCHITE_MESSAGE_V1_JSON_GET_MODE_PRETTY:
		get_mode = PTYCHITE_JSON_GET_PRETTY;
		break;
	case ZPTYCHITE_MESSAGE_V1_JSON_GET_MODE_COMPACT:
		get_mode = PTYCHITE_JSON_GET_COMPACT;
		break;
	default:
		return -1;
	}

	*mode_out = get_mode;
	return 0;
}

#define CALLBACK_SUCCESS_SEND_AND_DESTROY(callback, data) \
	zptychite_message_callback_v1_send_success(callback, data); \
	wl_resource_destroy(callback)

#define CALLBACK_FAILURE_SEND_AND_DESTROY(callback, message) \
	zptychite_message_callback_v1_send_failure(callback, message); \
	wl_resource_destroy(callback)

static void message_set_property(struct wl_client *client, struct wl_resource *resource,
		const char *path, const char *string, uint32_t mode, uint32_t id) {
	struct wl_resource *callback = wl_resource_create(client,
			&zptychite_message_callback_v1_interface, wl_resource_get_version(resource), id);
	if (!callback) {
		return;
	}

	struct ptychite_server *server = wl_resource_get_user_data(resource);

	enum ptychite_property_set_mode set_mode;
	switch (mode) {
	case ZPTYCHITE_MESSAGE_V1_PROPERTY_SET_MODE_APPEND:
		set_mode = PTYCHITE_PROPERTY_SET_APPEND;
		break;
	case ZPTYCHITE_MESSAGE_V1_PROPERTY_SET_MODE_OVERWRITE:
		set_mode = PTYCHITE_PROPERTY_SET_OVERWRITE;
		break;
	default:
		CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "invalid setting mode");
		return;
	}

	char *error;
	if (!ptychite_config_set_property_from_string(
				server->compositor->config, path, string, set_mode, &error)) {
		zptychite_message_callback_v1_send_success(callback, "");
	} else {
		zptychite_message_callback_v1_send_failure(callback, error);
	}

	wl_resource_destroy(callback);
}

static void message_get_property(struct wl_client *client, struct wl_resource *resource,
		const char *path, uint32_t mode, uint32_t id) {
	struct wl_resource *callback = wl_resource_create(client,
			&zptychite_message_callback_v1_interface, wl_resource_get_version(resource), id);
	if (!callback) {
		return;
	}

	enum ptychite_json_get_mode get_mode;
	if (protocol_json_get_mode_convert_to_native(mode, &get_mode)) {
		CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "invalid getting mode");
		return;
	}

	struct ptychite_server *server = wl_resource_get_user_data(resource);

	char *error;
	char *string = ptychite_config_get_property(server->compositor->config, path, get_mode, &error);
	if (!string) {
		CALLBACK_FAILURE_SEND_AND_DESTROY(callback, error);
		return;
	}

	CALLBACK_SUCCESS_SEND_AND_DESTROY(callback, string);
	free(string);
}

static struct json_object *view_describe(struct view *view) {
	struct json_object *description = json_object_new_object();
	if (!description) {
		return NULL;
	}

#define JSON_OBJECT_ADD_MEMBER_OR_RETURN(object, member, key, type, value) \
	if (!(member = json_object_new_##type(value))) { \
		json_object_put(object); \
		return NULL; \
	} \
	if (json_object_object_add(object, key, member)) { \
		json_object_put(member); \
		json_object_put(object); \
		return NULL; \
	}

	struct json_object *member;
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(
			description, member, "appid", string, view->xdg_toplevel->app_id)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(
			description, member, "title", string, view->xdg_toplevel->title)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(
			description, member, "x", int, view->element.scene_tree->node.x)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(
			description, member, "y", int, view->element.scene_tree->node.y)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "width", int, view->element.width)
	JSON_OBJECT_ADD_MEMBER_OR_RETURN(description, member, "height", int, view->element.height)

#undef JSON_OBJECT_ADD_MEMBER_OR_RETURN

	return description;
}

static void message_dump_views(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *output_resource, uint32_t mode, uint32_t id) {
	struct wl_resource *callback = wl_resource_create(client,
			&zptychite_message_callback_v1_interface, wl_resource_get_version(resource), id);
	if (!callback) {
		return;
	}

	struct ptychite_server *server = wl_resource_get_user_data(resource);

	enum ptychite_json_get_mode get_mode;
	if (protocol_json_get_mode_convert_to_native(mode, &get_mode)) {
		CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "invalid getting mode");
		return;
	}

	struct json_object *array;
	if (output_resource) {
		struct wlr_output *output = wlr_output_from_resource(output_resource);
		if (!output) {
			CALLBACK_FAILURE_SEND_AND_DESTROY(
					callback, "unable to obtain wlr_output from resource");
			return;
		}

		struct monitor *monitor = output->data;
		if (!(array = json_object_new_array_ext(wl_list_length(&monitor->views)))) {
			CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "memory error");
			return;
		}

		size_t idx = 0;
		struct view *view;
		wl_list_for_each(view, &monitor->views, monitor_link) {
			struct json_object *description = view_describe(view);
			if (!description) {
				json_object_put(array);
				CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "memory error");
				return;
			}
			json_object_array_put_idx(array, idx, description);
			idx++;
		}
	} else {
		if (!(array = json_object_new_array_ext(wl_list_length(&server->views)))) {
			CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "memory error");
		}

		size_t idx = 0;
		struct view *view;
		wl_list_for_each(view, &server->views, link) {
			struct json_object *description = view_describe(view);
			if (!description) {
				json_object_put(array);
				CALLBACK_FAILURE_SEND_AND_DESTROY(callback, "memory error");
				return;
			}
			json_object_array_put_idx(array, idx, description);
			idx++;
		}
	}

	char *error;
	const char *string = ptychite_json_object_convert_to_string(array, get_mode, &error);
	if (!string) {
		json_object_put(array);
		CALLBACK_FAILURE_SEND_AND_DESTROY(callback, error);
		return;
	}

	CALLBACK_SUCCESS_SEND_AND_DESTROY(callback, string);
	json_object_put(array);
}

static void message_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zptychite_message_v1_interface ptychite_message_impl = {
		.set_property = message_set_property,
		.get_property = message_get_property,
		.dump_views = message_dump_views,
		.destroy = message_destroy,
};

static void message_handle_server_destroy(struct wl_resource *resource) {
}

static void message_handle_bind(
		struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	struct wl_resource *resource =
			wl_resource_create(client, &zptychite_message_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(
			resource, &ptychite_message_impl, data, message_handle_server_destroy);
}

static struct view *element_get_view(struct element *element) {
	assert(element->type == ELEMENT_VIEW);

	struct view *view = wl_container_of(element, view, element);

	return view;
}

static struct window *element_get_window(struct element *element) {
	assert(element->type == ELEMENT_WINDOW);

	struct window *window = wl_container_of(element, window, element);

	return window;
}

static void buffer_destroy(struct wlr_buffer *buffer) {
	struct buffer *p_buffer = wl_container_of(buffer, p_buffer, base);

	cairo_surface_destroy(p_buffer->surface);
	cairo_destroy(p_buffer->cairo);
	free(p_buffer);
}

static bool buffer_begin_data_ptr_access(
		struct wlr_buffer *buffer, uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct buffer *p_buffer = wl_container_of(buffer, p_buffer, base);

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

static const struct wlr_buffer_impl buffer_buffer_impl = {
		.destroy = buffer_destroy,
		.begin_data_ptr_access = buffer_begin_data_ptr_access,
		.end_data_ptr_access = buffer_end_data_ptr_access,
};

static void cairo_draw_rounded_rect(
		cairo_t *cairo, double x, double y, double width, double height, double corner_radius) {
	cairo_new_sub_path(cairo);

	cairo_arc(cairo, x + width - corner_radius, y + corner_radius, corner_radius, -PI / 2, 0);
	cairo_arc(
			cairo, x + width - corner_radius, y + height - corner_radius, corner_radius, 0, PI / 2);
	cairo_arc(cairo, x + corner_radius, y + height - corner_radius, corner_radius, PI / 2, PI);
	cairo_arc(cairo, x + corner_radius, y + corner_radius, corner_radius, PI, 3 * PI / 2);

	cairo_close_path(cairo);
}

static PangoLayout *cairo_get_pango_layout(
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

static int cairo_draw_text(cairo_t *cairo, PangoFontDescription *font, const char *text,
		float foreground[4], float background[4], double scale, bool markup, int *width,
		int *height) {
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
			cairo_set_source_rgba(
					cairo, background[0], background[1], background[2], background[3]);
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

static int cairo_get_text_size(cairo_t *cairo, PangoFontDescription *font, const char *text,
		double scale, bool markup, int *width, int *height) {
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

static int cairo_draw_text_center(cairo_t *cairo, int y, int geom_x, int geom_width, int *x_out,
		PangoFontDescription *font, const char *text, float foreground[4], float background[4],
		double scale, bool markup, int *width, int *height) {
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
		return cairo_draw_text(
				cairo, font, text, foreground, background, scale, markup, NULL, NULL);
	}

	return -1;
}

static int cairo_draw_text_right(cairo_t *cairo, int y, int right_x, int *x_out,
		PangoFontDescription *font, const char *text, float foreground[4], float background[4],
		double scale, bool markup, int *width, int *height) {
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
		return cairo_draw_text(
				cairo, font, text, foreground, background, scale, markup, NULL, NULL);
	}

	return -1;
}

/* returns whether a change has been made */
static bool mouse_region_update_state(struct mouse_region *region, double x, double y) {
	if (wlr_box_contains_point(&region->box, x, y)) {
		if (region->entered) {
			return false;
		}
		region->entered = true;
		return true;
	}
	if (region->entered) {
		region->entered = false;
		return true;
	}

	return false;
}

static void window_handle_destroy(struct wl_listener *listener, void *data) {
	struct window *window = wl_container_of(listener, window, destroy);

	wl_list_remove(&window->destroy.link);

	if (window->server->hovered_window == window) {
		window->server->hovered_window = NULL;
	}

	window->impl->destroy(window);
}

static int window_init(struct window *window, struct ptychite_server *server,
		const struct window_impl *impl, struct wlr_scene_tree *parent, struct wlr_output *output) {
	if (!(window->element.scene_tree = wlr_scene_tree_create(parent))) {
		return -1;
	}

	struct wlr_scene_buffer *scene_buffer =
			wlr_scene_buffer_create(window->element.scene_tree, NULL);
	if (!scene_buffer) {
		wlr_scene_node_destroy(&window->element.scene_tree->node);
		return -1;
	}

	window->element.type = ELEMENT_WINDOW;
	window->element.scene_tree->node.data = &window->element;
	window->scene_buffer = scene_buffer;
	window->server = server;
	window->impl = impl;
	window->output = output;

	window->destroy.notify = window_handle_destroy;
	wl_signal_add(&scene_buffer->node.events.destroy, &window->destroy);

	return 0;
}

static int window_relay_draw(struct window *window, int width, int height) {
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

	cairo_surface_t *surface =
			cairo_image_surface_create(CAIRO_FORMAT_ARGB32, scaled_width, scaled_height);
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

	struct buffer *buffer = calloc(1, sizeof(struct buffer));
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

static void window_relay_draw_same_size(struct window *window) {
	window_relay_draw(window, window->element.width, window->element.height);
}

static void window_relay_pointer_enter(struct window *window) {
	if (!window->impl || !window->impl->handle_pointer_enter) {
		return;
	}

	window->impl->handle_pointer_enter(window);
}

static void window_relay_pointer_leave(struct window *window) {
	if (!window->impl || !window->impl->handle_pointer_leave) {
		return;
	}

	window->impl->handle_pointer_leave(window);
}

static void window_relay_pointer_move(struct window *window, double x, double y) {
	if (!window->impl || !window->impl->handle_pointer_move) {
		return;
	}

	float scale = window->output->scale;
	double scale_x = scale * x;
	double scale_y = scale * y;

	window->impl->handle_pointer_move(window, scale_x, scale_y);
}

static void window_relay_pointer_button(
		struct window *window, double x, double y, struct wlr_pointer_button_event *event) {
	if (!window->impl || !window->impl->handle_pointer_button) {
		return;
	}

	float scale = window->output->scale;
	double scale_x = scale * x;
	double scale_y = scale * y;

	window->impl->handle_pointer_button(window, scale_x, scale_y, event);
}

static void wallpaper_draw(
		struct window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct panel *wallpaper = wl_container_of(window, wallpaper, base);
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
		cairo_scale(
				cairo, (double)surface_width / image_width, (double)surface_height / image_height);
		break;
	}

	cairo_set_source_surface(cairo, image_surface, 0, 0);
	cairo_paint(cairo);
	cairo_restore(cairo);
}

static void wallpaper_destroy(struct window *window) {
	struct wallpaper *wallpaper = wl_container_of(window, wallpaper, base);

	free(wallpaper);
}

static const struct window_impl wallpaper_window_impl = {
		.draw = wallpaper_draw,
		.handle_pointer_enter = NULL,
		.handle_pointer_leave = NULL,
		.handle_pointer_move = NULL,
		.handle_pointer_button = NULL,
		.destroy = wallpaper_destroy,
};

static void wallpaper_draw_auto(struct wallpaper *wallpaper) {
	window_relay_draw(&wallpaper->base, wallpaper->monitor->geometry.width,
			wallpaper->monitor->geometry.height);
}

static const uint32_t ptychite_svg[] = {
		1836597052,
		1702240364,
		1869181810,
		824327534,
		539111470,
		1868787301,
		1735289188,
		1414865469,
		574106950,
		1933327935,
		1998612342,
		1752458345,
		959652413,
		942946094,
		539127149,
		1734960488,
		574452840,
		858665011,
		577596724,
		1919252000,
		1852795251,
		774971965,
		1981817393,
		1115121001,
		574453871,
		540024880,
		925776179,
		857749556,
		875769392,
		1836589090,
		1030975084,
		1953785890,
		791624304,
		779581303,
		1865298807,
		841967474,
		791687216,
		577205875,
		543636542,
		1851880052,
		1919903347,
		1948401005,
		1936613746,
		1702125932,
		926035240,
		925906478,
		775302432,
		859060019,
		1010704937,
		1882996321,
		543716449,
		1830960484,
		858665524,
		775238176,
		1664692535,
		808463920,
		840970802,
		1697921070,
		757085229,
		876097078,
		540290405,
		808463920,
		942486070,
		1698116910,
		807416877,
		909127726,
		774909237,
		808792368,
		774905906,
		808661552,
		774909240,
		909194544,
		774909235,
		842084400,
		808333357,
		842412598,
		808333357,
		943207473,
		808333357,
		858928694,
		808333357,
		959656504,
		808333344,
		876163378,
		825110573,
		909456441,
		808333344,
		842216761,
		841887789,
		959787571,
		841887776,
		875836210,
		825110573,
		540554545,
		909389360,
		540162352,
		909127216,
		540424243,
		909454896,
		540162098,
		909258288,
		540553523,
		825241136,
		540161078,
		858795568,
		540160306,
		858795568,
		540423990,
		942681648,
		540227127,
		875572784,
		807416886,
		842150190,
		807416121,
		875835694,
		807416116,
		942814510,
		808269363,
		959591214,
		824194864,
		925970478,
		774909236,
		959525176,
		774971448,
		909653041,
		808333613,
		807416629,
		909652526,
		825046321,
		892877102,
		908996653,
		943011123,
		825111085,
		758199864,
		808857137,
		808268341,
		876032814,
		825046576,
		858798126,
		774971446,
		859191601,
		858665773,
		540619315,
		926428722,
		858599732,
		842020398,
		775036981,
		959526456,
		875442221,
		842018870,
		841889056,
		540293425,
		858795570,
		891303985,
		909456686,
		925774880,
		540227121,
		859123248,
		540096055,
		942812724,
		858601016,
		892351022,
		925775648,
		758659380,
		892481079,
		941634101,
		892612910,
		775302449,
		540488753,
		959786544,
		758462777,
		808333617,
		875377459,
		825767726,
		825306424,
		842478638,
		774910253,
		758722870,
		859188784,
		758658873,
		808791608,
		908079410,
		808663086,
		892415283,
		842543406,
		775172384,
		758264889,
		908997937,
		824193844,
		842280497,
		774909233,
		876164150,
		808591411,
		892875566,
		875444512,
		540358705,
		925773874,
		840971830,
		909323824,
		774905905,
		808728368,
		775036979,
		875901489,
		925775149,
		758723635,
		926297650,
		825045816,
		808988210,
		774909235,
		909195313,
		1713381941,
		1030515817,
		1852796450,
		1931485797,
		1802465908,
		589446501,
		577136230,
		1920234272,
		761621359,
		1701734764,
		1030775139,
		1970237986,
		539124846,
		1869771891,
		1999463787,
		1752458345,
		775168573,
		790770993,
		1630485566,
		1731148862,
		1932475454,
		171861878,
};

static void panel_draw(
		struct window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct panel *panel = wl_container_of(window, panel, base);

	struct ptychite_server *server = panel->monitor->server;
	struct ptychite_config *config = server->compositor->config;
	float *background = config->panel.colors.background;
	float *foreground = config->panel.colors.foreground;
	float *accent = config->panel.colors.accent;
	float *chord_color = config->panel.colors.chord;

	cairo_set_source_rgba(cairo, background[0], background[1], background[2], background[3]);
	cairo_rectangle(cairo, 0, 0, surface_width, surface_height);
	cairo_fill(cairo);

	cairo_set_source_rgba(cairo, foreground[0], foreground[1], foreground[2], foreground[3]);
	struct ptychite_font *font = &config->panel.font;
	int font_height = font->height * scale;

	int x = font_height / 2;
	int y = (surface_height - font_height) / 2;

	GError *error = NULL;
	RsvgHandle *svg_handle =
			rsvg_handle_new_from_data((guint8 *)ptychite_svg, sizeof(ptychite_svg), &error);
	if (svg_handle) {
		RsvgRectangle viewport = {
				.x = x,
				.y = y,
				.width = surface_height,
				.height = font_height,
		};

		panel->regions.shell.box = (struct wlr_box){
				.x = 0,
				.y = 0,
				.width = viewport.width + font_height,
				.height = surface_height,
		};
		if (panel->regions.shell.entered) {
			cairo_set_source_rgba(cairo, accent[0], accent[1], accent[2], accent[3]);
			cairo_rectangle(cairo, panel->regions.shell.box.x, panel->regions.shell.box.y,
					panel->regions.shell.box.width, panel->regions.shell.box.height);
			cairo_fill(cairo);
		}

		rsvg_handle_render_document(svg_handle, cairo, &viewport, &error);
		g_object_unref(svg_handle);

		x += viewport.width + font_height;
	} else {
		g_error_free(error);
	}

	cairo_set_source_rgba(cairo, foreground[0], foreground[1], foreground[2], foreground[3]);
	struct workspace *workspace;
	wl_list_for_each(workspace, &panel->monitor->workspaces, link) {
		int radius = font_height / (workspace == panel->monitor->current_workspace ? 5 : 10);
		cairo_arc(cairo, x, surface_height / 2.0, radius, 0, PI * 2);
		cairo_fill(cairo);
		x += font_height / 2;
	}

	if (server->keys.size) {
		struct ptychite_chord chord = {
				.keys = server->keys.data,
				.keys_l = server->keys.size / sizeof(struct ptychite_key),
		};
		char *chord_string = ptychite_chord_get_pattern(&chord);
		if (chord_string) {
			cairo_draw_text_right(cairo, y, surface_width - font_height, NULL, font->font,
					chord_string, foreground, chord_color, scale, false, NULL, NULL);
			free(chord_string);
		}
	}

	if (*server->panel_date) {
		float *bg = ((server->active_monitor == panel->monitor &&
							 server->control->base.element.scene_tree->node.enabled) ||
							panel->regions.time.entered)
				? accent
				: NULL;
		int width;
		if (!cairo_draw_text_center(cairo, y, 0, surface_width, &x, font->font, server->panel_date,
					foreground, bg, scale, false, &width, NULL)) {
			panel->regions.time.box = (struct wlr_box){
					.x = x,
					.y = 0,
					.width = width,
					.height = surface_height,
			};
		}
	}
}

static void panel_handle_pointer_enter(struct window *window) {
}

static void panel_handle_pointer_leave(struct window *window) {
	struct panel *panel = wl_container_of(window, panel, base);
	bool redraw = false;

	redraw |= panel->regions.time.entered;
	panel->regions.time.entered = false;
	redraw |= panel->regions.shell.entered;
	panel->regions.shell.entered = false;

	if (redraw) {
		window_relay_draw_same_size(window);
	}
}

static void panel_handle_pointer_move(struct window *window, double x, double y) {
	struct panel *panel = wl_container_of(window, panel, base);

	bool redraw = false;
	redraw |= mouse_region_update_state(&panel->regions.shell, x, y);
	redraw |= mouse_region_update_state(&panel->regions.time, x, y);

	if (redraw) {
		window_relay_draw_same_size(window);
	}
}

static void panel_handle_pointer_button(
		struct window *window, double x, double y, struct wlr_pointer_button_event *event) {
	struct panel *panel = wl_container_of(window, panel, base);

	if (event->state != WLR_BUTTON_PRESSED) {
		return;
	}

	if (panel->regions.shell.entered) {
	}
	if (panel->regions.time.entered) {
		ptychite_server_toggle_control(panel->monitor->server);
	}
}

static void panel_destroy(struct window *window) {
	struct panel *panel = wl_container_of(window, panel, base);

	free(panel);
}

static const struct window_impl panel_window_impl = {
		.draw = panel_draw,
		.handle_pointer_enter = panel_handle_pointer_enter,
		.handle_pointer_leave = panel_handle_pointer_leave,
		.handle_pointer_move = panel_handle_pointer_move,
		.handle_pointer_button = panel_handle_pointer_button,
		.destroy = panel_destroy,
};

static void panel_draw_auto(struct panel *panel) {
	struct ptychite_font *font = &panel->monitor->server->compositor->config->panel.font;
	int height = font->height + font->height / 2;

	panel->monitor->window_geometry.y = panel->monitor->geometry.y + height;
	panel->monitor->window_geometry.height = panel->monitor->geometry.height - height;

	window_relay_draw(&panel->base, panel->monitor->geometry.width, height);
}

static void control_draw(
		struct window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct control *control = wl_container_of(window, control, base);

	struct wlr_box box = {
			.x = 2,
			.y = 2,
			.width = surface_width - 4,
			.height = surface_height - 4,
	};

	struct ptychite_server *server = control->base.server;
	struct ptychite_config *config = server->compositor->config;

	float *foreground = config->panel.colors.foreground;
	float *accent = config->panel.colors.accent;
	float *gray1 = config->panel.colors.gray1;
	float *gray2 = config->panel.colors.gray2;
	float *border = config->panel.colors.border;
	float *seperator = config->panel.colors.seperator;

	int rect_radius = fmin(box.width, box.height) / 20;
	cairo_draw_rounded_rect(cairo, box.x, box.y, box.width, box.height, rect_radius);
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
		if (!cairo_draw_text_center(cairo, y, box.x, box.width, NULL, font->font,
					server->control_greeting, gray2, NULL, scale * 0.8, false, NULL, &height)) {
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
		cairo_draw_rounded_rect(cairo, box.x, y, box.width, font_height * 3, font_height);
		cairo_set_source_rgba(cairo, gray1[0], gray1[1], gray1[2], gray1[3]);
		cairo_fill(cairo);
		y += font_height * 3 + rect_radius;
	}
}

static void control_handle_pointer_move(struct window *window, double x, double y) {
}

static void control_handle_pointer_button(
		struct window *window, double x, double y, struct wlr_pointer_button_event *event) {
}

static void control_destroy(struct window *window) {
	struct control *control = wl_container_of(window, control, base);

	free(control);
}

static const struct window_impl control_window_impl = {
		.draw = control_draw,
		.handle_pointer_enter = NULL,
		.handle_pointer_leave = NULL,
		.handle_pointer_move = control_handle_pointer_move,
		.handle_pointer_button = control_handle_pointer_button,
		.destroy = control_destroy,
};

static void control_draw_auto(struct control *control) {
	struct monitor *monitor = control->base.server->active_monitor;

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
	window_relay_draw(&control->base, width, height);
}

static void control_show(struct control *control) {
	control_draw_auto(control);
	wlr_scene_node_set_enabled(&control->base.element.scene_tree->node, true);
	struct monitor *monitor = control->base.server->active_monitor;
	if (monitor && monitor->panel && monitor->panel->base.element.scene_tree->node.enabled) {
		window_relay_draw_same_size(&monitor->panel->base);
	}
}

static void control_hide(struct control *control) {
	wlr_scene_node_set_enabled(&control->base.element.scene_tree->node, false);
	struct monitor *monitor = control->base.server->active_monitor;
	if (monitor && monitor->panel && monitor->panel->base.element.scene_tree->node.enabled) {
		window_relay_draw_same_size(&monitor->panel->base);
	}
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct wlr_keyboard_key_event *event = data;
	struct ptychite_server *server = keyboard->server;
	struct wlr_seat *seat = server->seat;

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->keyboard);

	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		size_t old_keys_size = server->keys.size;

		int i;
		for (i = 0; i < nsyms; i++) {
			if (server->keys.size &&
					(syms[i] == XKB_KEY_Super_L || syms[i] == XKB_KEY_Super_R ||
							syms[i] == XKB_KEY_Alt_L || syms[i] == XKB_KEY_Alt_R ||
							syms[i] == XKB_KEY_Shift_L || syms[i] == XKB_KEY_Shift_R ||
							syms[i] == XKB_KEY_Control_L || syms[i] == XKB_KEY_Control_R ||
							syms[i] == XKB_KEY_Caps_Lock)) {
				handled = true;
				continue;
			}

			handled = ptychite_compositor_evaluate_key(
					server->compositor, syms[i], modifiers, &server->keys);

			if (!handled && server->session &&
					modifiers == (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT)) {
				unsigned int vt = 0;
				switch (syms[i]) {
				case XKB_KEY_XF86Switch_VT_1:
					vt = 1;
					break;
				case XKB_KEY_XF86Switch_VT_2:
					vt = 2;
					break;
				case XKB_KEY_XF86Switch_VT_3:
					vt = 3;
					break;
				case XKB_KEY_XF86Switch_VT_4:
					vt = 4;
					break;
				case XKB_KEY_XF86Switch_VT_5:
					vt = 5;
					break;
				case XKB_KEY_XF86Switch_VT_6:
					vt = 6;
					break;
				case XKB_KEY_XF86Switch_VT_7:
					vt = 7;
					break;
				case XKB_KEY_XF86Switch_VT_8:
					vt = 8;
					break;
				case XKB_KEY_XF86Switch_VT_9:
					vt = 9;
					break;
				case XKB_KEY_XF86Switch_VT_10:
					vt = 10;
					break;
				case XKB_KEY_XF86Switch_VT_11:
					vt = 11;
					break;
				case XKB_KEY_XF86Switch_VT_12:
					vt = 12;
					break;
				}
				if (vt) {
					handled = true;
					wlr_session_change_vt(server->session, vt);
				}
			}
		}

		if (server->keys.size != old_keys_size) {
			ptychite_server_configure_panels(server);
		}
	}

	if (!handled) {
		wlr_seat_set_keyboard(seat, keyboard->keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	struct keyboard *keyboard = wl_container_of(listener, keyboard, destroy);

	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);

	free(keyboard);
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
	struct keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);

	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->keyboard->modifiers);
}

static void view_resize(struct view *view, int width, int height) {
	view->element.width = width;
	view->element.height = height;

	int border_thickness = view->server->compositor->config->views.border.thickness;
	int top_thickness;
	if (view->title_bar && view->title_bar->base.element.scene_tree->node.enabled) {
		struct ptychite_font *font = &view->server->compositor->config->panel.font;
		top_thickness = font->height + font->height / 6;
	} else {
		top_thickness = border_thickness;
	}

	if (view->xdg_toplevel->base->client->shell->version >=
					XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION &&
			view->element.width >= 0 && view->element.height >= 0) {
		wlr_xdg_toplevel_set_bounds(view->xdg_toplevel, view->element.width, view->element.height);
	}

	struct wlr_xdg_toplevel_state *state = &view->xdg_toplevel->current;
	int max_width = state->max_width;
	int max_height = state->max_height;
	int min_width = state->min_width;
	int min_height = state->min_height;

	view->element.width = fmax(min_width + (2 * border_thickness), view->element.width);
	view->element.height =
			fmax(min_height + (top_thickness + border_thickness), view->element.height);

	if (max_width > 0 && !(2 * border_thickness > INT_MAX - max_width)) {
		view->element.width = fmin(max_width + (2 * border_thickness), view->element.width);
	}
	if (max_height > 0 && !(top_thickness + border_thickness > INT_MAX - max_height)) {
		view->element.height =
				fmin(max_height + (top_thickness + border_thickness), view->element.height);
	}

	if (view->title_bar && view->title_bar->base.element.scene_tree->node.enabled &&
			(view->title_bar->base.element.width != view->element.width ||
					view->title_bar->base.element.height != top_thickness)) {
		if (view->monitor) {
			view->title_bar->base.output = view->monitor->output;
		} else {
			view->title_bar->base.output = NULL;
		}
		window_relay_draw(&view->title_bar->base, view->element.width, top_thickness);
	}

	wlr_scene_node_set_position(&view->scene_tree_surface->node, border_thickness, top_thickness);
	wlr_xdg_toplevel_set_size(view->xdg_toplevel, view->element.width - 2 * border_thickness,
			view->element.height - (top_thickness + border_thickness));

	if (view->border.top->node.enabled) {
		wlr_scene_rect_set_size(view->border.top, view->element.width, border_thickness);
	}
	wlr_scene_node_set_position(
			&view->border.right->node, view->element.width - border_thickness, top_thickness);
	wlr_scene_rect_set_size(view->border.right, border_thickness,
			view->element.height - (top_thickness + border_thickness));
	wlr_scene_node_set_position(
			&view->border.bottom->node, 0, view->element.height - border_thickness);
	wlr_scene_rect_set_size(view->border.bottom, view->element.width, border_thickness);
	wlr_scene_node_set_position(&view->border.left->node, 0, top_thickness);
	wlr_scene_rect_set_size(view->border.left, border_thickness,
			view->element.height - (top_thickness + border_thickness));
}

static struct workspace *monitor_add_workspace(struct monitor *monitor) {
	struct workspace *workspace = calloc(1, sizeof(struct workspace));
	if (!workspace) {
		return NULL;
	}

	wl_list_init(&workspace->views);
	workspace->tiling.traditional.views_in_master = 1;
	workspace->tiling.traditional.master_factor = 0.55;
	workspace->tiling.traditional.right_master = false;

	wl_list_insert(monitor->workspaces.prev, &workspace->link);

	return workspace;
}

static void monitor_tile(struct monitor *monitor) {
	struct workspace *workspace = monitor->current_workspace;
	if (wl_list_empty(&workspace->views)) {
		return;
	}

	struct ptychite_config *config = monitor->server->compositor->config;
	int gaps = config->tiling.gaps;

	switch (config->tiling.mode) {
	case PTYCHITE_TILING_NONE:
		break;
	case PTYCHITE_TILING_TRADITIONAL: {
		int views_len = wl_list_length(&workspace->views);
		int views_in_master = workspace->tiling.traditional.views_in_master;
		double master_factor = workspace->tiling.traditional.master_factor;
		bool right_master = workspace->tiling.traditional.right_master;

		int master_width;
		if (views_len > views_in_master) {
			master_width =
					views_in_master ? (monitor->window_geometry.width + gaps) * master_factor : 0;
		} else {
			master_width = monitor->window_geometry.width - gaps;
		}

		int master_x = monitor->window_geometry.x +
				(right_master ? monitor->window_geometry.width - master_width : gaps);
		int stack_x = monitor->window_geometry.x + (right_master ? gaps : master_width + gaps);
		int master_y = gaps;
		int stack_y = gaps;
		int i = 0;
		struct view *view;
		wl_list_for_each(view, &workspace->views, workspace_link) {
			if (i < views_in_master) {
				int r = fmin(views_len, views_in_master) - i;
				int height = (monitor->window_geometry.height - master_y - gaps * r) / r;
				wlr_scene_node_set_position(&view->element.scene_tree->node, master_x,
						monitor->window_geometry.y + master_y);
				view_resize(view, master_width - gaps, height);
				master_y += view->element.height + gaps;
			} else {
				int r = views_len - i;
				int height = (monitor->window_geometry.height - stack_y - gaps * r) / r;
				wlr_scene_node_set_position(&view->element.scene_tree->node, stack_x,
						monitor->window_geometry.y + stack_y);
				view_resize(view, monitor->window_geometry.width - master_width - 2 * gaps, height);
				stack_y += view->element.height + gaps;
			}
			i++;
		}

		break;
	}
	}

	ptychite_server_check_cursor(monitor->server);
}

static int monitor_switch_workspace(struct monitor *monitor, struct workspace *workspace) {
	struct workspace *last_workspace = monitor->current_workspace;
	monitor->current_workspace = workspace;

	struct view *view;
	wl_list_for_each(view, &last_workspace->views, workspace_link) {
		wlr_scene_node_set_enabled(&view->element.scene_tree->node, false);
	}
	wl_list_for_each(view, &monitor->current_workspace->views, workspace_link) {
		wlr_scene_node_set_enabled(&view->element.scene_tree->node, true);
	}

	monitor_tile(monitor);
	if (monitor->panel && monitor->panel->base.scene_buffer->node.enabled) {
		window_relay_draw_same_size(&monitor->panel->base);
	}

	return 0;
}

static void monitor_disable(struct monitor *monitor) {
	struct ptychite_server *server = monitor->server;

	if (monitor == server->active_monitor) {
		server->active_monitor = NULL;
		struct monitor *iter;
		wl_list_for_each(iter, &server->monitors, link) {
			if (iter->output->enabled) {
				server->active_monitor = iter;
				break;
			}
		}
	}

	if (server->active_monitor) {
		struct view *view, *view_tmp;
		wl_list_for_each_safe(view, view_tmp, &monitor->views, monitor_link) {
			wl_list_insert(&server->active_monitor->views, &view->monitor_link);
			wl_list_insert(
					&server->active_monitor->current_workspace->views, &view->workspace_link);
		}
		monitor_tile(server->active_monitor);
	}
}

static void monitor_handle_frame(struct wl_listener *listener, void *data) {
	struct monitor *monitor = wl_container_of(listener, monitor, frame);

	struct wlr_scene_output *scene_output =
			wlr_scene_get_scene_output(monitor->server->scene, monitor->output);

	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void monitor_handle_request_state(struct wl_listener *listener, void *data) {
	struct monitor *monitor = wl_container_of(listener, monitor, request_state);
	const struct wlr_output_event_request_state *event = data;

	wlr_output_commit_state(monitor->output, event->state);
}

static void monitor_handle_destroy(struct wl_listener *listener, void *data) {
	struct monitor *monitor = wl_container_of(listener, monitor, destroy);

	wl_list_remove(&monitor->frame.link);
	wl_list_remove(&monitor->request_state.link);
	wl_list_remove(&monitor->destroy.link);
	wl_list_remove(&monitor->link);

	if (!monitor->server->terminated) {
		if (monitor->wallpaper) {
			wlr_scene_node_destroy(&monitor->wallpaper->base.scene_buffer->node);
		}
		if (monitor->panel) {
			wlr_scene_node_destroy(&monitor->panel->base.scene_buffer->node);
		}
	}

	free(monitor);
}

static void view_focus(struct view *view, struct wlr_surface *surface) {
	struct ptychite_server *server = view->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct ptychite_config *config = server->compositor->config;

	if (prev_surface == surface) {
		return;
	}
	if (prev_surface) {
		struct wlr_xdg_surface *prev_xdg_surface =
				wlr_xdg_surface_try_from_wlr_surface(seat->keyboard_state.focused_surface);
		assert(prev_xdg_surface && prev_xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
		wlr_xdg_toplevel_set_activated(prev_xdg_surface->toplevel, false);

		struct wlr_scene_tree *scene_tree = prev_xdg_surface->data;
		struct element *element = scene_tree->node.data;
		if (element) {
			struct view *prev_view = element_get_view(element);
			prev_view->focused = false;
			wlr_scene_rect_set_color(prev_view->border.top, config->views.border.colors.inactive);
			wlr_scene_rect_set_color(prev_view->border.right, config->views.border.colors.inactive);
			wlr_scene_rect_set_color(
					prev_view->border.bottom, config->views.border.colors.inactive);
			wlr_scene_rect_set_color(prev_view->border.left, config->views.border.colors.inactive);
			if (prev_view->title_bar &&
					prev_view->title_bar->base.element.scene_tree->node.enabled) {
				window_relay_draw_same_size(&prev_view->title_bar->base);
			}
		}
	}

	view->focused = true;
	wlr_scene_node_raise_to_top(&view->element.scene_tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);
	wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);

	wlr_scene_rect_set_color(view->border.top, config->views.border.colors.active);
	wlr_scene_rect_set_color(view->border.right, config->views.border.colors.active);
	wlr_scene_rect_set_color(view->border.bottom, config->views.border.colors.active);
	wlr_scene_rect_set_color(view->border.left, config->views.border.colors.active);
	if (view->title_bar && view->title_bar->base.element.scene_tree->node.enabled) {
		window_relay_draw_same_size(&view->title_bar->base);
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(seat, view->xdg_toplevel->base->surface, keyboard->keycodes,
				keyboard->num_keycodes, &keyboard->modifiers);
	}
}

static void view_begin_interactive(struct view *view, enum cursor_mode mode) {
	struct ptychite_server *server = view->server;
	struct wlr_surface *focused_surface = server->seat->pointer_state.focused_surface;

	if (focused_surface &&
			view->xdg_toplevel->base->surface != wlr_surface_get_root_surface(focused_surface)) {
		return;
	}
	server->grabbed_view = view;
	server->cursor_mode = mode;

	if (mode == CURSOR_MOVE) {
		server->grab_x = server->cursor->x - view->element.scene_tree->node.x;
		server->grab_y = server->cursor->y - view->element.scene_tree->node.y;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "fleur");
	} else {
		double border_x = view->element.scene_tree->node.x + view->element.width;
		double border_y = view->element.scene_tree->node.y + view->element.height;
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "bottom_right_corner");
	}
}

static void view_handle_set_title(struct wl_listener *listener, void *data) {
	struct view *view = wl_container_of(listener, view, set_title);

	if (view->title_bar && view->title_bar->base.element.scene_tree->node.enabled) {
		window_relay_draw_same_size(&view->title_bar->base);
	}
}

static void view_handle_map(struct wl_listener *listener, void *data) {
	struct view *view = wl_container_of(listener, view, map);
	struct ptychite_config *config = view->server->compositor->config;

	wl_list_insert(&view->server->views, &view->link);
	if (view->server->active_monitor) {
		view->monitor = view->server->active_monitor;
		wl_list_insert(&view->monitor->views, &view->monitor_link);
		struct workspace *workspace = view->monitor->current_workspace;
		if (!config->views.map_to_front) {
			wl_list_insert(workspace->views.prev, &view->workspace_link);
		} else {
			wl_list_insert(&workspace->views, &view->workspace_link);
		}
	}
	view->set_title.notify = view_handle_set_title;
	wl_signal_add(&view->xdg_toplevel->events.set_title, &view->set_title);

	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geometry);
	view->initial_width = geometry.width + 2 * config->views.border.thickness;
	view->initial_height = geometry.height + 2 * config->views.border.thickness;

	if (!view->monitor || config->tiling.mode == PTYCHITE_TILING_NONE) {
		struct wlr_box box;
		if (view->monitor) {
			box = view->monitor->window_geometry;
		} else {
			wlr_output_layout_get_box(view->server->output_layout, NULL, &box);
		}

		wlr_scene_node_set_position(&view->element.scene_tree->node,
				box.x + (box.width - view->initial_width) / 2,
				box.y + (box.height - view->initial_height) / 2);
		view_resize(view, view->initial_width, view->initial_height);
	} else {
		monitor_tile(view->monitor);
	}

	wlr_scene_node_set_enabled(&view->element.scene_tree->node, true);
	view_focus(view, view->xdg_toplevel->base->surface);
}

static void view_handle_unmap(struct wl_listener *listener, void *data) {
	struct view *view = wl_container_of(listener, view, unmap);

	if (view == view->server->grabbed_view) {
		view->server->cursor_mode = CURSOR_PASSTHROUGH;
		view->server->grabbed_view = NULL;
	}

	// wl_list_remove(&view->commit.link);
	wl_list_remove(&view->workspace_link);
	wl_list_remove(&view->monitor_link);
	wl_list_remove(&view->link);
	wl_list_remove(&view->set_title.link);

	wlr_scene_node_set_enabled(&view->element.scene_tree->node, false);

	if (view->monitor) {
		monitor_tile(view->monitor);
	}
}

static void view_handle_destroy(struct wl_listener *listener, void *data) {
	struct view *view = wl_container_of(listener, view, destroy);

	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);

	wlr_scene_node_destroy(&view->element.scene_tree->node);

	free(view);
}

static void view_handle_request_maximize(struct wl_listener *listener, void *data) {
	struct view *view = wl_container_of(listener, view, request_maximize);

	wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void view_handle_request_fullscreen(struct wl_listener *listener, void *data) {
	struct view *view = wl_container_of(listener, view, request_fullscreen);

	wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void title_bar_draw(
		struct window *window, cairo_t *cairo, int surface_width, int surface_height, float scale) {
	struct title_bar *title_bar = wl_container_of(window, title_bar, base);

	struct ptychite_server *server = title_bar->view->server;
	struct ptychite_config *config = server->compositor->config;

	float *background = title_bar->view->focused ? config->views.border.colors.active
												 : config->views.border.colors.inactive;
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
	cairo_draw_text(cairo, font->font, title_bar->view->xdg_toplevel->title, foreground, NULL,
			scale, false, NULL, NULL);

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

	if (title_bar->regions.close.entered) {
		cairo_arc(cairo, close_draw_box.x + close_draw_box.width / 2.0,
				close_draw_box.y + close_draw_box.height / 2.0, close_draw_box.width, 0, PI * 2);
		cairo_set_source_rgba(cairo, close[0], close[1], close[2], close[3]);
		cairo_fill(cairo);
	}

	cairo_move_to(cairo, close_draw_box.x, close_draw_box.y);
	cairo_line_to(cairo, close_draw_box.x + close_draw_box.width,
			close_draw_box.y + close_draw_box.height);
	cairo_move_to(cairo, close_draw_box.x, close_draw_box.y + close_draw_box.height);
	cairo_line_to(cairo, close_draw_box.x + close_draw_box.width, close_draw_box.y);

	cairo_set_line_width(cairo, font_height / 10.0);
	cairo_set_source_rgba(cairo, foreground[0], foreground[1], foreground[2], foreground[3]);
	cairo_stroke(cairo);
}

static void title_bar_handle_pointer_enter(struct window *window) {
}

static void title_bar_handle_pointer_leave(struct window *window) {
	struct title_bar *title_bar = wl_container_of(window, title_bar, base);

	bool redraw = false;
	redraw |= title_bar->regions.hide.entered;
	title_bar->regions.hide.entered = false;
	redraw |= title_bar->regions.close.entered;
	title_bar->regions.close.entered = false;

	if (redraw) {
		window_relay_draw_same_size(window);
	}
}

static void title_bar_handle_pointer_move(struct window *window, double x, double y) {
	struct title_bar *title_bar = wl_container_of(window, title_bar, base);

	bool redraw = false;
	redraw |= mouse_region_update_state(&title_bar->regions.hide, x, y);
	redraw |= mouse_region_update_state(&title_bar->regions.close, x, y);

	if (redraw) {
		window_relay_draw_same_size(window);
	}
}

static void title_bar_handle_pointer_button(
		struct window *window, double x, double y, struct wlr_pointer_button_event *event) {
	struct title_bar *title_bar = wl_container_of(window, title_bar, base);

	if (event->state != WLR_BUTTON_PRESSED) {
		return;
	}

	if (title_bar->regions.close.entered) {
		wlr_xdg_toplevel_send_close(title_bar->view->xdg_toplevel);
		return;
	}

	view_focus(title_bar->view, NULL);
	view_begin_interactive(title_bar->view, CURSOR_MOVE);
}

static void title_bar_destroy(struct window *window) {
	struct title_bar *title_bar = wl_container_of(window, title_bar, base);

	free(title_bar);
}

static const struct window_impl title_bar_window_impl = {
		.draw = title_bar_draw,
		.handle_pointer_enter = title_bar_handle_pointer_enter,
		.handle_pointer_leave = title_bar_handle_pointer_leave,
		.handle_pointer_move = title_bar_handle_pointer_move,
		.handle_pointer_button = title_bar_handle_pointer_button,
		.destroy = title_bar_destroy,
};

static void server_activate_monitor(struct ptychite_server *server, struct monitor *monitor) {
	server->active_monitor = monitor;
}

static struct element *server_identify_element_at(struct ptychite_server *server, double lx,
		double ly, double *sx, double *sy, struct wlr_scene_buffer **scene_buffer) {
	struct wlr_scene_node *scene_node =
			wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);

	if (!scene_node || scene_node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}

	if (scene_buffer) {
		*scene_buffer = wlr_scene_buffer_from_node(scene_node);
	}

	struct wlr_scene_tree *scene_tree = scene_node->parent;
	while (scene_tree && !scene_tree->node.data) {
		scene_tree = scene_tree->node.parent;
	}

	return scene_tree->node.data;
}

static void server_process_cursor_move(struct ptychite_server *server, uint32_t time) {
	struct view *view = server->grabbed_view;

	wlr_scene_node_set_position(&view->element.scene_tree->node, server->cursor->x - server->grab_x,
			server->cursor->y - server->grab_y);
}

static void server_process_cursor_resize(struct ptychite_server *server, uint32_t time) {
	struct view *view = server->grabbed_view;

	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;

	int new_left = view->element.scene_tree->node.x;
	int new_right = view->element.scene_tree->node.x + view->element.width;
	int new_top = view->element.scene_tree->node.y;
	int new_bottom = view->element.scene_tree->node.y + view->element.height;

	new_right = border_x;
	if (new_right <= new_left) {
		new_right = new_left + 1;
	}
	new_bottom = border_y;
	if (new_bottom <= new_top) {
		new_bottom = new_top + 1;
	}

	view_resize(view, new_right - new_left, new_bottom - new_top);
}

static void server_process_cursor_motion(struct ptychite_server *server, uint32_t time) {
	struct wlr_output *output = wlr_output_layout_output_at(
			server->output_layout, server->cursor->x, server->cursor->y);
	if (output && output->data != server->active_monitor) {
		server_activate_monitor(server, output->data);
	}

	if (server->cursor_mode == CURSOR_MOVE) {
		server_process_cursor_move(server, time);
		return;
	} else if (server->cursor_mode == CURSOR_RESIZE) {
		server_process_cursor_resize(server, time);
		return;
	}

	double sx, sy;
	struct wlr_scene_buffer *scene_buffer;
	struct element *element = server_identify_element_at(
			server, server->cursor->x, server->cursor->y, &sx, &sy, &scene_buffer);
	if (element) {
		switch (element->type) {
		case ELEMENT_VIEW: {
			struct wlr_scene_surface *scene_surface =
					wlr_scene_surface_try_from_buffer(scene_buffer);
			if (scene_surface) {
				wlr_seat_pointer_notify_enter(server->seat, scene_surface->surface, sx, sy);
				wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
			} else {
				wlr_seat_pointer_clear_focus(server->seat);
			}
			if (server->hovered_window) {
				window_relay_pointer_leave(server->hovered_window);
				server->hovered_window = NULL;
			}
			break;
		}
		case ELEMENT_WINDOW: {
			struct window *window = element_get_window(element);
			if (window != server->hovered_window) {
				if (server->hovered_window) {
					window_relay_pointer_leave(server->hovered_window);
				}
				server->hovered_window = window;
				window_relay_pointer_enter(window);
			}
			window_relay_pointer_move(window, sx, sy);
			wlr_seat_pointer_clear_focus(server->seat);
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
			break;
		}
		}
	} else {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
		wlr_seat_pointer_clear_focus(server->seat);
		if (server->hovered_window) {
			window_relay_pointer_leave(server->hovered_window);
			server->hovered_window = NULL;
		}
	}
}

static void server_new_pointer(struct ptychite_server *server, struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_keyboard(struct ptychite_server *server, struct wlr_input_device *device) {
	struct keyboard *p_keyboard = calloc(1, sizeof(struct keyboard));
	if (!p_keyboard) {
		wlr_log(WLR_ERROR, "Could not initialize keyboard: insufficent memory");
		return;
	}

	struct wlr_keyboard *keyboard = wlr_keyboard_from_input_device(device);

	p_keyboard->server = server;
	p_keyboard->keyboard = keyboard;

	wlr_keyboard_set_repeat_info(keyboard, server->compositor->config->keyboard.repeat.rate,
			server->compositor->config->keyboard.repeat.delay);

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context) {
		struct xkb_rule_names rules = (struct xkb_rule_names){
				.options = server->compositor->config->keyboard.xkb.options,
				.rules = NULL,
				.layout = NULL,
				.model = NULL,
				.variant = NULL,
		};
		struct xkb_keymap *keymap =
				xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
		wlr_keyboard_set_keymap(keyboard, keymap);
		xkb_keymap_unref(keymap);
		xkb_context_unref(context);
	} else {
		wlr_keyboard_set_keymap(keyboard, NULL);
	}

	p_keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&p_keyboard->keyboard->events.modifiers, &p_keyboard->modifiers);
	p_keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&p_keyboard->keyboard->events.key, &p_keyboard->key);
	p_keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &p_keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, p_keyboard->keyboard);

	wl_list_insert(&server->keyboards, &p_keyboard->link);
}

static int server_time_tick_update(void *data) {
	struct ptychite_server *server = data;

	time_t t;
	time(&t);
	struct tm *info = localtime(&t);

	if (info) {
		if (!*server->panel_date || !info->tm_sec) {
			strftime(server->panel_date, sizeof(server->panel_date), "%b %-d %-H:%M", info);
			struct monitor *monitor;
			wl_list_for_each(monitor, &server->monitors, link) {
				if (!monitor->panel || !monitor->panel->base.element.scene_tree->node.enabled) {
					continue;
				}
				window_relay_draw_same_size(&monitor->panel->base);
			}
		}

		if (!server->control_greeting || !info->tm_sec) {
			char *greeting;
			if (info->tm_hour >= 18) {
				greeting = "Good Evening";
			} else if (info->tm_hour >= 12) {
				greeting = "Good Afternoon";
			} else {
				greeting = "Good Morning";
			}
			if (server->control_greeting != greeting) {
				server->control_greeting = greeting;
				control_draw_auto(server->control);
			}
		}
	}

	wl_event_source_timer_update(server->time_tick, 1000);

	return 0;
}

static void server_update_monitors(struct ptychite_server *server) {
	struct wlr_output_configuration_v1 *output_config = wlr_output_configuration_v1_create();

	struct monitor *monitor;
	wl_list_for_each(monitor, &server->monitors, link) {
		if (monitor->output->enabled) {
			continue;
		}

		if (output_config) {
			struct wlr_output_configuration_head_v1 *head =
					wlr_output_configuration_head_v1_create(output_config, monitor->output);
			if (head) {
				head->state.enabled = false;
			}
		}
		wlr_output_layout_remove(server->output_layout, monitor->output);
		monitor_disable(monitor);
	}

	wl_list_for_each(monitor, &server->monitors, link) {
		if (monitor->output->enabled &&
				!wlr_output_layout_get(server->output_layout, monitor->output)) {
			wlr_output_layout_add_auto(server->output_layout, monitor->output);
		}
	}

	wl_list_for_each(monitor, &server->monitors, link) {
		if (!monitor->output->enabled) {
			continue;
		}

		wlr_output_layout_get_box(server->output_layout, monitor->output, &monitor->geometry);
		if (monitor->panel && monitor->panel->base.element.scene_tree->node.enabled) {
			monitor->window_geometry = (struct wlr_box){
					.x = monitor->geometry.x,
					.y = monitor->geometry.y + monitor->panel->base.element.height,
					.width = monitor->geometry.width,
					.height = monitor->geometry.height - monitor->panel->base.element.height,
			};
		} else {
			monitor->window_geometry = monitor->geometry;
		}

		if (monitor->wallpaper) {
			wlr_scene_node_set_position(&monitor->wallpaper->base.element.scene_tree->node,
					monitor->geometry.x, monitor->geometry.y);
			wallpaper_draw_auto(monitor->wallpaper);
		}

		if (monitor->panel) {
			wlr_scene_node_set_position(&monitor->panel->base.element.scene_tree->node,
					monitor->geometry.x, monitor->geometry.y);
			if (monitor->panel->base.element.scene_tree->node.enabled) {
				panel_draw_auto(monitor->panel);
			}
		}

		monitor_tile(monitor);

		if (output_config) {
			struct wlr_output_configuration_head_v1 *head =
					wlr_output_configuration_head_v1_create(output_config, monitor->output);
			if (head) {
				head->state.enabled = true;
				head->state.mode = monitor->output->current_mode;
				head->state.x = monitor->geometry.x;
				head->state.y = monitor->geometry.y;
			}
		}
	}

	if (server->control->base.element.scene_tree->node.enabled) {
		control_draw_auto(server->control);
	}

	wlr_output_manager_v1_set_configuration(server->output_mgr, output_config);
}

static void server_apply_output_config(struct ptychite_server *server,
		struct wlr_output_configuration_v1 *output_config, bool test) {
	bool ok = true;

	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &output_config->heads, link) {
		struct wlr_output *output = head->state.output;
		struct monitor *monitor = output->data;

		wlr_output_enable(output, head->state.enabled);
		if (head->state.enabled) {
			if (head->state.mode) {
				wlr_output_set_mode(output, head->state.mode);
			} else {
				wlr_output_set_custom_mode(output, head->state.custom_mode.width,
						head->state.custom_mode.height, head->state.custom_mode.refresh);
			}

			if (monitor->geometry.x != head->state.x || monitor->geometry.y != head->state.y) {
				wlr_output_layout_add(server->output_layout, output, head->state.x, head->state.y);
			}
			wlr_output_set_transform(output, head->state.transform);
			wlr_output_set_scale(output, head->state.scale);
			wlr_output_enable_adaptive_sync(output, head->state.adaptive_sync_enabled);
		}

		if (test) {
			ok &= wlr_output_test(output);
			wlr_output_rollback(output);
		} else {
			ok &= wlr_output_commit(output);
		}
	}

	if (ok) {
		wlr_output_configuration_v1_send_succeeded(output_config);
	} else {
		wlr_output_configuration_v1_send_failed(output_config);
	}

	server_update_monitors(server);
}

static void server_handle_output_mgr_apply(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, output_mgr_apply);
	struct wlr_output_configuration_v1 *output_config = data;

	server_apply_output_config(server, output_config, false);
	wlr_output_configuration_v1_destroy(output_config);
}

static void server_handle_output_mgr_test(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, output_mgr_test);
	struct wlr_output_configuration_v1 *output_config = data;

	server_apply_output_config(server, output_config, true);
	wlr_output_configuration_v1_destroy(output_config);
}

static void server_handle_layout_change(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, layout_change);

	server_update_monitors(server);
}

static void server_handle_new_output(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *output = data;

	struct monitor *monitor = calloc(1, sizeof(struct monitor));
	if (!monitor) {
		wlr_log(WLR_ERROR, "Could not initialize output: insufficent memory");
		return;
	}

	wl_list_init(&monitor->workspaces);
	if (!(monitor->current_workspace = monitor_add_workspace(monitor))) {
		wlr_log(WLR_ERROR, "Could not initialize output: insufficent memory");
		free(monitor);
		return;
	}

	if (!wlr_output_init_render(output, server->allocator, server->renderer)) {
		wlr_log(WLR_ERROR, "Could not initialize output render");
		free(monitor);
		return;
	}

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_state_set_scale(&state, server->compositor->config->monitors.default_scale);

	wlr_output_commit_state(output, &state);
	wlr_output_state_finish(&state);

	output->data = monitor;
	monitor->output = output;
	monitor->server = server;
	wl_list_init(&monitor->views);

	monitor->frame.notify = monitor_handle_frame;
	wl_signal_add(&monitor->output->events.frame, &monitor->frame);
	monitor->request_state.notify = monitor_handle_request_state;
	wl_signal_add(&monitor->output->events.request_state, &monitor->request_state);
	monitor->destroy.notify = monitor_handle_destroy;
	wl_signal_add(&monitor->output->events.destroy, &monitor->destroy);

	wl_list_insert(&server->monitors, &monitor->link);
	if (!server->active_monitor) {
		server->active_monitor = monitor;
	}

	if ((monitor->wallpaper = calloc(1, sizeof(struct wallpaper)))) {
		if (!window_init(&monitor->wallpaper->base, server, &wallpaper_window_impl,
					server->layers.bottom, output)) {
			monitor->wallpaper->monitor = monitor;
		} else {
			free(monitor->wallpaper);
			monitor->wallpaper = NULL;
		}
	}

	if ((monitor->panel = calloc(1, sizeof(struct panel)))) {
		if (!window_init(&monitor->panel->base, server, &panel_window_impl, server->layers.bottom,
					output)) {
			monitor->panel->monitor = monitor;
		} else {
			free(monitor->panel);
			monitor->panel = NULL;
		}
	}

	wlr_output_layout_add_auto(server->output_layout, output);
}

static void server_handle_new_xdg_surface(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;

	if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		struct wlr_xdg_surface *parent =
				wlr_xdg_surface_try_from_wlr_surface(xdg_surface->popup->parent);
		assert(parent);
		struct wlr_scene_tree *parent_tree = parent->data;
		struct wlr_scene_tree *scene_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_surface);
		xdg_surface->data = scene_tree;
		return;
	}
	assert(xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

	struct view *view = calloc(1, sizeof(struct view));
	if (!view) {
		wlr_log(WLR_ERROR, "Could not initialize view: insufficent memory");
		return;
	}

	view->element.type = ELEMENT_VIEW;
	view->server = server;
	view->xdg_toplevel = xdg_surface->toplevel;
	view->element.scene_tree = wlr_scene_tree_create(server->layers.tiled);
	xdg_surface->data = view->element.scene_tree;
	view->scene_tree_surface =
			wlr_scene_xdg_surface_create(view->element.scene_tree, view->xdg_toplevel->base);
	view->element.scene_tree->node.data = view->scene_tree_surface->node.data = &view->element;

	float *colors = server->compositor->config->views.border.colors.inactive;
	struct wlr_scene_rect **borders[] = {
			&view->border.top, &view->border.right, &view->border.bottom, &view->border.left};
	size_t i;
	for (i = 0; i < LENGTH(borders); i++) {
		*borders[i] = wlr_scene_rect_create(view->element.scene_tree, 0, 0, colors);
		(*borders[i])->node.data = view;
	}

	if ((view->title_bar = calloc(1, sizeof(struct title_bar)))) {
		if (!window_init(&view->title_bar->base, server, &title_bar_window_impl,
					view->element.scene_tree, NULL)) {
			view->title_bar->view = view;
			wlr_scene_node_set_enabled(&view->title_bar->base.element.scene_tree->node,
					server->compositor->config->views.title_bar.enabled);
			wlr_scene_node_set_enabled(
					&view->border.top->node, !server->compositor->config->views.title_bar.enabled);
		} else {
			free(view->title_bar);
			view->title_bar = NULL;
		}
	}

	view->map.notify = view_handle_map;
	wl_signal_add(&xdg_surface->surface->events.map, &view->map);
	view->unmap.notify = view_handle_unmap;
	wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
	view->destroy.notify = view_handle_destroy;
	wl_signal_add(&xdg_surface->surface->events.destroy, &view->destroy);

	struct wlr_xdg_toplevel *toplevel = xdg_surface->toplevel;
	view->request_maximize.notify = view_handle_request_maximize;
	wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);
	view->request_fullscreen.notify = view_handle_request_fullscreen;
	wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);
}

static void server_handle_idle_inhibitor_create(struct wl_listener *listener, void *data) {
	struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;

	// TODO
}

static void server_handle_new_xdg_decoration(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *xdg_toplevel_decoration = data;

	wlr_xdg_toplevel_decoration_v1_set_mode(
			xdg_toplevel_decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void server_handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	server_process_cursor_motion(server, event->time_msec);
}

static void server_handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;

	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	server_process_cursor_motion(server, event->time_msec);
}

static void server_handle_cursor_button(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	if (event->state == WLR_BUTTON_RELEASED) {
		if (server->cursor_mode != CURSOR_PASSTHROUGH) {
			server->cursor_mode = CURSOR_PASSTHROUGH;
			server->grabbed_view = NULL;
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
		}
	}

	double sx, sy;
	struct wlr_scene_buffer *scene_buffer;
	struct element *element = server_identify_element_at(
			server, server->cursor->x, server->cursor->y, &sx, &sy, &scene_buffer);
	if (element) {
		switch (element->type) {
		case ELEMENT_VIEW:
			if (event->state == WLR_BUTTON_PRESSED) {
				struct view *view = element_get_view(element);
				struct wlr_scene_surface *scene_surface =
						wlr_scene_surface_try_from_buffer(scene_buffer);
				view_focus(view, scene_surface->surface);

				struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
				if (!keyboard) {
					break;
				}

				uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard);
				if (modifiers == WLR_MODIFIER_LOGO) {
					if (event->button == BTN_LEFT) {
						view_begin_interactive(view, CURSOR_MOVE);
						return;
					} else if (event->button == BTN_RIGHT) {
						view_begin_interactive(view, CURSOR_RESIZE);
						return;
					}
				}
			}
			break;
		case ELEMENT_WINDOW: {
			struct window *window = element_get_window(element);
			window_relay_pointer_button(window, sx, sy, event);
			return;
		}
		}
	}

	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

static void server_handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source);
}

static void server_handle_cursor_frame(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, cursor_frame);

	wlr_seat_pointer_notify_frame(server->seat);
}

static void server_handle_new_input(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}

	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void server_handle_seat_request_cursor(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;

	if (focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

static void server_handle_seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct ptychite_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;

	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static struct view *server_get_top_view(struct ptychite_server *server) {
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		return view;
	}

	return NULL;
}

static struct view *server_get_focused_view(struct ptychite_server *server) {
	struct wlr_surface *surface = server->seat->keyboard_state.focused_surface;
	if (!surface) {
		return NULL;
	}

	struct view *view = server_get_top_view(server);
	if (!view) {
		return NULL;
	}

	if (view->xdg_toplevel->base->surface != surface) {
		return NULL;
	}

	return view;
}

static void server_tiling_change_views_in_master(struct ptychite_server *server, int delta) {
	struct monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct workspace *workspace = monitor->current_workspace;
	int views_in_master = workspace->tiling.traditional.views_in_master + delta;
	if (views_in_master > 100) {
		views_in_master = 100;
	} else if (views_in_master < 0) {
		views_in_master = 0;
	}

	if (views_in_master == workspace->tiling.traditional.views_in_master) {
		return;
	}

	workspace->tiling.traditional.views_in_master = views_in_master;
	monitor_tile(monitor);
}

static void server_tiling_change_master_factor(struct ptychite_server *server, double delta) {
	struct monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct workspace *workspace = monitor->current_workspace;
	double master_factor = workspace->tiling.traditional.master_factor + delta;
	if (master_factor > 0.95) {
		master_factor = 0.95;
	} else if (master_factor < 0.05) {
		master_factor = 0.05;
	}

	if (master_factor == workspace->tiling.traditional.master_factor) {
		return;
	}

	workspace->tiling.traditional.master_factor = master_factor;
	monitor_tile(monitor);
}

struct ptychite_server *ptychite_server_create(void) {
	return calloc(1, sizeof(struct ptychite_server));
}

int ptychite_server_init_and_run(
		struct ptychite_server *server, struct ptychite_compositor *compositor) {
	server->compositor = compositor;
	wl_array_init(&server->keys);
	server->terminated = false;

	if (!(server->display = wl_display_create())) {
		return -1;
	};

	if (!(server->backend = wlr_backend_autocreate(server->display, &server->session))) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		return -1;
	}
	if (!(server->renderer = wlr_renderer_autocreate(server->backend))) {
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		return -1;
	}

	if (!wlr_renderer_init_wl_display(server->renderer, server->display)) {
		return -1;
	}

	if (!(server->allocator = wlr_allocator_autocreate(server->backend, server->renderer))) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		return -1;
	}

	wlr_compositor_create(server->display, 5, server->renderer);
	wlr_subcompositor_create(server->display);
	wlr_data_device_manager_create(server->display);

	if (!(server->output_layout = wlr_output_layout_create())) {
		return -1;
	}
	server->layout_change.notify = server_handle_layout_change;
	wl_signal_add(&server->output_layout->events.change, &server->layout_change);
	wlr_xdg_output_manager_v1_create(server->display, server->output_layout);

	if (!(server->output_mgr = wlr_output_manager_v1_create(server->display))) {
		return -1;
	}
	server->output_mgr_apply.notify = server_handle_output_mgr_apply;
	wl_signal_add(&server->output_mgr->events.apply, &server->output_mgr_apply);
	server->output_mgr_test.notify = server_handle_output_mgr_test;
	wl_signal_add(&server->output_mgr->events.test, &server->output_mgr_test);

	wl_list_init(&server->monitors);
	server->new_output.notify = server_handle_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);

	if (!(server->scene = wlr_scene_create())) {
		return -1;
	}
	struct wlr_scene_tree **layers[] = {&server->layers.background, &server->layers.bottom,
			&server->layers.tiled, &server->layers.floating, &server->layers.fullscreen,
			&server->layers.top, &server->layers.overlay, &server->layers.block};
	size_t i;
	for (i = 0; i < LENGTH(layers); i++) {
		struct wlr_scene_tree **layer = layers[i];
		if (!(*layer = wlr_scene_tree_create(&server->scene->tree))) {
			return -1;
		}
	}
	if (!wlr_scene_attach_output_layout(server->scene, server->output_layout)) {
		return -1;
	}

	server->idle = wlr_idle_create(server->display);
	server->idle_notifier = wlr_idle_notifier_v1_create(server->display);
	server->idle_inhibit_mgr = wlr_idle_inhibit_v1_create(server->display);
	server->idle_inhibitor_create.notify = server_handle_idle_inhibitor_create;
	wl_signal_add(&server->idle_inhibit_mgr->events.new_inhibitor, &server->idle_inhibitor_create);

	wl_list_init(&server->views);
	server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
	server->new_xdg_surface.notify = server_handle_new_xdg_surface;
	wl_signal_add(&server->xdg_shell->events.new_surface, &server->new_xdg_surface);

	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

	server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	server->cursor_mode = CURSOR_PASSTHROUGH;
	server->cursor_motion.notify = server_handle_cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = server_handle_cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
	server->cursor_button.notify = server_handle_cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = server_handle_cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = server_handle_cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");

	wl_list_init(&server->keyboards);
	server->new_input.notify = server_handle_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);
	server->seat = wlr_seat_create(server->display, "seat0");
	server->request_cursor.notify = server_handle_seat_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
	server->request_set_selection.notify = server_handle_seat_request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);

	wl_global_create(
			server->display, &zptychite_message_v1_interface, 1, server, message_handle_bind);

	wlr_viewporter_create(server->display);
	wlr_single_pixel_buffer_manager_v1_create(server->display);
	wlr_gamma_control_manager_v1_create(server->display);
	wlr_screencopy_manager_v1_create(server->display);
	wlr_export_dmabuf_manager_v1_create(server->display);
	wlr_single_pixel_buffer_manager_v1_create(server->display);
	wlr_fractional_scale_manager_v1_create(server->display, 1);
	wlr_data_control_manager_v1_create(server->display);

	struct wlr_server_decoration_manager *server_decoration_manager =
			wlr_server_decoration_manager_create(server->display);
	if (!server_decoration_manager) {
		return -1;
	}
	wlr_server_decoration_manager_set_default_mode(
			server_decoration_manager, WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager =
			wlr_xdg_decoration_manager_v1_create(server->display);
	if (!xdg_decoration_manager) {
		return -1;
	}
	server->new_xdg_decoration.notify = server_handle_new_xdg_decoration;
	wl_signal_add(
			&xdg_decoration_manager->events.new_toplevel_decoration, &server->new_xdg_decoration);

	if (!(server->control = calloc(1, sizeof(struct control)))) {
		return -1;
	}
	if (window_init(&server->control->base, server, &control_window_impl, server->layers.overlay,
				NULL)) {
		return -1;
	}
	control_hide(server->control);

	if (!(server->time_tick = wl_event_loop_add_timer(
				  wl_display_get_event_loop(server->display), server_time_tick_update, server))) {
		return -1;
	}
	server_time_tick_update(server);

	const char *socket = wl_display_add_socket_auto(server->display);
	if (!socket) {
		wlr_backend_destroy(server->backend);
		return -1;
	}

	if (!wlr_backend_start(server->backend)) {
		wlr_backend_destroy(server->backend);
		wl_display_destroy(server->display);
		return -1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);

	wlr_log(WLR_INFO, "Running ptychite on WAYLAND_DISPLAY=%s", socket);
	wl_display_run(server->display);
	server->terminated = true;

	wl_display_destroy_clients(server->display);
	wlr_scene_node_destroy(&server->scene->tree.node);
	wlr_xcursor_manager_destroy(server->cursor_mgr);
	wlr_output_layout_destroy(server->output_layout);
	wl_display_destroy(server->display);

	return 0;
}

void ptychite_server_terminate(struct ptychite_server *server) {
	wl_display_terminate(server->display);
}

int ptychite_server_close_focused_client(struct ptychite_server *server) {
	struct view *view = server_get_focused_view(server);
	if (!view) {
		return -1;
	}

	wlr_xdg_toplevel_send_close(view->xdg_toplevel);
	return 0;
}

void ptychite_server_configure_keyboards(struct ptychite_server *server) {
	struct ptychite_config *config = server->compositor->config;

	struct keyboard *keyboard;
	wl_list_for_each(keyboard, &server->keyboards, link) {
		wlr_keyboard_set_repeat_info(
				keyboard->keyboard, config->keyboard.repeat.rate, config->keyboard.repeat.delay);

		struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (!context) {
			continue;
		}
		const struct xkb_rule_names rule_names = {
				.options = config->keyboard.xkb.options,
				.rules = NULL,
				.layout = NULL,
				.model = NULL,
				.variant = NULL,
		};
		struct xkb_keymap *keymap =
				xkb_keymap_new_from_names(context, &rule_names, XKB_KEYMAP_COMPILE_NO_FLAGS);
		if (!keymap) {
			xkb_context_unref(context);
			continue;
		}
		wlr_keyboard_set_keymap(keyboard->keyboard, keymap);
		xkb_keymap_unref(keymap);
		xkb_context_unref(context);
	}
}

void ptychite_server_configure_panels(struct ptychite_server *server) {
	struct monitor *monitor;
	wl_list_for_each(monitor, &server->monitors, link) {
		if (!monitor->panel) {
			continue;
		}

		wlr_scene_node_set_enabled(&monitor->panel->base.element.scene_tree->node,
				server->compositor->config->panel.enabled);
		if (monitor->panel->base.element.scene_tree->node.enabled) {
			panel_draw_auto(monitor->panel);
		} else {
			monitor->window_geometry = monitor->geometry;
		}
		monitor_tile(monitor);
	}

	if (server->control->base.element.scene_tree->node.enabled) {
		control_draw_auto(server->control);
	}
}

void ptychite_server_configure_views(struct ptychite_server *server) {
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->title_bar) {
			wlr_scene_node_set_enabled(&view->title_bar->base.element.scene_tree->node,
					server->compositor->config->views.title_bar.enabled);
			wlr_scene_node_set_enabled(
					&view->border.top->node, !server->compositor->config->views.title_bar.enabled);
		}
		view_resize(view, view->element.width, view->element.height);
	}

	ptychite_server_check_cursor(server);
}

void ptychite_server_toggle_control(struct ptychite_server *server) {
	if (server->control->base.element.scene_tree->node.enabled) {
		control_hide(server->control);
	} else {
		control_show(server->control);
	}
}

void ptychite_server_refresh_wallpapers(struct ptychite_server *server) {
	struct monitor *monitor;
	wl_list_for_each(monitor, &server->monitors, link) {
		if (!monitor->wallpaper) {
			continue;
		}

		wallpaper_draw_auto(monitor->wallpaper);
	}
}

void ptychite_server_retile(struct ptychite_server *server) {
	struct monitor *monitor;
	wl_list_for_each(monitor, &server->monitors, link) {
		monitor_tile(monitor);
	}
}

void ptychite_server_tiling_increase_views_in_master(struct ptychite_server *server) {
	server_tiling_change_views_in_master(server, 1);
}

void ptychite_server_tiling_decrease_views_in_master(struct ptychite_server *server) {
	server_tiling_change_views_in_master(server, -1);
}

void ptychite_server_tiling_increase_master_factor(struct ptychite_server *server) {
	server_tiling_change_master_factor(server, 0.05);
}

void ptychite_server_tiling_decrease_master_factor(struct ptychite_server *server) {
	server_tiling_change_master_factor(server, -0.05);
}

void ptychite_server_tiling_toggle_right_master(struct ptychite_server *server) {
	struct monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct workspace *workspace = monitor->current_workspace;
	workspace->tiling.traditional.right_master = !workspace->tiling.traditional.right_master;

	monitor_tile(monitor);
}

void ptychite_server_check_cursor(struct ptychite_server *server) {
	server_process_cursor_motion(server, 0);
}

void ptychite_server_goto_next_workspace(struct ptychite_server *server) {
	struct monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct wl_list *list = monitor->current_workspace->link.next == &monitor->workspaces
			? monitor->current_workspace->link.next->next
			: monitor->current_workspace->link.next;
	struct workspace *workspace = wl_container_of(list, workspace, link);

	monitor_switch_workspace(monitor, workspace);
}

void ptychite_server_goto_previous_workspace(struct ptychite_server *server) {
	struct monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct wl_list *list = monitor->current_workspace->link.prev == &monitor->workspaces
			? monitor->current_workspace->link.prev->prev
			: monitor->current_workspace->link.prev;
	struct workspace *workspace = wl_container_of(list, workspace, link);

	monitor_switch_workspace(monitor, workspace);
}

void ptychite_server_add_workspace(struct ptychite_server *server) {
	struct monitor *monitor = server->active_monitor;
	if (!monitor) {
		return;
	}

	struct workspace *workspace = monitor_add_workspace(monitor);
	if (!workspace) {
		return;
	}

	monitor_switch_workspace(monitor, workspace);
}
