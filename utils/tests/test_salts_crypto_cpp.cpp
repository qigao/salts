#include "salts_crypto.h"
#include "tinytest.h"

#include <cstddef>
#include <cstdint>

static_assert(SALTS_MD5_DIGEST_BYTES == 16u, "MD5 ABI size changed");
static_assert(SALTS_SHA1_DIGEST_BYTES == 20u, "SHA-1 ABI size changed");
static_assert(SALTS_SHA256_DIGEST_BYTES == 32u, "SHA-256 ABI size changed");

spec("salts_crypto C++ ABI") {
  it("exposes the provider-neutral C ABI to C++17 consumers") {
    std::uint8_t md5[SALTS_MD5_DIGEST_BYTES];
    std::uint8_t sha1_mac[SALTS_SHA1_DIGEST_BYTES];
    std::uint8_t sha256_mac[SALTS_SHA256_DIGEST_BYTES];
    int equal = 0;

    check_equal(salts_md5("abc", static_cast<std::size_t>(3), md5), SALTS_OK);
    check_equal(salts_hmac_sha1("key", static_cast<std::size_t>(3),
                                "data", static_cast<std::size_t>(4),
                                sha1_mac),
                SALTS_OK);
    check_equal(salts_hmac_sha256("key", static_cast<std::size_t>(3),
                                  "data", static_cast<std::size_t>(4),
                                  sha256_mac),
                SALTS_OK);
    check_equal(salts_crypto_equal(md5, md5, sizeof(md5), &equal), SALTS_OK);
    check_equal(equal, 1);
  }
}
