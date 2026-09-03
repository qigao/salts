# TurboSCXML Ownership Boundary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `qigao/turbo-scxml` the sole SCXML implementation while Salts retains only the format-neutral CFlow StateChart host protocol.

**Architecture:** Delete the duplicated interpreter as one atomic package-boundary change. Remove every CMake and installed-consumer route to the old target, replace the embedded CFlow README section with a migration pointer, then prove that standalone TurboSCXML consumes the resulting Salts SDK.

**Tech Stack:** C11, CMake 3.20+, CMake Presets, Ninja/MSVC, CTest, TinyTest.

**Spec:** `docs/superpowers/specs/2026-09-01-turboscxml-ownership-boundary.md`

## Global Constraints

- `TurboSCXML -> Salts::CFlow` is the only permitted dependency direction.
- Do not retain a compatibility alias, fallback implementation, or duplicate SCXML source.
- Removed options and targets must fail explicitly when requested.
- Historical design records remain in Git; current user documentation must point to TurboSCXML.

---

### Task 1: Remove the duplicated build and package surface

**Files:**
- Delete: `cflow-scxml/`
- Modify: `CMakeOptions.cmake`
- Modify: `CMakeLists.txt`
- Modify: `cmake/VerifyInstalledPackage.cmake`
- Modify: `tests/install_consumer/CMakeLists.txt`
- Modify: `tests/install_consumer/consumer.c`

**Interfaces:**
- Consumes: existing `Salts::CFlow` package target.
- Produces: a Salts package with no SCXML option, header, library, or exported target.

- [x] **Step 1: Capture the current exported surface**

Run:

```powershell
rg.exe -n "CFLOW_ENABLE_SCXML|CFlowScxml|CONSUME_CFLOW_SCXML|EXPECT_CFLOW_SCXML" CMakeLists.txt CMakeOptions.cmake cmake tests
```

Expected: references identify the optional subdirectory, install verification,
and installed consumer that must be removed.

- [x] **Step 2: Delete the duplicate module and remove every build switch**

Delete `cflow-scxml/`. Remove the `CFLOW_ENABLE_SCXML` option, conditional
`add_subdirectory`, `cflow_scxml` dependency, `EXPECT_CFLOW_SCXML` forwarding,
installed target checks, and `CONSUME_CFLOW_SCXML` consumer implementation.

- [x] **Step 3: Verify the removed build surface is absent**

Run:

```powershell
rg.exe -n "CFLOW_ENABLE_SCXML|CFlowScxml|CONSUME_CFLOW_SCXML|EXPECT_CFLOW_SCXML|SALTS_EXPECT_CFLOW_SCXML" CMakeLists.txt CMakeOptions.cmake cmake tests
```

Expected: the only match is the `CFLOW_ENABLE_SCXML` configure-time migration
guard in `CMakeOptions.cmake`; no removed target, consumer macro, or forwarding
variable remains.

### Task 2: Replace current SCXML documentation with the standalone owner

**Files:**
- Modify: `cflow/README.md`

**Interfaces:**
- Consumes: standalone `<scxml/scxml.h>` and `TurboSCXML::SCXML` package names.
- Produces: one short migration and dependency-boundary section.

- [x] **Step 1: Remove the embedded interpreter manual**

Delete the `Optional SCXML Core frontend` section through the paragraph before
`Bounded Actor lifecycle`, including obsolete `<cflow/scxml.h>` examples and
`Salts::CFlowScxml` package instructions.

- [x] **Step 2: Add the ownership pointer**

Document that CFlow is format-neutral and that SCXML users must consume
`qigao/turbo-scxml`, `<scxml/scxml.h>`, and `TurboSCXML::SCXML`.

- [x] **Step 3: Scan current documentation**

Run:

```powershell
rg.exe -n "Salts::CFlowScxml|CFLOW_ENABLE_SCXML|<cflow/scxml.h>" cflow
```

Expected: no obsolete current-documentation matches.

### Task 3: Verify both sides of the package boundary

**Files:**
- Test: Salts configured build and installed consumer.
- Test: standalone TurboSCXML configured against the generated Salts package.

**Interfaces:**
- Consumes: installed `Salts::CFlow`, `CMeta`, `XmlParser`, `Core`, and `QueryVM`.
- Produces: passing Salts and TurboSCXML build/test/package evidence.

- [x] **Step 1: Configure Salts from a clean Release tree**

First configure a disposable tree with `-DCFLOW_ENABLE_SCXML=ON`; expected:
configure fails with the TurboSCXML migration message. Then run the repository
Release configure preset without `CFLOW_ENABLE_SCXML`; expected: configure
succeeds and the cache contains no SCXML option.

- [x] **Step 2: Build and test Salts**

Build the Release preset, run all `cflow_*` tests, and run
`verify_installed_package`. Expected: all commands succeed and no installed
consumer links an SCXML target.

- [x] **Step 3: Configure and test TurboSCXML against the installed SDK**

Use the standalone repository's documented preset with `SALTS_ROOT`
pointing at the package-smoke install prefix. Build, run its focused CTest
suite, and execute its installed-package verification target. Expected: all
commands succeed using `TurboSCXML::SCXML`.

- [x] **Step 4: Commit and update PR #196**

Commit the deletion and boundary documentation, push
`feat/statechart-host-protocol`, and update PR #196 to state that
`Salts::CFlowScxml` is intentionally removed in favor of TurboSCXML.
