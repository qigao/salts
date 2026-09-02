/**
 * @file turbo_str.h
 * @brief High-performance dynamic string type for Rocida
 *
 * API uses snake_case naming with tstr_ prefix:
 * - tstr_len, tstr_cpy, tstr_cat, etc.
 *
 * Features:
 * - O(1) length queries
 * - Efficient append/concat with preallocation
 * - Binary-safe (can contain \0)
 * - Compatible with C string functions for reading
 * - Seamless integration with vstr (string view)
 * - Typed "{}" formatting is provided by fmt.h via tstr_format() and
 *   tstr_append_format(). turbo_str.h keeps only the printf-compatible
 *   tstr_cat_fmt() entry to avoid a reverse dependency on the formatter.
 *
 * Ownership model:
 * - tstr is a unique-owned mutable string. Do not share one mutable tstr
 *   instance across owners; use tstr_clone() for a deep copy or tstr_move() for
 *   explicit ownership transfer.
 * - Functions that may grow a tstr return the updated pointer; callers must
 *   assign it back, e.g. s = tstr_cat(s, "text")
 * - Use tstr_free() or tstr_freep() to release ownership.
 * - tstr_to_cstr() returns a malloc'd copy; use free().
 * 
 * THREAD SAFETY: Single-owner model. One tstr instance must NOT be shared
 *                across threads without external synchronization. Use tstr_clone()
 *                to create independent copies for other threads.
 * 
 * CONCURRENCY MODEL:
 * - Multiple threads may safely READ a const tstr if no thread is modifying it
 * - Functions returning tstr may reallocate: old pointer becomes invalid
 * - For shared strings: protect with mutex or use immutable vstr views
 * 
 * CRITICAL: Functions returning tstr may reallocate the string buffer.
 *           ALWAYS reassign the return value:
 * 
 *           CORRECT:
 *             s = tstr_cat(s, "text");  // ✅
 *           
 *           INCORRECT:
 *             tstr_cat(s, "text");      // ❌ Memory leak or dangling pointer!
 *           
 *           The old pointer becomes invalid after reallocation. Continuing to
 *           use it is undefined behavior (use-after-free).
 */

#ifndef TURBO_STR_H
#define TURBO_STR_H

#include "platform.h"
#include "turbo_vstr.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Dynamic string type - can be used directly with printf("%s", s)
 */
typedef char *tstr;

/* ============================================================================
 * tstr <-> vstr conversion
 * ========================================================================= */

/** Create tstr from view (copies data); returns NULL for an invalid view or overflow. */
TURBO_C_API tstr tstr_from_v(vstr v);

/** Create view from tstr (no copy, O(1)) */
TURBO_C_API vstr tstr_to_v(tstr s);

/* ============================================================================
 * Creation / Destruction
 * ========================================================================= */

/** Create empty string */
TURBO_C_API tstr tstr_new(void);

/** Create from C string (like strdup) */
TURBO_C_API tstr tstr_dup(const char *s);

/** Deep-copy an owned string; NULL stays NULL to preserve optional ownership */
TURBO_C_API tstr tstr_clone(tstr s);

/** Create from buffer with length (binary-safe) */
TURBO_C_API tstr tstr_dup_len(const char *s, size_t n);

/** Create from buffer with length (binary-safe, NULL init allowed) */
TURBO_C_API tstr tstr_new_len(const void *init, size_t n);

/** Free string */
TURBO_C_API void tstr_free(tstr s);

/** Free string and set the caller's handle to NULL */
TURBO_C_API void tstr_freep(tstr *s);

/** Move ownership out of *s and set *s to NULL */
TURBO_C_API tstr tstr_move(tstr *s);

/* ============================================================================
 * Properties
 * ========================================================================= */

/** Get length in O(1) */
TURBO_C_API size_t tstr_len(tstr s);

/** Get available space before realloc */
TURBO_C_API size_t tstr_avail(tstr s);

/** Check if empty */
TURBO_C_API int tstr_empty(tstr s);

/**
 * Set length manually after writing into reserved capacity.
 *
 * This is a no-op if n is greater than the current allocation. Prefer
 * tstr_set_len_checked() when the caller needs to detect invalid lengths.
 */
TURBO_C_API void tstr_set_len(tstr s, size_t n);

