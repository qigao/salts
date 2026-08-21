# CMeta Type Identity Applicability and Descriptor Bridge

Status: draft for user review  
Date: 2026-08-21  
Branch: `leanv4`  
Parent architecture: `2026-08-21-cmeta-hexagonal-architecture-design.md`  
Related type architecture: `2026-08-21-cmeta-type-application-design.md`

## 1. Purpose

This specification defines what it means for CMeta Core `TypeId` to be **applicable** to the real CMeta type system, and defines the proof obligations that must be satisfied before `cmeta_type_desc` can use `TypeId` as semantic authority.

The goal is not merely to prove that the abstract `TypeId` model is internally consistent. The goal is to establish that the model is adequate for existing strict-C11 CMeta descriptors, multi-translation-unit use, pointers, aliases, finite generic applications, containers, callable signatures, and CFlow consumers.

The required evidence has three distinct layers:

```text
1. Semantic proof
   Is the TypeId/equality model internally well-defined?

2. Implementation conformance
   Does the real C implementation agree with the formal model on generated witnesses?

3. Applicability proof
   Can real CMeta type scenarios be represented without descriptor addresses,
   display strings, generated symbols, or accidental layout equality becoming identity?
```

No one layer substitutes for another.

## 2. Current verified baseline

The implemented baseline already contains:

```text
TypeId forms:
    ATOM
    POINTER
    CONST
    APPLY

GenericConstructor:
    stable semantic id
    finite arity

KnownTypes / CallableSignatures:
    separate finite universes
```

The current CI also contains real C -> generated Lean -> kernel conformance witnesses for selected TypeId cases, plus a probe showing that adding a known-only type does not enlarge the full callable Cartesian-product universe.

This baseline is evidence for the semantic kernel. It is not yet a proof that every existing `cmeta_type_desc` carries or correctly projects to a structural TypeId.

## 3. Scope

This specification covers:

- semantic descriptor equality once TypeId is attached;
- migration from legacy descriptors;
- atom and pointer descriptor identity;
- multi-TU address independence;
- negative/adversarial identity cases;
- existing generic/value/container applicability obligations;
- KnownTypes/CallableSignatures applicability;
- CFlow regression obligations;
- Lean model and C conformance structure;
- the acceptance gate for the descriptor bridge.

This specification does **not** implement:

- `M<A,B>` parsing;
- CMeta Extend;
- canonical generated C names;
- Task/State/Exec;
- arbitrary non-type generic arguments;
- a general compile-time evaluator.

## 4. Core principle: semantic identity dominates representation

For any descriptor with structural identity, the identity object is authoritative.

Semantic equality MUST NOT depend on:

```text
descriptor address
display name
typedef alias
generated symbol
source whitespace
same size/alignment
translation-unit-local object address
```

A descriptor may still carry name/layout metadata for diagnostics, ABI checks, storage, and legacy migration, but those fields cease to define semantic identity once both compared descriptors have structural TypeIds.

## 5. Descriptor bridge target

The architectural target is:

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

Before this public layout is changed, existing positional `cmeta_type_desc` initializers MUST be migrated to designated initializers so the new field cannot silently bind to old positional assumptions or produce warning-driven build failures.

Header-local generated descriptors remain supported.

## 6. Equality modes

The migration has exactly three equality modes.

### 6.1 Structural / structural

```text
A.identity != NULL
B.identity != NULL
```

Then:

```text
cmeta_type_equal(A,B)
    = cmeta_type_identity_equal(A.identity, B.identity)
```

Legacy name/layout fields MUST NOT override a structural inequality.

### 6.2 Legacy / legacy

```text
A.identity == NULL
B.identity == NULL
```

The existing conservative legacy comparison remains temporarily valid:

```text
kind
size
align
name
recursive pointee comparison for pointers
```

This preserves existing strict-C11 descriptors during staged migration.

### 6.3 Structural / legacy — conservative isolation

```text
exactly one identity is NULL
```

Then:

```text
cmeta_type_equal(A,B) == false
```

This rule is mandatory.

Mixed-mode equality MUST NOT fall back to display-name/layout comparison. Otherwise a descriptor that already has semantic identity could still be equated to an unrelated legacy descriptor through string/layout coincidence, defeating TypeId authority.

If a real consumer requires two descriptors to compare equal, the migration MUST give both descriptors the correct identity rather than weaken `cmeta_type_equal`.

A separate layout-compatibility API is not introduced in this slice. It may be designed later only if a concrete consumer requires representation compatibility that is intentionally weaker than semantic type equality.

## 7. Formal descriptor model

The Lean applicability model SHOULD distinguish semantic identity from legacy representation.

Conceptually:

