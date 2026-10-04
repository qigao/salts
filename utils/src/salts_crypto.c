#include "salts_crypto.h"

#include <gmssl/aes.h>
#include <gmssl/des.h>
#include <gmssl/digest.h>
#include <gmssl/hmac.h>
#include <gmssl/md5.h>
#include <gmssl/mem.h>
#include <gmssl/sha1.h>
#include <gmssl/sha2.h>

#include <string.h>

int salts_md5(const void *data, size_t size,
              uint8_t out[SALTS_MD5_DIGEST_BYTES]) {
  MD5_CTX context;

  if (out == NULL || (data == NULL && size != 0u)) return SALTS_EINVAL;

  md5_init(&context);
  if (size != 0u) md5_update(&context, (const uint8_t *)data, size);
  md5_finish(&context, out);
  gmssl_secure_clear(&context, sizeof(context));
  return SALTS_OK;
}

int salts_sha1(const void *data, size_t size,
               uint8_t out[SALTS_SHA1_DIGEST_BYTES]) {
  SHA1_CTX context;

  if (out == NULL || (data == NULL && size != 0u)) return SALTS_EINVAL;
  sha1_init(&context);
  if (size != 0u) sha1_update(&context, (const uint8_t *)data, size);
  sha1_finish(&context, out);
  memset(&context, 0, sizeof(context));
  return SALTS_OK;
}

int salts_sha256(const void *data, size_t size,
                 uint8_t out[SALTS_SHA256_DIGEST_BYTES]) {
  SHA256_CTX context;

  if (out == NULL || (data == NULL && size != 0u)) return SALTS_EINVAL;
  sha256_init(&context);
  if (size != 0u) sha256_update(&context, (const uint8_t *)data, size);
  sha256_finish(&context, out);
  memset(&context, 0, sizeof(context));
  return SALTS_OK;
}

int salts_hmac_md5(const void *key, size_t key_size,
                   const void *data, size_t data_size,
                   uint8_t out[SALTS_MD5_DIGEST_BYTES]) {
  MD5_CTX context;
  uint8_t key_block[MD5_BLOCK_SIZE] = {0};
  uint8_t inner_pad[MD5_BLOCK_SIZE];
  uint8_t outer_pad[MD5_BLOCK_SIZE];
  uint8_t inner_digest[SALTS_MD5_DIGEST_BYTES];
  size_t i;

  if (out == NULL || (key == NULL && key_size != 0u) ||
      (data == NULL && data_size != 0u))
    return SALTS_EINVAL;

  if (key_size > sizeof(key_block)) {
    md5_init(&context);
    md5_update(&context, (const uint8_t *)key, key_size);
    md5_finish(&context, key_block);
    gmssl_secure_clear(&context, sizeof(context));
  } else if (key_size != 0u) {
    memcpy(key_block, key, key_size);
  }

  for (i = 0u; i < sizeof(key_block); ++i) {
    inner_pad[i] = (uint8_t)(key_block[i] ^ 0x36u);
    outer_pad[i] = (uint8_t)(key_block[i] ^ 0x5cu);
  }

  md5_init(&context);
  md5_update(&context, inner_pad, sizeof(inner_pad));
  if (data_size != 0u) md5_update(&context, (const uint8_t *)data, data_size);
  md5_finish(&context, inner_digest);

  md5_init(&context);
  md5_update(&context, outer_pad, sizeof(outer_pad));
  md5_update(&context, inner_digest, sizeof(inner_digest));
  md5_finish(&context, out);

  gmssl_secure_clear(&context, sizeof(context));
  gmssl_secure_clear(key_block, sizeof(key_block));
  gmssl_secure_clear(inner_pad, sizeof(inner_pad));
  gmssl_secure_clear(outer_pad, sizeof(outer_pad));
  gmssl_secure_clear(inner_digest, sizeof(inner_digest));
  return SALTS_OK;
}

static int salts_hmac(const DIGEST *digest, size_t expected_size,
                      const void *key, size_t key_size,
                      const void *data, size_t data_size,
                      uint8_t *out) {
  size_t output_size = 0u;

  if (digest == NULL || out == NULL ||
      (key == NULL && key_size != 0u) ||
      (data == NULL && data_size != 0u))
    return SALTS_EINVAL;

  if (hmac(digest, (const uint8_t *)key, key_size,
           (const uint8_t *)data, data_size, out, &output_size) != 1 ||
      output_size != expected_size)
    return SALTS_EIO;

  return SALTS_OK;
}

int salts_hmac_sha1(const void *key, size_t key_size,
                    const void *data, size_t data_size,
                    uint8_t out[SALTS_SHA1_DIGEST_BYTES]) {
  return salts_hmac(DIGEST_sha1(), SALTS_SHA1_DIGEST_BYTES,
                    key, key_size, data, data_size, out);
}

