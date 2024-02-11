#ifndef PTYCHITE_UTIL_H
#define PTYCHITE_UTIL_H

#include <wlr/util/box.h>

struct ptychite_mouse_region {
	struct wlr_box box;
	bool entered;
};

bool mouse_region_update_state(struct ptychite_mouse_region *region, double x, double y);

void spawn(char **args);

#endif
