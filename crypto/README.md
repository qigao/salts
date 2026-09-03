# Salts Crypto

`Salts::Crypto` provides the narrow cryptographic surface that Salts consumers
need but the configured BoringSSL build does not implement. The current API is
RFC 8032 pure Ed448 key generation, public-key derivation, signing, and
verification. It uses the operating-system CSPRNG through `Salts::Platform` and
keeps all libecc types private.

```c
#include <salts/crypto.h>

uint8_t private_key[SALTS_CRYPTO_ED448_PRIVATE_KEY_SIZE];
uint8_t public_key[SALTS_CRYPTO_ED448_PUBLIC_KEY_SIZE];
int status = salts_crypto_ed448_keygen(private_key, public_key);
```

The implementation compiles only libecc's WEI448, SHAKE256, and EDDSA448
feature set. libecc is vendored from <https://github.com/libecc/libecc> at
commit `6e8f214f41f65d5f30b04da75472f9c24f2100db` under its BSD terms; see
`vendor/libecc/UPSTREAM.md` and `vendor/libecc/LICENSE`.

The API returns module-specific statuses so callers can distinguish invalid
signatures from operational failures. Secret intermediate state is wiped
before return. A caller owns all input and output buffers, and concurrent calls
are independent.
