#include <rocida/stl/list.h>
#include <rocida/stl/detail/instance_meta.h>

#include "sequence_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct list_node {
  struct list_node *previous;
  struct list_node *next;
  void *value;
} list_node_t;

typedef struct list_impl {
  list_node_t *head;
  list_node_t *tail;
  size_t size;
  size_t elem_size;
  size_t elem_stride;
  size_t elem_align;
  size_t element_limit;
  const cmeta_type_desc *element_type;
} list_impl_t;

static list_impl_t *list_impl(list_t *list) {
  return list == NULL ? NULL : (list_impl_t *)list->impl;
}

static const list_impl_t *list_impl_const(
    const list_t *list) {
  return list == NULL ? NULL : (const list_impl_t *)list->impl;
}

static bool list_valid(const list_t *list) {
  const list_impl_t *impl = list_impl_const(list);
  return impl != NULL && impl->elem_size != 0u;
}

static void list_set_iterator(list_iter_t *out_iterator,
                              const list_t *owner,
                              list_node_t *node) {
  if (out_iterator != NULL) {
    out_iterator->owner = owner;
    out_iterator->node = node;
  }
}

static list_node_t *list_node_new(
    const list_impl_t *impl, const void *elem,
    stl_status *out_status) {
  list_node_t *node;
  stl_status status;

  node = (list_node_t *)calloc(1u, sizeof(*node));
  if (node == NULL) {
    *out_status = STL_OUT_OF_MEMORY;
    return NULL;
  }
  status = sequence_allocate(1u, impl->elem_stride, impl->elem_align,
                             &node->value);
  if (status == STL_OK)
    status = sequence_copy(impl->element_type, impl->elem_size,
                           node->value, elem);
  if (status != STL_OK) {
    sequence_deallocate(node->value);
    free(node);
    *out_status = status;
    return NULL;
  }
  *out_status = STL_OK;
  return node;
}

static void list_node_free(list_node_t *node) {
  if (node == NULL) return;
  sequence_deallocate(node->value);
  free(node);
}

