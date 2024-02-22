#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>

#include "util.h"
#include "macros.h"

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

/* HASH MAP IMPL */
#define HASH_SEED 80085

static uint32_t rotl32 (uint32_t x, int8_t r) {
    return (x << r) | (x >> (32 - r));
}

static uint32_t fmix32 (uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

uint32_t ptychite_murmur3_hash(const void *key, size_t len, uint32_t seed) {
    if (!key) return 0;

    uint32_t h1 = seed,
             c1 = 0xcc9e2d51,
             c2 = 0x1b873593,
             k1;
    const uint8_t *data = (const uint8_t*)key;
    const int nblocks = len / 4;
    int i;

    const uint32_t *blocks = (const uint32_t *)(data + nblocks * 4);

    for (i = -nblocks; i; i++) {
        k1 = blocks[i];

        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;

        h1 ^= k1;
        h1 = rotl32(h1, 13);
        h1 = h1 * 5 + 0xe6546b64;
    }

    const uint8_t *tail = (const uint8_t*)(data+nblocks*4);

    k1 = 0;

    switch (len&3) {
        case 3: k1 ^= tail[2] << 16;
        case 2: k1 ^= tail[1] << 8;
        case 1: k1 ^= tail[0];
                k1 ^= c1; k1 = rotl32(k1, 15); k1 *= c2; h1 ^= k1;
    }

    h1 ^= len;
    h1 = fmix32(h1);

    return h1;
}

uint32_t ptychite_murmur3_string_hash(const void *str) {
    return ptychite_murmur3_hash(str, strlen(str), HASH_SEED);
}

uint32_t ptychite_murmur3_uint32_t_hash(const void *uint32) {
    return ptychite_murmur3_hash(uint32, sizeof(uint32_t), HASH_SEED);
}

static void hash_table_rehash(struct ptychite_hash_table *table, uint32_t new_hash_table_size);
static struct ptychite_hash_table_entry *hash_table_get_entry(struct ptychite_hash_table *table, uint32_t hash);

static const uint32_t deleted_entry;
static const struct hash_table_size {
   uint32_t max_entries, size, rehash;
} hash_sizes[] = {
    { 2,		    5,		        3	         },
    { 4,		    7,		        5	         },
    { 8,		    13,		        11	         },
    { 16,		    19,		        17	         },
    { 32,		    43,		        41           },
    { 64,		    73,		        71           },
    { 128,		    151,		    149          },
    { 256,		    283,		    281          },
    { 512,		    571,		    569          },
    { 1024,		    1153,		    1151         },
    { 2048,		    2269,		    2267         },
    { 4096,		    4519,		    4517         },
    { 8192,		    9013,		    9011         },
    { 16384,		18043,		    18041        },
    { 32768,		36109,		    36107        },
    { 65536,		72091,		    72089        },
    { 131072,		144409,		    144407       },
    { 262144,		288361,		    288359       },
    { 524288,		576883,		    576881       },
    { 1048576,		1153459,	    1153457      },
    { 2097152,		2307163,	    2307161      },
    { 4194304,		4613893,	    4613891      },
    { 8388608,		9227641,	    9227639      },
    { 16777216,		18455029,	    18455027     },
    { 33554432,		36911011,	    36911009     },
    { 67108864,		73819861,	    73819859     },
    { 134217728,	147639589,	    147639587    },
    { 268435456,	295279081,	    295279079    },
    { 536870912,	590559793,	    590559791    },
    { 1073741824,	1181116273,	    1181116271   },
    { 2147483648ul,	2362232233ul,	2362232231ul }
};

void hash_table_rehash(struct ptychite_hash_table *table, uint32_t new_hash_table_size) {
    if (!table || new_hash_table_size >= LENGTH(hash_sizes)) return;

    const struct hash_table_size *table_size = &hash_sizes[new_hash_table_size];
    struct ptychite_hash_table old = *table;
    struct ptychite_hash_table_entry *entry;

    table->entries = calloc(table_size->size, sizeof(*table->entries));
    if (!table->entries) return;

    table->hash_sizes_index = new_hash_table_size;
    table->allocated = table_size->size;
    table->deleted = 0;
    table->amnt = 0;

    for (entry = old.entries; entry != old.entries + old.allocated; entry++) {
        if (ptychite_hash_table_entry_is_filled(entry)) ptychite_hash_table_insert(table, entry->hash, entry->data);
    }

    free(old.entries);
}

struct ptychite_hash_table_entry *hash_table_get_entry(struct ptychite_hash_table *table, uint32_t hash) {
    if (!table) return NULL;

    uint32_t hash_index = hash % table->allocated, double_hash;
    struct ptychite_hash_table_entry *entry;
    do {
        entry = table->entries + hash_index;
        if (entry && entry->data == NULL) {
            return NULL;
        }
        if (!ptychite_hash_table_entry_is_filled(entry) || entry->hash != hash) {
            double_hash = 1 + hash % hash_sizes[table->hash_sizes_index].rehash;
            hash_index = (hash_index + double_hash) % table->allocated;
            continue;
        }

        return entry;
    } while (hash_index != hash % table->allocated);

    return NULL;
}

bool ptychite_hash_table_init(struct ptychite_hash_table *table) {
    if (!table) return false;

    memset(table, 0, sizeof(*table));

    table->allocated = hash_sizes[0].size;
    table->entries = calloc(table->allocated, sizeof(*table->entries));
    if (!table->entries) return false;

    return true;
}

bool ptychite_hash_table_destroy(struct ptychite_hash_table *table) {
    if (!table) return false;

    free(table->entries);
    return true;
}

bool ptychite_hash_table_elements_destroy(struct ptychite_hash_table *table, ptychite_destroy_func destroy) {
    if (!table || !destroy) return false;

    for (struct ptychite_hash_table_entry *entry = table->entries; entry != table->entries + table->amnt; entry++) {
        if (!ptychite_hash_table_entry_is_filled(entry)) continue;

        destroy(entry->data);
    }

    ptychite_hash_table_destroy(table);
    return true;
}

bool ptychite_hash_table_insert(struct ptychite_hash_table *table, uint32_t hash, void *data) {
    if (!table || !data) return false;

    const struct hash_table_size *table_size = &hash_sizes[table->hash_sizes_index];
    if (table->amnt >= table_size->max_entries) {
        hash_table_rehash(table, ++table->hash_sizes_index);
    }
    else if (table->amnt + table->deleted >= table_size->max_entries) {
        hash_table_rehash(table, table->hash_sizes_index);
    }

    uint32_t hash_index = hash % table->allocated, double_hash;
    struct ptychite_hash_table_entry *entry;
    do {
        entry = table->entries + hash_index;
        if (ptychite_hash_table_entry_is_filled(entry)) {
            double_hash = 1 + hash % hash_sizes[table->hash_sizes_index].rehash;
            hash_index = (hash_index + double_hash) % table->allocated;
            continue;
        }

        if (ptychite_hash_table_entry_is_deleted(entry)) table->deleted--;
        else if (entry->hash == hash) break;

        entry->hash = hash;
        entry->data = data;
        table->amnt++;
        return true;
    } while (hash_index != hash % table->allocated);

    return false;
}

void *ptychite_hash_table_remove(struct ptychite_hash_table *table, uint32_t hash) {
    if (!table) return NULL;

    struct ptychite_hash_table_entry *entry = hash_table_get_entry(table, hash);
    if (!entry) return NULL;

    void *data = entry->data;

    entry->data = (void*)&deleted_entry;
    table->deleted++;

    return data;
}

void *ptychite_hash_table_get(struct ptychite_hash_table *table, uint32_t hash)  {
    if (!table) return NULL;

    struct ptychite_hash_table_entry *entry = hash_table_get_entry(table, hash);
    if (!entry) return NULL;

    return entry->data;
}


bool ptychite_hash_table_entry_is_filled(struct ptychite_hash_table_entry *entry) {
    return entry && entry->data != NULL && entry->data != &deleted_entry;
}

bool ptychite_hash_table_entry_is_deleted(struct ptychite_hash_table_entry *entry) {
    return entry && entry->data == &deleted_entry;
}

bool ptychite_hash_map_init(struct ptychite_hash_map *map, ptychite_hash_func hash) {
    if (!map || !hash) return false;

    map->hash = hash;
    return ptychite_hash_table_init(&map->table);
}

bool ptychite_hash_map_destroy(struct ptychite_hash_map *map) {
    if (!map) return false;

    return ptychite_hash_table_destroy(&map->table);
}

bool ptychite_hash_map_elements_destroy(struct ptychite_hash_map *map, ptychite_destroy_func destroy) {
    if (!map) return false;

    return ptychite_hash_table_elements_destroy(&map->table, destroy);
}

bool ptychite_hash_map_insert(struct ptychite_hash_map *map, const void *key, void *value) {
    if (!map || !key || !value) return false;

    return ptychite_hash_table_insert(&map->table, map->hash(key), value);
}

void *ptychite_hash_map_remove(struct ptychite_hash_map *map, const void *key) {
    if (!map || !key) return NULL;

    return ptychite_hash_table_remove(&map->table, map->hash(key));
}

void *ptychite_hash_map_get(struct ptychite_hash_map *map, const void *key) {
    if (!map || !key) return NULL;

    return ptychite_hash_table_get(&map->table, map->hash(key));
}

bool ptychite_hash_map_iterate(struct ptychite_hash_map *map, void *user_data, ptychite_iterator_func iterate) {
    if (!map || !iterate) return false;

    struct ptychite_hash_table_entry *entry;
    for (uint32_t i = 0; i < map->table.allocated; i++) {
        entry = map->table.entries + i;

        if (!ptychite_hash_table_entry_is_filled(entry) || ptychite_hash_table_entry_is_deleted(entry)) continue;
        if (iterate(entry->data, user_data)) return false;
    }

    return true;
}
