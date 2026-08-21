#ifndef TURBO_BTREE_H
#define TURBO_BTREE_H

#include <cmeta/range.h>
#include <turbo/container/export.h>
#include <turbo/container/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TURBO_BTREE_DEFAULT_MIN_DEGREE
#define TURBO_BTREE_DEFAULT_MIN_DEGREE 4U
#endif

typedef int (*turbo_btree_compare_fn)(const void *left, const void *right,
                                      void *ctx);

typedef struct turbo_btree_entry_link {
  void *key;
  void *value;
  struct turbo_btree_entry_link *previous;
  struct turbo_btree_entry_link *next;
} turbo_btree_entry_link_t;

typedef struct turbo_btree_node {
  bool leaf;
  size_t num_keys;
  void **keys;
  void **values;
  turbo_btree_entry_link_t **links;
  struct turbo_btree_node **children;
} turbo_btree_node_t;

typedef struct {
  turbo_btree_node_t *root;
  size_t key_size;
  size_t key_align;
  size_t key_stride;
  size_t value_size;
  size_t value_align;
  size_t value_stride;
  size_t min_degree;
  size_t max_keys;
  size_t max_children;
  size_t entry_limit;
  size_t size;
  turbo_btree_entry_link_t *first;
  turbo_btree_entry_link_t *last;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  turbo_btree_compare_fn compare;
  void *compare_ctx;
  uint64_t generation;
  bool initialized;
} turbo_btree_t;

/* Descriptors and raw comparator/context are borrowed through destroy.
 * Typed keys require COMPARE|COPY|MOVE|DESTROY and typed values require
 * COPY|MOVE|DESTROY. Handles must be zero-initialized before first use.
 * Search, put, and remove are O(log n) for fixed min_degree; from-arrays is
 * O(rows log live_entries). Indexed key/value lookup is O(n), while the
 * ordered Range cursor follows the derived entry links in O(n) total. */
CONTAINER_API container_status turbo_btree_init(
    turbo_btree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit);
CONTAINER_API container_status turbo_btree_init_with_order(
    turbo_btree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t min_degree,
    size_t entry_limit);
CONTAINER_API container_status turbo_btree_init_bytes(
    turbo_btree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    turbo_btree_compare_fn compare, void *compare_ctx);
CONTAINER_API container_status turbo_btree_init_bytes_with_order(
    turbo_btree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, turbo_btree_compare_fn compare, void *compare_ctx);

CONTAINER_API container_status turbo_btree_from_arrays(
    turbo_btree_t *tree, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t entry_limit);
CONTAINER_API container_status turbo_btree_from_arrays_bytes(
    turbo_btree_t *tree, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, turbo_btree_compare_fn compare, void *compare_ctx);

CONTAINER_API void turbo_btree_destroy(turbo_btree_t *tree);
CONTAINER_API void turbo_btree_clear(turbo_btree_t *tree);
CONTAINER_API container_status turbo_btree_reserve(turbo_btree_t *tree,
                                                    size_t min_capacity);
CONTAINER_API container_status turbo_btree_put(turbo_btree_t *tree,
                                                const void *key,
                                                const void *value);
/* Returned pointers are borrowed and invalidate after any successful mutation,
 * clear, or destroy. */
CONTAINER_API void *turbo_btree_get(turbo_btree_t *tree, const void *key);
CONTAINER_API const void *turbo_btree_get_const(const turbo_btree_t *tree,
                                                const void *key);
CONTAINER_API bool turbo_btree_contains(const turbo_btree_t *tree,
                                        const void *key);
/* out_value is aligned uninitialized storage. Success transfers the removed
 * value there; NULL destroys it. Failure leaves tree and output unchanged. */
CONTAINER_API container_status turbo_btree_remove(turbo_btree_t *tree,
                                                   const void *key,
                                                   void *out_value);
CONTAINER_API size_t turbo_btree_size(const turbo_btree_t *tree);
CONTAINER_API size_t turbo_btree_capacity(const turbo_btree_t *tree);
CONTAINER_API size_t turbo_btree_entry_limit(const turbo_btree_t *tree);
CONTAINER_API uint64_t turbo_btree_generation(const turbo_btree_t *tree);
CONTAINER_API bool turbo_btree_empty(const turbo_btree_t *tree);
CONTAINER_API void *turbo_btree_key_at(turbo_btree_t *tree, size_t index);
CONTAINER_API const void *turbo_btree_key_at_const(const turbo_btree_t *tree,
                                                   size_t index);
CONTAINER_API void *turbo_btree_value_at(turbo_btree_t *tree, size_t index);
CONTAINER_API const void *turbo_btree_value_at_const(const turbo_btree_t *tree,
                                                     size_t index);
/* cursor is zero-initialized before first use. The returned key/value pointers
 * are borrowed until the next successful mutation. */
CONTAINER_API bool turbo_btree_range_next(const turbo_btree_t *tree,
                                          cmeta_range_cursor *cursor,
                                          const void **out_key,
                                          const void **out_value);

#ifdef __cplusplus
}
#endif
#endif /* TURBO_BTREE_H */
