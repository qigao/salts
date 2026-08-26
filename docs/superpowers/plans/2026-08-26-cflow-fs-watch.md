# CFlow Filesystem Watch Implementation Plan

**Goal:** Deliver issue #113 as a bounded native event source in
`TurboUtils::CFlowFS`.

**Spec:** `docs/superpowers/specs/2026-08-26-cflow-fs-watch-design.md`

### Task 1: Contract and fake backend

- [x] Define the opaque source, event/capability types, config, statistics,
  rescan acknowledgement, close, drive, and destroy API.
- [ ] Add a fake backend and RED tests for ownership, bounds, overflow
  coalescing, callback affinity, and lifecycle.
- [x] Implement the bounded backend-neutral state machine GREEN.

### Task 2: Native bridges

- [x] Implement and test Windows overlapped `ReadDirectoryChangesW` parsing,
  cancellation, rename pairing, overflow, and root removal.
- [ ] Implement and test Linux inotify recursion, dynamic watch maintenance,
  cookies, `IN_Q_OVERFLOW`, and root removal.
- [ ] Implement and test macOS FSEvents stream/run-loop shutdown and dropped or
  coalesced event flags.

### Task 3: Verification

- [ ] Run native Release tests on Windows, Linux, and macOS.
- [ ] Run CFlowFS control regressions, install/export checks, and lifecycle
  review before commit.
