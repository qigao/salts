#include "salts_crypto.h"

#include <gmssl/sha1.h>
#include <gmssl/sha2.h>

#include <string.h>

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
