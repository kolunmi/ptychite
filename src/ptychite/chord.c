#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_keyboard.h>

#include "chord.h"
#include "macros.h"

static const struct {
	char *name;
	uint32_t value;
} modifier_name_table[] = {
		{"Sh", WLR_MODIFIER_SHIFT},
		{"Cp", WLR_MODIFIER_CAPS},
		{"C", WLR_MODIFIER_CTRL},
		{"M", WLR_MODIFIER_ALT},
		{"M1", WLR_MODIFIER_ALT},
		{"A", WLR_MODIFIER_ALT},
		{"M2", WLR_MODIFIER_MOD2},
		{"M3", WLR_MODIFIER_MOD3},
		{"S", WLR_MODIFIER_LOGO},
		{"M4", WLR_MODIFIER_LOGO},
		{"M5", WLR_MODIFIER_MOD5},
};

int ptychite_chord_parse_pattern(struct ptychite_chord *chord, const char *pattern, char **error) {
	char *pos;
	bool on_space = true;
	size_t tokens = 0;
	for (pos = (char *)pattern; *pos; pos++) {
		bool is_space = *pos == ' ';
		if (on_space && !is_space) {
			tokens++;
		}
		on_space = is_space;
	}
	if (!tokens) {
		*error = "chord pattern is empty";
		return -1;
	}

	char *dup_pattern = malloc(pos - pattern + 1);
	if (!dup_pattern) {
		*error = "memory error";
		return -1;
	}
	strcpy(dup_pattern, pattern);

	struct ptychite_key *keys = calloc(tokens, sizeof(struct ptychite_key));
	if (!keys) {
		*error = "memory error";
		goto err_alloc_keys;
	}

	size_t keys_l;
	for (pos = dup_pattern, keys_l = 0; *pos; pos++) {
		if (*pos == ' ') {
			continue;
		}
		if (*pos == '-') {
			*error = "chord pattern has an unexpected character";
			goto err_rest;
		}

		char *end;
		for (end = pos; *end && *end != ' '; end++) {
			;
		}

		bool done = false;
		if (*end) {
			*end = '\0';
		} else {
			done = true;
		}

		char *stay, *seek;
		for (stay = pos; (seek = strchr(stay, '-')); stay = seek) {
			*seek++ = '\0';
			if (!*seek || *seek == ' ' || *seek == '-') {
				*error = "chord pattern has an unexpected character";
				goto err_rest;
			}

			size_t i;
			bool found = false;
			for (i = 0; i < LENGTH(modifier_name_table); i++) {
				if (!strcmp(stay, modifier_name_table[i].name)) {
					keys[keys_l].modifiers |= modifier_name_table[i].value;
					found = true;
					break;
				}
			}
			if (!found) {
				*error = "chord pattern contains an unknown modifier";
				goto err_rest;
			}
		}
		if (!*stay) {
			goto err_rest;
		}

		uint32_t sym = xkb_keysym_from_name(stay, XKB_KEYSYM_NO_FLAGS);
		if (sym == XKB_KEY_NoSymbol) {
			*error = "chord pattern contains an unknown sym name";
			goto err_rest;
		}
		keys[keys_l].sym = sym;

		if (done) {
			break;
		}

		pos = end;
		keys_l++;
	}

	chord->keys = keys;
	chord->keys_l = tokens;

	free(dup_pattern);
	return 0;

err_rest:
	free(keys);
err_alloc_keys:
	free(dup_pattern);
	return -1;
}

void ptychite_chord_deinit(struct ptychite_chord *chord) {
	free(chord->keys);
}

char *ptychite_chord_get_pattern(struct ptychite_chord *chord) {
	if (!chord->keys_l) {
		return NULL;
	}

	struct wl_array output;
	wl_array_init(&output);

	char *append;
	size_t i;
	for (i = 0; i < chord->keys_l; i++) {
		int j;
		for (j = 0; j <= 7; j++) {
			uint32_t modifier = 1 << j;
			if (!(chord->keys[i].modifiers & modifier)) {
				continue;
			}

			char *name = NULL;
			size_t k;
			for (k = 0; k < LENGTH(modifier_name_table); k++) {
				if (modifier_name_table[k].value == modifier) {
					name = modifier_name_table[k].name;
					break;
				}
			}
			if (!name) {
				goto err;
			}

			size_t name_l = strlen(name);
			if (!(append = wl_array_add(&output, name_l + 1))) {
				goto err;
			}
			memcpy(append, name, name_l);
			append[name_l] = '-';
		}

		char buffer[128];
		int sym_l = xkb_keysym_get_name(chord->keys[i].sym, buffer, sizeof(buffer));
		if (sym_l < 0) {
			goto err;
		}
		if (!(append = wl_array_add(&output, sym_l + 1))) {
			goto err;
		}
		memcpy(append, buffer, sym_l);
		append[sym_l] = ' ';
	}

	char *term = (char *)output.data + output.size - 1;
	*term = '\0';

	char *pattern = strdup(output.data);
	wl_array_release(&output);
	return pattern;

err:
	wl_array_release(&output);
	return NULL;
}
