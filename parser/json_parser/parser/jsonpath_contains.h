#ifndef JSONPATH_CONTAINS_H
#define JSONPATH_CONTAINS_H

#include "re_scan.h"

#include <stddef.h>
#include <string.h>

/* Byte-exact substring search with explicit lengths. The SIMDe variant scans
 * for the first needle byte with a 16-byte vector compare, then verifies the
 * remaining bytes with memcmp; the scalar variant is the same algorithm and
 * is kept for equivalence tests and benchmarks. */
static inline int jsonpath_contains_scalar(const char *haystack, size_t haystack_len,
                                           const char *needle, size_t needle_len) {
  size_t i;
  if (needle_len == 0) return 1;
  if (needle_len > haystack_len) return 0;
  for (i = 0; i + needle_len <= haystack_len; ++i) {
    if (memcmp(haystack + i, needle, needle_len) == 0) return 1;
  }
  return 0;
}

static inline int jsonpath_contains_simde(const char *haystack, size_t haystack_len,
                                          const char *needle, size_t needle_len) {
  const char *cursor = haystack;
  const char *end = haystack + haystack_len;
  if (needle_len == 0) return 1;
  if (needle_len > haystack_len) return 0;
  if (needle_len == 1)
    return re_scan_first_byte_simde(haystack, end, (unsigned char)needle[0]) != NULL;
  while ((size_t)(end - cursor) >= needle_len) {
    const char *candidate = re_scan_first_byte_simde(cursor, end, (unsigned char)needle[0]);
    if (candidate == NULL) return 0;
    if ((size_t)(end - candidate) >= needle_len &&
        memcmp(candidate, needle, needle_len) == 0)
      return 1;
    cursor = candidate + 1;
  }
  return 0;
}

#endif