static stl_status list_initialize(
    list_t *list, const cmeta_type_desc *element_type,
    size_t elem_size, size_t elem_align, size_t element_limit) {
  list_impl_t *impl;
  size_t stride;
  stl_status status;

  if (list == NULL || list->impl != NULL) return STL_INVALID_ARGUMENT;
  status = sequence_stride(elem_size, elem_align, &stride);
  if (status != STL_OK) return status;
  impl = (list_impl_t *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return STL_OUT_OF_MEMORY;
  impl->elem_size = elem_size;
  impl->elem_stride = stride;
  impl->elem_align = elem_align;
  impl->element_limit = element_limit;
  impl->element_type = element_type;
  list->impl = impl;
  ++list->generation;
  return STL_OK;
}

stl_status list_init_bytes(list_t *list, size_t elem_size,
                           size_t elem_align,
                           size_t element_limit) {
  return list_initialize(list, NULL, elem_size, elem_align,
                         element_limit);
}

stl_status list_raw_init(list_t *list,
                         const cmeta_type_desc *element_type,
                         size_t element_limit) {
  stl_status status;

  if (list == NULL || list->impl != NULL) return STL_INVALID_ARGUMENT;
  status = sequence_require_type(element_type, false);
  if (status != STL_OK) return status;
  return list_initialize(list, element_type, element_type->size,
                         element_type->align, element_limit);
}

static stl_status list_insert_between(
    list_t *list, list_node_t *previous,
    list_node_t *next, const void *elem,
    list_iter_t *out_iterator) {
  list_impl_t *impl = list_impl(list);
  list_node_t *node;
  stl_status status;

  if (impl == NULL || elem == NULL) return STL_INVALID_ARGUMENT;
  if (impl->size >= impl->element_limit) return STL_CAPACITY_EXCEEDED;
  node = list_node_new(impl, elem, &status);
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
  list_set_iterator(out_iterator, list, node);
  return STL_OK;
}

stl_status list_push_front(list_t *list, const void *elem,
                           list_iter_t *out_iterator) {
  list_impl_t *impl = list_impl(list);
  if (impl == NULL) return STL_INVALID_ARGUMENT;
  return list_insert_between(list, NULL, impl->head, elem,
                             out_iterator);
}

stl_status list_push_back(list_t *list, const void *elem,
                          list_iter_t *out_iterator) {
  list_impl_t *impl = list_impl(list);
  if (impl == NULL) return STL_INVALID_ARGUMENT;
  return list_insert_between(list, impl->tail, NULL, elem,
                             out_iterator);
}

static bool list_iterator_matches(const list_t *list,
                                  list_iter_t position) {
  return list_valid(list) && position.owner == list &&
         position.node != NULL;
}

stl_status list_insert_before(list_t *list,
                              list_iter_t position,
                              const void *elem,
                              list_iter_t *out_iterator) {
  list_node_t *next;
  if (!list_iterator_matches(list, position))
    return STL_INVALID_ARGUMENT;
  next = (list_node_t *)position.node;
  return list_insert_between(list, next->previous, next, elem,
                             out_iterator);
}

stl_status list_insert_after(list_t *list,
                             list_iter_t position,
                             const void *elem,
                             list_iter_t *out_iterator) {
  list_node_t *previous;
  if (!list_iterator_matches(list, position))
    return STL_INVALID_ARGUMENT;
  previous = (list_node_t *)position.node;
  return list_insert_between(list, previous, previous->next, elem,
                             out_iterator);
}

static stl_status list_remove_node(list_t *list,
                                   list_node_t *node,
                                   void *out_elem) {
  list_impl_t *impl = list_impl(list);
  stl_status status;

  status = out_elem != NULL
               ? sequence_move_destroy(impl->element_type,
                                       impl->elem_size, out_elem,
                                       node->value)
               : sequence_destroy_value(impl->element_type,
                                        node->value);
  if (status != STL_OK) return status;
  if (node->previous != NULL)
    node->previous->next = node->next;
  else
    impl->head = node->next;
  if (node->next != NULL)
    node->next->previous = node->previous;
  else
    impl->tail = node->previous;
  list_node_free(node);
  --impl->size;
  ++list->generation;
  return STL_OK;
}

stl_status list_erase(list_t *list,
                      list_iter_t position,
                      void *out_elem) {
  if (!list_iterator_matches(list, position))
    return STL_INVALID_ARGUMENT;
  return list_remove_node(list, (list_node_t *)position.node,
                          out_elem);
}

stl_status list_pop_front(list_t *list, void *out_elem) {
  list_impl_t *impl = list_impl(list);
  if (impl == NULL) return STL_INVALID_ARGUMENT;
  if (impl->head == NULL) return STL_EMPTY;
  return list_remove_node(list, impl->head, out_elem);
}

stl_status list_pop_back(list_t *list, void *out_elem) {
  list_impl_t *impl = list_impl(list);
  if (impl == NULL) return STL_INVALID_ARGUMENT;
  if (impl->tail == NULL) return STL_EMPTY;
  return list_remove_node(list, impl->tail, out_elem);
}

static void list_release_nodes(list_impl_t *impl) {
  list_node_t *node;

  node = impl->head;
  while (node != NULL) {
    list_node_t *next = node->next;
    (void)sequence_destroy_value(impl->element_type, node->value);
    list_node_free(node);
    node = next;
  }
  impl->head = NULL;
  impl->tail = NULL;
  impl->size = 0u;
}

void list_clear(list_t *list) {
  list_impl_t *impl = list_impl(list);

  if (impl == NULL || impl->head == NULL) return;
  list_release_nodes(impl);
  ++list->generation;
}

void list_raw_destroy_storage(list_t *list) {
  list_impl_t *impl;

  if (list == NULL) return;
  impl = list_impl(list);
  if (impl == NULL) return;
  list_release_nodes(impl);
  free(impl);
  list->impl = NULL;
  ++list->generation;
}

static stl_status list_from_common(
    list_t *list, const void *elements, size_t count,
    const cmeta_type_desc *element_type, size_t elem_size, size_t elem_align,
    size_t element_limit) {
  list_t temporary = {0};
  stl_status status;
  size_t index;

  if (list == NULL || list->impl != NULL) return STL_INVALID_ARGUMENT;
  if (count > element_limit) return STL_CAPACITY_EXCEEDED;
  if (count != 0u && elements == NULL) return STL_INVALID_ARGUMENT;
  status = element_type != NULL
               ? list_raw_init(&temporary, element_type, element_limit)
               : list_init_bytes(&temporary, elem_size, elem_align,
                                 element_limit);
  if (status != STL_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = list_push_back(
        &temporary, (const unsigned char *)elements + index * elem_size,
        NULL);
    if (status != STL_OK) {
      list_raw_destroy_storage(&temporary);
      return status;
    }
  }
  temporary.generation = list->generation + UINT64_C(1);
  *list = temporary;
  return STL_OK;
}

stl_status list_raw_from_array(
    list_t *list, const void *elements, size_t count,
    const cmeta_type_desc *element_type, size_t element_limit) {
  if (element_type == NULL) return STL_INVALID_ARGUMENT;
  return list_from_common(list, elements, count, element_type,
                          element_type->size, element_type->align,
                          element_limit);
}

stl_status list_from_array_bytes(
    list_t *list, const void *elements, size_t count, size_t elem_size,
    size_t elem_align, size_t element_limit) {
  return list_from_common(list, elements, count, NULL, elem_size,
                          elem_align, element_limit);
}

list_iter_t list_begin(const list_t *list) {
  const list_impl_t *impl = list_impl_const(list);
  list_iter_t result = {list, impl == NULL ? NULL : impl->head};
  return result;
}

list_iter_t list_end(const list_t *list) {
  list_iter_t result = {list, NULL};
  return result;
}

stl_status list_iter_next(list_iter_t *iterator) {
  list_node_t *node;
  if (iterator == NULL || !list_valid(iterator->owner) ||
      iterator->node == NULL)
    return STL_NOT_FOUND;
  node = (list_node_t *)iterator->node;
  iterator->node = node->next;
  return STL_OK;
}

stl_status list_iter_prev(list_iter_t *iterator) {
  const list_impl_t *impl;
  list_node_t *node;

  if (iterator == NULL || !list_valid(iterator->owner))
    return STL_INVALID_ARGUMENT;
  impl = list_impl_const(iterator->owner);
  if (iterator->node == NULL) {
    if (impl->tail == NULL) return STL_NOT_FOUND;
    iterator->node = impl->tail;
    return STL_OK;
  }
  node = (list_node_t *)iterator->node;
  if (node->previous == NULL) return STL_NOT_FOUND;
  iterator->node = node->previous;
  return STL_OK;
}

bool list_iter_equal(list_iter_t left, list_iter_t right) {
  return left.owner == right.owner && left.node == right.node;
}

void *list_iter_value(list_iter_t iterator) {
  list_node_t *node;
  if (!list_valid(iterator.owner) || iterator.node == NULL) return NULL;
  node = (list_node_t *)iterator.node;
  return node->value;
}

const void *list_iter_value_const(list_iter_t iterator) {
  return list_iter_value(iterator);
}

void *list_front(list_t *list) {
  list_impl_t *impl = list_impl(list);
  return impl != NULL && impl->head != NULL ? impl->head->value : NULL;
}

const void *list_front_const(const list_t *list) {
  const list_impl_t *impl = list_impl_const(list);
  return impl != NULL && impl->head != NULL ? impl->head->value : NULL;
}

void *list_back(list_t *list) {
  list_impl_t *impl = list_impl(list);
  return impl != NULL && impl->tail != NULL ? impl->tail->value : NULL;
}

const void *list_back_const(const list_t *list) {
  const list_impl_t *impl = list_impl_const(list);
  return impl != NULL && impl->tail != NULL ? impl->tail->value : NULL;
}

size_t list_size(const list_t *list) {
  const list_impl_t *impl = list_impl_const(list);
  return impl == NULL ? 0u : impl->size;
}

uint64_t list_generation(const list_t *list) {
  return list == NULL ? UINT64_C(0) : list->generation;
}

bool list_empty(const list_t *list) {
  return list_size(list) == 0u;
}

bool list_range_next(const list_t *list,
                     cmeta_range_cursor *cursor,
                     const void **out_value) {
  const list_impl_t *impl = list_impl_const(list);
  list_node_t *node;

  if (impl == NULL || cursor == NULL || out_value == NULL) return false;
  if (cursor->state[1] == NULL) {
    cursor->state[1] = (void *)list;
    node = impl->head;
  } else {
    if (cursor->state[1] != (void *)list) return false;
    node = (list_node_t *)cursor->state[0];
  }
  if (node == NULL) return false;
  *out_value = node->value;
  cursor->state[0] = node->next;
  return true;
}

static cmeta_status stl_list_cmeta_status(stl_status status) {
  switch (status) {
    case STL_OK: return CMETA_OK;
    case STL_INVALID_ARGUMENT: return CMETA_INVALID_ARGUMENT;
    case STL_OUT_OF_MEMORY: return CMETA_OUT_OF_MEMORY;
    case STL_CAPACITY_EXCEEDED: return CMETA_CAPACITY_EXCEEDED;
    case STL_TYPE_MISMATCH: return CMETA_TYPE_MISMATCH;
    case STL_TRAIT_MISSING: return CMETA_TRAIT_MISSING;
    case STL_EMPTY:
    case STL_NOT_FOUND:
    default: return CMETA_CALLBACK_ERROR;
  }
}

static size_t stl_list_range_size(const void *object) {
  return list_size((const list_t *)object);
}

static uint64_t stl_list_range_version(const void *object) {
  return list_generation((const list_t *)object);
}

static cmeta_gen_status stl_list_meta_range_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
  const list_t *list = (const list_t *)object;
  const void *value = NULL;
  cmeta_range_cursor next_cursor;
  if (list == NULL || cursor == NULL || out_value == NULL ||
      list->element_type == NULL)
    return CMETA_GEN_ERROR;
  next_cursor = *cursor;
  if (!list_range_next(list, &next_cursor, &value))
    return CMETA_GEN_DONE;
  if (!stl_cmeta_range_construct(list->element_type, out_value, value))
    return CMETA_GEN_ERROR;
  *cursor = next_cursor;
  return cursor->state[0] == NULL ? CMETA_GEN_VALUE_AND_DONE :
                                    CMETA_GEN_VALUE;
}

