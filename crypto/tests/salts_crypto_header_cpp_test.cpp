#include <salts/crypto.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(SALTS_CRYPTO_ED448_PRIVATE_KEY_SIZE == 57U);
static_assert(SALTS_CRYPTO_ED448_PUBLIC_KEY_SIZE == 57U);
static_assert(SALTS_CRYPTO_ED448_SIGNATURE_SIZE == 114U);
static_assert(
    std::is_same_v<decltype(&salts_crypto_ed448_verify),
                   int (*)(const std::uint8_t *, const void *, std::size_t, const std::uint8_t *)>);

int main() { return SALTS_CRYPTO_OK == 0 ? 0 : 1; }
