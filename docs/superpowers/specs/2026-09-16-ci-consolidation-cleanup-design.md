# CI Consolidation and Cleanup Design

## Context

The repository currently has seven GitHub Actions workflows under `.github/workflows`:

- `ci.yml`
- `cmeta-cflow-calculus.yml`
- `cmeta-shared-owner.yml`
- `cmeta.yml`
- `cstl-allocator.yml`
- `native-io-release-benchmarks.yml`
- `xml-sax.yml`

Several of these were created as issue-specific evidence gates and remain active after their originating feature work has been merged. The result is duplicated compilation, overlapping test execution, stale feature-branch triggers, and expensive performance CI running for changes that do not affect the performance surface.

The goal is to reduce the permanent workflow surface from seven workflows to four canonical workflows without losing meaningful regression coverage, while also removing avoidable dependency rebuilds from the surviving native CI.

## Design principles

1. Long-lived CI should represent stable repository capabilities, not completed issue histories.
2. Specialized regressions that remain valuable must migrate into a canonical workflow before their dedicated workflow is deleted.
3. Performance CI should run only for code or build-system changes that can materially affect the measured NativeIO/CNet path.
4. Hosted-run performance noise must not become a functional CI failure. The existing run-quality gate remains evidence qualification only.
5. Cleanup must not change production CNet, NativeIO, CSTL, XML, CMeta, or CFlow behavior.
6. No workflow is deleted until equivalent or stronger coverage is demonstrated on the cleanup branch.
7. Dependency caches may accelerate builds but must never substitute stale binaries for current-source verification.
8. Cache identity must encode every input that can change binary compatibility or dependency contents.

## Options considered

### Option A: Delete all issue-specific workflows immediately

Delete `cmeta-shared-owner.yml`, `cstl-allocator.yml`, and `xml-sax.yml` without migration.

This gives the smallest workflow list but drops sanitizer, installed-consumer, and shared-owner regression coverage. Rejected.

### Option B: Keep all workflows and only remove stale branch triggers

This preserves all coverage but retains duplicated dependency setup, build time, artifacts, and maintenance burden. It does not solve the repository-level CI fragmentation. Rejected.

### Option C: Consolidate durable coverage into canonical CI, then delete feature workflows

This retains meaningful regressions while reducing the permanent workflow count from seven to four. This is the selected cleanup design.

For build acceleration, three cache approaches were considered:

- dependency-only caching;
- dependency caching plus exact-SHA installed Salts SDK caching;
- a central producer workflow that builds Salts once and makes every other workflow depend on its artifact.

The selected cache design is dependency caching plus exact-SHA installed SDK caching. A central producer workflow is rejected because it would couple otherwise independent policy, conformance, formal, and performance workflows, while the benchmark workflow still needs a current-source instrumented build and therefore cannot safely consume a prebuilt Salts SDK as its tested implementation.

## Target workflow architecture

### 1. `ci.yml` — lightweight policy gate

Keep as a cheap fast-fail workflow for C API notation policy.

Responsibilities:

- reject C++-style generic notation in C-facing code and documentation;
- remain path-scoped to `cmeta`, `cstl`, `cflow`, docs, tests, and the workflow itself;
- remain independently dispatchable.

It must not grow into a general build workflow and does not participate in native dependency or Salts SDK caching.

### 2. `cmeta.yml` — canonical conformance workflow

Keep and extend as the repository's general native conformance CI.

Existing responsibilities remain:

- Linux release build and full Linux test suite;
- Linux sanitizer coverage for existing CFlow lifecycle paths;
- package installation and installed-target verification;
- macOS release build/tests;
- Windows release build/tests;
- Android arm64 release cross-build and package export verification;
- Lean refinement certificate integration already present in the Linux job.

Migrate the durable checks from the three issue-specific workflows into this workflow.

#### Shared-owner regression migration

Move the behavior currently protected by `cmeta-shared-owner.yml` into the Linux and Windows conformance surface.

Required retained checks:

- build the production `Salts::CMeta` target, not a test-owned implementation copy;
- build the two independent shared-library consumers in `tests/cmeta_shared_owner`;
- run `cmeta_shared_owner_test` on Linux and Windows;
- retain Linux sanitizer coverage for this regression;
- retain the installed-package consumer verification where the test uses `find_package(Salts CONFIG REQUIRED)`;
- retain the Linux static-export check where applicable.

