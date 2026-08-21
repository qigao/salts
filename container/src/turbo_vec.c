#include <turbo/container/vec.h>

#include <stdlib.h>
#include <string.h>

#define TURBO_VEC_MIN_CAPACITY 8U

static int turbo_vec_valid(const turbo_vec_t *vec) {
  return vec != NULL && vec->elem_size > 0;
}

static int turbo_vec_grow_to(turbo_vec_t *vec, size_t min_capacity) {
  size_t new_capacity;
  void *new_data;

  if (!turbo_vec_valid(vec)) return CONTAINER_INVALID_ARGUMENT;
  if (min_capacity <= vec->capacity) return CONTAINER_OK;
  if (vec->elem_size != 0 && min_capacity > SIZE_MAX / vec->elem_size) return CONTAINER_OUT_OF_MEMORY;

  new_capacity = vec->capacity ? vec->capacity : TURBO_VEC_MIN_CAPACITY;
  while (new_capacity < min_capacity) {
    if (new_capacity > SIZE_MAX / 2U) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2U;
  }
  if (new_capacity > SIZE_MAX / vec->elem_size) return CONTAINER_OUT_OF_MEMORY;

  new_data = realloc(vec->data, new_capacity * vec->elem_size);
  if (!new_data) return CONTAINER_OUT_OF_MEMORY;
  vec->data = new_data;
  vec->capacity = new_capacity;
  return CONTAINER_OK;
}

int turbo_vec_init(turbo_vec_t *vec, size_t elem_size) {
  if (!vec || elem_size == 0) return CONTAINER_INVALID_ARGUMENT;
  memset(vec, 0, sizeof(*vec));
  vec->elem_size = elem_size;
  return CONTAINER_OK;
}

int turbo_vec_from_array(turbo_vec_t *vec, const void *elements, size_t count,
                         size_t elem_size) {
  int rc;

  if (!vec || elem_size == 0 || (count > 0 && !elements)) return CONTAINER_INVALID_ARGUMENT;
  rc = turbo_vec_init(vec, elem_size);
  if (rc != CONTAINER_OK) return rc;
  rc = turbo_vec_reserve(vec, count);
  if (rc != CONTAINER_OK) {
    turbo_vec_destroy(vec);
    return rc;
  }
  if (count > 0) memcpy(vec->data, elements, count * elem_size);
  vec->size = count;
  return CONTAINER_OK;
}

void turbo_vec_destroy(turbo_vec_t *vec) {
  if (!vec) return;
  free(vec->data);
  memset(vec, 0, sizeof(*vec));
}

void turbo_vec_clear(turbo_vec_t *vec) {
  if (!vec) return;
  vec->size = 0;
}

int turbo_vec_reserve(turbo_vec_t *vec, size_t min_capacity) {
  return turbo_vec_grow_to(vec, min_capacity);
}

int turbo_vec_resize(turbo_vec_t *vec, size_t new_size) {
  int rc;
  size_t old_size;

  if (!turbo_vec_valid(vec)) return CONTAINER_INVALID_ARGUMENT;
  rc = turbo_vec_grow_to(vec, new_size);
  if (rc != CONTAINER_OK) return rc;
  old_size = vec->size;
  if (new_size > old_size) {
    memset((unsigned char *)vec->data + old_size * vec->elem_size, 0,
           (new_size - old_size) * vec->elem_size);
  }
  vec->size = new_size;
  return CONTAINER_OK;
}

int turbo_vec_push(turbo_vec_t *vec, const void *elem) {
  int rc;

  if (!turbo_vec_valid(vec) || !elem) return CONTAINER_INVALID_ARGUMENT;
  if (vec->size == SIZE_MAX) return CONTAINER_OUT_OF_MEMORY;
  rc = turbo_vec_grow_to(vec, vec->size + 1U);
  if (rc != CONTAINER_OK) return rc;
  memcpy((unsigned char *)vec->data + vec->size * vec->elem_size, elem, vec->elem_size);
  vec->size += 1U;
  return CONTAINER_OK;
}

int turbo_vec_pop(turbo_vec_t *vec, void *out_elem) {
  if (!turbo_vec_valid(vec)) return CONTAINER_INVALID_ARGUMENT;
  if (vec->size == 0) return CONTAINER_EMPTY;
  vec->size -= 1U;
  if (out_elem) {
    memcpy(out_elem, (unsigned char *)vec->data + vec->size * vec->elem_size, vec->elem_size);
  }
  return CONTAINER_OK;
}

int turbo_vec_insert(turbo_vec_t *vec, size_t index, const void *elem) {
  int rc;
  unsigned char *base;

  if (!turbo_vec_valid(vec) || !elem || index > vec->size) return CONTAINER_INVALID_ARGUMENT;
  if (vec->size == SIZE_MAX) return CONTAINER_OUT_OF_MEMORY;
  rc = turbo_vec_grow_to(vec, vec->size + 1U);
  if (rc != CONTAINER_OK) return rc;
  base = (unsigned char *)vec->data;
  memmove(base + (index + 1U) * vec->elem_size, base + index * vec->elem_size,
          (vec->size - index) * vec->elem_size);
  memcpy(base + index * vec->elem_size, elem, vec->elem_size);
  vec->size += 1U;
  return CONTAINER_OK;
}

int turbo_vec_erase(turbo_vec_t *vec, size_t index, void *out_elem) {
  unsigned char *base;

  if (!turbo_vec_valid(vec) || index >= vec->size) return CONTAINER_INVALID_ARGUMENT;
  base = (unsigned char *)vec->data;
  if (out_elem) memcpy(out_elem, base + index * vec->elem_size, vec->elem_size);
  memmove(base + index * vec->elem_size, base + (index + 1U) * vec->elem_size,
          (vec->size - index - 1U) * vec->elem_size);
  vec->size -= 1U;
  return CONTAINER_OK;
}

int turbo_vec_swap_remove(turbo_vec_t *vec, size_t index, void *out_elem) {
  unsigned char *base;

  if (!turbo_vec_valid(vec) || index >= vec->size) return CONTAINER_INVALID_ARGUMENT;
  base = (unsigned char *)vec->data;
  if (out_elem) memcpy(out_elem, base + index * vec->elem_size, vec->elem_size);
  if (index != vec->size - 1U) {
    memcpy(base + index * vec->elem_size, base + (vec->size - 1U) * vec->elem_size,
           vec->elem_size);
  }
  vec->size -= 1U;
  return CONTAINER_OK;
}

void *turbo_vec_at(turbo_vec_t *vec, size_t index) {
  if (!turbo_vec_valid(vec) || index >= vec->size) return NULL;
  return (unsigned char *)vec->data + index * vec->elem_size;
}

const void *turbo_vec_at_const(const turbo_vec_t *vec, size_t index) {
  if (!turbo_vec_valid(vec) || index >= vec->size) return NULL;
  return (const unsigned char *)vec->data + index * vec->elem_size;
}

void *turbo_vec_data(turbo_vec_t *vec) {
  if (!vec) return NULL;
  return vec->data;
}

const void *turbo_vec_data_const(const turbo_vec_t *vec) {
  if (!vec) return NULL;
  return vec->data;
}

size_t turbo_vec_size(const turbo_vec_t *vec) {
  if (!vec) return 0;
  return vec->size;
}

size_t turbo_vec_capacity(const turbo_vec_t *vec) {
  if (!vec) return 0;
  return vec->capacity;
}

bool turbo_vec_empty(const turbo_vec_t *vec) {
  return vec == NULL || vec->size == 0;
}
