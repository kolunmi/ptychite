#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-util.h>

#include "util.h"

struct pool {
	char *data;
	size_t used;
};

void pool_array_init(struct pool_array *pool_array, size_t pool_size) {
	wl_array_init(&pool_array->pools);
	pool_array->pool_size = pool_size;
}

void pool_array_release(struct pool_array *pool_array) {
	struct pool *pool;
	wl_array_for_each(pool, &pool_array->pools) {
		free(pool->data);
	}
	wl_array_release(&pool_array->pools);
}

void *pool_array_add(struct pool_array *pool_array, size_t size) {
	if (!size || size > pool_array->pool_size) {
		return NULL;
	}

	struct pool *pool;
	wl_array_for_each(pool, &pool_array->pools) {
		if (pool->used + size <= pool_array->pool_size) {
			void *data = pool->data + pool->used;
			pool->used += size;
			return data;
		}
	}

	if (!(pool = wl_array_add(&pool_array->pools, sizeof(struct pool)))) {
		return NULL;
	}
	if (!(pool->data = malloc(pool_array->pool_size))) {
		pool_array->pools.size -= sizeof(struct pool);
		return NULL;
	}
	pool->used = size;
	return pool->data;
}

void pool_array_zero(struct pool_array *pool_array) {
	struct pool *pool;
	wl_array_for_each(pool, &pool_array->pools) {
		pool->used = 0;
	}
}

void util_spawn(char **args) {
	if (!fork()) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(args[0], args);
		exit(EXIT_SUCCESS);
	}
}
