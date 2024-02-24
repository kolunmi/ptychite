#define _POSIX_C_SOURCE 200809L

#include <wlr/util/log.h>

#include "../dbus.h"
#include "../icon.h"
#include "../monitor.h"
#include "../notification.h"
#include "../server.h"

struct nm_handle_property_data {
	struct ptychite_server *server;
	bool redraw_panel;
};

static int handle_property(const char *property, sd_bus_message *msg, void *data) {
	struct nm_handle_property_data *state = data;
	int ret;

	if (!strcmp(property, "State")) {
		uint32_t nm_state;
		ret = sd_bus_message_read(msg, "v", "u", &nm_state);
		if (ret < 0) {
			return ret;
		}
		bool connected = nm_state == 70;
		state->redraw_panel = state->server->dbus.internet != connected;
		state->server->dbus.internet = connected;
	} else {
		ret = sd_bus_message_skip(msg, "v");
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int handle_nm(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error) {
	struct ptychite_server *server = userdata;

	int ret = sd_bus_message_skip(msg, "s");
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Error skipping message fields: %s", strerror(-ret));
		return ret;
	}

	struct nm_handle_property_data data = {
			.server = server,
			.redraw_panel = false,
	};

	ret = ptychite_dbus_read_properties_changed_event(msg, handle_property, &data);
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

int ptychite_dbus_init_nm(struct ptychite_server *server) {
	sd_bus_message *reply;
	int ret = sd_bus_call_method(server->dbus.system_bus, "org.freedesktop.NetworkManager",
			"/org/freedesktop/NetworkManager", "org.freedesktop.NetworkManager", "state", NULL, &reply, NULL);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to call method: %s", strerror(-ret));
		return ret;
	}

	uint32_t state;
	ret = sd_bus_message_read(reply, "u", &state);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to parse message: %s", strerror(-ret));
		return ret;
	}
	server->dbus.internet = state == 70;
	sd_bus_message_unref(reply);

	return sd_bus_add_match(server->dbus.system_bus, NULL,
			"type='signal',"
			"interface='org.freedesktop.DBus.Properties',"
			"member='PropertiesChanged',"
			"path='/org/freedesktop/NetworkManager',"
			"arg0='org.freedesktop.NetworkManager'",
			handle_nm, server);
}