```lean
structure LegacyDesc where
  name : String
  size : Nat
  align : Nat
  kind : LegacyKind
  pointee : Option LegacyDesc

structure DescriptorView where
  identity : Option TypeId
  legacy : LegacyDesc
```

The model equality is:

```lean
def DescriptorView.semanticEq (a b : DescriptorView) : Bool :=
  match a.identity, b.identity with
  | some x, some y => x == y
  | none, none     => legacyEq a.legacy b.legacy
  | _, _           => false
```

The real C representation may use descriptor pointers for recursive metadata, but descriptor addresses are intentionally absent from the formal semantic equality model.

## 8. Required formal properties

The descriptor model MUST establish at least the following properties.

### 8.1 Reflexivity

Every well-formed descriptor is equal to itself.

```text
semanticEq d d = true
```

### 8.2 Symmetry

```text
semanticEq a b = semanticEq b a
```

### 8.3 Transitivity for well-formed descriptors

If:

```text
semanticEq a b = true
semanticEq b c = true
```

then:

```text
semanticEq a c = true
```

The mixed-mode isolation rule makes structural and legacy equivalence classes disjoint during migration.

### 8.4 Identity dominance

For two structural descriptors, changing legacy `name/size/align` metadata in the formal witness MUST NOT change semantic equality if their TypeIds remain equal.

Conversely, equal legacy metadata MUST NOT make two different structural TypeIds equal.

### 8.5 Mixed-mode isolation

```text
some(TypeId) vs none
    => false
```

must be explicit in the formal model and C witness.

### 8.6 Address independence

Semantic equality cannot mention descriptor address. Multi-TU C witnesses provide the implementation evidence for this property.

## 9. Descriptor well-formedness

A structural descriptor SHOULD have a validation relation separate from equality.

Minimum bridge requirements:

- `identity` must itself be a valid TypeId;
- if `kind == CMETA_T_POINTER` and identity is structural, the TypeId form MUST be `POINTER`;
- a structural pointer descriptor MUST have a non-null `pointee`;
- the pointer TypeId base MUST equal the pointee descriptor TypeId when the pointee is structural;
- if the pointee remains legacy, the pointer descriptor MUST remain legacy in the first bridge slice rather than claim a partially structural pointer identity.

The first bridge slice does not over-constrain non-pointer atoms/applications by layout kind. For example `CONST(T)` may have the same storage kind/layout as `T`, while remaining a distinct semantic TypeId.

## 10. Applicability class A — built-in atoms

The bridge MUST demonstrate stable structural identities for Core built-ins such as:

```text
bool
int
long
float
double
void
size_t
cmeta_gen_status
```

Example semantic ids:

```text
cmeta.bool
cmeta.int
cmeta.long
cmeta.float
cmeta.double
cmeta.void
cmeta.size
cmeta.gen_status
```

Exact stable IDs are part of Core semantic configuration once published and MUST NOT be derived by parsing the display string in `cmeta_type_desc.name`.

The first bridge slice MAY leave project-defined atom descriptors legacy until an explicit stable-id registration/schema mechanism is designed.

## 11. Applicability class B — pointers

Pointer identity MUST be structural:

```text
identity(T*) = POINTER(identity(T))
```

It MUST NOT be represented as an atom such as:

```text
ATOM("int *")
```

Required positive/negative witnesses include:

```text
int* == int*        true
int* == long*       false
int* == const int*  false
int* == int          false
```

Pointer descriptor equality across TUs must remain independent of the addresses of both the pointer descriptor and its pointee descriptor.

## 12. Applicability class C — aliases and display names

Alias transparency is a required semantic property.

For a future canonical application:

```text
Users        = Map<int,User>
AnotherUsers = Map<int,User>
```

both names MUST resolve to the same TypeId.

Applicability witnesses MUST include:

```text
same TypeId + different display names => equal
```

and:

```text
different TypeId + same display name/layout => not equal
```

This proves that TypeId is not merely a renamed string-based identity scheme.

## 13. Applicability class D — adversarial same-layout types

The test suite MUST deliberately construct semantically different types with identical representation metadata.

Example:

```text
User:
  kind  = OBJECT
  size  = 16
  align = 8

Order:
  kind  = OBJECT
  size  = 16
  align = 8
```

With structural identities:

```text
ATOM("app.User") != ATOM("app.Order")
```

`cmeta_type_equal` MUST return false even if names are deliberately made equal in an adversarial witness.

## 14. Applicability class E — multi-translation-unit identity

A mandatory conformance executable MUST be built from at least three translation units:

```text
type_identity_tu_a.c
type_identity_tu_b.c
type_identity_multi_tu.c
```

