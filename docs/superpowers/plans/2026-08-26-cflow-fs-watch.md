# CFlow Filesystem Watch Implementation Plan

**Goal:** Deliver issue #113 as a bounded native event source in
`TurboUtils::CFlowFS`.

**Spec:** `docs/superpowers/specs/2026-08-26-cflow-fs-watch-design.md`

### Task 1: Contract and bounded state machine

- [x] Define the opaque source, event/capability types, config, statistics,
  rescan acknowledgement, close, drive, and destroy API.
- [x] Add C/C++ contract and native lifecycle tests for ownership, bounds,
  overflow coalescing, callback delivery, and shutdown.
- [x] Implement the bounded backend-neutral state machine GREEN.

### Task 2: Native bridges

- [x] Implement and test Windows overlapped `ReadDirectoryChangesW` parsing,
  cancellation, rename pairing, overflow, and root removal.
- [x] Implement and test Linux inotify recursion, dynamic watch maintenance,
  cookies, `IN_Q_OVERFLOW`, and root removal.
- [x] Implement and test macOS FSEvents dispatch-queue shutdown and dropped,
  coalesced, or unpairable event flags.

### Task 3: Verification

- [x] Run native Release tests on Windows, Linux, and macOS.
- [x] Run CFlowFS control regressions, install/export checks, and lifecycle
  review before commit.