The feature-specific push trigger for `fix/cmeta-252-shared-owner` must disappear.

#### CSTL allocator migration

The ordinary allocator tests are already part of the canonical CSTL test tree. Preserve the extra coverage that is unique to `cstl-allocator.yml`:

- Linux ASan/UBSan execution of allocator-bound Vec C/C++ tests and relevant ownership/sequence/header regressions;
- full native SDK install;
- independent installed C and C++ consumers from `cstl/tests/allocator_installed`;
- verification that allocator headers are present in the installed SDK.

This should reuse the canonical Linux build/install environment rather than bootstrapping a second standalone vcpkg checkout.

#### XML SAX migration

`xml_parser_test` and `xml_sax_parser_test` are normal repository tests. Preserve the unique sanitizer coverage from `xml-sax.yml`:

- run both DOM and SAX parser regressions under the canonical Linux sanitizer configuration;
- include `parser/**` in the `cmeta.yml` pull-request and master-push path filters so parser changes activate canonical conformance CI;
- retain exact-head source cleanliness through the canonical checkout/build process rather than a separate feature artifact workflow.

The feature-specific `feat/xml-native-sax` push trigger must disappear.

### 3. `cmeta-cflow-calculus.yml` — formal/generated-contract gate

Keep unchanged except for incidental version maintenance if required by implementation.

It owns capabilities not duplicated by the native conformance workflow:

- Lean build/test for `formal/cmeta_cflow_calculus`;
- generated CMeta signature manifest verification;
- generated CFlow operator policy verification;
- generated CFlow machine schema verification.

This remains a separate workflow because generated-code consistency is a distinct contract and should fail independently from native compilation. It does not participate in vcpkg or installed Salts SDK caching because it does not build the native dependency graph.

### 4. `native-io-release-benchmarks.yml` — performance evidence only

Keep as the sole NativeIO/CNet performance workflow.

Preserve:

- epoll, io_uring, kqueue, and IOCP matrix;
- NativeIO/NativeIPC and CNet benchmark-specific contract tests required to validate the benchmark harness;
- benchmark artifacts;
- Windows IOCP A/A control;
- IOCP TCP 1 KiB run-quality qualification;
- manual `workflow_dispatch`.

Narrow automatic path triggers so ordinary CMeta/TinyTest changes no longer start four-platform performance runs unless they also change a performance-relevant integration/build surface.

Automatic triggers should cover:

- `native-io/**`
- `cnet/**`
- `coroutine/**`
- `concurrency/**`
- `platform/**`
- `cmake/**`
- `CMakeLists.txt`
- `CMakePresets.json`
- `vcpkg.json`
- `vcpkg-configuration.json`
- `.github/workflows/native-io-release-benchmarks.yml`

Remove broad automatic triggers for:

- `cmeta/**`
- `tinytest/**`

If a future CMeta or TinyTest change is believed to affect performance, the workflow can still be run explicitly with `workflow_dispatch` or its trigger set can be changed in the same PR with justification.

## Native build cache architecture

The surviving native workflows should use a two-layer cache model. The cache is an acceleration mechanism only; test execution and current-source verification remain mandatory.

### Layer 1: shared vcpkg binary cache

All native build jobs in `cmeta.yml` and `native-io-release-benchmarks.yml` should use vcpkg binary caching on Linux, macOS, Windows, and Android where vcpkg participates in the build.

Use a repository-local binary-cache directory managed by `actions/cache`, then expose it through `VCPKG_BINARY_SOURCES` (or the equivalent supported vcpkg binary-cache environment for the platform). The cache contents are compiled dependency packages, not the Salts build tree.

The cache key must include at least:

- runner OS and architecture;
- effective target triplet/toolchain identity;
- runner/toolchain image identity when it can change ABI compatibility;
- a dependency fingerprint covering `vcpkg.json`, `vcpkg-configuration.json`, and `vcpkg-ports/**`.

The key deliberately does not include the Salts source SHA. If the dependency fingerprint and ABI-relevant platform identity are unchanged, vcpkg packages may be reused across Salts commits.

Restore fallbacks may widen only within the same platform/triplet/toolchain family. They must not allow a Windows package cache to satisfy Linux, a host package to satisfy Android, or one incompatible triplet/toolchain to satisfy another.

