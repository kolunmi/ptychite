#define _POSIX_C_SOURCE 200112L
#include <signal.h>

#include <wlr/util/log.h>

#include "compositor.h"

int main(int argc, char **argv) {
	signal(SIGCHLD, SIG_IGN);
	wlr_log_init(WLR_DEBUG, NULL);

	struct ptychite_compositor compositor = {0};

	if (ptychite_compositor_init(&compositor)) {
		return 1;
	}

	int return_status = ptychite_compositor_run(&compositor);
	ptychite_compositor_deinit(&compositor);

	if (return_status) {
		return 1;
	}
	return 0;
}
