#ifndef PTYCHITE_SERVER_H
#define PTYCHITE_SERVER_H

struct ptychite_compositor;
struct ptychite_server;

struct ptychite_server *ptychite_server_create(void);

int ptychite_server_init_and_run(
		struct ptychite_server *server, struct ptychite_compositor *compositor);

void ptychite_server_terminate(struct ptychite_server *server);

int ptychite_server_close_focused_client(struct ptychite_server *server);

void ptychite_server_configure_keyboards(struct ptychite_server *server);

void ptychite_server_configure_panels(struct ptychite_server *server);

void ptychite_server_configure_views(struct ptychite_server *server);

void ptychite_server_toggle_control(struct ptychite_server *server);

void ptychite_server_refresh_wallpapers(struct ptychite_server *server);

void ptychite_server_retile(struct ptychite_server *server);

void ptychite_server_tiling_increase_views_in_master(struct ptychite_server *server);

void ptychite_server_tiling_decrease_views_in_master(struct ptychite_server *server);

void ptychite_server_tiling_increase_master_factor(struct ptychite_server *server);

void ptychite_server_tiling_decrease_master_factor(struct ptychite_server *server);

void ptychite_server_tiling_toggle_right_master(struct ptychite_server *server);

void ptychite_server_check_cursor(struct ptychite_server *server);

void ptychite_server_goto_next_workspace(struct ptychite_server *server);

void ptychite_server_goto_previous_workspace(struct ptychite_server *server);

#endif
