#ifndef SALTS_CRYPTO_H
#define SALTS_CRYPTO_H

#include "salts_api.h"
#include "salts_error.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_SHA1_DIGEST_BYTES 20u
#define SALTS_SHA256_DIGEST_BYTES 32u

/**
 * Compute SHA-1 over one contiguous byte range.
 *
 * SHA-1 is exposed only for legacy protocol compatibility. New designs should
 * use SHA-256 or stronger algorithms.
 *
 * @param data Input bytes; NULL is allowed only when size is zero.
 * @param size Input byte count.
 * @param out Caller-owned 20-byte digest buffer.
 * @return SALTS_OK or SALTS_EINVAL.
 */
SALTS_C_API int salts_sha1(const void *data, size_t size,
                           uint8_t out[SALTS_SHA1_DIGEST_BYTES]);

/**
 * Compute SHA-256 over one contiguous byte range.
 *
 * @param data Input bytes; NULL is allowed only when size is zero.
 * @param size Input byte count.
 * @param out Caller-owned 32-byte digest buffer.
 * @return SALTS_OK or SALTS_EINVAL.
 */
SALTS_C_API int salts_sha256(const void *data, size_t size,
                             uint8_t out[SALTS_SHA256_DIGEST_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CRYPTO_H */
