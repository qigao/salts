#ifndef TURBO_BTREE_H
#define TURBO_BTREE_H

#include <turbo/container/export.h>
#include <turbo/container/status.h>
#include <turbo/container/meta.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TURBO_BTREE_DEFAULT_MIN_DEGREE
#define TURBO_BTREE_DEFAULT_MIN_DEGREE 4U
#endif

typedef int (*turbo_btree_compare_fn)(const void *left, const void *right, void *ctx);

typedef struct turbo_btree_node {
  bool leaf;
  size_t num_keys;
  unsigned char *keys;
  unsigned char *values;
  struct turbo_btree_node **children;
} turbo_btree_node_t;

typedef struct {
  turbo_btree_node_t *root;
  size_t key_size;
  size_t value_size;
  size_t key_stride;
  size_t value_stride;
  size_t min_degree;
  size_t max_keys;
  size_t max_children;
  turbo_btree_compare_fn compare;
  void *compare_ctx;
  size_t size;
} turbo_btree_t;

CONTAINER_API int turbo_btree_init(turbo_btree_t *tree, size_t key_size, size_t value_size,
                               turbo_btree_compare_fn compare, void *compare_ctx);
CONTAINER_API int turbo_btree_init_with_order(turbo_btree_t *tree, size_t key_size, size_t value_size,
                                          turbo_btree_compare_fn compare, void *compare_ctx,
                                          size_t min_degree);
CONTAINER_API int turbo_btree_from_arrays(turbo_btree_t *tree, const void *keys,
                                      const void *values, size_t count, size_t key_size,
                                      size_t value_size, turbo_btree_compare_fn compare,
                                      void *compare_ctx);
CONTAINER_API void turbo_btree_destroy(turbo_btree_t *tree);
CONTAINER_API void turbo_btree_clear(turbo_btree_t *tree);
CONTAINER_API int turbo_btree_reserve(turbo_btree_t *tree, size_t min_capacity);
CONTAINER_API int turbo_btree_put(turbo_btree_t *tree, const void *key, const void *value);
CONTAINER_API void *turbo_btree_get(turbo_btree_t *tree, const void *key);
CONTAINER_API const void *turbo_btree_get_const(const turbo_btree_t *tree, const void *key);
CONTAINER_API bool turbo_btree_contains(const turbo_btree_t *tree, const void *key);
CONTAINER_API int turbo_btree_remove(turbo_btree_t *tree, const void *key, void *out_value);
CONTAINER_API size_t turbo_btree_size(const turbo_btree_t *tree);
CONTAINER_API size_t turbo_btree_capacity(const turbo_btree_t *tree);
CONTAINER_API bool turbo_btree_empty(const turbo_btree_t *tree);
CONTAINER_API void *turbo_btree_key_at(turbo_btree_t *tree, size_t index);
CONTAINER_API const void *turbo_btree_key_at_const(const turbo_btree_t *tree, size_t index);
CONTAINER_API void *turbo_btree_value_at(turbo_btree_t *tree, size_t index);
CONTAINER_API const void *turbo_btree_value_at_const(const turbo_btree_t *tree, size_t index);

#define TURBO_BTREE_DEFINE(name, key_type, value_type, compare_fn) \
  CMETA_CONTAINER2_DEFINE(name, key_type, value_type, turbo_btree_t, turbo_btree, CONTAINER_OK, compare_fn, TURBO_META_BTREE_METHODS) \
  CMETA_CONTAINER2_RANGES_DEFINE(name, key_type, value_type, turbo_btree, key_at_const, value_at_const, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE)

#ifdef __cplusplus
}
#endif
#endif /* TURBO_BTREE_H */
