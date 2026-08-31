# CFlow Statechart Host Transaction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace incremental V1-V3 composition with one lazy, bounded, transactional V4-only Statechart host callback.

**Architecture:** The Statechart instance remains the single owner of state, configuration, queues, and effect tickets. An opaque call-scoped host context exposes read-only trigger/configuration access and lazy transactional writes in trigger and quiescence phases. The public hook table has one exact V4 shape and no legacy runtime paths.

**Tech Stack:** C11, CMeta managed values, CFlow Statechart, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-09-01-cflow-statechart-host-transaction.md`

### Task 1: Lock the V4 public contract with RED tests

**Files:**
- Modify: `cflow/include/cflow/statechart_instance.h`
- Modify: `cflow/tests/cflow_statechart_instance_test.c`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

- [x] Declare the phase/result enums, opaque context accessors, callback type,
  ABI V4 constant, and the sole V4 callback field.
- [x] Add a managed-state fixture proving `PREPARE_TRIGGER` commits an edit
  before a guard reads it and receives the exact external trigger metadata.
- [x] Add fixtures for quiescence, true no-op copy avoidance, external `DROP`,
  `FATAL` rollback, and rejection of non-V4 hook tables.
- [x] Build the focused targets and record the expected link/test failures
  before production implementation.

### Task 2: Implement one internal host transaction engine

**Files:**
- Modify: `cflow/src/statechart_instance.c`

- [x] Add the private context state and public accessors with call-scope,
  phase, null, and capacity validation.
- [x] Factor state-copy, Event staging, effect staging, commit, and rollback so
  trigger and quiescence phases use one implementation.
- [x] Keep state copying lazy; `CONTINUE` with no staged work must not publish a
  state slot or advance the configuration version.
- [x] Make every successful ticket stage reach exactly one commit/discard
  callback and clear all staging counters on every exit.
- [x] Insert `PREPARE_TRIGGER` before guard selection for external, internal,
  and completion triggers; permit `DROP` only for external Events.
- [x] Insert `PREPARE_QUIESCENCE` before settlement and retain existing
  internal/Eventless drain ordering.

### Task 3: Remove and verify the legacy adapter boundary

**Files:**
- Modify: `cflow/src/statechart_instance.c`
- Modify: `cflow/tests/cflow_statechart_instance_test.c`

- [x] Require the exact V4 hook-table shape and delete V1-V3 prefixes,
  callback fields, copying logic, and execution branches.
- [x] Migrate Statechart tests and the installed consumer to V4, and prove
  every pre-V4 ABI is rejected.
- [x] Verify invalid result, invalid phase action, state-copy failure, internal
  queue full, and effect journal full fail fast with deterministic status.
- [x] Run the focused TinyTest filter, full Statechart instance test, C++
  header test, `git diff --check`, and CodeGraph affected analysis.
