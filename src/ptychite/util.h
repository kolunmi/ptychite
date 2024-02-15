#ifndef PTYCHITE_UTIL_H
#define PTYCHITE_UTIL_H

#include <wlr/util/box.h>

char *ptychite_asprintf(const char *fmt, ...);

struct ptychite_mouse_region {
	struct wlr_box box;
	bool entered;
};

bool ptychite_mouse_region_update_state(struct ptychite_mouse_region *region, double x, double y);


typedef uint32_t (*ptychite_hash_func)(const void *data);
typedef int64_t (*ptychite_cmp_func)(const void *data1, const void *data2);
typedef void (*ptychite_destroy_func)(void *data);
typedef bool (*ptychite_iterator_func)(const void *data, void *user_data);

uint32_t ptychite_murmur3_hash(const void *key, size_t len, uint32_t seed);
uint32_t ptychite_murmur3_string_hash(const void *str);
uint32_t ptychite_murmur3_uint32_t_hash(const void *uint32);

struct ptychite_hash_table_entry {
    uint32_t hash;
    void *data;
};

struct ptychite_hash_table {
    struct ptychite_hash_table_entry *entries; //!< array of entries
    uint32_t hash_sizes_index,            //!< an internal value
             amnt,                        //!< the amount of entries
             allocated,                   //!< the allocated entries
             deleted;                     //!< the deleted entries
};

bool ptychite_hash_table_init(struct ptychite_hash_table *table);
bool ptychite_hash_table_destroy(struct ptychite_hash_table *table);
bool ptychite_hash_table_elements_destroy(struct ptychite_hash_table *table, ptychite_destroy_func destroy);
bool ptychite_hash_table_insert(struct ptychite_hash_table *table, uint32_t hash, void *data);
void *ptychite_hash_table_remove(struct ptychite_hash_table *table, uint32_t hash);
void *ptychite_hash_table_get(struct ptychite_hash_table *table, uint32_t hash);
bool ptychite_hash_table_entry_is_deleted(struct ptychite_hash_table_entry *entry);
bool ptychite_hash_table_entry_is_filled(struct ptychite_hash_table_entry *entry);


struct ptychite_hash_map {
    struct ptychite_hash_table table;
    ptychite_hash_func hash;
};

bool ptychite_hash_map_init(struct ptychite_hash_map *map, ptychite_hash_func hash);
bool ptychite_hash_map_destroy(struct ptychite_hash_map *map);
bool ptychite_hash_map_elements_destroy(struct ptychite_hash_map *map, ptychite_destroy_func destroy);
bool ptychite_hash_map_insert(struct ptychite_hash_map *map, const void *key, void *value);
void *ptychite_hash_map_get(struct ptychite_hash_map *map, const void *key);
void *ptychite_hash_map_remove(struct ptychite_hash_map *map, const void *key);
bool ptychite_hash_map_iterate(struct ptychite_hash_map *map, void *user_data, ptychite_iterator_func iterate);

void ptychite_spawn(char **args);

#endif
