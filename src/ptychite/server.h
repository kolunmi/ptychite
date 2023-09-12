#ifndef PTYCHITE_SERVER_H
#define PTYCHITE_SERVER_H

struct ptychite_compositor;
struct ptychite_server;
struct ptychite_action;

struct ptychite_server *ptychite_server_create(void);

int ptychite_server_init_and_run(
		struct ptychite_server *server, struct ptychite_compositor *compositor);

void ptychite_server_configure_keyboards(struct ptychite_server *server);

void ptychite_server_configure_panels(struct ptychite_server *server);

void ptychite_server_configure_views(struct ptychite_server *server);

void ptychite_server_refresh_wallpapers(struct ptychite_server *server);

void ptychite_server_retile(struct ptychite_server *server);

void ptychite_server_check_cursor(struct ptychite_server *server);

void ptychite_server_execute_action(struct ptychite_server *server, struct ptychite_action *action);

struct ptychite_action *ptychite_action_create(const char **args, int args_l, char **error);

int ptychite_action_get_args(struct ptychite_action *action, char ***args_out, int *args_l_out);

void ptychite_action_destroy(struct ptychite_action *action);

#endif
