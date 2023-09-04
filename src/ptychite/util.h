#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <wayland-util.h>

struct pool_array {
  struct wl_array pools;
  size_t pool_size;
};

void pool_array_init(struct pool_array *pool_array, size_t pool_size);

void pool_array_release(struct pool_array *pool_array);

void *pool_array_add(struct pool_array *pool_array, size_t size);

void pool_array_zero(struct pool_array *pool_array);

void util_spawn(char **args);

#endif
