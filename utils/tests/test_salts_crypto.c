#include <stdint.h>
#include <string.h>

#include "salts_crypto.h"
#include "tinytest.h"

static const uint8_t MD5_ABC[SALTS_MD5_DIGEST_BYTES] = {
    0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
    0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72};

static const uint8_t SHA1_ABC[SALTS_SHA1_DIGEST_BYTES] = {
    0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
    0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};

static const uint8_t SHA256_ABC[SALTS_SHA256_DIGEST_BYTES] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

static const uint8_t HMAC_SHA1_HI_THERE[SALTS_SHA1_DIGEST_BYTES] = {
    0xb6, 0x17, 0x31, 0x86, 0x55, 0x05, 0x72, 0x64, 0xe2, 0x8b,
    0xc0, 0xb6, 0xfb, 0x37, 0x8c, 0x8e, 0xf1, 0x46, 0xbe, 0x00};

static const uint8_t HMAC_SHA256_HI_THERE[SALTS_SHA256_DIGEST_BYTES] = {
    0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
    0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
    0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
    0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7};

spec("salts_crypto") {
  it("computes legacy MD5 for the standard abc vector") {
    uint8_t digest[SALTS_MD5_DIGEST_BYTES];
    check_equal(salts_md5("abc", 3u, digest), SALTS_OK);
    check_equal(memcmp(digest, MD5_ABC, sizeof(digest)), 0);
  }

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

  it("computes RFC 2202 HMAC-SHA1") {
    uint8_t key[20];
    uint8_t mac[SALTS_SHA1_DIGEST_BYTES];
    memset(key, 0x0b, sizeof(key));

    check_equal(salts_hmac_sha1(key, sizeof(key), "Hi There", 8u, mac), SALTS_OK);
    check_equal(memcmp(mac, HMAC_SHA1_HI_THERE, sizeof(mac)), 0);
  }

  it("computes RFC 4231 HMAC-SHA256") {
    uint8_t key[20];
    uint8_t mac[SALTS_SHA256_DIGEST_BYTES];
    memset(key, 0x0b, sizeof(key));

    check_equal(salts_hmac_sha256(key, sizeof(key), "Hi There", 8u, mac), SALTS_OK);
    check_equal(memcmp(mac, HMAC_SHA256_HI_THERE, sizeof(mac)), 0);
  }

  it("compares fixed ranges without exposing provider semantics") {
    const uint8_t same_a[] = {1u, 2u, 3u, 4u};
    const uint8_t same_b[] = {1u, 2u, 3u, 4u};
    const uint8_t different[] = {1u, 2u, 3u, 5u};
    int equal = 0;

    check_equal(salts_crypto_equal(same_a, same_b, sizeof(same_a), &equal), SALTS_OK);
    check_equal(equal, 1);
    check_equal(salts_crypto_equal(same_a, different, sizeof(same_a), &equal), SALTS_OK);
    check_equal(equal, 0);
    check_equal(salts_crypto_equal(NULL, NULL, 0u, &equal), SALTS_OK);
    check_equal(equal, 1);
  }

  it("validates digest arguments") {
    uint8_t digest[SALTS_SHA256_DIGEST_BYTES];

    check_equal(salts_md5(NULL, 0u, digest), SALTS_OK);
    check_equal(salts_md5(NULL, 1u, digest), SALTS_EINVAL);
    check_equal(salts_md5("x", 1u, NULL), SALTS_EINVAL);

    check_equal(salts_sha256(NULL, 0u, digest), SALTS_OK);
    check_equal(salts_sha256(NULL, 1u, digest), SALTS_EINVAL);
    check_equal(salts_sha256("x", 1u, NULL), SALTS_EINVAL);
  }

  it("validates HMAC and compare arguments") {
    uint8_t mac[SALTS_SHA256_DIGEST_BYTES];
    int equal = 0;

    check_equal(salts_hmac_sha256(NULL, 0u, NULL, 0u, mac), SALTS_OK);
    check_equal(salts_hmac_sha256(NULL, 1u, "", 0u, mac), SALTS_EINVAL);
    check_equal(salts_hmac_sha256("", 0u, NULL, 1u, mac), SALTS_EINVAL);
    check_equal(salts_hmac_sha256("", 0u, "", 0u, NULL), SALTS_EINVAL);

    check_equal(salts_crypto_equal(NULL, NULL, 1u, &equal), SALTS_EINVAL);
    check_equal(salts_crypto_equal("", "", 0u, NULL), SALTS_EINVAL);
  }
}
