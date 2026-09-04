#ifndef CSTL_TINYTEST_H
#define CSTL_TINYTEST_H

/* Optional TinyTest comparisons for explicitly declared typed CSTL instances. */

#include <cstl/typed.h>
#include <tinytest.h>

#include <stdbool.h>

static inline bool cstl_tinytest_range_has_value(cmeta_gen_status status) {
  return status == CMETA_GEN_VALUE || status == CMETA_GEN_VALUE_AND_DONE;
}

static inline bool cstl_tinytest_range_is_done(cmeta_gen_status status) {
  return status == CMETA_GEN_DONE;
}

static inline bool cstl_tinytest_ordered_range_comparable(const cmeta_range *range) {
  if (range == NULL || range->object == NULL || range->element_type == NULL ||
      range->size == NULL || range->next == NULL ||
      (range->flags & CMETA_RANGE_ORDERED) == 0u)
    return false;
  if (cmeta_type_require_traits(range->element_type, CMETA_TRAIT_EQUAL) != CMETA_OK)
    return false;
  return (range->flags & CMETA_RANGE_CONSTRUCTS_VALUES) == 0u ||
         cmeta_type_require_traits(range->element_type, CMETA_TRAIT_DESTROY) == CMETA_OK;
}

static inline bool cstl_tinytest_range_value_compatible(const cmeta_range *range,
                                                         const cmeta_type_desc *declared_type,
                                                         size_t value_size,
                                                         size_t value_align) {
  return range != NULL && range->element_type != NULL &&
         range->element_type->size == value_size &&
         range->element_type->align <= value_align &&
         (declared_type == NULL || cmeta_type_equal(range->element_type, declared_type));
}

#define CSTL_TINYTEST_EQUAL_FN_I(name) name##_cstl_tinytest_equal
#define CSTL_TINYTEST_EQUAL_FN(name) CSTL_TINYTEST_EQUAL_FN_I(name)

#define CSTL_TINYTEST_DESTROY_RANGE_VALUE(range, value)                                           \
  do {                                                                                              \
    if (((range).flags & CMETA_RANGE_CONSTRUCTS_VALUES) != 0u)                                    \
      (range).element_type->traits->destroy(&(value));                                             \
  } while (0)

/* Defines equality for one ordered, single-value typed CSTL instance. */
#define CSTL_TINYTEST_DEFINE_SEQUENCE_EQUAL(name, type)                                           \
  static inline bool CSTL_TINYTEST_EQUAL_FN(name)(const name *actual, const name *expected) {     \
    cmeta_range actual_range;                                                                       \
    cmeta_range expected_range;                                                                     \
    cmeta_range_cursor actual_cursor = {0};                                                        \
    cmeta_range_cursor expected_cursor = {0};                                                      \
    const cmeta_type_desc *element_type;                                                           \
    if (actual == NULL || expected == NULL) return false;                                          \
    actual_range = name##_range(actual);                                                           \
    expected_range = name##_range(expected);                                                       \
    if (!cstl_tinytest_ordered_range_comparable(&actual_range) ||                                 \
        !cstl_tinytest_ordered_range_comparable(&expected_range) ||                               \
        !cstl_tinytest_range_value_compatible(&actual_range, CMETA_TYPEOF(type), sizeof(type),   \
                                               _Alignof(type)) ||                                   \
        !cstl_tinytest_range_value_compatible(&expected_range, CMETA_TYPEOF(type), sizeof(type), \
                                               _Alignof(type)) ||                                   \
        !cmeta_type_equal(actual_range.element_type, expected_range.element_type) ||              \
        cmeta_range_size(&actual_range) != cmeta_range_size(&expected_range))                     \
      return false;                                                                                 \
    element_type = actual_range.element_type;                                                      \
    for (;;) {                                                                                       \
      type actual_value;                                                                            \
      type expected_value;                                                                          \
      cmeta_gen_status actual_status =                                                             \
          cmeta_range_next(&actual_range, &actual_cursor, &actual_value);                         \
      cmeta_gen_status expected_status =                                                           \
          cmeta_range_next(&expected_range, &expected_cursor, &expected_value);                   \
      const bool actual_has_value = cstl_tinytest_range_has_value(actual_status);                  \
      const bool expected_has_value = cstl_tinytest_range_has_value(expected_status);              \
      bool equal;                                                                                   \
      if (!actual_has_value || !expected_has_value) {                                              \
        equal = cstl_tinytest_range_is_done(actual_status) &&                                      \
                cstl_tinytest_range_is_done(expected_status);                                      \
        if (actual_has_value) CSTL_TINYTEST_DESTROY_RANGE_VALUE(actual_range, actual_value);       \
        if (expected_has_value)                                                                     \
          CSTL_TINYTEST_DESTROY_RANGE_VALUE(expected_range, expected_value);                       \
        return equal;                                                                               \
      }                                                                                             \
      equal = actual_status == expected_status &&                                                  \
              element_type->traits->equal(&actual_value, &expected_value);                        \
      CSTL_TINYTEST_DESTROY_RANGE_VALUE(actual_range, actual_value);                              \
      CSTL_TINYTEST_DESTROY_RANGE_VALUE(expected_range, expected_value);                          \
      if (!equal) return false;                                                                     \
      if (actual_status == CMETA_GEN_VALUE_AND_DONE) return true;                                 \
    }                                                                                                \
  }

