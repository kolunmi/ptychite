#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include <wlr/util/log.h>

#include "dbus.h"
#include "server.h"

static int handle_dbus(int fd, uint32_t mask, void *data) {
	sd_bus *bus = data;

	int ret;
	do {
		ret = sd_bus_process(bus, NULL);
	} while (ret > 0);

	/* ... */

	return 0;
}

int ptychite_dbus_init(struct ptychite_server *server) {
	int ret = 0;
	server->bus = NULL;
	server->xdg_slot = NULL;
	server->ptychite_slot = NULL;
	server->system_bus = NULL;

	wl_list_init(&server->notifications);
	wl_list_init(&server->history);

	ret = sd_bus_open_user(&server->bus);
	if (ret < 0) {
		fprintf(stderr, "Failed to connect to the user bus: %s\n", strerror(-ret));
		goto err;
	}

	ret = ptychite_dbus_init_ptychite(server);
	if (ret < 0) {
		fprintf(stderr, "Failed to initialize Ptychite interface: %s\n", strerror(-ret));
		goto err;
	}
	ret = ptychite_dbus_init_xdg(server);
	if (ret < 0) {
		fprintf(stderr, "Failed to initialize XDG interface: %s\n", strerror(-ret));
		goto err;
	}

	ret = sd_bus_request_name(server->bus, "org.freedesktop.Notifications", 0);
	if (ret < 0) {
		fprintf(stderr, "Failed to acquire service name: %s\n", strerror(-ret));
		if (ret == -EEXIST) {
			fprintf(stderr, "Is a notification daemon already running?\n");
		}
		goto err;
	}

	ret = sd_bus_open_system(&server->system_bus);
	if (ret < 0) {
		fprintf(stderr, "Failed to connect to the system bus: %s\n", strerror(-ret));
		goto err;
	}

	ret = ptychite_dbus_init_nm(server);
	if (ret < 0) {
		fprintf(stderr, "Failed to connect to NetworkManager: %s\n", strerror(-ret));
		goto err;
	}

	ret = ptychite_dbus_init_upower(server);
	if (ret < 0) {
		fprintf(stderr, "Failed to connect to UPower: %s\n", strerror(-ret));
		goto err;
	}

	server->dbus_active = true;

	struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
	wl_event_loop_add_fd(loop, sd_bus_get_fd(server->bus), WL_EVENT_READABLE, handle_dbus, server->bus);
	wl_event_loop_add_fd(loop, sd_bus_get_fd(server->system_bus), WL_EVENT_READABLE, handle_dbus, server->system_bus);

	return 0;

err:
	ptychite_dbus_finish(server);
	server->dbus_active = false;
	return -1;
}

void ptychite_dbus_finish(struct ptychite_server *server) {
	sd_bus_slot_unref(server->xdg_slot);
	sd_bus_slot_unref(server->ptychite_slot);
	sd_bus_flush_close_unref(server->bus);
	sd_bus_flush_close_unref(server->system_bus);
}
