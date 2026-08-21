# CMeta Architecture Specifications

This directory contains the architecture specifications for CMeta work on branch `leanv4`.

## Authority order

When documents overlap, use this order:

1. **CMeta Hexagonal Architecture** — global dependency and ownership rules;
2. **domain specifications** — Type Application, Type Identity Applicability, Producer/Replay, C++-Like Lambda, State/Exec, and later modules;
3. **implementation plans** — task sequencing only; they must not redefine architecture.

A lower-level document must not violate a higher-level dependency rule.

Within one domain, a later focused specification may refine a broader earlier one. In particular, **CMeta Type Identity Applicability and Descriptor Bridge** is authoritative for descriptor integration, migration equality modes, mixed structural/legacy behavior, and applicability proof obligations. Its conservative mixed-mode rule supersedes the broader migration fallback wording in the Type Application architecture.

## Current architecture baseline

### [CMeta Hexagonal Architecture](2026-08-21-cmeta-hexagonal-architecture-design.md)

Defines the system boundary:

```text
CMeta Core       semantic hexagonal kernel
CMeta Extend     optional source frontend/lowering adapter
State / Exec     first-party modules on Core
CFlow            typed value-flow consumer
minicoro / OS    infrastructure adapters
```

This document is the primary architecture authority.

### [CMeta Type Application Architecture](2026-08-21-cmeta-type-application-design.md)

Defines finite generic application and the split between:

```text
Core:   GenericConstructor / TypeId / equality / reflection
Extend: `M<A,B>` / TypeExpr / discovery / symbol emission / lowering
```

It also defines the mandatory separation between `KnownTypes` and `CallableSignatures`.

### [CMeta Type Identity Applicability and Descriptor Bridge](2026-08-21-cmeta-type-identity-applicability-design.md)

Defines the three evidence layers:

```text
semantic proof
implementation conformance
applicability proof
```

and the applicability gates for connecting structural `TypeId` to real `cmeta_type_desc` objects. It makes multi-TU address independence, adversarial same-layout/different-TypeId cases, mixed-mode isolation, generic/container applicability, and CFlow regression explicit proof obligations.

Descriptor migration equality is:

```text
structural / structural -> TypeId equality
legacy / legacy         -> legacy equality
structural / legacy     -> false
```

### [CMeta Producer/Replay Algebra](2026-08-21-cmeta-producer-replay-algebra-design.md)

Defines the semantic representation of finite zero-or-more CMeta sequences:

```text
Producer
  ↓ Replay
ordered mapper applications
```

The kernel-checked model proves empty replay, singleton replay, append homomorphism, exact producer count, normalized storage count, read-guard equivalence, and map composition. A strict-C11 witness also demonstrates that producer replay works for empty and non-empty sequences while the current `CMETA_PP_NARG` and public `CMETA_PP_FOR_EACH` are deliberately poisoned.

Therefore raw variadic arity machinery is not part of the semantic finite-sequence model. `NARG`, arity-specific `FOR_EACH_1..N`, empty-token detection, and consumer-specific `WRAP_0..N` are adapter/backend mechanics when a source spelling insists on inline raw `__VA_ARGS__`.

Nested replay lanes remain a separate backend problem and are not removed by this proof.

### [CMeta C++-Like Lambda Architecture](2026-08-21-cmeta-cpp-like-lambda-design.md)

Defines C++-like lambda syntax as an optional CMeta Extend adapter over one Core finite-arity closure model:

```text
Callable<Args,R>
Lambda<Env,Args,R>
arity = Args.length
```

The formal model uses one heterogeneous finite argument schema rather than arity-specific callable/lambda type families. The migration is a direct cut-over: Lean `Callable1/2` and `Lambda1/2` are removed rather than retained as aliases, and `Callable3/4/...` or `Lambda3/4/...` are forbidden as semantic API growth.

Generator is a separate callable protocol, not a three-argument lambda. Runtime protocol parameters such as output buffers and cursors do not define logical lambda arity.

V1 surface syntax remains deliberately finite and explicit:

```text
[]
[x]
[a,b]
[x = expr]
explicit parameter types
explicit result type
```

Reference/default capture, mutable captures, and C++ generic-template lambdas are outside v1. CFlow may retain consumer-specific C macros until a separate source-API migration, but they do not own Core lambda semantics.

### [CMeta State + Exec Concurrency Architecture](2026-08-21-cmeta-state-exec-concurrency-design.md)

Defines two first-party modules:

```text
CMeta State   finite table-driven application state machines
CMeta Exec    Task / Resumable / Waitable / Executor / Scope / Coordination
```

It also defines coroutine and platform ports, the minicoro adapter boundary, and State/Task integration.

## Core architecture rules

Every future CMeta design should preserve these rules:

- Core is strict-C11 usable and does not depend on Extend or infrastructure.
- Extend changes source spelling and diagnostics, not semantic authority.
- State, Exec and CFlow remain separate semantic domains.
- External runtimes and OS APIs enter through ports/adapters.
- semantic type identity is structural and never based on display strings or descriptor addresses.
- structural identity, once present, must not be weakened by legacy string/layout fallback.
- generic generation stays finite; arbitrary compile-time user programs are not introduced.
- finite zero-or-more schemas are semantically Producers; Replay does not require arity inspection.
- raw `__VA_ARGS__`, empty-token detection and arity-specific preprocessor dispatch are adapter/backend concerns rather than semantic list primitives.
- nested preprocessor expansion lanes are orthogonal to the Producer/Replay zero-or-more model.
- parser/frontend additions must lower to existing Core/module APIs.
- C++-like lambda syntax lowers to the same Core callable ABI and does not introduce C++ template semantics.
- callable arity is data (`Args.length`), not a semantic type-family suffix.
- callable protocol is orthogonal to arity.
- formal verification follows semantic ownership boundaries.
- applicability claims require real C witnesses and multi-TU/consumer evidence where the focused spec requires them.

## Proposed future specs

Future work should be split rather than growing one monolithic document. Natural next specifications include:

```text
CMeta Raw Variadic Adapter
CMeta Nested Replay Backend
CMeta Core Demand-Driven Callable Signatures
CMeta Generic Value Identity
CMeta Container Type Identity
CMeta Extend Frontend / cmc
CMeta State Core
CMeta Exec Core
CMeta Coroutine Backend
CMeta Native Executor
```

Each implementation plan should reference exactly one focused specification plus the hexagonal architecture baseline.
