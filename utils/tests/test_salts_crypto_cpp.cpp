#include "salts_crypto.h"
#include "tinytest.h"

#include <cstddef>
#include <cstdint>

static_assert(SALTS_MD5_DIGEST_BYTES == 16u, "MD5 ABI size changed");
static_assert(SALTS_SHA1_DIGEST_BYTES == 20u, "SHA-1 ABI size changed");
static_assert(SALTS_SHA256_DIGEST_BYTES == 32u, "SHA-256 ABI size changed");
static_assert(SALTS_AES128_KEY_BYTES == 16u, "AES-128 key ABI size changed");
static_assert(SALTS_AES_BLOCK_BYTES == 16u, "AES block ABI size changed");
static_assert(SALTS_DES_KEY_BYTES == 8u, "DES key ABI size changed");
static_assert(SALTS_DES_BLOCK_BYTES == 8u, "DES block ABI size changed");

spec("salts_crypto C++ ABI") {
  it("exposes the provider-neutral C ABI to C++17 consumers") {
    std::uint8_t md5[SALTS_MD5_DIGEST_BYTES];
    std::uint8_t sha1_mac[SALTS_SHA1_DIGEST_BYTES];
    std::uint8_t sha256_mac[SALTS_SHA256_DIGEST_BYTES];
    std::uint8_t aes_key[SALTS_AES128_KEY_BYTES] = {};
    std::uint8_t aes_iv[SALTS_AES_BLOCK_BYTES] = {};
    std::uint8_t aes_data[1] = {};
    std::uint8_t des_key[SALTS_DES_KEY_BYTES] = {};
    std::uint8_t des_iv[SALTS_DES_BLOCK_BYTES] = {};
    std::uint8_t des_data[SALTS_DES_BLOCK_BYTES] = {};
    salts_sha256_stream *stream = nullptr;
    int equal = 0;

    check_equal(salts_md5("abc", static_cast<std::size_t>(3), md5), SALTS_OK);
    check_equal(salts_hmac_md5("key", static_cast<std::size_t>(3),
                               "data", static_cast<std::size_t>(4),
                               md5),
                SALTS_OK);
    check_equal(salts_hmac_sha1("key", static_cast<std::size_t>(3),
                                "data", static_cast<std::size_t>(4),
                                sha1_mac),
                SALTS_OK);
    check_equal(salts_hmac_sha256("key", static_cast<std::size_t>(3),
                                  "data", static_cast<std::size_t>(4),
                                  sha256_mac),
                SALTS_OK);
    check_equal(salts_sha256_stream_create(&stream), SALTS_OK);
    check_equal(salts_sha256_stream_update(stream, "abc", static_cast<std::size_t>(3)),
                SALTS_OK);
    check_equal(salts_sha256_stream_finish(stream, sha256_mac), SALTS_OK);
    salts_sha256_stream_destroy(stream);
    stream = nullptr;
    check_equal(salts_des_cbc_encrypt(des_key, des_iv, des_data,
                                      sizeof(des_data), des_data),
                SALTS_OK);
    check_equal(salts_des_cbc_decrypt(des_key, des_iv, des_data,
                                      sizeof(des_data), des_data),
                SALTS_OK);
    check_equal(salts_aes128_cfb_encrypt(aes_key, aes_iv, aes_data,
                                          sizeof(aes_data), aes_data),
                SALTS_OK);
    check_equal(salts_aes128_cfb_decrypt(aes_key, aes_iv, aes_data,
                                          sizeof(aes_data), aes_data),
                SALTS_OK);
    check_equal(salts_crypto_equal(md5, md5, sizeof(md5), &equal), SALTS_OK);
    check_equal(equal, 1);
    salts_crypto_clear(sha256_mac, sizeof(sha256_mac));
  }
}
