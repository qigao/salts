#ifndef CSTL_DETAIL_INSTANCE_META_H
#define CSTL_DETAIL_INSTANCE_META_H

#include <cmeta/range.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline bool stl_cmeta_range_constructs_values(
    const cmeta_type_desc *type) {
  return type != NULL && type->traits != NULL &&
         (type->traits->flags &
          (CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY)) !=
             (CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) &&
         cmeta_type_require_traits(
             type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) == CMETA_OK;
}

static inline bool stl_cmeta_range_construct(
    const cmeta_type_desc *type, void *destination, const void *source) {
  if (type == NULL || destination == NULL || source == NULL)
    return false;
  if (stl_cmeta_range_constructs_values(type))
    return type->traits->copy_construct(destination, source);
  if (type->traits != NULL &&
      (type->traits->flags &
       (CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY)) !=
          (CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY))
    return false;
  memcpy(destination, source, type->size);
  return true;
}

static inline cmeta_range_flags stl_cmeta_range_flags_for(
    const cmeta_type_desc *type, cmeta_range_flags flags) {
  return stl_cmeta_range_constructs_values(type)
             ? flags | CMETA_RANGE_CONSTRUCTS_VALUES
             : flags;
}

/* Canonical generic constructors. Stable IDs, not object addresses, define
 * constructor identity across translation units. */
extern const cmeta_generic_desc stl_vec_generic_desc;
extern const cmeta_generic_desc stl_deque_generic_desc;
extern const cmeta_generic_desc stl_list_generic_desc;
extern const cmeta_generic_desc stl_stack_generic_desc;
extern const cmeta_generic_desc stl_queue_generic_desc;
extern const cmeta_generic_desc stl_heap_generic_desc;
extern const cmeta_generic_desc stl_set_generic_desc;
extern const cmeta_generic_desc stl_hash_set_generic_desc;
extern const cmeta_generic_desc stl_hash_map_generic_desc;
extern const cmeta_generic_desc stl_map_generic_desc;
extern const cmeta_generic_desc stl_multimap_generic_desc;
extern const cmeta_generic_desc stl_btree_generic_desc;
extern const cmeta_generic_desc stl_bplus_tree_generic_desc;

/* Layout-only descriptors used by reflected TYPE(...) fields. These describe
 * handle storage, not a whole-container owning value type. */
extern const cmeta_type_desc stl_vec_storage_type;
extern const cmeta_type_desc stl_deque_storage_type;
extern const cmeta_type_desc stl_list_storage_type;
extern const cmeta_type_desc stl_stack_storage_type;
extern const cmeta_type_desc stl_queue_storage_type;
extern const cmeta_type_desc stl_heap_storage_type;
extern const cmeta_type_desc stl_set_storage_type;
extern const cmeta_type_desc stl_hash_set_storage_type;
extern const cmeta_type_desc stl_hash_map_storage_type;
extern const cmeta_type_desc stl_map_storage_type;
extern const cmeta_type_desc stl_multimap_storage_type;
extern const cmeta_type_desc stl_btree_storage_type;
extern const cmeta_type_desc stl_bplus_tree_storage_type;

/* Declaration-side construction capabilities. They bind only descriptor and
 * T/K/V metadata on an uninitialized handle; they never allocate storage. */
extern const cmeta_container_construct_ops stl_vec_construct_ops;
extern const cmeta_container_construct_ops stl_deque_construct_ops;
extern const cmeta_container_construct_ops stl_list_construct_ops;
extern const cmeta_container_construct_ops stl_stack_construct_ops;
extern const cmeta_container_construct_ops stl_queue_construct_ops;
extern const cmeta_container_construct_ops stl_heap_construct_ops;
extern const cmeta_container_construct_ops stl_set_construct_ops;
extern const cmeta_container_construct_ops stl_hash_set_construct_ops;
extern const cmeta_container_construct_ops stl_hash_map_construct_ops;
extern const cmeta_container_construct_ops stl_map_construct_ops;
extern const cmeta_container_construct_ops stl_multimap_construct_ops;
extern const cmeta_container_construct_ops stl_btree_construct_ops;
extern const cmeta_container_construct_ops stl_bplus_tree_construct_ops;

/* Versioned generic type extensions attached to the canonical container
 * descriptors. Concrete type arguments remain stored on each typed handle. */
extern const cmeta_container_ext stl_vec_container_ext;
extern const cmeta_container_ext stl_deque_container_ext;
extern const cmeta_container_ext stl_list_container_ext;
extern const cmeta_container_ext stl_stack_container_ext;
extern const cmeta_container_ext stl_queue_container_ext;
extern const cmeta_container_ext stl_heap_container_ext;
extern const cmeta_container_ext stl_set_container_ext;
extern const cmeta_container_ext stl_hash_set_container_ext;
extern const cmeta_container_ext stl_hash_map_container_ext;
extern const cmeta_container_ext stl_map_container_ext;
extern const cmeta_container_ext stl_multimap_container_ext;
extern const cmeta_container_ext stl_btree_container_ext;
extern const cmeta_container_ext stl_bplus_tree_container_ext;

/* Canonical descriptors for kinds whose instance metadata is implemented in
 * the compiled STL target. Concrete type bindings stay on runtime handles. */
extern const cmeta_container_desc stl_vec_container_desc;
extern const cmeta_container_desc stl_deque_container_desc;
extern const cmeta_container_desc stl_list_container_desc;
extern const cmeta_container_desc stl_stack_container_desc;
extern const cmeta_container_desc stl_queue_container_desc;
extern const cmeta_container_desc stl_heap_container_desc;
extern const cmeta_container_desc stl_set_container_desc;
extern const cmeta_container_desc stl_hash_set_container_desc;
extern const cmeta_container_desc stl_hash_map_container_desc;
extern const cmeta_container_desc stl_map_container_desc;
extern const cmeta_container_desc stl_multimap_container_desc;
extern const cmeta_container_desc stl_btree_container_desc;
extern const cmeta_container_desc stl_bplus_tree_container_desc;

#ifdef __cplusplus
}
#endif

#endif /* CSTL_DETAIL_INSTANCE_META_H */
