#ifndef SALTS_CRYPTO_H
#define SALTS_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
  #if defined(SALTS_CRYPTO_BUILDING)
    #define SALTS_CRYPTO_API __declspec(dllexport)
  #else
    #define SALTS_CRYPTO_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define SALTS_CRYPTO_API __attribute__((visibility("default")))
#else
  #define SALTS_CRYPTO_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_CRYPTO_ED448_PRIVATE_KEY_SIZE 57U
#define SALTS_CRYPTO_ED448_PUBLIC_KEY_SIZE 57U
#define SALTS_CRYPTO_ED448_SIGNATURE_SIZE 114U
#define SALTS_CRYPTO_SHA256_DIGEST_SIZE 32U
#define SALTS_CRYPTO_SHA256_CONTEXT_SIZE 128U

typedef enum salts_crypto_status {
  SALTS_CRYPTO_OK = 0,
  SALTS_CRYPTO_EINVAL = -1,
  SALTS_CRYPTO_EVERIFY = -2,
  SALTS_CRYPTO_ERANDOM = -3,
  SALTS_CRYPTO_ECRYPTO = -4,
  SALTS_CRYPTO_ESTATE = -5
} salts_crypto_status;

/**
 * SHA-256 streaming state with implementation-private storage. It has no
 * heap ownership and is not thread-safe. Do not copy it after initialization.
 */
typedef union salts_crypto_sha256_ctx_u {
  void *pointer_alignment;
  uint64_t integer_alignment;
  long double floating_alignment;
  uint8_t bytes[SALTS_CRYPTO_SHA256_CONTEXT_SIZE];
} salts_crypto_sha256_ctx_t;

/** Computes a SHA-256 digest in one call. NULL data is valid only for size zero. */
SALTS_CRYPTO_API int salts_crypto_sha256(const void *data, size_t data_size,
                                         uint8_t digest[SALTS_CRYPTO_SHA256_DIGEST_SIZE]);

/** Initializes a caller-owned streaming SHA-256 state. */
SALTS_CRYPTO_API int salts_crypto_sha256_init(salts_crypto_sha256_ctx_t *context);

/** Adds input to an initialized SHA-256 state. */
SALTS_CRYPTO_API int salts_crypto_sha256_update(salts_crypto_sha256_ctx_t *context,
                                                const void *data, size_t data_size);

/** Finalizes a SHA-256 state. It cannot be updated or finalized again. */
SALTS_CRYPTO_API int salts_crypto_sha256_final(
    salts_crypto_sha256_ctx_t *context,
    uint8_t digest[SALTS_CRYPTO_SHA256_DIGEST_SIZE]);

/**
 * Derives an RFC 8032 Ed448 public key from a 57-byte private seed.
 *
 * @return SALTS_CRYPTO_OK, SALTS_CRYPTO_EINVAL for invalid arguments, or
 *         SALTS_CRYPTO_ECRYPTO when key derivation fails.
 */
SALTS_CRYPTO_API int
salts_crypto_ed448_public_key(const uint8_t private_key[SALTS_CRYPTO_ED448_PRIVATE_KEY_SIZE],
                              uint8_t public_key[SALTS_CRYPTO_ED448_PUBLIC_KEY_SIZE]);

/**
 * Creates a random RFC 8032 Ed448 key pair using the platform CSPRNG.
 * The two output ranges must not overlap.
 */
SALTS_CRYPTO_API int
salts_crypto_ed448_keygen(uint8_t private_key[SALTS_CRYPTO_ED448_PRIVATE_KEY_SIZE],
                          uint8_t public_key[SALTS_CRYPTO_ED448_PUBLIC_KEY_SIZE]);

/**
 * Signs data with RFC 8032 pure Ed448. A null data pointer is valid only when
 * data_size is zero.
 */
SALTS_CRYPTO_API int
salts_crypto_ed448_sign(const uint8_t private_key[SALTS_CRYPTO_ED448_PRIVATE_KEY_SIZE],
                        const void *data, size_t data_size,
                        uint8_t signature[SALTS_CRYPTO_ED448_SIGNATURE_SIZE]);

/**
 * Verifies an RFC 8032 pure Ed448 signature.
 *
 * @return SALTS_CRYPTO_OK for a valid signature, SALTS_CRYPTO_EVERIFY for an
 *         invalid key or signature, or another salts_crypto_status on failure.
 */
SALTS_CRYPTO_API int
salts_crypto_ed448_verify(const uint8_t public_key[SALTS_CRYPTO_ED448_PUBLIC_KEY_SIZE],
                          const void *data, size_t data_size,
                          const uint8_t signature[SALTS_CRYPTO_ED448_SIGNATURE_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CRYPTO_H */
