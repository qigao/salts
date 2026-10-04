#ifndef SALTS_CRYPTO_H
#define SALTS_CRYPTO_H

#include "salts_api.h"
#include "salts_error.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_MD5_DIGEST_BYTES 16u
#define SALTS_SHA1_DIGEST_BYTES 20u
#define SALTS_SHA256_DIGEST_BYTES 32u

/**
 * Compute MD5 over one contiguous byte range.
 *
 * MD5 is exposed only for legacy protocol compatibility. New designs must not
 * use MD5 for integrity, signatures, password hashing, or collision-sensitive
 * identifiers.
 *
 * @param data Input bytes; NULL is allowed only when size is zero.
 * @param size Input byte count.
 * @param out Caller-owned 16-byte digest buffer.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EIO.
 */
SALTS_C_API int salts_md5(const void *data, size_t size,
                          uint8_t out[SALTS_MD5_DIGEST_BYTES]);

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

/**
 * Compute HMAC-SHA1 over one contiguous byte range.
 *
 * HMAC-SHA1 is exposed only for legacy protocol compatibility.
 *
 * @param key Key bytes; NULL is allowed only when key_size is zero.
 * @param key_size Key byte count.
 * @param data Input bytes; NULL is allowed only when data_size is zero.
 * @param data_size Input byte count.
 * @param out Caller-owned 20-byte MAC buffer.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EIO.
 */
SALTS_C_API int salts_hmac_sha1(const void *key, size_t key_size,
                                const void *data, size_t data_size,
                                uint8_t out[SALTS_SHA1_DIGEST_BYTES]);

/**
 * Compute HMAC-SHA256 over one contiguous byte range.
 *
 * @param key Key bytes; NULL is allowed only when key_size is zero.
 * @param key_size Key byte count.
 * @param data Input bytes; NULL is allowed only when data_size is zero.
 * @param data_size Input byte count.
 * @param out Caller-owned 32-byte MAC buffer.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EIO.
 */
SALTS_C_API int salts_hmac_sha256(const void *key, size_t key_size,
                                  const void *data, size_t data_size,
                                  uint8_t out[SALTS_SHA256_DIGEST_BYTES]);

/**
 * Compare two fixed-length byte ranges in constant time with respect to their
 * contents.
 *
 * The comparison length is public. NULL inputs are accepted only when size is
 * zero. On success, out_equal receives 1 for equality and 0 for inequality.
 *
 * @param lhs First byte range.
 * @param rhs Second byte range.
 * @param size Number of bytes to compare.
 * @param out_equal Caller-owned equality result.
 * @return SALTS_OK or SALTS_EINVAL.
 */
SALTS_C_API int salts_crypto_equal(const void *lhs, const void *rhs, size_t size,
                                   int *out_equal);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CRYPTO_H */