int salts_hmac_sha256(const void *key, size_t key_size,
                      const void *data, size_t data_size,
                      uint8_t out[SALTS_SHA256_DIGEST_BYTES]) {
  return salts_hmac(DIGEST_sha256(), SALTS_SHA256_DIGEST_BYTES,
                    key, key_size, data, data_size, out);
}

static int salts_des_cbc_crypt(
    const uint8_t key[SALTS_DES_KEY_BYTES],
    const uint8_t iv[SALTS_DES_BLOCK_BYTES],
    const void *input, size_t size, void *output, int decrypt) {
  int result;

  if (key == NULL || iv == NULL ||
      (size % SALTS_DES_BLOCK_BYTES) != 0u ||
      (size != 0u && (input == NULL || output == NULL)))
    return SALTS_EINVAL;

  result = decrypt
      ? des_cbc_decrypt(key, iv, (const uint8_t *)input, size, (uint8_t *)output)
      : des_cbc_encrypt(key, iv, (const uint8_t *)input, size, (uint8_t *)output);
  return result == 1 ? SALTS_OK : SALTS_EIO;
}

int salts_des_cbc_encrypt(
    const uint8_t key[SALTS_DES_KEY_BYTES],
    const uint8_t iv[SALTS_DES_BLOCK_BYTES],
    const void *input, size_t size, void *output) {
  return salts_des_cbc_crypt(key, iv, input, size, output, 0);
}

int salts_des_cbc_decrypt(
    const uint8_t key[SALTS_DES_KEY_BYTES],
    const uint8_t iv[SALTS_DES_BLOCK_BYTES],
    const void *input, size_t size, void *output) {
  return salts_des_cbc_crypt(key, iv, input, size, output, 1);
}

static int salts_aes128_cfb_crypt(
    const uint8_t key[SALTS_AES128_KEY_BYTES],
    const uint8_t iv[SALTS_AES_BLOCK_BYTES],
    const void *input, size_t size, void *output, int decrypt) {
  AES_KEY aes_key;
  uint8_t feedback[SALTS_AES_BLOCK_BYTES];
  uint8_t stream[SALTS_AES_BLOCK_BYTES];
  const uint8_t *src = (const uint8_t *)input;
  uint8_t *dst = (uint8_t *)output;
  size_t offset = 0u;
  size_t i;
  int status = SALTS_OK;

  if (key == NULL || iv == NULL ||
      (size != 0u && (input == NULL || output == NULL)))
    return SALTS_EINVAL;
  if (size == 0u) return SALTS_OK;

  if (aes_set_encrypt_key(&aes_key, key, SALTS_AES128_KEY_BYTES) != 1)
    return SALTS_EIO;

  memcpy(feedback, iv, sizeof(feedback));
  memset(stream, 0, sizeof(stream));

  for (i = 0u; i < size; ++i) {
    uint8_t input_byte;
    uint8_t output_byte;

    if (offset == 0u) aes_encrypt(&aes_key, feedback, stream);
    input_byte = src[i];
    output_byte = (uint8_t)(input_byte ^ stream[offset]);
    dst[i] = output_byte;
    feedback[offset] = decrypt ? input_byte : output_byte;
    offset = (offset + 1u) % SALTS_AES_BLOCK_BYTES;
  }

  gmssl_secure_clear(&aes_key, sizeof(aes_key));
  gmssl_secure_clear(feedback, sizeof(feedback));
  gmssl_secure_clear(stream, sizeof(stream));
  return status;
}

int salts_aes128_cfb_encrypt(
    const uint8_t key[SALTS_AES128_KEY_BYTES],
    const uint8_t iv[SALTS_AES_BLOCK_BYTES],
    const void *input, size_t size, void *output) {
  return salts_aes128_cfb_crypt(key, iv, input, size, output, 0);
}

int salts_aes128_cfb_decrypt(
    const uint8_t key[SALTS_AES128_KEY_BYTES],
    const uint8_t iv[SALTS_AES_BLOCK_BYTES],
    const void *input, size_t size, void *output) {
  return salts_aes128_cfb_crypt(key, iv, input, size, output, 1);
}

int salts_crypto_equal(const void *lhs, const void *rhs, size_t size,
                       int *out_equal) {
  if (out_equal == NULL || (size != 0u && (lhs == NULL || rhs == NULL)))
    return SALTS_EINVAL;

  if (size == 0u) {
    *out_equal = 1;
    return SALTS_OK;
  }

  *out_equal = gmssl_secure_memcmp(lhs, rhs, size) == 0 ? 1 : 0;
  return SALTS_OK;
}
