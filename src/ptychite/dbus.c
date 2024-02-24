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
	server->dbus.user_bus = NULL;
	server->dbus.xdg_slot = NULL;
	server->dbus.ptychite_slot = NULL;
	server->dbus.system_bus = NULL;

	wl_list_init(&server->notifications.active);
	wl_list_init(&server->notifications.history);

	ret = sd_bus_open_user(&server->dbus.user_bus);
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

	ret = sd_bus_request_name(server->dbus.user_bus, "org.freedesktop.Notifications", 0);
	if (ret < 0) {
		fprintf(stderr, "Failed to acquire service name: %s\n", strerror(-ret));
		if (ret == -EEXIST) {
			fprintf(stderr, "Is a notification daemon already running?\n");
		}
		goto err;
	}

	ret = sd_bus_open_system(&server->dbus.system_bus);
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

	server->dbus.active = true;

	struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
	wl_event_loop_add_fd(
			loop, sd_bus_get_fd(server->dbus.user_bus), WL_EVENT_READABLE, handle_dbus, server->dbus.user_bus);
	wl_event_loop_add_fd(
			loop, sd_bus_get_fd(server->dbus.system_bus), WL_EVENT_READABLE, handle_dbus, server->dbus.system_bus);

	return 0;

err:
	ptychite_dbus_finish(server);
	server->dbus.active = false;
	return -1;
}

void ptychite_dbus_finish(struct ptychite_server *server) {
	sd_bus_slot_unref(server->dbus.xdg_slot);
	sd_bus_slot_unref(server->dbus.ptychite_slot);
	sd_bus_flush_close_unref(server->dbus.user_bus);
	sd_bus_flush_close_unref(server->dbus.system_bus);
}

int ptychite_dbus_read_properties_changed_event(
		sd_bus_message *msg, ptychite_dbus_properties_changed_func_t handler, void *data) {
	int ret = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "{sv}");
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Error entering container: %s", strerror(-ret));
		return ret;
	}

	while ((ret = sd_bus_message_enter_container(msg, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
		const char *property;
		ret = sd_bus_message_read(msg, "s", &property);
		if (ret < 0) {
			wlr_log(WLR_ERROR, "Error reading message: %s", strerror(-ret));
			return ret;
		}

		ret = handler(property, msg, data);
		if (ret < 0) {
			wlr_log(WLR_ERROR, "Failed to handle property change: %s", strerror(-ret));
		}

		ret = sd_bus_message_exit_container(msg);
		if (ret < 0) {
			wlr_log(WLR_ERROR, "Failed to exit container: %s", strerror(-ret));
			return ret;
		}
	}

	ret = sd_bus_message_exit_container(msg);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to exit container: %s", strerror(-ret));
		return ret;
	}

	return 0;
}
