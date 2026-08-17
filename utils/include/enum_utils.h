#ifndef TURBO_ENUM_UTILS_H
#define TURBO_ENUM_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**
 * @file enum_utils.h
 * @brief X-Macro based enum/string helper utilities for C
 *
 * X-Macro pattern:
 *
 * #define COLOR_ITEMS(X) \
 *   X(COLOR_RED, 0, "red") \
 *   X(COLOR_GREEN, 1, "green") \
 *   X(COLOR_BLUE, 2, "blue")
 *
 * TURBO_ENUM_DECLARE(color_t, color, COLOR_ITEMS, "UNKNOWN");
 *
 * Then use:
 *   color_t c = color_from_string("green", &c) == 0 ? c : COLOR_RED;
 *   const char *s = color_to_string(c); // "green"
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct turbo_enum_entry_s {
  long long value;
  const char *name;
} turbo_enum_entry_t;

/**
 * @brief Expand an enum item as an enum constant.
 * USER_ITEMS macro shape: X(NAME, VALUE, STRING)
 */
#define TURBO_ENUM_DECL_ITEM(name, value, str) name = value,

/**
 * @brief Expand an enum item as a lookup table entry.
 */
#define TURBO_ENUM_TABLE_ITEM(name, value, str) {value, str},

/**
 * @brief C11 _Generic mapping item for dispatch helpers.
 *
 * usage:
 *   TURBO_ENUM_DISPATCH_TO_STRING_OF(color, color_t, color_to_string)
 *   TURBO_ENUM_DISPATCH_TO_STRING_OF(status, status_t, status_to_string)
 */
#define TURBO_ENUM_DISPATCH_TO_STRING_CASE(enum_type, fn) enum_type: fn

static inline const char *turbo_enum_unknown_to_string(void) {
  return "UNKNOWN";
}

/**
 * @brief Build _Generic dispatch to_string wrapper from a case list.
 *
 * NOTE:
 * - Requires C11 or later.
 * - Use one association per call in case_list. Some compilers (notably MSVC) may
 *   treat different enums as compatible and reject multiple enum associations.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define TURBO_ENUM_DISPATCH_TO_STRING(value, case_list) \
  _Generic((value), case_list)(value)

#define TURBO_ENUM_DISPATCH_TO_STRING_OF(value, enum_type, fn) \
  _Generic((value), enum_type: fn)(value)
#else
#define TURBO_ENUM_DISPATCH_TO_STRING(value, case_list) \
  turbo_enum_unknown_to_string()

#define TURBO_ENUM_DISPATCH_TO_STRING_OF(value, enum_type, fn) \
  turbo_enum_unknown_to_string()
#endif

/**
 * @brief Generate enum type and conversion helpers from an X-Macro list.
 *
 * @param enum_type C enum type name, e.g. color_t
 * @param prefix    symbol prefix for generated helpers, e.g. color
 * @param item_list user-defined macro of form X(NAME, VALUE, STRING)
 * @param unknown_str fallback string for unmatched values
 *
 * Generates:
 *   - enum_type enum declaration
 *   - prefix##_entries[] lookup table
 *   - prefix##_count() number of entries
 *   - prefix##_to_string(enum_type) -> const char *
 *   - prefix##_from_string(const char *, enum_type *)
 *   - prefix##_is_valid(enum_type)
 */
#define TURBO_ENUM_DECLARE(enum_type, prefix, item_list, unknown_str) \
  typedef enum { item_list(TURBO_ENUM_DECL_ITEM) } enum_type; \
  static const turbo_enum_entry_t prefix##_entries[] = { \
      item_list(TURBO_ENUM_TABLE_ITEM) \
  }; \
  static inline size_t prefix##_count(void) { \
    return sizeof(prefix##_entries) / sizeof(prefix##_entries[0]); \
  } \
  static inline const char *prefix##_to_string(enum_type value) { \
    size_t i; \
    for (i = 0; i < prefix##_count(); ++i) { \
      if (prefix##_entries[i].value == (long long)value) { \
        return prefix##_entries[i].name; \
      } \
    } \
    return (unknown_str); \
  } \
  static inline int prefix##_from_string(const char *text, enum_type *out) { \
    size_t i; \
    if (!text || !out) return -1; \
    for (i = 0; i < prefix##_count(); ++i) { \
      if (strcmp(prefix##_entries[i].name, text) == 0) { \
        *out = (enum_type)prefix##_entries[i].value; \
        return 0; \
      } \
    } \
    return -1; \
  } \
  static inline bool prefix##_is_valid(enum_type value) { \
    size_t i; \
    for (i = 0; i < prefix##_count(); ++i) { \
      if (prefix##_entries[i].value == (long long)value) { \
        return true; \
      } \
    } \
    return false; \
  } \
  static inline bool prefix##_equals(enum_type lhs, enum_type rhs) { \
    return lhs == rhs; \
  }

#ifdef __cplusplus
}
#endif

#endif // TURBO_ENUM_UTILS_H
