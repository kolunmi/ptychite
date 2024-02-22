#ifndef PTYCHITE_DBUS_H
#define PTYCHITE_DBUS_H

#include <systemd/sd-bus.h>

#include "notification.h"

int ptychite_dbus_init(struct ptychite_server *server);
void ptychite_dbus_finish(struct ptychite_server *server);

int init_dbus_ptychite(struct ptychite_server *server);

int ptychite_dbus_init_xdg(struct ptychite_server *server);
void ptychite_dbus_notify_notification_closed(struct ptychite_notification *notif, enum ptychite_notification_close_reason reason);
void ptychite_dbus_notify_action_invoked(struct ptychite_notification_action *action, const char *activation_token);

#endif
