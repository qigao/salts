# CMeta Language Reference and MSVC CI Design

## Purpose

CMeta is a pragmatic modern-C dialect/toolkit built on strict C11, preprocessor schemas, finite generic routing, metadata descriptors, and ordinary C runtime libraries. Its goal is fast, convenient, composable C programming—not to reproduce C++ templates, Rust traits, a general-purpose macro language, or a universal formal semantics.

The project should evolve by combining small proven patterns. New syntax is added only when it is implementable with the existing CMeta mechanisms or with a small extension that preserves ordinary C interoperation.

## Formal proof stopping policy

The verified Plan B M7g checkpoint is the current formalization stopping point. It is sufficient to support the implemented CMeta syntax and the Producer/replay/backend/registry semantics used by current code.

Further formal work is optional and must not block normal CMeta/CFlow product development unless a future feature depends on a property that is not already covered. The remaining useful ideas are:

- M8 root-module isolation and `CMeta.InternalChecks` packaging;
- deeper theorem-surface minimization beyond the current module contracts;
- stronger completeness or maximality claims for preprocessor behavior.

These are TODOs, not release gates. Existing GCC/Clang C-to-Lean witness checks remain useful regression evidence and should continue running.

There is no MSVC Lean/proof lane in this design. MSVC is used to validate the CMeta/CFlow C implementation and DSL portability. Lean's own Windows toolchain/build support belongs to the Lean toolchain project and is not coupled to CMeta's MSVC compiler lane.

## Language surface

The authoritative language reference is `cmeta/LANGUAGE_REFERENCE.md` and is divided into four layers.

### Application DSL

Stable user-facing declaration vocabulary:

- `Struct(...)`
- `Enum(...)`
- `Traits(...)`
- `typed(...)`
- `typed_any(...)`
- `interface(...)`
- `implements(...)`

`typed(...)` is the only generic/container instantiation entry point. `Containers(...)` is removed and must not be retained as an alias.

### Framework DSL

Framework-author generation vocabulary:

- `Schema(...)`
- `Replay(...)`
- `Operators(...)`

These are code-generation/schema tools, not additional application type systems.

### Runtime Protocol

Ordinary C runtime/meta protocols include type descriptors and type identity, callable metadata, ranges, collectors, interface vtables/capabilities, and container descriptors. They are APIs rather than new language keywords.

### Reserved future syntax

Names such as `Lambda`, `Bind`, `Variant`, `Match`, `Array`, `SmallVec`, and `RingBuffer` may be discussed as future directions. Reservation is not a compatibility promise and does not imply implementation. New vocabulary should be promoted to the Application or Framework DSL only after a concrete implementation exists.

## Removed syntax

`Containers(...)` is removed. Multiple declarations are written explicitly:

```c
typed(List, UserList, User);
typed(Vec, UserVec, User);
typed(HashMap, UsersById, int, User);
```

The removed container `implement(...)` / declaration-then-implementation model also remains removed.

## MSVC CI role

MSVC support is a portability/conformance lane, not a formally certified preprocessor backend and not a Lean build lane.

A Windows `windows-latest` job should:

1. enter an MSVC developer environment;
2. configure the repository through a dedicated `formal-windows-msvc` CMake preset;
3. build portable CMeta/CFlow witness targets that do not encode GCC/Clang nested-replay certification;
4. run portable applicability/conformance executables and CTest.

The MSVC lane must not invoke `elan`, `lean`, or `lake`, and must not generate or compare `NestedReplay*GeneratedC.lean` backend certificates. The existing GCC and Clang jobs remain the backend-certified Lean lanes.

## CMake boundary

`formal/CMakeLists.txt` should explicitly distinguish backend-certified proof targets from portable conformance targets.

- GNU and Clang keep the direct-nested-replay negative probe, generated namespace selection, and `cmeta_nested_replay_deferred_witness`.
- MSVC skips that backend-certificate slice with an explicit status message instead of failing configuration.
- Other unknown compiler families continue to fail the formal-only configuration unless intentionally supported.

The Windows preset should reuse the repository's Windows/MSVC flags but rely on the CI developer environment for `cl`, rather than embedding GitHub-runner installation paths in YAML.

## Documentation contract

`cmeta/README.md` should point to `LANGUAGE_REFERENCE.md` and stay an overview. `C_META_CAPABILITIES.md` remains a capability inventory and must agree with the reference that `typed(...)` is the sole generic/container declaration entry.

## Non-goals

- proving every CMeta behavior before implementation;
- proving an absolute maximum nested-replay depth;
- treating Lean as the source language for CMeta;
- introducing syntax merely to imitate C++/Rust features;
- keeping compatibility aliases for removed experimental DSL words;
- adding an MSVC Lean/proof lane;
- making CMeta's MSVC portability CI responsible for Lean's own Windows toolchain validation.
