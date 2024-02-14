#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>

#include "util.h"

char *ptychite_asprintf(const char *fmt, ...) {
	char *text;
	va_list args;

	va_start(args, fmt);
	int size = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (size < 0) {
		return NULL;
	}

	text = malloc(size + 1);
	if (text == NULL) {
		return NULL;
	}

	va_start(args, fmt);
	vsnprintf(text, size + 1, fmt, args);
	va_end(args);

	return text;
}

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
