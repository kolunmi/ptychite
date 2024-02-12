#ifndef PTYCHITE_CHORD_H
#define PTYCHITE_CHORD_H

#include <stddef.h>
#include <stdint.h>

struct ptychite_key {
	uint32_t modifiers;
	uint32_t sym;
};

struct ptychite_chord {
	struct ptychite_key *keys;
	size_t keys_l;
};

int ptychite_chord_parse_pattern(struct ptychite_chord *chord, const char *pattern, char **error);
void ptychite_chord_deinit(struct ptychite_chord *chord);
char *ptychite_chord_get_pattern(struct ptychite_chord *chord);

#endif
