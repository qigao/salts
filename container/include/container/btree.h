#ifndef CONTAINER_BTREE_H
#define CONTAINER_BTREE_H

#include "platform.h"
#include "turbo_error.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONTAINER_BTREE_DEFAULT_MIN_DEGREE
#define CONTAINER_BTREE_DEFAULT_MIN_DEGREE 4U
#endif

typedef int (*container_btree_compare_fn)(const void *left, const void *right, void *ctx);

typedef struct container_btree_node {
  bool leaf;
  size_t num_keys;
  unsigned char *keys;
  unsigned char *values;
  struct container_btree_node **children;
} container_btree_node_t;

typedef struct {
  container_btree_node_t *root;
  size_t key_size;
  size_t value_size;
  size_t key_stride;
  size_t value_stride;
  size_t min_degree;
  size_t max_keys;
  size_t max_children;
  container_btree_compare_fn compare;
  void *compare_ctx;
  size_t size;
} container_btree_t;

CXX_C_API int container_btree_init(container_btree_t *tree, size_t key_size, size_t value_size,
                               container_btree_compare_fn compare, void *compare_ctx);
CXX_C_API int container_btree_init_with_order(container_btree_t *tree, size_t key_size, size_t value_size,
                                          container_btree_compare_fn compare, void *compare_ctx,
                                          size_t min_degree);
CXX_C_API int container_btree_from_arrays(container_btree_t *tree, const void *keys,
                                      const void *values, size_t count, size_t key_size,
                                      size_t value_size, container_btree_compare_fn compare,
                                      void *compare_ctx);
CXX_C_API void container_btree_destroy(container_btree_t *tree);
CXX_C_API void container_btree_clear(container_btree_t *tree);
CXX_C_API int container_btree_reserve(container_btree_t *tree, size_t min_capacity);
CXX_C_API int container_btree_put(container_btree_t *tree, const void *key, const void *value);
CXX_C_API void *container_btree_get(container_btree_t *tree, const void *key);
CXX_C_API const void *container_btree_get_const(const container_btree_t *tree, const void *key);
CXX_C_API bool container_btree_contains(const container_btree_t *tree, const void *key);
CXX_C_API int container_btree_remove(container_btree_t *tree, const void *key, void *out_value);
CXX_C_API size_t container_btree_size(const container_btree_t *tree);
CXX_C_API size_t container_btree_capacity(const container_btree_t *tree);
CXX_C_API bool container_btree_empty(const container_btree_t *tree);
CXX_C_API void *container_btree_key_at(container_btree_t *tree, size_t index);
CXX_C_API const void *container_btree_key_at_const(const container_btree_t *tree, size_t index);
CXX_C_API void *container_btree_value_at(container_btree_t *tree, size_t index);
CXX_C_API const void *container_btree_value_at_const(const container_btree_t *tree, size_t index);


#ifdef __cplusplus
}
#endif
#endif /* CONTAINER_BTREE_H */
