#define _POSIX_C_SOURCE 200809L
#include <wlr/util/log.h>

#include "compositor.h"
#include "config.h"
#include "server.h"

int ptychite_compositor_init(struct ptychite_compositor *compositor) {
	if (!(compositor->config = calloc(1, sizeof(struct ptychite_config)))) {
		return -1;
	}
	if (ptychite_config_init(compositor->config, compositor)) {
		goto err_config_init;
	}

	if (!(compositor->server = ptychite_server_create())) {
		goto err_rest;
	}

	return 0;

err_rest:
	ptychite_config_deinit(compositor->config);
err_config_init:
	free(compositor->config);
	return -1;
}

int ptychite_compositor_run(struct ptychite_compositor *compositor) {
	compositor->config->compositor = NULL;
	char *error;
	if (ptychite_config_parse_config(compositor->config, &error)) {
		wlr_log(WLR_ERROR, "Failed to parse config: %s", error);
	}

	wlr_log(WLR_INFO, "Starting server");
	compositor->config->compositor = compositor;
	return ptychite_server_init_and_run(compositor->server, compositor);
}

void ptychite_compositor_deinit(struct ptychite_compositor *compositor) {
	ptychite_config_deinit(compositor->config);
	free(compositor->config);
	free(compositor->server);
}
