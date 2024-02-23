#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <sys/wait.h>

#include <wlr/util/log.h>

#include "compositor.h"

void signal_handle(int signal) {
	if (signal == SIGCHLD) {
		siginfo_t in;
		while (!waitid(P_ALL, 0, &in, WEXITED | WNOHANG | WNOWAIT) && in.si_pid) {
			waitpid(in.si_pid, NULL, 0);
		}
		return;
	}

	/* ... */
}

int main(int argc, char **argv) {
	struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = signal_handle};
	sigaction(SIGCHLD, &sa, NULL);

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