TU A and TU B each define distinct descriptor objects representing the same semantic type.

The executable must establish:

```text
&descriptorA != &descriptorB
identityA object address may differ from identityB object address
cmeta_type_identity_equal(identityA, identityB) == true
cmeta_type_equal(&descriptorA, &descriptorB) == true
```

The same witness must include a different semantic TypeId with equal layout metadata and show inequality.

This is the primary engineering proof that TU-local descriptors remain valid under structural identity.

## 15. Applicability class F — finite generic applications

The full TypeId architecture must be capable of representing current/future finite applications:

```text
Option<User>
Result<User,Error>
List<User>
Map<int,User>
Task<Result<User,Error>>
```

Required semantic properties:

```text
Apply(Result,[User,Error])
    == Apply(Result,[User,Error])

Apply(Result,[User,Error])
    != Apply(Result,[Error,User])

Apply(Option,[User])
    != Apply(List,[User])
```

The descriptor bridge first slice does not need to retrofit all existing value/container macros with application identities. Instead it MUST preserve a path for those modules to attach a structural application TypeId without changing `cmeta_type_equal` again.

## 16. Applicability class G — existing strict-C11 generic values

Current forms such as:

```c
typed(Option, MaybeUser, User);
typed(Result, LoadResult, User, Error);
```

must eventually map to semantic constructor applications.

However the current generated C structs are nominally named by the user. Until canonical generic representation is implemented, the bridge MUST NOT claim that two independently generated legacy `typed(Result, A, User, Error)` and `typed(Result, B, User, Error)` C structs are interchangeable C types.

The applicability proof concerns **semantic TypeId adequacy**, not automatic C ABI aliasing.

## 17. Applicability class H — containers and Range

Current typed containers are a required real-world applicability target.

Examples:

```c
typed(List, UserList, User);
typed(HashMap, UsersById, int, User);
```

The target semantic identities are:

```text
UserList  -> Apply(cmeta.List,[User])
UsersById -> Apply(cmeta.HashMap,[int,User])
```

Range applicability must preserve element/key/value semantics:

```text
UserList.range.element     -> User
UsersById.keys.element     -> int
UsersById.values.element   -> User
UsersById.entries.element  -> application/entry semantic type owned by container module
```

The first descriptor bridge MAY leave existing header-local container descriptors legacy. A later container-identity slice must prove cross-TU structural identity before those descriptors are declared fully migrated.

No container TypeId may be inferred by parsing generated display names such as `"UserList"`.

## 18. Applicability class I — KnownTypes vs CallableSignatures

The already implemented split is part of the applicability argument.

The invariant is:

```text
KnownTypes may grow
without automatically changing CallableSignatures
```

The existing full-profile probe, where a known-only type is added while the callable universe remains five types / 176 signatures, remains a required regression witness.

Future generic applications MUST enter KnownTypes independently from exact callable admission.

## 19. Applicability class J — CFlow regression

Descriptor migration is not acceptable if it changes current CFlow semantics accidentally.

Every descriptor-bridge implementation step MUST retain the existing CFlow conformance suite:

```text
header conformance
plan compiler/runtime
structured relations
policy handling
optimizer properties/effects/topology
Lean kernel proofs
```

Existing snapshots SHOULD remain byte-for-byte unchanged unless a descriptor-specific snapshot is deliberately extended.

A regression in CFlow during bridge work is evidence of an incomplete migration boundary, not justification to weaken TypeId equality.

## 20. Applicability matrix

The project tracks applicability with this matrix.

| Scenario | Lean semantic model | Real C witness | Multi-TU witness | Existing consumer evidence |
| --- | --- | --- | --- | --- |
| structural atom equality | required | required | required | type registry |
| pointer structural equality | required | required | required | signature descriptors |
| same TypeId / different display name | required | required | optional | aliases/generics |
| same layout / different TypeId | required | required | required | Struct/object descriptors |
| mixed structural/legacy isolation | required | required | required | migration boundary |
| finite generic application | implemented at TypeId level | required before generic bridge | required before canonical generic migration | Value/Container |
| container application | required before container migration | required | required | Range/containers |
| KnownTypes/CallableSignatures separation | implemented | implemented | not required | Callable/CFlow |
| descriptor address independence | semantic by construction | required | required | all descriptor consumers |
| CFlow behavior preservation | indirect | existing suite | not required | CFlow |

A row is not considered complete merely because its abstract Lean case exists. Real C evidence is required for applicability claims.

## 21. Staged acceptance gates

Applicability is staged so the bridge can progress without overclaiming later generic/container work.

### Gate A — Descriptor Bridge Applicability

Required before `cmeta_type_desc.identity` and structural `cmeta_type_equal` are called implemented:

