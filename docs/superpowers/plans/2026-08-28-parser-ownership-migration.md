# Parser Ownership Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and install the moved low-level parsers from Salts and make TurboParser consume those installed targets without duplicating parser sources.

**Architecture:** Salts exports component parser targets and a separate opt-in JSON-to-CSerde adapter. TurboParser retains its facade, XML vendor, Mustache, Cron, TBE, DataBind, and compiler layers, consumes only the installed parser components, and does not depend on CBind or CSerde.

**Tech Stack:** C11/C++17, CMake 3.20+, CMake Presets, re2c, Lemon, TinyTest, vcpkg.

**Spec:** `docs/superpowers/specs/2026-08-28-parser-ownership-migration.md`

## Global Constraints

- Preserve all C parser APIs, ownership, error codes, accepted syntax, and serialized data.
- Keep `cbind/` and `cserde/` in Salts; do not expose either through TurboParser.
- Use only installed `Salts::*` targets across the repository boundary.
- Do not copy parser sources or fall back to a source-tree dependency.
- Do not modify the existing CFlow working-tree changes.
- Configure, build, test, and install through version-controlled user presets.

---

### Task 1: Installed parser contract test

**Files:**
- Modify: `tests/install_consumer/CMakeLists.txt`
- Modify: `tests/install_consumer/consumer.c`

**Interfaces:**
- Consumes: installed `Salts::JsonCSerdeAdapter`, `Salts::CBind`, and `json_cserde_reader_create`.
- Produces: `consume_json_cbind`, an external consumer that parses JSON and decodes it through CSerde/CBind.

- [x] Add `consume_json_cbind` linked to `Salts::JsonCSerdeAdapter`, `Salts::CBind`, and `Salts::Core`.
- [x] Add a `CONSUME_JSON_CBIND` branch that parses `{"value":7}`, creates a JSON CSerde reader, decodes a CMeta-described C struct with `cbind_decode`, and releases reader/document ownership.
- [x] Configure the install consumer against the current installed package and confirm failure because `Salts::JsonParser` is absent.

### Task 2: Salts parser ownership and export

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `parser/CMakeLists.txt`
- Modify: `parser/*/CMakeLists.txt`

**Interfaces:**
- Consumes: `Salts::Core`, `Salts::CSTL`, and `Salts::TinyTest`; only the separate JSON CSerde adapter consumes `Salts::CSerde`.
- Produces: the component targets listed in the design spec under the `Salts::` namespace.

- [x] Add `parser/query_vm` within the parser subtree and add the parser subtree after Core/STL/CSerde/CBind exist.
- [x] Rename build-tree aliases and internal component links from `TurboParser::*` to `Salts::*`.
- [x] Export every cross-repository static target through `SaltsTargets` with stable `EXPORT_NAME` values.
- [x] Install public headers at include paths matching every target's install interface, including `json_cserde_reader.h` for `Salts::JsonCSerdeAdapter` and the CYaml JSON adapter header.
- [x] Reconfigure Salts and build `test_query_vm`, `test_json_parser`, and `test_json_cserde_reader`.

### Task 3: TurboParser installed dependency migration

**Files:**
- Modify: `C:\projects\cpp\turbonet\turbo-parser\CMakeLists.txt`
- Modify: `C:\projects\cpp\turbonet\turbo-parser\salts_parser\CMakeLists.txt`
- Modify: `C:\projects\cpp\turbonet\turbo-parser\mustache\CMakeLists.txt`
- Modify: `C:\projects\cpp\turbonet\turbo-parser\mustache\test\CMakeLists.txt`
- Modify: `C:\projects\cpp\turbonet\turbo-parser\mustache\benchmarks\CMakeLists.txt`
- Modify: `C:\projects\cpp\turbonet\turbo-parser\vendor\cxml\CMakeLists.txt`
- Modify: `C:\projects\cpp\turbonet\turbo-parser\tools\junit_to_html\CMakeLists.txt`
- Modify: `C:\projects\cpp\turbonet\turbo-parser\tbe\tbe_compiler\CMakeLists.txt`

**Interfaces:**
- Consumes: installed Salts parser component targets.
- Produces: the unchanged `TurboParser::Parser`, `TurboParser::Mustache`, `TurboParser::DataBind`, and TBE compiler interfaces.

- [x] Remove the deleted parser subdirectories from the TurboParser root and fail configuration if required Salts parser targets are absent.
- [x] Replace local parser target names and low-level `TurboParser::*` aliases with their `Salts::*` counterparts.
- [x] Remove source-tree parser include paths and rely on exported include directories.
- [x] Move the Windows process/CRT environment bridge behind the installed DotEnv/CmdParser public boundary.
- [x] Configure and build TurboParser against the newly installed Salts package.

### Task 4: Regression and package verification

**Files:**
- Inspect: generated `SaltsTargets.cmake`
- Inspect: generated `TurboParserTargets.cmake`

**Interfaces:**
- Consumes: both installed package configurations.
- Produces: repeatable evidence that the new dependency direction works without source paths.

- [x] Run focused Salts parser/CSerde/CBind tests.
- [x] Run the full Salts CTest preset and install preset.
- [x] Configure, build, and run the Salts external install consumer.
- [x] Run the full TurboParser configure, build, CTest, and install flow against that SDK.
- [x] Search generated exports for source-tree paths and obsolete low-level `TurboParser::*` target references; fail the verification if either appears.
- [x] Review `git diff --check` and both repository status outputs, confirming the pre-existing CFlow changes remain untouched.
