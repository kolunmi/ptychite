#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include <wlr/util/log.h>

#include "dbus.h"
#include "server.h"

static int handle_dbus(int fd, uint32_t mask, void *data) {
	struct ptychite_server *server = data;

	int ret;
	do {
		ret = sd_bus_process(server->bus, NULL);
	} while (ret > 0);

	/* ... */

	return 0;
}

static const char service_name[] = "org.freedesktop.Notifications";

int init_dbus(struct ptychite_server *server) {
	int ret = 0;
	server->bus = NULL;
	server->xdg_slot = NULL;

	ret = sd_bus_open_user(&server->bus);
	if (ret < 0) {
		fprintf(stderr, "Failed to connect to user bus: %s\n", strerror(-ret));
		goto err;
	}

	ret = init_dbus_xdg(server);
	if (ret < 0) {
		fprintf(stderr, "Failed to initialize XDG interface: %s\n", strerror(-ret));
		goto err;
	}

	ret = sd_bus_request_name(server->bus, service_name, 0);
	if (ret < 0) {
		fprintf(stderr, "Failed to acquire service name: %s\n", strerror(-ret));
		if (ret == -EEXIST) {
			fprintf(stderr, "Is a notification daemon already running?\n");
		}
		goto err;
	}

	server->dbus_active = true;
	wl_list_init(&server->notifications);
	wl_event_loop_add_fd(wl_display_get_event_loop(server->display), sd_bus_get_fd(server->bus), WL_EVENT_READABLE,
			handle_dbus, server);

	return 0;

err:
	finish_dbus(server);
	server->dbus_active = false;
	return -1;
}

void finish_dbus(struct ptychite_server *server) {
	sd_bus_slot_unref(server->xdg_slot);
	sd_bus_flush_close_unref(server->bus);
}
