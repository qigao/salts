#include <turbo/container/list.h>

#include "turbo_sequence_internal.h"

#include <stdlib.h>
#include <string.h>

struct turbo_list_node {
  struct turbo_list_node *previous;
  struct turbo_list_node *next;
  struct turbo_list_node *free_next;
  void *value;
};

static bool turbo_list_valid(const turbo_list_t *list) {
  return list != NULL && list->initialized && list->elem_size != 0u;
}

static turbo_list_node_t *turbo_list_node_new(const turbo_list_t *list) {
  turbo_list_node_t *node;
  container_status status;
  node = (turbo_list_node_t *)calloc(1u, sizeof(*node));
  if (node == NULL) return NULL;
  status = turbo_sequence_allocate(1u, list->elem_stride, list->elem_align,
                                   &node->value);
  if (status != CONTAINER_OK) {
    free(node);
    return NULL;
  }
  return node;
}

static void turbo_list_node_free(turbo_list_node_t *node) {
  if (node == NULL) return;
  turbo_sequence_deallocate(node->value);
  free(node);
}

static void turbo_list_free_chain(turbo_list_node_t *node) {
  while (node != NULL) {
    turbo_list_node_t *next = node->free_next;
    turbo_list_node_free(node);
    node = next;
  }
}

static container_status turbo_list_initialize(
    turbo_list_t *list, const cmeta_type_desc *element_type,
    size_t elem_size, size_t elem_align, size_t element_limit) {
  size_t stride;
  uint64_t generation;
  container_status status;
  if (list == NULL || list->initialized) return CONTAINER_INVALID_ARGUMENT;
  status = turbo_sequence_stride(elem_size, elem_align, &stride);
  if (status != CONTAINER_OK) return status;
  generation = list->generation + UINT64_C(1);
  memset(list, 0, sizeof(*list));
  list->elem_size = elem_size;
  list->elem_stride = stride;
  list->elem_align = elem_align;
  list->element_limit = element_limit;
  list->element_type = element_type;
  list->generation = generation;
  list->initialized = true;
  return CONTAINER_OK;
}

container_status turbo_list_init_bytes(turbo_list_t *list, size_t elem_size,
                                       size_t elem_align,
                                       size_t element_limit) {
  return turbo_list_initialize(list, NULL, elem_size, elem_align,
                               element_limit);
}

container_status turbo_list_init(turbo_list_t *list,
                                 const cmeta_type_desc *element_type,
                                 size_t element_limit) {
  container_status status;
  if (list == NULL || list->initialized) return CONTAINER_INVALID_ARGUMENT;
  status = turbo_sequence_require_type(element_type, false);
  if (status != CONTAINER_OK) return status;
  return turbo_list_initialize(list, element_type, element_type->size,
                               element_type->align, element_limit);
}

container_status turbo_list_reserve(turbo_list_t *list,
                                    size_t min_capacity) {
  turbo_list_node_t *new_nodes = NULL;
  turbo_list_node_t *new_tail = NULL;
  size_t needed;
  size_t index;
  if (!turbo_list_valid(list)) return CONTAINER_INVALID_ARGUMENT;
  if (min_capacity > list->element_limit)
    return CONTAINER_CAPACITY_EXCEEDED;
  if (min_capacity <= list->capacity) return CONTAINER_OK;
  needed = min_capacity - list->capacity;
  for (index = 0u; index < needed; ++index) {
    turbo_list_node_t *node = turbo_list_node_new(list);
    if (node == NULL) {
      turbo_list_free_chain(new_nodes);
      return CONTAINER_OUT_OF_MEMORY;
    }
    if (new_tail != NULL)
      new_tail->free_next = node;
    else
      new_nodes = node;
    new_tail = node;
  }
  new_tail->free_next = list->free_nodes;
  list->free_nodes = new_nodes;
  list->capacity = min_capacity;
  return CONTAINER_OK;
}

static container_status turbo_list_prepare_node(turbo_list_t *list,
                                                const void *elem,
                                                turbo_list_node_t **out_node) {
  turbo_list_node_t *node;
  bool allocated = false;
  container_status status;
  if (elem == NULL || out_node == NULL) return CONTAINER_INVALID_ARGUMENT;
  node = list->free_nodes;
  if (node == NULL) {
    node = turbo_list_node_new(list);
    if (node == NULL) return CONTAINER_OUT_OF_MEMORY;
    allocated = true;
  }
  status = turbo_sequence_copy(list->element_type, list->elem_size,
                               node->value, elem);
  if (status != CONTAINER_OK) {
    if (allocated) turbo_list_node_free(node);
    return status;
  }
  if (allocated)
    ++list->capacity;
  else
    list->free_nodes = node->free_next;
  node->previous = NULL;
  node->next = NULL;
  node->free_next = NULL;
  *out_node = node;
  return CONTAINER_OK;
}

