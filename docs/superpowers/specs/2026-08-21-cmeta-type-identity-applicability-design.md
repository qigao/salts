# CMeta Type Identity Applicability and Descriptor Bridge

Status: architecture baseline — Gate A implemented and verified  
Date: 2026-08-21  
Branch: `leanv4`  
Parent architecture: `2026-08-21-cmeta-hexagonal-architecture-design.md`  
Related type architecture: `2026-08-21-cmeta-type-application-design.md`

## 1. Purpose

This specification defines what it means for CMeta Core `TypeId` to be applicable to the real CMeta type system. It separates three kinds of evidence:

```text
Semantic proof
    Is the TypeId/equality model internally well-defined?

Implementation conformance
    Does the real C implementation agree with the formal model on checked witnesses?

Applicability proof
    Do real CMeta scenarios, including multi-TU descriptors and existing consumers,
    obey the intended semantic identity rules?
```

No one layer substitutes for another.

Gate A connects the already implemented TypeId kernel to real `cmeta_type_desc` objects. Later gates extend structural identity to current value generics, containers, and CMeta Extend.

## 2. Verified baseline

CMeta Core already implements structural TypeId forms:

```text
ATOM
POINTER
CONST
APPLY
```

and finite `GenericConstructor` metadata. `KnownTypes` and `CallableSignatures` are separate finite universes.

Gate A now additionally implements:

```text
cmeta_type_desc.identity
cmeta_type_identity_of(...)
cmeta_type_desc_valid(...)
three-mode cmeta_type_equal(...)
Core builtin atom TypeIds
Core builtin pointer TypeIds
multi-TU structural descriptor equality
C -> generated Lean descriptor conformance
Lean DescriptorView semantic equality laws
```

The current header-local value/container descriptors are not thereby declared structurally identified. That is Gate B/C work.

## 3. Hexagonal ownership

The descriptor bridge belongs to CMeta Core. It does not introduce a dependency on Extend, State, Exec, CFlow, minicoro, or OS APIs.

```text
CMeta Extend
     │ lowers to
     ▼
CMeta Core
  TypeId
  cmeta_type_desc
  semantic equality
     ▲
     │ consumed by
State / Exec / CFlow / value & container modules
```

The bridge changes Core semantic metadata, not source syntax.

## 4. Descriptor representation

The implemented descriptor shape is:

```c
typedef struct cmeta_type_desc {
    const char *name;
    size_t size;
    size_t align;
    cmeta_type_kind kind;
    const struct cmeta_type_desc *pointee;
    const cmeta_type_identity *identity;
} cmeta_type_desc;
```

Core and generated CMeta container descriptor initializers touched by this migration use designated fields. Gate A container descriptors explicitly keep `.identity = NULL` until container structural identity is implemented.

Descriptor addresses are never semantic identity.

## 5. Equality authority

The migration has exactly three modes.

### 5.1 Structural / structural

```text
A.identity != NULL
B.identity != NULL
```

Then:

```text
cmeta_type_equal(A,B)
    = cmeta_type_identity_equal(A.identity, B.identity)
```

Display names, size, alignment, descriptor addresses, and generated symbol names cannot override a structural inequality.

### 5.2 Legacy / legacy

```text
A.identity == NULL
B.identity == NULL
```

The compatibility comparison remains:

```text
kind
size
align
name
recursive pointee comparison for legacy pointers
```

This is a migration path, not the target identity model.

### 5.3 Structural / legacy

If exactly one descriptor has structural identity:

```text
cmeta_type_equal(A,B) == false
```

This isolation rule is mandatory. A structural descriptor never falls back to display-name/layout equality.

If two real descriptors should be semantically equal, migration must give both the correct TypeId rather than weaken equality.

## 6. Descriptor validation

Gate A exposes:

```c
const cmeta_type_identity *cmeta_type_identity_of(
    const cmeta_type_desc *desc);

bool cmeta_type_desc_valid(
    const cmeta_type_desc *desc);
```

A structural descriptor must contain a valid TypeId.

A structural pointer descriptor additionally requires:

```text
kind == CMETA_T_POINTER
pointee != NULL
pointee.identity != NULL
identity.form == POINTER
identity.base == pointee.identity
```

A pointer whose pointee is still legacy remains legacy in this gate.

Equality does not repair malformed descriptors; validation and equality have separate responsibilities.

## 7. Core builtin stable IDs

Gate A publishes explicit Core-owned semantic IDs:

```text
cmeta.void
cmeta.bool
cmeta.int
cmeta.long
cmeta.float
cmeta.double
cmeta.size
cmeta.gen_status
```

