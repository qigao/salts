#include <turbostl/list.h>

#include "turbo_sequence_internal.h"

#include <stdlib.h>

typedef struct turbo_list_node {
  struct turbo_list_node *previous;
  struct turbo_list_node *next;
  void *value;
} turbo_list_node_t;

typedef struct turbo_list_impl {
  turbo_list_node_t *head;
  turbo_list_node_t *tail;
  size_t size;
  size_t elem_size;
  size_t elem_stride;
  size_t elem_align;
  size_t element_limit;
  const cmeta_type_desc *element_type;
} turbo_list_impl_t;

static turbo_list_impl_t *turbo_list_impl(turbo_list_t *list) {
  return list == NULL ? NULL : (turbo_list_impl_t *)list->impl;
}

static const turbo_list_impl_t *turbo_list_impl_const(
    const turbo_list_t *list) {
  return list == NULL ? NULL : (const turbo_list_impl_t *)list->impl;
}

static bool turbo_list_valid(const turbo_list_t *list) {
  const turbo_list_impl_t *impl = turbo_list_impl_const(list);
  return impl != NULL && impl->elem_size != 0u;
}

static void turbo_list_set_iterator(turbo_list_iter_t *out_iterator,
                                    const turbo_list_t *owner,
                                    turbo_list_node_t *node) {
  if (out_iterator != NULL) {
    out_iterator->owner = owner;
    out_iterator->node = node;
  }
}

static turbo_list_node_t *turbo_list_node_new(
    const turbo_list_impl_t *impl, const void *elem,
    turbo_stl_status *out_status) {
  turbo_list_node_t *node;
  turbo_stl_status status;

  node = (turbo_list_node_t *)calloc(1u, sizeof(*node));
  if (node == NULL) {
    *out_status = TURBO_STL_OUT_OF_MEMORY;
    return NULL;
  }
  status = turbo_sequence_allocate(1u, impl->elem_stride, impl->elem_align,
                                   &node->value);
  if (status == TURBO_STL_OK)
    status = turbo_sequence_copy(impl->element_type, impl->elem_size,
                                 node->value, elem);
  if (status != TURBO_STL_OK) {
    turbo_sequence_deallocate(node->value);
    free(node);
    *out_status = status;
    return NULL;
  }
  *out_status = TURBO_STL_OK;
  return node;
}

static void turbo_list_node_free(turbo_list_node_t *node) {
  if (node == NULL) return;
  turbo_sequence_deallocate(node->value);
  free(node);
}

