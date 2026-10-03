#include <stdint.h>
#include <string.h>

#include "salts_crypto.h"
#include "tinytest.h"

static const uint8_t SHA1_ABC[SALTS_SHA1_DIGEST_BYTES] = {
    0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
    0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};

static const uint8_t SHA256_ABC[SALTS_SHA256_DIGEST_BYTES] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

spec("salts_crypto") {
  it("computes SHA-1 for the standard abc vector") {
    uint8_t digest[SALTS_SHA1_DIGEST_BYTES];
    check_equal(salts_sha1("abc", 3u, digest), SALTS_OK);
    check_equal(memcmp(digest, SHA1_ABC, sizeof(digest)), 0);
  }

  it("computes SHA-256 for the standard abc vector") {
    uint8_t digest[SALTS_SHA256_DIGEST_BYTES];
    check_equal(salts_sha256("abc", 3u, digest), SALTS_OK);
    check_equal(memcmp(digest, SHA256_ABC, sizeof(digest)), 0);
  }

  it("validates arguments without heap allocation") {
    uint8_t digest[SALTS_SHA256_DIGEST_BYTES];
    check_equal(salts_sha256(NULL, 0u, digest), SALTS_OK);
    check_equal(salts_sha256(NULL, 1u, digest), SALTS_EINVAL);
    check_equal(salts_sha256("x", 1u, NULL), SALTS_EINVAL);
  }
}
