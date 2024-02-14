#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <linux/input-event-codes.h>
#include <pango/pango-markup.h>
#include <stdlib.h>

#include "dbus.h"
#include "icon.h"
#include "server.h"
#include "windows.h"

bool hotspot_at(struct wlr_box *hotspot, int32_t x, int32_t y) {
	return x >= hotspot->x && y >= hotspot->y && x < hotspot->x + hotspot->width && y < hotspot->y + hotspot->height;
}

void reset_notification(struct ptychite_notification *notif) {
	struct ptychite_notification_action *action, *tmp;
	wl_list_for_each_safe(action, tmp, &notif->actions, link) {
		wl_list_remove(&action->link);
		free(action->key);
		free(action->title);
		free(action);
	}

	notif->urgency = PTYCHITE_NOTIFICATION_URGENCY_UNKNOWN;
	notif->progress = -1;

	/* destroy_timer(notif->timer); */
	notif->timer = NULL;

	free(notif->app_name);
	free(notif->app_icon);
	free(notif->summary);
	free(notif->body);
	free(notif->category);
	free(notif->desktop_entry);
	free(notif->tag);
	if (notif->image_data != NULL) {
		free(notif->image_data->data);
		free(notif->image_data);
	}

	notif->app_name = strdup("");
	notif->app_icon = strdup("");
	notif->summary = strdup("");
	notif->body = strdup("");
	notif->category = strdup("");
	notif->desktop_entry = strdup("");
	notif->tag = strdup("");

	notif->image_data = NULL;

	destroy_icon(notif->icon);
	notif->icon = NULL;
}

struct ptychite_notification *create_notification(struct ptychite_server *server) {
	struct ptychite_notification *notif = calloc(1, sizeof(struct ptychite_notification));
	if (notif == NULL) {
		fprintf(stderr, "allocation failed\n");
		return NULL;
	}

	notif->server = server;
	++server->last_id;
	notif->id = server->last_id;
	wl_list_init(&notif->actions);
	wl_list_init(&notif->link);
	reset_notification(notif);

	// Start ungrouped.
	notif->group_index = -1;

	return notif;
}

void destroy_notification(struct ptychite_notification *notif) {
	wl_list_remove(&notif->link);

	reset_notification(notif);

	free(notif->app_name);
	free(notif->app_icon);
	free(notif->summary);
	free(notif->body);
	free(notif->category);
	free(notif->desktop_entry);
	free(notif->tag);

	free(notif);
}

void close_notification(
		struct ptychite_notification *notif, enum ptychite_notification_close_reason reason, bool add_to_history) {
	notify_notification_closed(notif, reason);
	wl_list_remove(&notif->link); // Remove so regrouping works...
	wl_list_init(&notif->link); // ...but destroy will remove again.

	/* destroy_timer(notif->timer); */
	notif->timer = NULL;

	if (add_to_history) {
		wl_list_insert(&notif->server->history, &notif->link);
		while (wl_list_length(&notif->server->history) > 5) {
			struct ptychite_notification *n = wl_container_of(notif->server->history.prev, n, link);
			destroy_notification(n);
		}
	} else {
		destroy_notification(notif);
	}
}

struct ptychite_notification *get_notification(struct ptychite_server *server, uint32_t id) {
	struct ptychite_notification *notif;
	wl_list_for_each(notif, &server->notifications, link) {
		if (notif->id == id) {
			return notif;
		}
	}
	return NULL;
}

struct ptychite_notification *get_tagged_notification(
		struct ptychite_server *server, const char *tag, const char *app_name) {
	struct ptychite_notification *notif;
	wl_list_for_each(notif, &server->notifications, link) {
		if (notif->tag && strlen(notif->tag) != 0 && strcmp(notif->tag, tag) == 0 &&
				strcmp(notif->app_name, app_name) == 0) {
			return notif;
		}
	}
	return NULL;
}

