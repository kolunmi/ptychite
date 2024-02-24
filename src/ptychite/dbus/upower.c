#define _POSIX_C_SOURCE 200809L

#include <wlr/util/log.h>

#include "../dbus.h"
#include "../icon.h"
#include "../monitor.h"
#include "../notification.h"
#include "../server.h"

struct upower_handle_property_data {
	struct ptychite_server *server;
	bool redraw_panel;
};

static int handle_property(const char *property, sd_bus_message *msg, void *data) {
	struct upower_handle_property_data *state = data;
	int ret;

	if (!strcmp(property, "OnBattery")) {
		bool on_battery;
		ret = sd_bus_message_read(msg, "v", "b", &on_battery);
		if (ret < 0) {
			return ret;
		}
		state->server->dbus.battery.enabled = on_battery;
		state->redraw_panel = true;
	} else {
		ret = sd_bus_message_skip(msg, "v");
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int handle_device_property(const char *property, sd_bus_message *msg, void *data) {
	struct upower_handle_property_data *state = data;
	int ret;

	if (!strcmp(property, "Percentage")) {
		double percent;
		ret = sd_bus_message_read(msg, "v", "d", &percent);
		if (ret < 0) {
			return ret;
		}
		state->server->dbus.battery.percent = percent;
		state->redraw_panel = true;
	} else {
		ret = sd_bus_message_skip(msg, "v");
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int handle_upower(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error) {
	struct ptychite_server *server = userdata;

	const char *interface;
	int ret = sd_bus_message_read(msg, "s", &interface);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Error reading message: %s", strerror(-ret));
		return ret;
	}

	struct upower_handle_property_data data = {
			.server = server,
			.redraw_panel = false,
	};

	ret = ptychite_dbus_read_properties_changed_event(
			msg, !strcmp(interface, "org.freedesktop.UPower.Device") ? handle_device_property : handle_property, &data);
	if (ret < 0) {
		return ret;
	}

	if (data.redraw_panel) {
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
	int ret = sd_bus_get_property(server->dbus.system_bus, "org.freedesktop.UPower", "/org/freedesktop/UPower",
			"org.freedesktop.UPower", "OnBattery", NULL, &reply, "b");
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to get property: %s", strerror(-ret));
		return ret;
	}

	bool on_battery;
	ret = sd_bus_message_read(reply, "b", &on_battery);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to parse message: %s", strerror(-ret));
		return ret;
	}
	server->dbus.battery.enabled = on_battery;
	sd_bus_message_unref(reply);

	sd_bus_add_match(server->dbus.system_bus, NULL,
			"type='signal',"
			"interface='org.freedesktop.DBus.Properties',"
			"member='PropertiesChanged',"
			"path='/org/freedesktop/UPower',"
			"arg0='org.freedesktop.UPower'",
			handle_upower, server);

	ret = sd_bus_get_property(server->dbus.system_bus, "org.freedesktop.UPower",
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
	server->dbus.battery.percent = percent;
	sd_bus_message_unref(reply);

	return sd_bus_add_match(server->dbus.system_bus, NULL,
			"type='signal',"
			"interface='org.freedesktop.DBus.Properties',"
			"member='PropertiesChanged',"
			"path='/org/freedesktop/UPower/devices/DisplayDevice',"
			"arg0='org.freedesktop.UPower.Device'",
			handle_upower, server);
}