These IDs are selected from the finite built-in CMeta row/token schema. They are not derived by parsing `desc->name`.

Project/user atom rows remain legacy unless an explicit stable-ID schema is provided by a later design.

## 8. Pointer identity

Builtin pointer identity is structural:

```text
identity(T*) = POINTER(identity(T))
```

It is not encoded as `ATOM("T *")`.

Gate A witnesses include distinct pointer types such as:

```text
POINTER(cmeta.int) != POINTER(cmeta.long)
```

The descriptor validator also checks that a pointer TypeId base matches its structural pointee descriptor.

## 9. Formal DescriptorView model

`formal/CMeta/DescriptorBridge.lean` separates structural identity from legacy representation.

Conceptually:

```lean
structure LegacyDesc where
  name : String
  size : Nat
  align : Nat
  kind : LegacyKind
  pointeeKey : Option String

structure DescriptorView where
  identity : Option TypeId
  legacy : LegacyDesc
```

The semantic authority is modeled as a tagged key:

```lean
inductive DescriptorSemanticKey where
  | legacy (desc : LegacyDesc)
  | structural (typeId : TypeId)
```

Therefore structural and legacy equivalence classes are disjoint by construction.

The formal model proves proposition-level semantic equality properties:

```text
reflexivity
symmetry
transitivity
structural identity dominance over legacy metadata
same legacy metadata cannot erase different TypeIds
mixed structural/legacy isolation
```

Descriptor/object addresses are absent from the formal semantic model.

## 10. Real C conformance witness

`formal/cmeta_descriptor_bridge_conformance_gen.c` uses explicit runtime checks that remain active under Release/`NDEBUG` builds.

This is intentional: plain C `assert(...)` is not an acceptable formal witness because the formal preset defines `NDEBUG`.

The C witness checks and emits computed results for:

```text
legacy/legacy equality
same TypeId + different display/layout metadata
same layout/display + different TypeId
structural/legacy isolation
builtin int structural identity
int* versus long* inequality
```

Its output is checked byte-for-byte against:

```text
formal/CMeta/DescriptorBridgeGeneratedC.lean
```

and re-evaluated by:

```text
formal/CMeta/DescriptorBridgeConformance.lean
```

## 11. Multi-translation-unit applicability

Gate A includes a real executable built from:

```text
formal/type_identity_tu_a.c
formal/type_identity_tu_b.c
formal/type_identity_multi_tu.c
```

It demonstrates:

```text
descriptorA address != descriptorB address
identityA address   != identityB address
TypeId(identityA)   == TypeId(identityB)
cmeta_type_equal(descriptorA, descriptorB) == true
```

The same executable proves this for structural pointer descriptors and also proves:

```text
same layout + different TypeId -> false
structural + legacy            -> false
```

This is the primary engineering evidence that TU-local metadata addresses are not program-wide type identity.

## 12. Adversarial applicability

Gate A deliberately includes representation collisions. Two descriptors may have the same object kind, size, alignment, and even the same display name while carrying different structural TypeIds.

They must compare unequal.

Conversely, two structural descriptors may have different display names and representation metadata but the same TypeId; semantic equality follows TypeId.

This demonstrates that TypeId is not merely renamed string/layout identity.

## 13. KnownTypes versus CallableSignatures

The previously implemented invariant remains part of the applicability argument:

```text
KnownTypes may grow
without automatically changing CallableSignatures
```

The full-profile C probe adds a known-only type while retaining five callable builtins and verifies the full signature count remains 176 including the invalid sentinel.

Generic applications therefore have a path into reflection without forcing N²/N³ callable expansion.

## 14. CFlow regression obligation

Descriptor migration is not accepted if it silently changes existing CFlow semantics.

Gate A keeps the existing suite unchanged:

```text
header conformance
plan compiler/runtime
structured relations
policy handling
optimizer properties/effects/topology
Lean kernel proofs
```

Existing snapshots remain byte-for-byte stable apart from the deliberately added descriptor-bridge snapshot.

## 15. Applicability matrix

Current verified status is:

