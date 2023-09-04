#ifndef PTYCHITE_COMPOSITOR_H
#define PTYCHITE_COMPOSITOR_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-util.h>

struct ptychite_compositor {
	struct ptychite_server *server;
	struct ptychite_config *config;
};

struct ptychite_action;

int ptychite_compositor_init(struct ptychite_compositor *compositor);

int ptychite_compositor_run(struct ptychite_compositor *compositor);

void ptychite_compositor_deinit(struct ptychite_compositor *compositor);

struct ptychite_action *ptychite_action_create(const char **args, int args_l, char **error);

int ptychite_action_get_args(struct ptychite_action *action, char ***args_out, int *args_l_out);

void ptychite_action_destroy(struct ptychite_action *action);

void ptychite_compositor_execute_action(
		struct ptychite_compositor *compositor, struct ptychite_action *action);

/* returns whether key was handled */
bool ptychite_compositor_evaluate_key(struct ptychite_compositor *compositor, uint32_t sym,
		uint32_t modifiers, struct wl_array *history);

#endif