/** Checked length setter: returns 1 on success, 0 on invalid input/capacity */
TURBO_C_API int tstr_set_len_checked(tstr s, size_t n);

/* ============================================================================
 * Concatenation
 * ========================================================================= */

/** Append C string - MUST reassign: s = tstr_cat(s, "text") */
TURBO_C_API tstr tstr_cat(tstr s, const char *t);

/** Append with length (binary-safe) */
TURBO_C_API tstr tstr_cat_len(tstr s, const char *t, size_t n);

/** Append another tstr */
TURBO_C_API tstr tstr_cat_str(tstr s, tstr t);

/** Append view (no strlen needed); an invalid view leaves s unchanged. */
TURBO_C_API tstr tstr_cat_v(tstr s, vstr v);

/** Append formatted (printf-style) */
TURBO_C_API tstr tstr_cat_fmt(tstr s, const char *fmt, ...);

/** Append formatted (va_list version) */
TURBO_C_API tstr tstr_cat_vfmt(tstr s, const char *fmt, va_list ap);

/* ============================================================================
 * Copy
 * ========================================================================= */

/** Copy C string into existing tstr (replaces content) */
TURBO_C_API tstr tstr_cpy(tstr s, const char *t);

/** Copy with length (binary-safe) */
TURBO_C_API tstr tstr_cpy_len(tstr s, const char *t, size_t n);

/** Copy view into existing tstr; an invalid view leaves s unchanged. */
TURBO_C_API tstr tstr_cpy_v(tstr s, vstr v);

/** Clear content (keeps memory) */
TURBO_C_API void tstr_clear(tstr s);

/* ============================================================================
 * Comparison
 * ========================================================================= */

/** Compare two tstr (like strcmp) */
TURBO_C_API int tstr_cmp(tstr s1, tstr s2);

/** Compare tstr with view; an invalid view compares unequal. */
TURBO_C_API int tstr_cmp_v(tstr s, vstr v);

/** Case-insensitive compare (like strcasecmp) */
TURBO_C_API int tstr_casecmp(const char *s1, const char *s2);

/** Case-insensitive compare with length (like strncasecmp) */
TURBO_C_API int tstr_ncasecmp(const char *s1, const char *s2, size_t n);

/** Check equality with view */
TURBO_C_API int tstr_eq_v(tstr s, vstr v);

/** Case-insensitive equality with view */
TURBO_C_API int tstr_ieq_v(tstr s, vstr v);

/** Check if string starts with prefix */
TURBO_C_API int tstr_starts_with(const char *s, const char *prefix);

/** Check if string starts with view prefix */
TURBO_C_API int tstr_starts_with_v(tstr s, vstr prefix);

/** Check if string starts with prefix (case-insensitive) */
TURBO_C_API int tstr_istarts_with(const char *s, const char *prefix);

/** Check if string ends with suffix */
TURBO_C_API int tstr_ends_with(const char *s, const char *suffix);

/** Check if string ends with view suffix */
TURBO_C_API int tstr_ends_with_v(tstr s, vstr suffix);

/** Check if string contains substring */
TURBO_C_API int tstr_contains(const char *s, const char *substr);

/** Check if string contains view */
TURBO_C_API int tstr_contains_v(tstr s, vstr needle);

/** Count non-overlapping occurrences of a view */
TURBO_C_API size_t tstr_count_v(tstr s, vstr needle);

/* ============================================================================
 * Search (returns position, VSTR_NPOS if not found)
 * ========================================================================= */

/** Find view in tstr */
TURBO_C_API size_t tstr_find_v(tstr s, vstr needle);

/** Find char in tstr */
TURBO_C_API size_t tstr_find_char(tstr s, char c);

/** Reverse find view in tstr */
TURBO_C_API size_t tstr_rfind_v(tstr s, vstr needle);

/** Reverse find char in tstr */
TURBO_C_API size_t tstr_rfind_char(tstr s, char c);

/* ============================================================================
 * Transformation
 * ========================================================================= */

/** Trim characters from both ends */
TURBO_C_API tstr tstr_trim(tstr s, const char *cset);

/** Trim characters from the left side */
TURBO_C_API tstr tstr_ltrim(tstr s, const char *cset);

