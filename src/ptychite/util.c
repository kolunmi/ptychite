#include <stdlib.h>
#include <unistd.h>

#include "util.h"

bool ptychite_mouse_region_update_state(struct ptychite_mouse_region *region, double x, double y) {
	if (wlr_box_contains_point(&region->box, x, y)) {
		if (region->entered) {
			return false;
		}
		region->entered = true;
		return true;
	}
	if (region->entered) {
		region->entered = false;
		return true;
	}

	return false;
}

void ptychite_spawn(char **args) {
	if (!fork()) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(args[0], args);
		exit(EXIT_SUCCESS);
	}
}