/* Defines equality for one ordered key-to-value typed CSTL instance. */
#define CSTL_TINYTEST_DEFINE_ORDERED_MAP_EQUAL(name, key_type, value_type)                        \
  static inline bool CSTL_TINYTEST_EQUAL_FN(name)(const name *actual, const name *expected) {     \
    cmeta_range actual_keys;                                                                        \
    cmeta_range expected_keys;                                                                      \
    cmeta_range actual_values;                                                                      \
    cmeta_range expected_values;                                                                    \
    cmeta_range_cursor actual_key_cursor = {0};                                                    \
    cmeta_range_cursor expected_key_cursor = {0};                                                  \
    cmeta_range_cursor actual_value_cursor = {0};                                                  \
    cmeta_range_cursor expected_value_cursor = {0};                                                \
    const cmeta_type_desc *key_desc;                                                               \
    const cmeta_type_desc *value_desc;                                                             \
    if (actual == NULL || expected == NULL) return false;                                          \
    actual_keys = name##_keys_range(actual);                                                       \
    expected_keys = name##_keys_range(expected);                                                   \
    actual_values = name##_values_range(actual);                                                   \
    expected_values = name##_values_range(expected);                                               \
    if (!cstl_tinytest_ordered_range_comparable(&actual_keys) ||                                  \
        !cstl_tinytest_ordered_range_comparable(&expected_keys) ||                                \
        !cstl_tinytest_ordered_range_comparable(&actual_values) ||                                \
        !cstl_tinytest_ordered_range_comparable(&expected_values) ||                              \
        !cstl_tinytest_range_value_compatible(&actual_keys, CMETA_TYPEOF(key_type),               \
                                               sizeof(key_type), _Alignof(key_type)) ||             \
        !cstl_tinytest_range_value_compatible(&expected_keys, CMETA_TYPEOF(key_type),             \
                                               sizeof(key_type), _Alignof(key_type)) ||             \
        !cstl_tinytest_range_value_compatible(&actual_values, CMETA_TYPEOF(value_type),           \
                                               sizeof(value_type), _Alignof(value_type)) ||         \
        !cstl_tinytest_range_value_compatible(&expected_values, CMETA_TYPEOF(value_type),         \
                                               sizeof(value_type), _Alignof(value_type)) ||         \
        !cmeta_type_equal(actual_keys.element_type, expected_keys.element_type) ||                \
        !cmeta_type_equal(actual_values.element_type, expected_values.element_type) ||            \
        cmeta_range_size(&actual_keys) != cmeta_range_size(&actual_values) ||                     \
        cmeta_range_size(&expected_keys) != cmeta_range_size(&expected_values) ||                 \
        cmeta_range_size(&actual_keys) != cmeta_range_size(&expected_keys))                       \
      return false;                                                                                 \
    key_desc = actual_keys.element_type;                                                           \
    value_desc = actual_values.element_type;                                                       \
    for (;;) {                                                                                       \
      key_type actual_key;                                                                          \
      key_type expected_key;                                                                        \
      value_type actual_value;                                                                      \
      value_type expected_value;                                                                    \
      cmeta_gen_status actual_key_status =                                                         \
          cmeta_range_next(&actual_keys, &actual_key_cursor, &actual_key);                        \
      cmeta_gen_status expected_key_status =                                                       \
          cmeta_range_next(&expected_keys, &expected_key_cursor, &expected_key);                  \
      cmeta_gen_status actual_value_status =                                                       \
          cmeta_range_next(&actual_values, &actual_value_cursor, &actual_value);                  \
      cmeta_gen_status expected_value_status =                                                     \
          cmeta_range_next(&expected_values, &expected_value_cursor, &expected_value);            \
      const bool actual_key_has_value = cstl_tinytest_range_has_value(actual_key_status);          \
      const bool expected_key_has_value = cstl_tinytest_range_has_value(expected_key_status);      \
      const bool actual_value_has_value = cstl_tinytest_range_has_value(actual_value_status);      \
      const bool expected_value_has_value = cstl_tinytest_range_has_value(expected_value_status);  \
      bool equal;                                                                                   \
      if (!actual_key_has_value || !expected_key_has_value || !actual_value_has_value ||          \
          !expected_value_has_value) {                                                             \
        equal = cstl_tinytest_range_is_done(actual_key_status) &&                                  \
                cstl_tinytest_range_is_done(expected_key_status) &&                                \
                cstl_tinytest_range_is_done(actual_value_status) &&                                \
                cstl_tinytest_range_is_done(expected_value_status);                                \
        if (actual_key_has_value) CSTL_TINYTEST_DESTROY_RANGE_VALUE(actual_keys, actual_key);      \
        if (expected_key_has_value)                                                                 \
          CSTL_TINYTEST_DESTROY_RANGE_VALUE(expected_keys, expected_key);                          \
        if (actual_value_has_value)                                                                 \
          CSTL_TINYTEST_DESTROY_RANGE_VALUE(actual_values, actual_value);                          \
        if (expected_value_has_value)                                                               \
          CSTL_TINYTEST_DESTROY_RANGE_VALUE(expected_values, expected_value);                      \
        return equal;                                                                               \
      }                                                                                             \
      equal = actual_key_status == expected_key_status &&                                          \
              actual_value_status == expected_value_status &&                                     \
              key_desc->traits->equal(&actual_key, &expected_key) &&                              \
              value_desc->traits->equal(&actual_value, &expected_value);                          \
      CSTL_TINYTEST_DESTROY_RANGE_VALUE(actual_keys, actual_key);                                 \
      CSTL_TINYTEST_DESTROY_RANGE_VALUE(expected_keys, expected_key);                             \
      CSTL_TINYTEST_DESTROY_RANGE_VALUE(actual_values, actual_value);                             \
      CSTL_TINYTEST_DESTROY_RANGE_VALUE(expected_values, expected_value);                         \
      if (!equal) return false;                                                                     \
      if (actual_key_status == CMETA_GEN_VALUE_AND_DONE) return true;                             \
    }                                                                                                \
  }

/* `actual` and `expected` must be lvalues of the explicit typed container. */
#define check_cstl_equal(type, actual, expected)                                                   \
  do {                                                                                              \
    const type *const cstl_tinytest_actual__ = &(actual);                                          \
    const type *const cstl_tinytest_expected__ = &(expected);                                      \
    TTEST_CHECK__(CSTL_TINYTEST_EQUAL_FN(type)(cstl_tinytest_actual__,                             \
                                              cstl_tinytest_expected__),                            \
                  "expected %s containers to be equal", #type);                                  \
  } while (0)

#endif /* CSTL_TINYTEST_H */
