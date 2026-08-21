# CMeta Architecture Specifications

This directory contains the architecture specifications for CMeta work on branch `leanv4`.

## Authority order

When documents overlap, use this order:

1. **CMeta Hexagonal Architecture** — global dependency and ownership rules;
2. **domain specifications** — Type Application, Type Identity Applicability, State/Exec, and later modules;
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
- parser/frontend additions must lower to existing Core/module APIs.
- formal verification follows semantic ownership boundaries.
- applicability claims require real C witnesses and multi-TU/consumer evidence where the focused spec requires them.

## Proposed future specs

Future work should be split rather than growing one monolithic document. Natural next specifications include:

```text
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
