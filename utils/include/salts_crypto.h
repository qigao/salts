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
#define SALTS_AES128_KEY_BYTES 16u
#define SALTS_AES_BLOCK_BYTES 16u
#define SALTS_DES_KEY_BYTES 8u
#define SALTS_DES_BLOCK_BYTES 8u

/** Opaque provider-neutral incremental SHA-256 context. */
typedef struct salts_sha256_stream salts_sha256_stream;

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
 * Create an incremental SHA-256 context.
 *
 * The returned context owns provider state privately; no provider type crosses
 * the public ABI. Destroy the context with salts_sha256_stream_destroy().
 */
SALTS_C_API int salts_sha256_stream_create(salts_sha256_stream **out_stream);

/** Reset an incremental SHA-256 context to the empty-message state. */
SALTS_C_API int salts_sha256_stream_reset(salts_sha256_stream *stream);

/**
 * Add one byte range to an incremental SHA-256 context.
 * NULL data is accepted only when size is zero.
 */
SALTS_C_API int salts_sha256_stream_update(salts_sha256_stream *stream,
                                           const void *data, size_t size);

/**
 * Finish the current digest. Further update/finish calls fail until reset.
 */
SALTS_C_API int salts_sha256_stream_finish(
    salts_sha256_stream *stream,
    uint8_t out[SALTS_SHA256_DIGEST_BYTES]);

/** Wipe provider state and release an incremental SHA-256 context. */
SALTS_C_API void salts_sha256_stream_destroy(salts_sha256_stream *stream);

/**
 * Compute HMAC-MD5 over one contiguous byte range.
 *
 * HMAC-MD5 is exposed only for legacy protocol compatibility.
 *
 * @param key Key bytes; NULL is allowed only when key_size is zero.
 * @param key_size Key byte count.
 * @param data Input bytes; NULL is allowed only when data_size is zero.
 * @param data_size Input byte count.
 * @param out Caller-owned 16-byte MAC buffer.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EIO.
 */
SALTS_C_API int salts_hmac_md5(const void *key, size_t key_size,
                               const void *data, size_t data_size,
                               uint8_t out[SALTS_MD5_DIGEST_BYTES]);

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
 * Encrypt whole DES-CBC blocks.
 *
 * DES is exposed only for legacy protocol compatibility. Padding remains the
 * responsibility of the protocol layer.
 *
 * @param key Caller-owned 8-byte DES key.
 * @param iv Caller-owned 8-byte initial CBC block.
 * @param input Input bytes; NULL is allowed only when size is zero.
 * @param size Input/output byte count; must be a multiple of 8.
 * @param output Output bytes; NULL is allowed only when size is zero. Input and
 * output may alias exactly for in-place operation.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EIO.
 */
SALTS_C_API int salts_des_cbc_encrypt(
    const uint8_t key[SALTS_DES_KEY_BYTES],
    const uint8_t iv[SALTS_DES_BLOCK_BYTES],
    const void *input, size_t size, void *output);

/**
 * Decrypt whole DES-CBC blocks.
 *
 * DES is exposed only for legacy protocol compatibility. Padding remains the
 * responsibility of the protocol layer.
 *
 * @param key Caller-owned 8-byte DES key.
 * @param iv Caller-owned 8-byte initial CBC block.
 * @param input Input bytes; NULL is allowed only when size is zero.
 * @param size Input/output byte count; must be a multiple of 8.
 * @param output Output bytes; NULL is allowed only when size is zero. Input and
 * output may alias exactly for in-place operation.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EIO.
 */
SALTS_C_API int salts_des_cbc_decrypt(
    const uint8_t key[SALTS_DES_KEY_BYTES],
    const uint8_t iv[SALTS_DES_BLOCK_BYTES],
    const void *input, size_t size, void *output);

/**
 * Encrypt one byte range with AES-128-CFB128.
 *
 * @param key Caller-owned 16-byte AES key.
 * @param iv Caller-owned 16-byte initial feedback block.
 * @param input Input bytes; NULL is allowed only when size is zero.
 * @param size Input/output byte count.
 * @param output Output bytes; NULL is allowed only when size is zero. Input and
 * output may alias exactly for in-place operation.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EIO.
 */
SALTS_C_API int salts_aes128_cfb_encrypt(
    const uint8_t key[SALTS_AES128_KEY_BYTES],
    const uint8_t iv[SALTS_AES_BLOCK_BYTES],
    const void *input, size_t size, void *output);

/**
 * Decrypt one byte range with AES-128-CFB128.
 *
 * @param key Caller-owned 16-byte AES key.
 * @param iv Caller-owned 16-byte initial feedback block.
 * @param input Input bytes; NULL is allowed only when size is zero.
 * @param size Input/output byte count.
 * @param output Output bytes; NULL is allowed only when size is zero. Input and
 * output may alias exactly for in-place operation.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EIO.
 */
SALTS_C_API int salts_aes128_cfb_decrypt(
    const uint8_t key[SALTS_AES128_KEY_BYTES],
    const uint8_t iv[SALTS_AES_BLOCK_BYTES],
    const void *input, size_t size, void *output);

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
