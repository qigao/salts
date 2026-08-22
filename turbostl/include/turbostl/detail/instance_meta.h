#ifndef TURBOSTL_DETAIL_INSTANCE_META_H
#define TURBOSTL_DETAIL_INSTANCE_META_H

#include <cmeta/range.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical descriptors for kinds whose instance metadata is implemented in
 * the compiled STL target. List keeps its existing header-local adapter during
 * the current migration step. Concrete type bindings stay on runtime handles. */
extern const cmeta_container_desc stl_vec_container_desc;
extern const cmeta_container_desc stl_deque_container_desc;
extern const cmeta_container_desc stl_stack_container_desc;
extern const cmeta_container_desc stl_queue_container_desc;
extern const cmeta_container_desc stl_heap_container_desc;
extern const cmeta_container_desc stl_set_container_desc;
extern const cmeta_container_desc stl_hash_set_container_desc;
extern const cmeta_container_desc stl_hash_map_container_desc;
extern const cmeta_container_desc stl_map_instance_container_desc;
extern const cmeta_container_desc stl_multimap_container_desc;
extern const cmeta_container_desc stl_btree_container_desc;
extern const cmeta_container_desc stl_bplus_tree_container_desc;

#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_DETAIL_INSTANCE_META_H */