The existing Windows binary-cache pattern is the starting point; Linux, macOS, and Android should converge on the same cache semantics rather than keeping feature-workflow-specific archive caches.

### Layer 2: exact-SHA installed Salts SDK cache

Canonical release builds may publish/cache the installed Salts package rooted at the canonical install prefix, including headers, libraries, runtime binaries where applicable, and `lib/cmake/Salts` package metadata.

This cache exists only to accelerate independent installed-package consumers. It is not a replacement for compiling the current Salts source in any job whose purpose is source conformance or performance measurement.

The installed SDK cache key must include:

- exact Salts commit SHA;
- runner OS and architecture;
- compiler/toolchain identity;
- target triplet where applicable;
- build profile/configuration;
- dependency fingerprint.

There must be no restore fallback across Salts commit SHAs. A consumer either restores the exact-SHA SDK built for its ABI tuple or performs/uses the canonical install from the same exact source revision.

Independent consumer checks must still compile and execute their own tests after an SDK cache hit. A cache hit may skip rebuilding the provider package for a consumer-only job, but it must never skip the consumer test itself.

Within a job that has already built and installed Salts, consumers should use that in-job install directly instead of round-tripping through the cache.

### Cache consumers and non-consumers

Safe consumers of the exact-SHA SDK cache include jobs whose contract is specifically to validate the installed package from an already verified exact source revision, such as:

- CMeta installed shared-owner consumers;
- CSTL allocator installed C/C++ consumers;
- future independent package consumers that use `find_package(Salts CONFIG REQUIRED)` and do not claim to validate a newly modified Salts implementation by themselves.

The following must not replace their source build with the installed SDK cache:

- canonical Linux/macOS/Windows/Android source conformance builds;
- sanitizer builds that are meant to instrument current Salts sources;
- NativeIO/CNet release benchmarks;
- benchmark-specific internal/profile targets.

The NativeIO/CNet workflow may and should consume the shared vcpkg binary cache, but it must compile Salts and its benchmark/profile targets from the exact tested head.

### Cache observability and failure behavior

Each native job that restores a cache should make hit/miss state visible in the job log. Cache miss is not a CI failure. Cache corruption or an ABI/configuration mismatch must fall back to a normal configure/build path rather than weakening tests.

A first clean run is allowed to populate a cache. A subsequent run with the same dependency fingerprint should demonstrate vcpkg cache reuse. For installed Salts SDK reuse, only an exact-SHA, same-ABI consumer run is eligible for a hit.

Performance claims must never be based on whether cache restore happened; cache affects build/setup time only, not the benchmark protocol or measured executable provenance.

## Workflows to delete after migration

Delete only after the canonical replacement checks are present and green:

- `.github/workflows/cmeta-shared-owner.yml`
- `.github/workflows/cstl-allocator.yml`
- `.github/workflows/xml-sax.yml`

No other workflow is deleted by this cleanup. The vcpkg caches that existed solely inside these feature workflows are not preserved as separate cache schemes; their useful behavior is superseded by the canonical native cache architecture.

## Coverage invariants

The cleanup is accepted only if all of the following remain true:

1. CMeta shared-owner regression runs against production CMeta on both Linux and Windows.
2. The Linux shared-owner regression retains ASan/UBSan or equivalent canonical sanitizer coverage.
3. CSTL allocator-bound Vec C and C++ tests remain part of normal conformance testing.
4. CSTL allocator installed C/C++ consumers still build and run against the installed SDK.
5. XML DOM and incremental SAX tests remain in normal testing and are exercised under Linux sanitizer coverage.
6. Linux/macOS/Windows native conformance remains green.
7. Android arm64 package cross-build remains green.
8. Lean/generated-contract verification remains independently green.
9. NativeIO/CNet four-platform benchmark CI remains independently green when explicitly or relevantly triggered.
10. IOCP run-quality remains evidence qualification only; a noise-limited run must not fail functional CI.
11. All native workflows that build vcpkg-managed dependencies use an ABI-scoped binary cache with dependency-driven invalidation.
12. No current-source conformance or benchmark job may satisfy its Salts implementation from a different source SHA.
13. Installed Salts SDK cache reuse is exact-SHA and ABI-scoped, with no cross-SHA restore fallback.
14. Cache hits never suppress the actual regression/consumer tests that provide CI evidence.