| Scenario | Lean semantic model | Real C witness | Multi-TU | Consumer evidence | Status |
| --- | --- | --- | --- | --- | --- |
| structural atom equality | yes | yes | yes | Core registry/builtins | Gate A verified |
| pointer structural equality | yes | yes | yes | signature descriptors | Gate A verified |
| same TypeId / different display metadata | yes | yes | yes | descriptor bridge | Gate A verified |
| same layout / different TypeId | yes | yes | yes | descriptor bridge | Gate A verified |
| mixed structural/legacy isolation | yes | yes | yes | migration boundary | Gate A verified |
| descriptor address independence | semantic by construction | yes | yes | descriptor consumers | Gate A verified |
| KnownTypes/CallableSignatures separation | yes | yes | not required | Callable/CFlow | verified |
| CFlow behavior preservation | indirect | existing suite | not required | CFlow | Gate A verified |
| finite generic application | TypeId level only | TypeId witness only | not yet generic descriptor witness | Value/Container | Gate B open |
| current Option/Result descriptors | not yet bridged | not yet | not yet | Value | Gate B open |
| current container applications | not yet bridged | not yet | not yet | Range/containers | Gate C open |
| `M<A,B>` source lowering | not yet | not yet | not applicable | Extend | Gate D open |

An abstract Lean case alone is never enough to mark an applicability row complete.

## 16. Gate A — Descriptor Bridge Applicability

**Status: CLOSED / IMPLEMENTED.**

Evidence required by this specification has been supplied:

- field-safe designated descriptor initialization for the migration surface;
- `cmeta_type_desc.identity` in the public Core descriptor;
- explicit builtin atom identities;
- structural builtin pointer identities;
- structural/structural equality through TypeId only;
- legacy/legacy compatibility;
- structural/legacy isolation;
- adversarial same-layout/different-TypeId C witness;
- multi-TU address-independence executable;
- Lean `DescriptorView` semantic equality laws;
- C -> generated Lean -> Lean-model conformance;
- unchanged existing CFlow/formal conformance suite;
- `lake build --wfail` under Lean 4.30.0.

The implementation evidence is tracked by the formal workflow. Gate A closure does not close any later gate.

## 17. Gate B — Generic Applicability

**Status: OPEN.**

Required before current strict-C11 value generics are called structurally identified:

- stable semantic IDs for `Option`, `Result` and other participating constructors;
- generated value descriptors carrying `Apply(...)` TypeIds;
- equivalent applications comparing equal across TUs;
- alias/display names not affecting semantic identity;
- explicit distinction between semantic equality and nominal C type compatibility.

Current separately named `typed(Result, A, User, Error)` and `typed(Result, B, User, Error)` structs must not be described as interchangeable C types merely because a future semantic TypeId is equal.

## 18. Gate C — Container Applicability

**Status: OPEN.**

Required before typed containers are called structurally identified:

- stable finite constructor schemas for List/Vec/Map/HashMap/etc.;
- container descriptors carrying application TypeIds;
- element/key/value/entry Range descriptors carrying correct semantic identities;
- cross-TU container descriptor equality witnesses;
- existing container/CFlow behavior preserved.

Gate A intentionally leaves current header-local container descriptors with `.identity = NULL`.

## 19. Gate D — Extend Applicability

**Status: OPEN.**

Required later for `M<A,B>`:

- Extend lowering resolves to the same Core TypeIds;
- aliases preserve TypeId;
- generated C names remain non-semantic;
- nested applications preserve finite dependency ordering and deduplication.

## 20. Stable atom policy

Core builtin stable IDs are explicit semantic API.

Gate A does not infer user/project stable IDs from:

```text
display names
C descriptor symbol names
typedef spellings
layout metadata
```

A future project/user stable-ID contract must be namespace-qualified and deterministic across TUs.

## 21. Rejected designs

The following remain rejected:

- a global descriptor-address -> TypeId side table as the primary architecture;
- parsing `desc->name` to recover TypeId;
- using size/alignment as semantic identity;
- structural/legacy fallback to legacy equality;
- generated C symbol names as semantic TypeIds;
- changing CFlow semantics to accommodate incomplete migration;
- claiming generic/container applicability before their descriptors carry structural identities;
- calling CI snapshots alone a formal proof;
- calling abstract Lean equality alone an applicability proof.

## 22. Proof language

Project documentation uses these terms deliberately:

```text
formally modeled/proved
    Lean theorem/kernel evidence for the semantic model

C implementation conformance
    real C-generated witness agrees with checked Lean snapshot/model

applicability demonstrated
    real CMeta scenario, including required multi-TU/consumer evidence,
    satisfies the relevant matrix row
```

The unqualified phrase “formally proved” should not be used for implementation behavior when only selected witnesses exist.

## 23. Success criterion

The overall TypeId architecture is adequate for CMeta when all relevant gates eventually establish:

```text
structural semantic identity
        +
real descriptors carrying it
        +
TU-local address independence
        +
legacy isolation
        +
finite generic/value/container application mapping
        +
KnownTypes independent from callable demand
        +
existing consumer behavior preserved
```

Gate A establishes the descriptor bridge portion of that criterion. Gate B/C/D remain explicit follow-up work.