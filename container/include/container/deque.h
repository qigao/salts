#ifndef CONTAINER_DEQUE_H
#define CONTAINER_DEQUE_H

#include "platform.h"
#include "turbo_error.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *data;
  size_t size;
  size_t capacity;
  size_t elem_size;
  size_t head;
} container_deque_t;

CXX_C_API int container_deque_init(container_deque_t *deque, size_t elem_size);
/** Initialize deque by copying count elements from a contiguous array in order. */
CXX_C_API int container_deque_from_array(container_deque_t *deque, const void *elements, size_t count,
                                     size_t elem_size);
CXX_C_API void container_deque_destroy(container_deque_t *deque);
CXX_C_API void container_deque_clear(container_deque_t *deque);
CXX_C_API int container_deque_reserve(container_deque_t *deque, size_t min_capacity);
CXX_C_API int container_deque_push_back(container_deque_t *deque, const void *elem);
CXX_C_API int container_deque_push_front(container_deque_t *deque, const void *elem);
CXX_C_API int container_deque_pop_back(container_deque_t *deque, void *out_elem);
CXX_C_API int container_deque_pop_front(container_deque_t *deque, void *out_elem);
CXX_C_API void *container_deque_front(container_deque_t *deque);
CXX_C_API const void *container_deque_front_const(const container_deque_t *deque);
CXX_C_API void *container_deque_back(container_deque_t *deque);
CXX_C_API const void *container_deque_back_const(const container_deque_t *deque);
CXX_C_API void *container_deque_at(container_deque_t *deque, size_t index);
CXX_C_API const void *container_deque_at_const(const container_deque_t *deque, size_t index);
CXX_C_API size_t container_deque_size(const container_deque_t *deque);
CXX_C_API size_t container_deque_capacity(const container_deque_t *deque);
CXX_C_API bool container_deque_empty(const container_deque_t *deque);


#ifdef __cplusplus
}
#endif

#endif /* CONTAINER_DEQUE_H */
