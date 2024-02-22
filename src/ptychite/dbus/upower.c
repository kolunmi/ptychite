#define _POSIX_C_SOURCE 200809L

#include <wlr/util/log.h>

#include "../dbus.h"
#include "../icon.h"
#include "../monitor.h"
#include "../notification.h"
#include "../server.h"

static int handle_upower(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error) {
	struct ptychite_server *server = userdata;
	bool redraw_panel = false;
	int ret;

	ret = sd_bus_message_skip(msg, "s");
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Error skipping message fields: %s", strerror(-ret));
		return ret;
	}

	ret = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "{sv}");
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Error entering container: %s", strerror(-ret));
		return ret;
	}

	while ((ret = sd_bus_message_enter_container(msg, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
		char *property;
		ret = sd_bus_message_read(msg, "s", &property);
		if (ret < 0) {
			wlr_log(WLR_ERROR, "Error reading message: %s", strerror(-ret));
			return ret;
		}

		if (!strcmp(property, "Percentage")) {
			double percent;
			ret = sd_bus_message_read(msg, "v", "d", &percent);
			if (ret < 0) {
				wlr_log(WLR_ERROR, "Error reading message: %s", strerror(-ret));
				return ret;
			}
			server->battery.percent = percent;
			redraw_panel = true;
		} else {
			ret = sd_bus_message_skip(msg, "v");
			if (ret < 0) {
				wlr_log(WLR_ERROR, "Failed to skip: %s", strerror(-ret));
				return ret;
			}
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

	if (redraw_panel) {
		struct ptychite_monitor *monitor;
		wl_list_for_each(monitor, &server->monitors, link) {
			if (!monitor->panel) {
				continue;
			}
			ptychite_panel_draw_auto(monitor->panel);
		}
	}

	return 0;
}

int ptychite_dbus_init_upower(struct ptychite_server *server) {
	sd_bus_message *reply;
	int ret = sd_bus_get_property(server->system_bus, "org.freedesktop.UPower",
			"/org/freedesktop/UPower/devices/DisplayDevice", "org.freedesktop.UPower.Device", "Percentage", NULL,
			&reply, "d");
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to get property: %s", strerror(-ret));
		return ret;
	}

	double percent;
	ret = sd_bus_message_read(reply, "d", &percent);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to parse message: %s", strerror(-ret));
		return ret;
	}
	server->battery.percent = percent;
	sd_bus_message_unref(reply);

	server->battery.enabled = true;

	return sd_bus_add_match(server->system_bus, NULL,
			"type='signal',"
			"interface='org.freedesktop.DBus.Properties',"
			"member='PropertiesChanged',"
			"path='/org/freedesktop/UPower/devices/DisplayDevice',"
			"arg0='org.freedesktop.UPower.Device'",
			handle_upower, server);
}
