#ifndef PTYCHITE_UTIL_H
#define PTYCHITE_UTIL_H

#include <wlr/util/box.h>

struct ptychite_mouse_region {
	struct wlr_box box;
	bool entered;
};

char *ptychite_asprintf(const char *fmt, ...);

bool ptychite_mouse_region_update_state(struct ptychite_mouse_region *region, double x, double y);

void ptychite_spawn(char **args);


#endif
