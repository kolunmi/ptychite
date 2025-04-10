#ifndef PTYCHITE_NOTIFICATION_H
#define PTYCHITE_NOTIFICATION_H

#include <wlr/util/box.h>

struct ptychite_server;
struct ptychite_notification;

enum ptychite_notification_urgency {
	PTYCHITE_NOTIFICATION_URGENCY_LOW = 0,
	PTYCHITE_NOTIFICATION_URGENCY_NORMAL = 1,
	PTYCHITE_NOTIFICATION_URGENCY_CRITICAL = 2,
	PTYCHITE_NOTIFICATION_URGENCY_UNKNOWN = -1,
};

enum ptychite_notification_close_reason {
	PTYCHITE_NOTIFICATION_CLOSE_EXPIRED = 1,
	PTYCHITE_NOTIFICATION_CLOSE_DISMISSED = 2,
	PTYCHITE_NOTIFICATION_CLOSE_REQUEST = 3,
	PTYCHITE_NOTIFICATION_CLOSE_UNKNOWN = 4,
};

struct ptychite_notification_action {
	struct wl_list link;
	struct ptychite_notification *notification;
	char *key;
	char *title;
};

struct ptychite_hidden_format_data {
	size_t hidden;
	size_t count;
};

void ptychite_notification_reset(struct ptychite_notification *notif);
struct ptychite_notification *ptychite_notification_create(struct ptychite_server *server);
void ptychite_notification_destroy(struct ptychite_notification *notif);

void ptychite_notification_close(
		struct ptychite_notification *notif, enum ptychite_notification_close_reason reason, bool add_to_history);
void ptychite_server_close_all_notifications(struct ptychite_server *server, enum ptychite_notification_close_reason reason);

void close_group_notifications(struct ptychite_notification *notif, enum ptychite_notification_close_reason reason);

typedef char *(*ptychite_format_func_t)(char variable, bool *markup, void *data);

char *format_hidden_text(char variable, bool *markup, void *data);
char *format_notif_text(char variable, bool *markup, void *data);
size_t format_text(const char *format, char *buf, ptychite_format_func_t func, void *data);

struct ptychite_notification *ptychite_server_get_notification(struct ptychite_server *server, uint32_t id);

struct ptychite_notification *get_tagged_notification(
		struct ptychite_server *server, const char *tag, const char *app_name);

size_t format_notification(struct ptychite_notification *notif, const char *format, char *buf);
void insert_notification(struct ptychite_server *server, struct ptychite_notification *notif);

void ptychite_server_arrange_notifications(struct ptychite_server *server);

#endif
