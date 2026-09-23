# Salts SIMD

`Salts::SIMD` is the portable 128-bit SIMD execution layer for Salts consumers.

- CMeta owns canonical vector and mask semantic descriptors.
- This component owns the stable C ABI and execution helpers.
- SIMDe is supplied through the repository vcpkg dependency contract and is private to the implementation.
- Consumers must not include SIMDe or depend on `simde_v128_t`.
- CFlow may consume these semantics later, but SIMD operations are not WebAssembly-specific CFlow operators.

The public carrier is `salts_v128`, an alias of CMeta's neutral 16-byte storage.
Lane interpretation comes from a `cmeta_vector_desc`, not from storage layout.

## Descriptor-driven execution

New consumers should use the generic operation families:

```c
salts_simd_splat(desc, out, scalar);
salts_simd_unary(desc, op, out, value);
salts_simd_binary(desc, op, out, left, right);
salts_simd_compare(desc, cmp, out_mask, left, right);
salts_simd_shift(desc, op, out, value, count);
salts_simd_select(desc, out, when_set, when_unset, mask);
```

The descriptor supplies lane width, lane count, signedness/floating-point kind,
and mask semantics. Unsupported descriptor/operation combinations return
`false`; the caller decides whether to fall back, reject, or lower elsewhere.

The operation enums are domain-neutral. They intentionally do not contain
WebAssembly opcode names.

Existing helpers such as `salts_simd_i32x4_add` remain available as
compatibility wrappers over the generic dispatcher.

## Layering

```text
CMeta vector/mask descriptors
          |
          v
Salts::SIMD generic operations
          |
          v
private SIMDe/native implementation
```

TurboWasm, CFlow, DSP, image, and database frontends can share this execution
boundary without importing SIMDe or defining another vector type system.
