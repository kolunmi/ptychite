#ifndef PTYCHITE_DBUS_H
#define PTYCHITE_DBUS_H

#include <systemd/sd-bus.h>

#include "notification.h"

int ptychite_dbus_init(struct ptychite_server *server);
void ptychite_dbus_finish(struct ptychite_server *server);

int ptychite_dbus_init_ptychite(struct ptychite_server *server);

int ptychite_dbus_init_xdg(struct ptychite_server *server);
void ptychite_dbus_notify_notification_closed(struct ptychite_notification *notif, enum ptychite_notification_close_reason reason);
void ptychite_dbus_notify_action_invoked(struct ptychite_notification_action *action, const char *activation_token);

int ptychite_dbus_init_nm(struct ptychite_server *server);
int ptychite_dbus_init_upower(struct ptychite_server *server);

typedef int (*ptychite_dbus_properties_changed_func_t)(const char *property, sd_bus_message *msg, void *data);
int ptychite_dbus_read_properties_changed_event(sd_bus_message *msg, ptychite_dbus_properties_changed_func_t handler, void *data);

#endif
