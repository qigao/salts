#ifndef TURBO_ENUM_UTILS_H
#define TURBO_ENUM_UTILS_H

#include <cmeta/enum.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**
 * @file enum_utils.h
 * @brief CMeta based enum/string helper utilities for C
 *
 * CMeta tuple schema:
 *
 * #define COLOR_ITEMS \
 *   (COLOR_RED, 0, "red"), \
 *   (COLOR_GREEN, 1, "green"), \
 *   (COLOR_BLUE, 2, "blue")
 *
 * TURBO_ENUM_DECLARE(color_t, color, COLOR_ITEMS, "UNKNOWN");
 *
 * Then use:
 *   color_t c = COLOR_RED;
 *   (void)color_from_string("green", &c);
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
 * @brief Generate a reflected enum type and compatibility conversion helpers.
 *
 * @param enum_type C enum type name, e.g. color_t
 * @param prefix    symbol prefix for generated helpers, e.g. color
 * @param item_list 1-16 comma-separated CMeta rows of form
 *                  (NAME, VALUE, STRING)
 * @param unknown_str fallback string for unmatched values
 *
 * Generates:
 *   - CMeta enum_type declaration, metadata, and typed reflection helpers
 *   - prefix##_entries[] lookup table
 *   - prefix##_count() number of entries
 *   - prefix##_to_string(enum_type) -> const char *
 *   - prefix##_from_string(const char *, enum_type *)
 *   - prefix##_is_valid(enum_type)
 */
#define TURBO_ENUM_DECLARE(enum_type, prefix, item_list, unknown_str) \
  Enum(enum_type, item_list); \
  static const turbo_enum_entry_t prefix##_entries[] = { \
      Schema(TURBO_ENUM_TABLE_ITEM, item_list) \
  }; \
  static inline size_t prefix##_count(void) { \
    return sizeof(prefix##_entries) / sizeof(prefix##_entries[0]); \
  } \
  static inline const char *prefix##_to_string(enum_type value) { \
    const char *text = enum_type##_to_string(value); \
    return text ? text : (unknown_str); \
  } \
  static inline int prefix##_from_string(const char *text, enum_type *out) { \
    enum_type parsed; \
    const char *parsed_text; \
    if (!text || !out || !enum_type##_from_string(text, &parsed)) return -1; \
    parsed_text = enum_type##_to_string(parsed); \
    if (!parsed_text || strcmp(parsed_text, text) != 0) return -1; \
    *out = parsed; \
    return 0; \
  } \
  static inline bool prefix##_is_valid(enum_type value) { \
    return enum_type##_to_string(value) != NULL; \
  } \
  static inline bool prefix##_equals(enum_type lhs, enum_type rhs) { \
    return lhs == rhs; \
  }

#ifdef __cplusplus
}
#endif

#endif // TURBO_ENUM_UTILS_H