## Trigger model after cleanup

A typical PR should run the minimum stable gates required by what changed:

- C-facing notation/docs change: `ci.yml`.
- CMeta/CSTL/CFlow/parser/build-system change: `cmeta.yml`.
- formal/generated-contract change: `cmeta-cflow-calculus.yml` in addition to `cmeta.yml` where its path filters apply.
- NativeIO/CNet/runtime-performance-path change: `native-io-release-benchmarks.yml` plus `cmeta.yml` when the native conformance paths overlap.

The repository should no longer start issue-specific CI merely because a file falls under a broad historical path filter.

## Implementation strategy

Implementation should happen on a dedicated cleanup branch and follow these ordered gates:

1. Add the migrated shared-owner regression to `cmeta.yml` and prove exact-head Linux/macOS/Windows/Android canonical conformance remains green.
2. Add the canonical native cache layer: dependency-scoped vcpkg binary caches for all native build jobs, plus exact-SHA installed Salts SDK publication/reuse boundaries for consumer-only checks.
3. Demonstrate a cold/miss-capable run and a subsequent same-dependency run with vcpkg cache reuse; verify any SDK reuse is exact-SHA only.
4. Add the migrated CSTL allocator and XML sanitizer checks to `cmeta.yml` without deleting old workflows.
5. Run the canonical conformance workflow and confirm the migrated checks execute and pass.
6. Narrow `native-io-release-benchmarks.yml` automatic path filters while preserving its current-source build and manual dispatch.
7. Delete the three issue-specific workflow files.
8. Confirm only four workflows remain under `.github/workflows`.
9. Run exact-head verification for the surviving canonical workflows that are affected by the cleanup.
10. Review the final workflow diff for duplicated checks, stale feature-branch names, temporary helper workflows/scripts, accidental production-source changes, and unsafe cache fallbacks.

If any migrated check cannot be reproduced inside canonical CI without materially weakening the contract, stop the deletion of that specific workflow and revise the design rather than silently dropping coverage.

If cache reuse requires weakening exact-head provenance, bypassing current-source compilation where it is part of the contract, or sharing artifacts across incompatible ABI tuples, reject that cache reuse rather than weakening evidence.

## Validation

The implementation PR must provide evidence for:

- exact changed-file list containing only CI/test-support/documentation files expected by the plan;
- canonical `cmeta.yml` success on the platforms it owns;
- explicit execution of the migrated shared-owner regression;
- explicit execution of allocator installed-consumer verification;
- explicit execution of XML DOM/SAX sanitizer tests;
- surviving `cmeta-cflow-calculus.yml` validity if touched;
- surviving `native-io-release-benchmarks.yml` validity after trigger/cache changes;
- a vcpkg cache miss/population path that still builds correctly;
- a later same-dependency native run showing a valid vcpkg cache restore hit;
- any installed Salts SDK restore proving exact commit SHA and matching ABI/configuration before consumer tests run;
- benchmark metadata still identifying the exact tested head, with benchmark executables built from that head rather than an installed SDK cache;
- final `.github/workflows` directory containing exactly the four canonical workflow files named above.

A workflow deletion by itself is not evidence of successful cleanup; migrated coverage must be demonstrated first. A cache hit by itself is not evidence of correctness; the associated build or consumer tests must still run and pass.

## Non-goals

- changing production runtime behavior;
- changing benchmark statistics or run-quality thresholds;
- redesigning the CMake preset hierarchy;
- reducing the platform matrix owned by canonical CI;
- deleting tests that originated in completed issues;
- deleting historical docs/specs/plans;
- introducing a monolithic single workflow for the entire repository;
- caching whole mutable build directories as a substitute for deterministic configure/build;
- reusing installed Salts SDKs across source SHAs;
- letting NativeIO/CNet benchmarks execute against a cached prebuilt Salts implementation.

## Expected final state

The permanent CI surface becomes:

```text
.github/workflows/
├── ci.yml
├── cmeta-cflow-calculus.yml
├── cmeta.yml
└── native-io-release-benchmarks.yml
```

The native build paths behind `cmeta.yml` and `native-io-release-benchmarks.yml` share dependency caches where ABI-safe, while exact-SHA installed Salts SDK caches are available only to independent installed-package consumers. Policy, conformance, formal/generated contracts, and performance evidence remain four distinct responsibilities.