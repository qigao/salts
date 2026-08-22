#ifndef TURBO_UUID_H
#define TURBO_UUID_H

/**
 * @file turbo_uuid.h
 * @brief Installed UUID value type and generation/conversion API.
 *
 * @code
 * turbo_uuid_t id;
 * char text[TURBO_UUID_STRING_SIZE];
 * if (turbo_uuid_v7_generate(&id) == TURBO_OK &&
 *     turbo_uuid_format(&id, text, sizeof(text)) == TURBO_OK) {
 *   use_uuid(text);
 * }
 * @endcode
 */

#include "turbo_error.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TURBO_UUID_SIZE 16U
#define TURBO_UUID_STRING_LENGTH 36U
#define TURBO_UUID_STRING_SIZE (TURBO_UUID_STRING_LENGTH + 1U)

/** Fixed-size UUID value with no heap ownership. */
typedef struct turbo_uuid_s {
  uint8_t bytes[TURBO_UUID_SIZE];
} turbo_uuid_t;

/**
 * Generate an RFC 4122/RFC 9562 UUID version 4 using the system CSPRNG.
 * The output remains unchanged if entropy acquisition fails.
 *
 * @param out Destination UUID
 * @return 0 on success, negative error code on failure
 */
TURBO_C_API int turbo_uuid_v4_generate(turbo_uuid_t *out);

/**
 * Generate an RFC 9562 UUID version 7 using the current Unix millisecond
 * timestamp and the system CSPRNG. The output remains unchanged on failure.
 * Values generated within the same millisecond have random rather than
 * monotonic ordering.
 *
 * @param out Destination UUID
 * @return 0 on success, negative error code on failure
 */
TURBO_C_API int turbo_uuid_v7_generate(turbo_uuid_t *out);

/**
 * Parse a canonical lowercase or uppercase 8-4-4-4-12 UUID string.
 * The output remains unchanged on failure.
 *
 * @param text NUL-terminated canonical UUID string
 * @param out Destination UUID
 * @return 0 on success, TURBO_EINVAL on invalid input
 */
TURBO_C_API int turbo_uuid_parse(const char *text, turbo_uuid_t *out);

/**
 * Format a UUID as a lowercase canonical 8-4-4-4-12 string.
 *
 * @param uuid UUID value
 * @param out Destination string buffer
 * @param out_size Destination capacity, at least TURBO_UUID_STRING_SIZE
 * @return 0 on success, TURBO_EINVAL or TURBO_ENOSPC on failure
 */
TURBO_C_API int turbo_uuid_format(const turbo_uuid_t *uuid, char *out, size_t out_size);

/** Return true when both UUID values contain the same 16 bytes. */
TURBO_C_API bool turbo_uuid_equal(const turbo_uuid_t *left, const turbo_uuid_t *right);

#ifdef __cplusplus
}

namespace TurboUtils {
  using UUID = ::turbo_uuid_t;
}
#endif

#endif /* TURBO_UUID_H */