static cmeta_range stl_list_range_factory(const void *object) {
  const list_t *list = (const list_t *)object;
  cmeta_range range = {
      object,
      list != NULL ? list->element_type : NULL,
      stl_cmeta_range_flags_for(
          list != NULL ? list->element_type : NULL,
          CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE),
      stl_list_range_size,
      stl_list_meta_range_next,
      stl_list_range_version(object),
      stl_list_range_version};
  return range;
}

static cmeta_status stl_list_collector_begin(
    void *context, const cmeta_type_desc *input, size_t limit) {
  list_t *output = (list_t *)context;
  if (output == NULL || output->element_type == NULL || input == NULL)
    return CMETA_INVALID_ARGUMENT;
  if (!cmeta_type_equal(output->element_type, input))
    return CMETA_TYPE_MISMATCH;
  return stl_list_cmeta_status(list_init(output, limit));
}

static cmeta_status stl_list_collector_accept(void *context,
                                              const void *value) {
  if (context == NULL || value == NULL)
    return CMETA_INVALID_ARGUMENT;
  return stl_list_cmeta_status(
      list_push_back((list_t *)context, value, NULL));
}

static cmeta_status stl_list_collector_finish(void *context) {
  (void)context;
  return CMETA_OK;
}

static void stl_list_collector_abort(void *context) {
  if (context != NULL)
    list_destroy((list_t *)context);
}

static const cmeta_collector_ops stl_list_collector_ops = {
    stl_list_collector_begin,
    stl_list_collector_accept,
    stl_list_collector_finish,
    stl_list_collector_abort};

static cmeta_collector stl_list_collector_factory(void *zero_output,
                                                  size_t limit) {
  list_t *output = (list_t *)zero_output;
  cmeta_collector result = {
      &stl_list_collector_ops,
      output,
      output,
      output != NULL ? output->element_type : NULL,
      limit,
      0u,
      CMETA_COLLECTOR_ZERO,
      CMETA_OK};
  return result;
}

const cmeta_container_desc stl_list_container_desc = {
    "List",
    NULL,
    NULL,
    NULL,
    NULL,
    stl_list_range_factory,
    NULL,
    NULL,
    NULL,
    stl_list_collector_factory,
    &stl_list_container_ext};
