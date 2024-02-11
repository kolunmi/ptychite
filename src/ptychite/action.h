#ifndef PTYCHITE_ACTION_H
#define PTYCHITE_ACTION_H

#include <wayland-util.h>

struct ptychite_server;

typedef void (*ptychite_action_func_t)(struct ptychite_server *server, void *data);

struct ptychite_action {
	struct wl_list link;
	ptychite_action_func_t action_func;
	void *data;
};

enum ptychite_action_func_data_mode {
	PTYCHITE_ACTION_FUNC_DATA_NONE,
	PTYCHITE_ACTION_FUNC_DATA_INT,
	PTYCHITE_ACTION_FUNC_DATA_STRING,
	PTYCHITE_ACTION_FUNC_DATA_ARGV,
};

struct ptychite_action *ptychite_action_create(const char **args, int args_l, char **error);
int ptychite_action_get_args(struct ptychite_action *action, char ***args_out, int *args_l_out);
void ptychite_action_destroy(struct ptychite_action *action);

#endif
