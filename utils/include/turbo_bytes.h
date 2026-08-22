#ifndef TURBO_BYTES_H
#define TURBO_BYTES_H

#include "platform.h"
#include "turbo_error.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Borrowed contiguous byte view.
 *
 * A view returned by turbo_bytes_view() remains valid only until the
 * buffer is appended to, consumed, reset, or destroyed. It must not cross a
 * thread, callback, or coroutine suspension boundary unless the caller can
 * prove that the owning buffer cannot be mutated during that interval.
 */
typedef struct turbo_bytes_view_s {
  const uint8_t *data;
  size_t size;
} turbo_bytes_view_t;

/**
 * @brief Bounded, single-owner accumulator for stream-oriented binary data.
 *
 * The object owns one contiguous allocation and is not thread-safe. Keep all
 * operations on the same owner thread or event loop. Its allocation never
 * exceeds max_bytes; reset retains the allocation for reuse.
 *
 * The fields are public so the type can be embedded in caller-owned objects,
 * but callers must treat them as read-only implementation state.
 *
 * @code
 * static int consume_input(const void *input, size_t input_size) {
 *   turbo_bytes_t buffer = turbo_bytes_INIT;
 *   turbo_bytes_view_t bytes;
 *   int rc = turbo_bytes_init(&buffer, 64 * 1024);
 *   if (rc == TURBO_OK) rc = turbo_bytes_append(&buffer, input, input_size);
 *   if (rc == TURBO_OK) rc = turbo_bytes_view(&buffer, &bytes);
 *   if (rc == TURBO_OK) rc = turbo_bytes_consume(&buffer, bytes.size);
 *   turbo_bytes_destroy(&buffer);
 *   return rc;
 * }
 * @endcode
 */
typedef struct turbo_bytes_s {
  uint8_t *data;
  size_t read_pos;
  size_t write_pos;
  size_t capacity;
  size_t max_bytes;
} turbo_bytes_t;

#define turbo_bytes_INIT {NULL, 0u, 0u, 0u, 0u}

/**
 * @brief Initialize an uninitialized buffer without allocating storage.
 * @param buffer Caller-owned buffer object.
 * @param max_bytes Hard limit for both unread bytes and allocation capacity.
 * @return TURBO_OK, or TURBO_EINVAL for a NULL buffer or zero limit.
 *
 * Destroy an initialized buffer before initializing it again.
 */
TURBO_C_API int turbo_bytes_init(turbo_bytes_t *buffer, size_t max_bytes);

/**
 * @brief Release storage and clear the object.
 * @param buffer Initialized buffer, or NULL.
 */
TURBO_C_API void turbo_bytes_destroy(turbo_bytes_t *buffer);

/**
 * @brief Append an entire byte range.
 * @param buffer Initialized buffer.
 * @param data Source bytes; NULL is allowed only when size is zero.
 * @param size Number of bytes to append.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ENOSPC, or TURBO_ENOMEM.
 *
 * Failure leaves all buffered bytes and cursors unchanged. A source range may
 * refer to the buffer's current unread view; other overlap with its allocation
 * is rejected. Time is amortized O(size), with occasional O(unread_size)
 * compaction or growth.
 */
TURBO_C_API int turbo_bytes_append(turbo_bytes_t *buffer, const void *data, size_t size);

/**
 * @brief Return a borrowed view of all unread bytes.
 * @param buffer Initialized buffer.
 * @param out Receives {NULL, 0} when the buffer is empty.
 * @return TURBO_OK or TURBO_EINVAL. On failure, out is unchanged.
 * @complexity Time O(1), space O(1).
 */
TURBO_C_API int turbo_bytes_view(const turbo_bytes_t *buffer,
                                     turbo_bytes_view_t *out);

/**
 * @brief Remove bytes from the front of the unread range.
 * @param buffer Initialized buffer.
 * @param size Number of bytes to consume.
 * @return TURBO_OK, TURBO_EINVAL, or TURBO_ERANGE.
 *
 * Over-consumption leaves the buffer unchanged. This operation never moves or
 * allocates memory; compaction is deferred until a later append needs space.
 * @complexity Time O(1), space O(1).
 */
TURBO_C_API int turbo_bytes_consume(turbo_bytes_t *buffer, size_t size);

/**
 * @brief Discard unread bytes while retaining allocation capacity.
 * @param buffer Initialized buffer. Invalid or NULL objects are ignored.
 * @complexity Time O(1), space O(1).
 */
TURBO_C_API void turbo_bytes_reset(turbo_bytes_t *buffer);

/** @brief Return unread byte count, or zero for an invalid object. */
TURBO_C_API size_t turbo_bytes_size(const turbo_bytes_t *buffer);

/** @brief Return remaining logical quota, or zero for an invalid object. */
TURBO_C_API size_t turbo_bytes_available(const turbo_bytes_t *buffer);

/** @brief Return allocated byte capacity, or zero for an invalid object. */
TURBO_C_API size_t turbo_bytes_capacity(const turbo_bytes_t *buffer);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_BYTES_H */
