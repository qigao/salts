#ifndef TURBO_LIST_H
#define TURBO_LIST_H

#include "platform.h"
#include "turbo_deque.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  turbo_deque_t raw;
} turbo_list_t;

static inline int turbo_list_init(turbo_list_t *list, size_t elem_size) {
  return turbo_deque_init(&list->raw, elem_size);
}

static inline void turbo_list_destroy(turbo_list_t *list) {
  turbo_deque_destroy(&list->raw);
}

static inline void turbo_list_clear(turbo_list_t *list) {
  turbo_deque_clear(&list->raw);
}

static inline int turbo_list_reserve(turbo_list_t *list, size_t min_capacity) {
  return turbo_deque_reserve(&list->raw, min_capacity);
}

static inline int turbo_list_push_front(turbo_list_t *list, const void *elem) {
  return turbo_deque_push_front(&list->raw, elem);
}

static inline int turbo_list_push_back(turbo_list_t *list, const void *elem) {
  return turbo_deque_push_back(&list->raw, elem);
}

static inline int turbo_list_pop_front(turbo_list_t *list, void *out_elem) {
  return turbo_deque_pop_front(&list->raw, out_elem);
}

static inline int turbo_list_pop_back(turbo_list_t *list, void *out_elem) {
  return turbo_deque_pop_back(&list->raw, out_elem);
}

static inline void *turbo_list_front(turbo_list_t *list) {
  return turbo_deque_front(&list->raw);
}

static inline const void *turbo_list_front_const(const turbo_list_t *list) {
  return turbo_deque_front_const(&list->raw);
}

static inline void *turbo_list_back(turbo_list_t *list) {
  return turbo_deque_back(&list->raw);
}

static inline const void *turbo_list_back_const(const turbo_list_t *list) {
  return turbo_deque_back_const(&list->raw);
}

static inline void *turbo_list_at(turbo_list_t *list, size_t index) {
  return turbo_deque_at(&list->raw, index);
}

static inline const void *turbo_list_at_const(const turbo_list_t *list, size_t index) {
  return turbo_deque_at_const(&list->raw, index);
}

static inline size_t turbo_list_size(const turbo_list_t *list) {
  return turbo_deque_size(&list->raw);
}

static inline size_t turbo_list_capacity(const turbo_list_t *list) {
  return turbo_deque_capacity(&list->raw);
}

static inline bool turbo_list_empty(const turbo_list_t *list) {
  return turbo_deque_empty(&list->raw);
}

#define TURBO_LIST_DEFINE(name, type)                                                                \
  typedef struct {                                                                                   \
    turbo_list_t raw;                                                                                \
  } name;                                                                                            \
  static inline int name##_init(name *list) {                                                         \
    return turbo_list_init(&list->raw, sizeof(type));                                                 \
  }                                                                                                  \
  static inline void name##_destroy(name *list) { turbo_list_destroy(&list->raw); }                   \
  static inline void name##_clear(name *list) { turbo_list_clear(&list->raw); }                       \
  static inline int name##_reserve(name *list, size_t capacity) {                                     \
    return turbo_list_reserve(&list->raw, capacity);                                                  \
  }                                                                                                  \
  static inline int name##_push_front(name *list, type value) {                                        \
    return turbo_list_push_front(&list->raw, &value);                                                 \
  }                                                                                                  \
  static inline int name##_push_back(name *list, type value) {                                         \
    return turbo_list_push_back(&list->raw, &value);                                                  \
  }                                                                                                  \
  static inline bool name##_pop_front(name *list, type *out_value) {                                  \
    return turbo_list_pop_front(&list->raw, out_value) == TURBO_OK;                                   \
  }                                                                                                  \
  static inline bool name##_pop_back(name *list, type *out_value) {                                   \
    return turbo_list_pop_back(&list->raw, out_value) == TURBO_OK;                                    \
  }                                                                                                  \
  static inline type *name##_front(name *list) { return (type *)turbo_list_front(&list->raw); }         \
  static inline const type *name##_front_const(const name *list) {                                     \
    return (const type *)turbo_list_front_const(&list->raw);                                          \
  }                                                                                                  \
  static inline type *name##_back(name *list) { return (type *)turbo_list_back(&list->raw); }          \
  static inline const type *name##_back_const(const name *list) {                                      \
    return (const type *)turbo_list_back_const(&list->raw);                                           \
  }                                                                                                  \
  static inline type *name##_at(name *list, size_t index) { return (type *)turbo_list_at(&list->raw, index); } \
  static inline const type *name##_at_const(const name *list, size_t index) {                          \
    return (const type *)turbo_list_at_const(&list->raw, index);                                      \
  }                                                                                                  \
  static inline size_t name##_size(const name *list) { return turbo_list_size(&list->raw); }            \
  static inline size_t name##_capacity(const name *list) { return turbo_list_capacity(&list->raw); }    \
  static inline bool name##_empty(const name *list) { return turbo_list_empty(&list->raw); }

#ifdef __cplusplus
}
#endif

#endif /* TURBO_LIST_H */
