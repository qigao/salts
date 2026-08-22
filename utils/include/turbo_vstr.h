// re2c --lang c
/**
 * @file turbo_vstr.h
 * @brief Non-owning string view type for TurboUtils (C string_view)
 *
 * Memory model:
 * - vstr does NOT own memory
 * - Underlying buffer must outlive the view
 * - Use *_to_* helpers to copy into arena/pool/heap when needed
 */

#ifndef TURBO_VSTR_H
#define TURBO_VSTR_H

#include "platform.h"
#include "memory_pool.h"
#include "turbo_buffer.h"
#include <cmeta/struct.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Borrowed byte view. vstr_meta() exposes its ABI metadata per translation unit. */
CMETA_STRUCT(vstr,
    (const char *, data),
    (size_t, len)
);

/* ============================================================================
 * Construction
 * ========================================================================= */

static inline vstr vstr_from_buf(const char *s, size_t n) {
  vstr v;
  v.data = s;
  v.len = s ? n : 0;
  return v;
}

static inline vstr vstr_from_cstr(const char *s) {
  vstr v;
  v.data = s;
  v.len = s ? strlen(s) : 0;
  return v;
}

/* ============================================================================
 * Properties
 * ========================================================================= */

static inline size_t vstr_len(vstr v) { return v.len; }

static inline int vstr_empty(vstr v) { return v.len == 0; }

/** Return nonzero when the view is empty or data is non-NULL. */
static inline int vstr_is_valid(vstr v) { return v.data != NULL || v.len == 0; }

/* ============================================================================
 * Comparison
 * ========================================================================= */

TURBO_C_API int vstr_eq(vstr a, vstr b);
TURBO_C_API int vstr_ieq(vstr a, vstr b);

/* ============================================================================
 * Predicates
 * ========================================================================= */

TURBO_C_API int vstr_starts_with(vstr s, vstr prefix);
TURBO_C_API int vstr_ends_with(vstr s, vstr suffix);
TURBO_C_API int vstr_contains(vstr s, vstr needle);

/* ============================================================================
 * Search
 * ========================================================================= */

#define VSTR_NPOS ((size_t)-1)

TURBO_C_API size_t vstr_find(vstr s, vstr needle);
TURBO_C_API size_t vstr_rfind(vstr s, vstr needle);
TURBO_C_API size_t vstr_find_char(vstr s, char c);
TURBO_C_API size_t vstr_rfind_char(vstr s, char c);
/** Find the first byte that equals any byte in delimiters. */
TURBO_C_API size_t vstr_find_any(vstr s, vstr delimiters);
TURBO_C_API size_t vstr_count(vstr s, vstr needle);

/** Split once from the left. Returns 1 when delim is present. */
TURBO_C_API int vstr_partition(vstr s, vstr delim, vstr *before, vstr *match,
                               vstr *after);

/** Split once from the right. Returns 1 when delim is present. */
TURBO_C_API int vstr_rpartition(vstr s, vstr delim, vstr *before, vstr *match,
                                vstr *after);

/* ============================================================================
 * Slicing
 * ========================================================================= */

TURBO_C_API vstr vstr_sub(vstr s, size_t pos, size_t n);
TURBO_C_API vstr vstr_trim(vstr s, const char *cset);
TURBO_C_API vstr vstr_trim_left(vstr s, const char *cset);
TURBO_C_API vstr vstr_trim_right(vstr s, const char *cset);

/* ============================================================================
 * UTF-8 helpers
 * ========================================================================= */

/** Strict UTF-8 validation */
TURBO_C_API int vstr_utf8_valid(vstr s);

/** Invalid byte offset, or VSTR_NPOS when the view is valid UTF-8 */
TURBO_C_API size_t vstr_utf8_invalid_offset(vstr s);

/** Count Unicode code points; returns VSTR_NPOS when input is invalid UTF-8 */
TURBO_C_API size_t vstr_utf8_len(vstr s);

/** Count Unicode code points in at most n bytes; invalid/truncated input returns VSTR_NPOS */
TURBO_C_API size_t vstr_utf8_nlen(vstr s, size_t n);

/** Number of bytes, equivalent to utf8size_lazy() for a bounded view */
TURBO_C_API size_t vstr_utf8_size_lazy(vstr s);

/** Convert code-point index to byte offset; index at end returns s.len */
TURBO_C_API size_t vstr_utf8_byte_offset(vstr s, size_t char_index);

/** Return a view sliced by Unicode code-point indexes */
TURBO_C_API vstr vstr_utf8_sub(vstr s, size_t char_pos, size_t char_count);

/** Decode and consume one Unicode code point from rest */
TURBO_C_API int vstr_utf8_next(vstr *rest, uint32_t *codepoint);

/** Find first/last byte offset of a Unicode code point */
TURBO_C_API size_t vstr_utf8_find_cp(vstr s, uint32_t codepoint);
TURBO_C_API size_t vstr_utf8_rfind_cp(vstr s, uint32_t codepoint);

/** Find a UTF-8 needle only at code-point boundaries */
TURBO_C_API size_t vstr_utf8_find(vstr haystack, vstr needle);

/** Encoded byte size of one Unicode code point, or 0 when invalid */
TURBO_C_API size_t tstr_utf8_codepoint_size(uint32_t codepoint);

/* ============================================================================
 * Split (zero-allocation iterator)
 * ========================================================================= */

TURBO_C_API vstr vstr_split_next(vstr *rest, vstr delim);

/** Return the next field from the right and shorten rest from the right. */
TURBO_C_API vstr vstr_rsplit_next(vstr *rest, vstr delim);

/* ============================================================================
 * Conversion (copies)
 * ========================================================================= */

TURBO_C_API char *vstr_to_cstr(vstr v);
TURBO_C_API char *vstr_to_pool(vstr v, MemoryPool *pool);
TURBO_C_API char *vstr_to_arena(vstr v, mem_pool_t *arena);

/* ============================================================================
 * Arena/Network interop (zero-copy)
 * ========================================================================= */

struct mem_slice_s;

/** Create view from arena slice (zero-copy) */
static inline vstr vstr_from_slice(const struct mem_slice_s *slice) {
  vstr v;
  if (slice) {
    v.data = slice->data;
    v.len = slice->length;
  } else {
    v.data = NULL;
    v.len = 0;
  }
  return v;
}

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VSTR_H */