- all `cmeta_type_desc` initializers touched by Core/container generation are safe for the new field, preferably designated initializers;
- built-in atom identities exist;
- built-in pointer identities are structural;
- structural/structural equality uses TypeId only;
- legacy/legacy compatibility is preserved;
- structural/legacy equality is false;
- same-layout/different-TypeId adversarial witness passes;
- multi-TU same-TypeId witness passes;
- Lean DescriptorView model establishes required equality properties;
- C -> generated Lean descriptor-equality conformance passes;
- all existing CFlow/formal CI remains green.

### Gate B — Generic Applicability

Required before claiming current value generics are structurally identified:

- Option/Result constructors have stable semantic IDs;
- strict-C11 generated descriptors carry `Apply(...)` TypeIds;
- alias/display names do not affect equality;
- repeated equivalent applications compare equal across TUs;
- no claim of C nominal-type interchangeability is made unless canonical representation is separately implemented.

### Gate C — Container Applicability

Required before claiming typed containers are structurally identified:

- List/Vec/Map/HashMap/etc. constructor schemas are finite and stable;
- container descriptors carry application TypeIds;
- key/value/entry/element Range descriptors have correct semantic identities;
- multi-TU container descriptor equality is proven by real C witnesses;
- existing container and CFlow tests remain unchanged in behavior.

### Gate D — Extend Applicability

Required later for `M<A,B>`:

- Extend lowering resolves to the same Core TypeIds proven above;
- aliases preserve TypeId;
- generated names do not enter semantic equality;
- nested applications preserve dependency order and finite generation.

## 22. Descriptor bridge implementation order

Once this written applicability specification is approved, the descriptor bridge implementation plan SHOULD use this order:

```text
1. initializer safety migration
   positional cmeta_type_desc initializers -> designated initializers
   no semantic behavior change

2. public descriptor bridge
   append identity field
   legacy descriptors explicitly use NULL

3. builtin structural identities
   atoms and pointers

4. descriptor equality modes
   structural/structural
   legacy/legacy
   structural/legacy false

5. multi-TU + adversarial C witnesses

6. Lean DescriptorView + C conformance

7. full CFlow/formal regression gate
```

Generic values and containers are separate follow-up plans after Gate A.

## 23. Stable atom ID policy

The first bridge slice MUST NOT invent project atom identities from display names or arbitrary generated C symbols.

Built-in stable IDs are explicitly owned by Core.

Project/user atoms remain legacy until CMeta has an explicit stable-id registration/schema contract. That later contract must be namespace-qualified and consistent across TUs.

This avoids accidentally freezing implementation symbol spellings as semantic API.

## 24. Error handling and validation

Malformed structural identities must fail closed.

Examples:

```text
pointer descriptor + ATOM identity           invalid
pointer identity + NULL pointee              invalid
POINTER(User) + pointee structural Order      invalid
application with invalid constructor arity    invalid
```

Equality is not responsible for repairing malformed descriptors.

A descriptor validation helper MAY be introduced as part of Gate A if tests demonstrate that runtime consumers need an explicit validity check. The implementation plan must not add a broad validation framework without a concrete use.

## 25. Rejected designs

The following are rejected:

- using a global descriptor-address -> TypeId side table as the primary architecture;
- parsing `desc->name` to recover TypeId;
- treating same size/alignment as semantic type equality;
- structural/legacy mixed fallback to legacy equality;
- using generated C symbol names as stable semantic TypeIds;
- changing existing CFlow semantics to accommodate an incomplete bridge;
- claiming generic/container applicability before their real descriptors have structural identities;
- claiming formal proof from CI snapshots alone;
- claiming applicability from abstract Lean equality alone.

## 26. Proof language

Project documentation must use precise language:

```text
"formally modeled/proved"
    Lean theorem/kernel evidence for the semantic model.

"C implementation conformance"
    real C-generated witness agrees with checked Lean snapshot/model.

"applicability demonstrated"
    real CMeta scenario, including required multi-TU/consumer evidence, satisfies the matrix.
```

Avoid the unqualified phrase "formally proved" when only selected implementation witnesses exist.

## 27. Success criterion

The TypeId architecture is considered adequate for CMeta when:

```text
semantic identity is structural
        +
real descriptors can carry it
        +
TU-local addresses are irrelevant
        +
legacy migration cannot override structural identity
        +
generic/container domains can map to finite applications
        +
KnownTypes remain independent from callable demand
        +
existing CFlow behavior is preserved
```

The descriptor bridge is only the first applicability gate. Its purpose is to connect the already implemented TypeId semantic kernel to real runtime descriptors without weakening the hexagonal Core boundary or overclaiming generic/container migration.