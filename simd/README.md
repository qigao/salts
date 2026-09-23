# Salts SIMD

`Salts::SIMD` is the portable 128-bit SIMD execution layer for Salts consumers.

- CMeta owns canonical vector and mask semantic descriptors.
- This component owns the stable C ABI and execution helpers.
- SIMDe is supplied through the repository vcpkg dependency contract and is private to the implementation.
- Consumers must not include SIMDe or depend on `simde_v128_t`.
- CFlow may consume these semantics later, but SIMD operations are not WebAssembly-specific CFlow operators.

The public carrier is `salts_v128`, an alias of CMeta's neutral 16-byte storage.
Lane interpretation comes from a `cmeta_vector_desc`, not from storage layout.