container_status turbo_list_push_front(turbo_list_t *list,
                                       const void *elem) {
  turbo_list_node_t *node;
  container_status status;
  if (!turbo_list_valid(list) || elem == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  if (list->size >= list->element_limit) return CONTAINER_CAPACITY_EXCEEDED;
  status = turbo_list_prepare_node(list, elem, &node);
  if (status != CONTAINER_OK) return status;
  node->next = list->head;
  if (list->head != NULL)
    list->head->previous = node;
  else
    list->tail = node;
  list->head = node;
  ++list->size;
  ++list->generation;
  return CONTAINER_OK;
}

container_status turbo_list_push_back(turbo_list_t *list,
                                      const void *elem) {
  turbo_list_node_t *node;
  container_status status;
  if (!turbo_list_valid(list) || elem == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  if (list->size >= list->element_limit) return CONTAINER_CAPACITY_EXCEEDED;
  status = turbo_list_prepare_node(list, elem, &node);
  if (status != CONTAINER_OK) return status;
  node->previous = list->tail;
  if (list->tail != NULL)
    list->tail->next = node;
  else
    list->head = node;
  list->tail = node;
  ++list->size;
  ++list->generation;
  return CONTAINER_OK;
}

static container_status turbo_list_remove_node(turbo_list_t *list,
                                               turbo_list_node_t *node,
                                               void *out_elem) {
  container_status status;
  status = out_elem != NULL
               ? turbo_sequence_move_destroy(list->element_type,
                                             list->elem_size, out_elem,
                                             node->value)
               : turbo_sequence_destroy_value(list->element_type,
                                              node->value);
  if (status != CONTAINER_OK) return status;
  if (node->previous != NULL)
    node->previous->next = node->next;
  else
    list->head = node->next;
  if (node->next != NULL)
    node->next->previous = node->previous;
  else
    list->tail = node->previous;
  node->previous = NULL;
  node->next = NULL;
  node->free_next = list->free_nodes;
  list->free_nodes = node;
  --list->size;
  ++list->generation;
  return CONTAINER_OK;
}

container_status turbo_list_pop_front(turbo_list_t *list, void *out_elem) {
  if (!turbo_list_valid(list)) return CONTAINER_INVALID_ARGUMENT;
  if (list->head == NULL) return CONTAINER_EMPTY;
  return turbo_list_remove_node(list, list->head, out_elem);
}

container_status turbo_list_pop_back(turbo_list_t *list, void *out_elem) {
  if (!turbo_list_valid(list)) return CONTAINER_INVALID_ARGUMENT;
  if (list->tail == NULL) return CONTAINER_EMPTY;
  return turbo_list_remove_node(list, list->tail, out_elem);
}

void turbo_list_clear(turbo_list_t *list) {
  turbo_list_node_t *node;
  turbo_list_node_t *old_free;
  if (!turbo_list_valid(list) || list->head == NULL) return;
  node = list->head;
  old_free = list->free_nodes;
  while (node != NULL) {
    turbo_list_node_t *next = node->next;
    (void)turbo_sequence_destroy_value(list->element_type, node->value);
    node->previous = NULL;
    node->next = NULL;
    node->free_next = next != NULL ? next : old_free;
    node = next;
  }
  list->free_nodes = list->head;
  list->head = NULL;
  list->tail = NULL;
  list->size = 0u;
  ++list->generation;
}

void turbo_list_destroy(turbo_list_t *list) {
  turbo_list_node_t *node;
  uint64_t generation;
  if (list == NULL) return;
  generation = list->generation;
  if (list->initialized) {
    node = list->head;
    while (node != NULL) {
      turbo_list_node_t *next = node->next;
      (void)turbo_sequence_destroy_value(list->element_type, node->value);
      turbo_list_node_free(node);
      node = next;
    }
    turbo_list_free_chain(list->free_nodes);
    ++generation;
  }
  memset(list, 0, sizeof(*list));
  list->generation = generation;
}

static container_status turbo_list_from_common(
    turbo_list_t *list, const void *elements, size_t count,
    const cmeta_type_desc *element_type, size_t elem_size, size_t elem_align,
    size_t element_limit) {
  turbo_list_t temporary = {0};
  container_status status;
  uint64_t generation;
  size_t index;
  if (list == NULL || list->initialized) return CONTAINER_INVALID_ARGUMENT;
  if (count > element_limit) return CONTAINER_CAPACITY_EXCEEDED;
  if (count != 0u && elements == NULL) return CONTAINER_INVALID_ARGUMENT;
  status = element_type != NULL
               ? turbo_list_init(&temporary, element_type, element_limit)
               : turbo_list_init_bytes(&temporary, elem_size, elem_align,
                                       element_limit);
  if (status != CONTAINER_OK) return status;
  status = turbo_list_reserve(&temporary, count);
  if (status != CONTAINER_OK) {
    turbo_list_destroy(&temporary);
    return status;
  }
  for (index = 0u; index < count; ++index) {
    status = turbo_list_push_back(
        &temporary, (const unsigned char *)elements + index * elem_size);
    if (status != CONTAINER_OK) {
      turbo_list_destroy(&temporary);
      return status;
    }
  }
  generation = list->generation + UINT64_C(1);
  temporary.generation = generation;
  *list = temporary;
  return CONTAINER_OK;
}

container_status turbo_list_from_array(
    turbo_list_t *list, const void *elements, size_t count,
    const cmeta_type_desc *element_type, size_t element_limit) {
  if (element_type == NULL) return CONTAINER_INVALID_ARGUMENT;
  return turbo_list_from_common(list, elements, count, element_type,
                                element_type->size, element_type->align,
                                element_limit);
}

container_status turbo_list_from_array_bytes(
    turbo_list_t *list, const void *elements, size_t count, size_t elem_size,
    size_t elem_align, size_t element_limit) {
  return turbo_list_from_common(list, elements, count, NULL, elem_size,
                                elem_align, element_limit);
}

void *turbo_list_front(turbo_list_t *list) {
  return turbo_list_valid(list) && list->head != NULL ? list->head->value
                                                       : NULL;
}

const void *turbo_list_front_const(const turbo_list_t *list) {
  return turbo_list_valid(list) && list->head != NULL ? list->head->value
                                                       : NULL;
}

void *turbo_list_back(turbo_list_t *list) {
  return turbo_list_valid(list) && list->tail != NULL ? list->tail->value
                                                       : NULL;
}

const void *turbo_list_back_const(const turbo_list_t *list) {
  return turbo_list_valid(list) && list->tail != NULL ? list->tail->value
                                                       : NULL;
}

static turbo_list_node_t *turbo_list_node_at(const turbo_list_t *list,
                                             size_t index) {
  turbo_list_node_t *node;
  if (!turbo_list_valid(list) || index >= list->size) return NULL;
  if (index <= list->size / 2u) {
    node = list->head;
    while (index-- != 0u) node = node->next;
  } else {
    size_t remaining = list->size - index - 1u;
    node = list->tail;
    while (remaining-- != 0u) node = node->previous;
  }
  return node;
}

void *turbo_list_at(turbo_list_t *list, size_t index) {
  turbo_list_node_t *node = turbo_list_node_at(list, index);
  return node == NULL ? NULL : node->value;
}

const void *turbo_list_at_const(const turbo_list_t *list, size_t index) {
  turbo_list_node_t *node = turbo_list_node_at(list, index);
  return node == NULL ? NULL : node->value;
}

size_t turbo_list_size(const turbo_list_t *list) {
  return turbo_list_valid(list) ? list->size : 0u;
}

size_t turbo_list_capacity(const turbo_list_t *list) {
  return turbo_list_valid(list) ? list->capacity : 0u;
}

uint64_t turbo_list_generation(const turbo_list_t *list) {
  return list == NULL ? UINT64_C(0) : list->generation;
}

bool turbo_list_empty(const turbo_list_t *list) {
  return turbo_list_size(list) == 0u;
}

bool turbo_list_range_next(const turbo_list_t *list, size_t *cursor,
                           const void **out_value) {
  turbo_list_node_t *node;
  if (!turbo_list_valid(list) || cursor == NULL || out_value == NULL ||
      *cursor == SIZE_MAX)
    return false;
  node = *cursor == 0u ? list->head
                       : (turbo_list_node_t *)(uintptr_t)*cursor;
  if (node == NULL) return false;
  *out_value = node->value;
  *cursor = node->next == NULL ? SIZE_MAX : (size_t)(uintptr_t)node->next;
  return true;
}
