# Salts SIMD

`Salts::SIMD` is the portable 128-bit SIMD execution layer for Salts consumers.

- CMeta owns canonical vector and mask semantic descriptors.
- This component owns the stable C ABI and execution helpers.
- SIMDe is supplied through the repository vcpkg dependency contract and is private to the implementation.
- Consumers must not include SIMDe or depend on `simde_v128_t`.
- CFlow may consume these semantics later, but SIMD operations are not WebAssembly-specific CFlow operators.

The public carrier is `cmeta_v128`, an alias of CMeta's neutral 16-byte storage.
Lane interpretation comes from a `cmeta_vector_desc`, not from storage layout.

## Descriptor-driven execution

New consumers should use the generic operation families:

```c
cmeta_simd_splat(desc, out, scalar);
cmeta_simd_unary(desc, op, out, value);
cmeta_simd_binary(desc, op, out, left, right);
cmeta_simd_compare(desc, cmp, out_mask, left, right);
cmeta_simd_shift(desc, op, out, value, count);
cmeta_simd_select(desc, out, when_set, when_unset, mask);
```

The descriptor supplies lane width, lane count, signedness/floating-point kind,
and mask semantics. Unsupported descriptor/operation combinations return
`false`; the caller decides whether to fall back, reject, or lower elsewhere.

The operation enums are domain-neutral. They intentionally do not contain
WebAssembly opcode names.

Existing helpers such as `cmeta_simd_i32x4_add` remain available as
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


## Lane and memory transforms

Salts also provides portable transforms over caller-owned, already-validated
bytes:

```c
cmeta_simd_load_splat(desc, out, source);
cmeta_simd_load_extend(desc, out, source);
cmeta_simd_load_zero(loaded_bits, out, source);

cmeta_simd_extract_lane(desc, value, lane, &scalar);
cmeta_simd_replace_lane(desc, out, value, lane, scalar);

cmeta_simd_shuffle_bytes(out, left, right, lanes);
cmeta_simd_swizzle_bytes(out, value, indices);
```

These helpers do **not** own:
- memory allocation;
- effective-address calculation;
- bounds checking;
- Wasm memarg decoding or traps.

A runtime such as TurboWasm first checks its memory/table rules, then hands the
checked bytes or v128 value to Salts for the portable transformation.

`cmeta_simd_load_extend` derives the widening rule from the destination CMeta
descriptor:
- i16x8/u16x8 widen 8 lanes from 8-bit source values;
- i32x4/u32x4 widen 4 lanes from 16-bit source values;
- i64x2/u64x2 widen 2 lanes from 32-bit source values.

Signedness is therefore semantic metadata, not a separate backend flag.


## Advanced numeric core

The generic operation layer also covers reusable advanced numeric semantics:

- `SALTS_SIMD_UNARY_ABS`, `NEG`, `SQRT`;
- `SALTS_SIMD_UNARY_POPCOUNT` for byte vectors;
- `SALTS_SIMD_BINARY_MIN` / `MAX`;
- signed/unsigned saturating add/sub for 8- and 16-bit integer lanes;
- reductions:
  - any true;
  - all lanes true;
  - lane sign-bit mask.

These remain descriptor-driven. Signedness and lane width come from
`cmeta_vector_desc`; consumers do not pass a second lane-type enum.

Unsupported descriptor/operation pairs return `false`. This is intentional:
higher layers such as TurboWasm may keep validated instructions fail-closed
until the reusable Salts primitive exists.
