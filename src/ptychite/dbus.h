#ifndef PTYCHITE_DBUS_H
#define PTYCHITE_DBUS_H

#include <systemd/sd-bus.h>

#include "notification.h"

int init_dbus(struct ptychite_server *server);
void finish_dbus(struct ptychite_server *server);

int init_dbus_xdg(struct ptychite_server *server);
void notify_notification_closed(struct ptychite_notification *notif, enum ptychite_notification_close_reason reason);
void notify_action_invoked(struct ptychite_notification_action *action, const char *activation_token);

#endif
