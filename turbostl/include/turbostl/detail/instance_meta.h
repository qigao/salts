#ifndef TURBOSTL_DETAIL_INSTANCE_META_H
#define TURBOSTL_DETAIL_INSTANCE_META_H

#include <cmeta/range.h>

/* Kind identity only. Range/collector callbacks are added in the next
 * migration step; concrete element bindings stay on each runtime handle. */
static const cmeta_container_desc stl_vec_container_desc = {
    "Vec", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const cmeta_container_desc stl_deque_container_desc = {
    "Deque", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const cmeta_container_desc stl_stack_container_desc = {
    "Stack", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const cmeta_container_desc stl_queue_container_desc = {
    "Queue", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const cmeta_container_desc stl_heap_container_desc = {
    "Heap", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const cmeta_container_desc stl_set_container_desc = {
    "Set", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const cmeta_container_desc stl_hash_set_container_desc = {
    "HashSet", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

#endif /* TURBOSTL_DETAIL_INSTANCE_META_H */