static turbo_stl_status turbo_list_initialize(
    turbo_list_t *list, const cmeta_type_desc *element_type,
    size_t elem_size, size_t elem_align, size_t element_limit) {
  turbo_list_impl_t *impl;
  size_t stride;
  turbo_stl_status status;

  if (list == NULL || list->impl != NULL) return TURBO_STL_INVALID_ARGUMENT;
  status = turbo_sequence_stride(elem_size, elem_align, &stride);
  if (status != TURBO_STL_OK) return status;
  impl = (turbo_list_impl_t *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_STL_OUT_OF_MEMORY;
  impl->elem_size = elem_size;
  impl->elem_stride = stride;
  impl->elem_align = elem_align;
  impl->element_limit = element_limit;
  impl->element_type = element_type;
  list->impl = impl;
  ++list->generation;
  return TURBO_STL_OK;
}

turbo_stl_status turbo_list_init_bytes(turbo_list_t *list, size_t elem_size,
                                       size_t elem_align,
                                       size_t element_limit) {
  return turbo_list_initialize(list, NULL, elem_size, elem_align,
                               element_limit);
}

turbo_stl_status turbo_list_init(turbo_list_t *list,
                                 const cmeta_type_desc *element_type,
                                 size_t element_limit) {
  turbo_stl_status status;

  if (list == NULL || list->impl != NULL) return TURBO_STL_INVALID_ARGUMENT;
  status = turbo_sequence_require_type(element_type, false);
  if (status != TURBO_STL_OK) return status;
  return turbo_list_initialize(list, element_type, element_type->size,
                               element_type->align, element_limit);
}

static turbo_stl_status turbo_list_insert_between(
    turbo_list_t *list, turbo_list_node_t *previous,
    turbo_list_node_t *next, const void *elem,
    turbo_list_iter_t *out_iterator) {
  turbo_list_impl_t *impl = turbo_list_impl(list);
  turbo_list_node_t *node;
  turbo_stl_status status;

  if (impl == NULL || elem == NULL) return TURBO_STL_INVALID_ARGUMENT;
  if (impl->size >= impl->element_limit) return TURBO_STL_CAPACITY_EXCEEDED;
  node = turbo_list_node_new(impl, elem, &status);
  if (node == NULL) return status;
  node->previous = previous;
  node->next = next;
  if (previous != NULL)
    previous->next = node;
  else
    impl->head = node;
  if (next != NULL)
    next->previous = node;
  else
    impl->tail = node;
  ++impl->size;
  ++list->generation;
  turbo_list_set_iterator(out_iterator, list, node);
  return TURBO_STL_OK;
}

turbo_stl_status turbo_list_push_front(turbo_list_t *list, const void *elem,
                                       turbo_list_iter_t *out_iterator) {
  turbo_list_impl_t *impl = turbo_list_impl(list);
  if (impl == NULL) return TURBO_STL_INVALID_ARGUMENT;
  return turbo_list_insert_between(list, NULL, impl->head, elem,
                                   out_iterator);
}

turbo_stl_status turbo_list_push_back(turbo_list_t *list, const void *elem,
                                      turbo_list_iter_t *out_iterator) {
  turbo_list_impl_t *impl = turbo_list_impl(list);
  if (impl == NULL) return TURBO_STL_INVALID_ARGUMENT;
  return turbo_list_insert_between(list, impl->tail, NULL, elem,
                                   out_iterator);
}

static bool turbo_list_iterator_matches(const turbo_list_t *list,
                                        turbo_list_iter_t position) {
  return turbo_list_valid(list) && position.owner == list &&
         position.node != NULL;
}

turbo_stl_status turbo_list_insert_before(turbo_list_t *list,
                                          turbo_list_iter_t position,
                                          const void *elem,
                                          turbo_list_iter_t *out_iterator) {
  turbo_list_node_t *next;
  if (!turbo_list_iterator_matches(list, position))
    return TURBO_STL_INVALID_ARGUMENT;
  next = (turbo_list_node_t *)position.node;
  return turbo_list_insert_between(list, next->previous, next, elem,
                                   out_iterator);
}

turbo_stl_status turbo_list_insert_after(turbo_list_t *list,
                                         turbo_list_iter_t position,
                                         const void *elem,
                                         turbo_list_iter_t *out_iterator) {
  turbo_list_node_t *previous;
  if (!turbo_list_iterator_matches(list, position))
    return TURBO_STL_INVALID_ARGUMENT;
  previous = (turbo_list_node_t *)position.node;
  return turbo_list_insert_between(list, previous, previous->next, elem,
                                   out_iterator);
}

static turbo_stl_status turbo_list_remove_node(turbo_list_t *list,
                                               turbo_list_node_t *node,
                                               void *out_elem) {
  turbo_list_impl_t *impl = turbo_list_impl(list);
  turbo_stl_status status;

  status = out_elem != NULL
               ? turbo_sequence_move_destroy(impl->element_type,
                                             impl->elem_size, out_elem,
                                             node->value)
               : turbo_sequence_destroy_value(impl->element_type,
                                              node->value);
  if (status != TURBO_STL_OK) return status;
  if (node->previous != NULL)
    node->previous->next = node->next;
  else
    impl->head = node->next;
  if (node->next != NULL)
    node->next->previous = node->previous;
  else
    impl->tail = node->previous;
  turbo_list_node_free(node);
  --impl->size;
  ++list->generation;
  return TURBO_STL_OK;
}

turbo_stl_status turbo_list_erase(turbo_list_t *list,
                                  turbo_list_iter_t position,
                                  void *out_elem) {
  if (!turbo_list_iterator_matches(list, position))
    return TURBO_STL_INVALID_ARGUMENT;
  return turbo_list_remove_node(list, (turbo_list_node_t *)position.node,
                                out_elem);
}

turbo_stl_status turbo_list_pop_front(turbo_list_t *list, void *out_elem) {
  turbo_list_impl_t *impl = turbo_list_impl(list);
  if (impl == NULL) return TURBO_STL_INVALID_ARGUMENT;
  if (impl->head == NULL) return TURBO_STL_EMPTY;
  return turbo_list_remove_node(list, impl->head, out_elem);
}

turbo_stl_status turbo_list_pop_back(turbo_list_t *list, void *out_elem) {
  turbo_list_impl_t *impl = turbo_list_impl(list);
  if (impl == NULL) return TURBO_STL_INVALID_ARGUMENT;
  if (impl->tail == NULL) return TURBO_STL_EMPTY;
  return turbo_list_remove_node(list, impl->tail, out_elem);
}

static void turbo_list_release_nodes(turbo_list_impl_t *impl) {
  turbo_list_node_t *node;

  node = impl->head;
  while (node != NULL) {
    turbo_list_node_t *next = node->next;
    (void)turbo_sequence_destroy_value(impl->element_type, node->value);
    turbo_list_node_free(node);
    node = next;
  }
  impl->head = NULL;
  impl->tail = NULL;
  impl->size = 0u;
}

void turbo_list_clear(turbo_list_t *list) {
  turbo_list_impl_t *impl = turbo_list_impl(list);

  if (impl == NULL || impl->head == NULL) return;
  turbo_list_release_nodes(impl);
  ++list->generation;
}

void turbo_list_destroy(turbo_list_t *list) {
  turbo_list_impl_t *impl;

  if (list == NULL) return;
  impl = turbo_list_impl(list);
  if (impl == NULL) return;
  turbo_list_release_nodes(impl);
  free(impl);
  list->impl = NULL;
  ++list->generation;
}

static turbo_stl_status turbo_list_from_common(
    turbo_list_t *list, const void *elements, size_t count,
    const cmeta_type_desc *element_type, size_t elem_size, size_t elem_align,
    size_t element_limit) {
  turbo_list_t temporary = {0};
  turbo_stl_status status;
  size_t index;

  if (list == NULL || list->impl != NULL) return TURBO_STL_INVALID_ARGUMENT;
  if (count > element_limit) return TURBO_STL_CAPACITY_EXCEEDED;
  if (count != 0u && elements == NULL) return TURBO_STL_INVALID_ARGUMENT;
  status = element_type != NULL
               ? turbo_list_init(&temporary, element_type, element_limit)
               : turbo_list_init_bytes(&temporary, elem_size, elem_align,
                                       element_limit);
  if (status != TURBO_STL_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = turbo_list_push_back(
        &temporary, (const unsigned char *)elements + index * elem_size,
        NULL);
    if (status != TURBO_STL_OK) {
      turbo_list_destroy(&temporary);
      return status;
    }
  }
  temporary.generation = list->generation + UINT64_C(1);
  *list = temporary;
  return TURBO_STL_OK;
}

turbo_stl_status turbo_list_from_array(
    turbo_list_t *list, const void *elements, size_t count,
    const cmeta_type_desc *element_type, size_t element_limit) {
  if (element_type == NULL) return TURBO_STL_INVALID_ARGUMENT;
  return turbo_list_from_common(list, elements, count, element_type,
                                element_type->size, element_type->align,
                                element_limit);
}

turbo_stl_status turbo_list_from_array_bytes(
    turbo_list_t *list, const void *elements, size_t count, size_t elem_size,
    size_t elem_align, size_t element_limit) {
  return turbo_list_from_common(list, elements, count, NULL, elem_size,
                                elem_align, element_limit);
}

turbo_list_iter_t turbo_list_begin(const turbo_list_t *list) {
  const turbo_list_impl_t *impl = turbo_list_impl_const(list);
  turbo_list_iter_t result = {list, impl == NULL ? NULL : impl->head};
  return result;
}

turbo_list_iter_t turbo_list_end(const turbo_list_t *list) {
  turbo_list_iter_t result = {list, NULL};
  return result;
}

turbo_stl_status turbo_list_iter_next(turbo_list_iter_t *iterator) {
  turbo_list_node_t *node;
  if (iterator == NULL || !turbo_list_valid(iterator->owner) ||
      iterator->node == NULL)
    return TURBO_STL_NOT_FOUND;
  node = (turbo_list_node_t *)iterator->node;
  iterator->node = node->next;
  return TURBO_STL_OK;
}

turbo_stl_status turbo_list_iter_prev(turbo_list_iter_t *iterator) {
  const turbo_list_impl_t *impl;
  turbo_list_node_t *node;

  if (iterator == NULL || !turbo_list_valid(iterator->owner))
    return TURBO_STL_INVALID_ARGUMENT;
  impl = turbo_list_impl_const(iterator->owner);
  if (iterator->node == NULL) {
    if (impl->tail == NULL) return TURBO_STL_NOT_FOUND;
    iterator->node = impl->tail;
    return TURBO_STL_OK;
  }
  node = (turbo_list_node_t *)iterator->node;
  if (node->previous == NULL) return TURBO_STL_NOT_FOUND;
  iterator->node = node->previous;
  return TURBO_STL_OK;
}

bool turbo_list_iter_equal(turbo_list_iter_t left, turbo_list_iter_t right) {
  return left.owner == right.owner && left.node == right.node;
}

void *turbo_list_iter_value(turbo_list_iter_t iterator) {
  turbo_list_node_t *node;
  if (!turbo_list_valid(iterator.owner) || iterator.node == NULL) return NULL;
  node = (turbo_list_node_t *)iterator.node;
  return node->value;
}

const void *turbo_list_iter_value_const(turbo_list_iter_t iterator) {
  return turbo_list_iter_value(iterator);
}

void *turbo_list_front(turbo_list_t *list) {
  turbo_list_impl_t *impl = turbo_list_impl(list);
  return impl != NULL && impl->head != NULL ? impl->head->value : NULL;
}

const void *turbo_list_front_const(const turbo_list_t *list) {
  const turbo_list_impl_t *impl = turbo_list_impl_const(list);
  return impl != NULL && impl->head != NULL ? impl->head->value : NULL;
}

void *turbo_list_back(turbo_list_t *list) {
  turbo_list_impl_t *impl = turbo_list_impl(list);
  return impl != NULL && impl->tail != NULL ? impl->tail->value : NULL;
}

const void *turbo_list_back_const(const turbo_list_t *list) {
  const turbo_list_impl_t *impl = turbo_list_impl_const(list);
  return impl != NULL && impl->tail != NULL ? impl->tail->value : NULL;
}

size_t turbo_list_size(const turbo_list_t *list) {
  const turbo_list_impl_t *impl = turbo_list_impl_const(list);
  return impl == NULL ? 0u : impl->size;
}

uint64_t turbo_list_generation(const turbo_list_t *list) {
  return list == NULL ? UINT64_C(0) : list->generation;
}

bool turbo_list_empty(const turbo_list_t *list) {
  return turbo_list_size(list) == 0u;
}

bool turbo_list_range_next(const turbo_list_t *list,
                           cmeta_range_cursor *cursor,
                           const void **out_value) {
  const turbo_list_impl_t *impl = turbo_list_impl_const(list);
  turbo_list_node_t *node;

  if (impl == NULL || cursor == NULL || out_value == NULL) return false;
  if (cursor->state[1] == NULL) {
    cursor->state[1] = (void *)list;
    node = impl->head;
  } else {
    if (cursor->state[1] != (void *)list) return false;
    node = (turbo_list_node_t *)cursor->state[0];
  }
  if (node == NULL) return false;
  *out_value = node->value;
  cursor->state[0] = node->next;
  return true;
}
