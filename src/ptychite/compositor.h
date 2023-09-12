#ifndef PTYCHITE_COMPOSITOR_H
#define PTYCHITE_COMPOSITOR_H

struct ptychite_compositor {
	struct ptychite_server *server;
	struct ptychite_config *config;
};

int ptychite_compositor_init(struct ptychite_compositor *compositor);

int ptychite_compositor_run(struct ptychite_compositor *compositor);

void ptychite_compositor_deinit(struct ptychite_compositor *compositor);

#endif