/** Trim characters from the right side */
TURBO_C_API tstr tstr_rtrim(tstr s, const char *cset);

/** Return an owned slice [pos, pos + n) */
TURBO_C_API tstr tstr_slice(tstr s, size_t pos, size_t n);

/** Strict UTF-8 validation */
TURBO_C_API int tstr_utf8_valid(tstr s);

/** Invalid byte offset, or VSTR_NPOS when the string is valid UTF-8 */
TURBO_C_API size_t tstr_utf8_invalid_offset(tstr s);

/** Count Unicode code points; returns VSTR_NPOS when input is invalid UTF-8 */
TURBO_C_API size_t tstr_utf8_len(tstr s);

/** Count Unicode code points in at most n bytes; invalid/truncated input returns VSTR_NPOS */
TURBO_C_API size_t tstr_utf8_nlen(tstr s, size_t n);

/** Byte size including/excluding the trailing NUL, matching utf8size/utf8size_lazy naming */
TURBO_C_API size_t tstr_utf8_size(tstr s);
TURBO_C_API size_t tstr_utf8_size_lazy(tstr s);

/** Return an owned slice by Unicode code-point indexes */
TURBO_C_API tstr tstr_utf8_slice(tstr s, size_t char_pos, size_t char_count);

/** Append one Unicode code point encoded as UTF-8; invalid code points are ignored */
TURBO_C_API tstr tstr_utf8_append_cp(tstr s, uint32_t codepoint);

/** Create a UTF-8 string from one Unicode code point; returns NULL for invalid code points */
TURBO_C_API tstr tstr_utf8_from_cp(uint32_t codepoint);

/** Find first/last byte offset of a Unicode code point */
TURBO_C_API size_t tstr_utf8_find_cp(tstr s, uint32_t codepoint);
TURBO_C_API size_t tstr_utf8_rfind_cp(tstr s, uint32_t codepoint);

/** Find a UTF-8 needle only at code-point boundaries */
TURBO_C_API size_t tstr_utf8_find(tstr haystack, vstr needle);

/** Repeat a C string count times */
TURBO_C_API tstr tstr_repeat(const char *s, size_t count);

/** Repeat a string view count times */
TURBO_C_API tstr tstr_repeat_v(vstr v, size_t count);

/** Replace at most max_count non-overlapping occurrences in-place */
TURBO_C_API tstr tstr_replace(tstr s, const char *needle, const char *replacement,
                              size_t max_count);

/** Replace at most max_count non-overlapping view occurrences in-place */
TURBO_C_API tstr tstr_replace_v(tstr s, vstr needle, vstr replacement,
                                size_t max_count);

/** Replace all non-overlapping occurrences in-place */
TURBO_C_API tstr tstr_replace_all(tstr s, const char *needle, const char *replacement);

/** Convert to lowercase in place */
TURBO_C_API void tstr_lower(tstr s);

/** Convert to uppercase in place */
TURBO_C_API void tstr_upper(tstr s);

/** Pad to at least width bytes on the left; fill is treated as one raw byte. */
TURBO_C_API tstr tstr_pad_left(tstr s, size_t width, char fill);

/** Pad to at least width bytes on the right; fill is treated as one raw byte. */
TURBO_C_API tstr tstr_pad_right(tstr s, size_t width, char fill);

/* ============================================================================
 * Memory Management
 * ========================================================================= */

/** Reserve space for additional bytes */
TURBO_C_API tstr tstr_reserve(tstr s, size_t addlen);

/** Shrink to fit current content */
TURBO_C_API tstr tstr_shrink(tstr s);

/* ============================================================================
 * Conversion
 * ========================================================================= */

/** Get malloc'd copy - caller must free() */
TURBO_C_API char *tstr_to_cstr(tstr s);

/** Create from long long */
TURBO_C_API tstr tstr_from_ll(long long value);

/* ============================================================================
 * Split / Join
 * ========================================================================= */

/** Split by separator - returns array, set count */
TURBO_C_API tstr *tstr_split(tstr s, const char *sep, int *count);

/** Free split result */
TURBO_C_API void tstr_free_split(tstr *tokens, int count);

/** Join C strings with separator */
TURBO_C_API tstr tstr_join(char **argv, int argc, const char *sep);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_STR_H */