void close_all_notifications(struct ptychite_server *server, enum ptychite_notification_close_reason reason) {
	struct ptychite_notification *notif, *tmp;
	wl_list_for_each_safe(notif, tmp, &server->notifications, link) {
		close_notification(notif, reason, true);
	}
}

static size_t trim_space(char *dst, const char *src) {
	size_t src_len = strlen(src);
	const char *start = src;
	const char *end = src + src_len;

	while (start != end && isspace(start[0])) {
		++start;
	}

	while (end != start && isspace(end[-1])) {
		--end;
	}

	size_t trimmed_len = end - start;
	memmove(dst, start, trimmed_len);
	dst[trimmed_len] = '\0';
	return trimmed_len;
}

static const char *escape_markup_char(char c) {
	switch (c) {
	case '&':
		return "&amp;";
	case '<':
		return "&lt;";
	case '>':
		return "&gt;";
	case '\'':
		return "&apos;";
	case '"':
		return "&quot;";
	}
	return NULL;
}

static size_t escape_markup(const char *s, char *buf) {
	size_t len = 0;
	while (s[0] != '\0') {
		const char *replacement = escape_markup_char(s[0]);
		if (replacement != NULL) {
			size_t replacement_len = strlen(replacement);
			if (buf != NULL) {
				memcpy(buf + len, replacement, replacement_len);
			}
			len += replacement_len;
		} else {
			if (buf != NULL) {
				buf[len] = s[0];
			}
			++len;
		}
		++s;
	}
	if (buf != NULL) {
		buf[len] = '\0';
	}
	return len;
}

// Any new format specifiers must also be added to VALID_FORMAT_SPECIFIERS.

char *format_hidden_text(char variable, bool *markup, void *data) {
	struct ptychite_hidden_format_data *format_data = data;
	switch (variable) {
	case 'h':
		return ptychite_asprintf("%zu", format_data->hidden);
	case 't':
		return ptychite_asprintf("%zu", format_data->count);
	}
	return NULL;
}

char *format_notif_text(char variable, bool *markup, void *data) {
	struct ptychite_notification *notif = data;
	switch (variable) {
	case 'a':
		return strdup(notif->app_name);
	case 'i':
		return ptychite_asprintf("%d", notif->id);
	case 's':
		return strdup(notif->summary);
	case 'b':
		*markup = notif->markup_enabled;
		return strdup(notif->body);
	case 'g':
		return ptychite_asprintf("%d", notif->group_count);
	}
	return NULL;
}

size_t format_text(const char *format, char *buf, ptychite_format_func_t format_func, void *data) {
	size_t len = 0;

	const char *last = format;
	while (1) {
		char *current = strchr(last, '%');
		if (current == NULL || current[1] == '\0') {
			size_t tail_len = strlen(last);
			if (buf != NULL) {
				memcpy(buf + len, last, tail_len + 1);
			}
			len += tail_len;
			break;
		}

		size_t chunk_len = current - last;
		if (buf != NULL) {
			memcpy(buf + len, last, chunk_len);
		}
		len += chunk_len;

		char *value = NULL;
		bool markup = false;

		if (current[1] == '%') {
			value = strdup("%");
		} else {
			value = format_func(current[1], &markup, data);
		}
		if (value == NULL) {
			value = strdup("");
		}

		size_t value_len;
		if (!markup || !pango_parse_markup(value, -1, 0, NULL, NULL, NULL, NULL)) {
			char *escaped = NULL;
			if (buf != NULL) {
				escaped = buf + len;
			}
			value_len = escape_markup(value, escaped);
		} else {
			value_len = strlen(value);
			if (buf != NULL) {
				memcpy(buf + len, value, value_len);
			}
		}
		free(value);

		len += value_len;
		last = current + 2;
	}

	if (buf != NULL) {
		trim_space(buf, buf);
	}
	return len;
}

void insert_notification(struct ptychite_server *server, struct ptychite_notification *notif) {
	struct wl_list *insert_node;

	insert_node = &server->notifications;
	wl_list_insert(insert_node, &notif->link);
}
