#ifndef PTYCHITE_ELEMENT_H
#define PTYCHITE_ELEMENT_H

enum ptychite_element_type {
	PTYCHITE_ELEMENT_VIEW,
	PTYCHITE_ELEMENT_WINDOW,
};

struct ptychite_element {
	enum ptychite_element_type type;
	struct wlr_scene_tree *scene_tree;
	int width, height;
};

#endif
