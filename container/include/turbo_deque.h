#ifndef TURBO_DEQUE_H
#define TURBO_DEQUE_H

#include "platform.h"
#include "turbo_error.h"

#include <stdbool.h>
#include <stddef.h>
#include "turbo_container_meta.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *data;
  size_t size;
  size_t capacity;
  size_t elem_size;
  size_t head;
} turbo_deque_t;

CXX_C_API int turbo_deque_init(turbo_deque_t *deque, size_t elem_size);
/** Initialize deque by copying count elements from a contiguous array in order. */
CXX_C_API int turbo_deque_from_array(turbo_deque_t *deque, const void *elements, size_t count,
                                     size_t elem_size);
CXX_C_API void turbo_deque_destroy(turbo_deque_t *deque);
CXX_C_API void turbo_deque_clear(turbo_deque_t *deque);
CXX_C_API int turbo_deque_reserve(turbo_deque_t *deque, size_t min_capacity);
CXX_C_API int turbo_deque_push_back(turbo_deque_t *deque, const void *elem);
CXX_C_API int turbo_deque_push_front(turbo_deque_t *deque, const void *elem);
CXX_C_API int turbo_deque_pop_back(turbo_deque_t *deque, void *out_elem);
CXX_C_API int turbo_deque_pop_front(turbo_deque_t *deque, void *out_elem);
CXX_C_API void *turbo_deque_front(turbo_deque_t *deque);
CXX_C_API const void *turbo_deque_front_const(const turbo_deque_t *deque);
CXX_C_API void *turbo_deque_back(turbo_deque_t *deque);
CXX_C_API const void *turbo_deque_back_const(const turbo_deque_t *deque);
CXX_C_API void *turbo_deque_at(turbo_deque_t *deque, size_t index);
CXX_C_API const void *turbo_deque_at_const(const turbo_deque_t *deque, size_t index);
CXX_C_API size_t turbo_deque_size(const turbo_deque_t *deque);
CXX_C_API size_t turbo_deque_capacity(const turbo_deque_t *deque);
CXX_C_API bool turbo_deque_empty(const turbo_deque_t *deque);

#define TURBO_DEQUE_DEFINE(name, type) \
  CMETA_CONTAINER1_DEFINE(name, type, turbo_deque_t, turbo_deque, TURBO_OK, _, TURBO_META_DEQUE_METHODS) \
  CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, turbo_deque, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

#ifdef __cplusplus
}
#endif

#endif /* TURBO_DEQUE_H */
