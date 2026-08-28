# CFlow pipe rendezvous and subprocess implementation plan

**Goal:** Complete #133 without changing ownership of the existing typed
byte-pipe operations or introducing a hidden blocking worker.

**Architecture:** Keep connected-byte I/O in CFlow, add an explicit
rendezvous control plane, extend Core process spawning with borrowed stdio
bindings, and combine the two existing owners in the separate
`TurboUtils::CFlowProcess` adapter target.

## Tasks

1. Core borrowed stdio spawn
   - Add conflict validation and platform-specific borrowed-handle binding.
   - Prove the caller retains ownership and existing capture behavior remains
     unchanged with real child-process tests.

2. CFlow rendezvous
   - Add capability discovery, endpoint ownership helpers, bounded overlapped
     Windows accept, single-attempt Windows connect, and POSIX FIFO open.
   - Test connection races, capacity, cancellation, close/drain, missing/busy
     mapping, FIFO rendezvous, and stale-`errno` independence.

3. CFlowProcess adapter
   - Own the Core process plus three asynchronous parent pipe endpoints and one
     bounded Actor/backend/Executor assembly.
   - Test stdin/stdout/stderr, partial reads, EOF, child exit, cancellation,
     explicit stdin close, retained statistics, and resource-count stability.

4. Public and package surface
   - Compile new headers as C and C++.
   - Export and consume `TurboUtils::CFlowProcess` from an installed package.
   - Publish capability, ownership, threading, error, and shutdown contracts.

5. Verification
   - Run focused Release tests on Windows.
   - Run focused ASan Debug tests on remote Linux `root@eu`.
   - Run adjacent native I/O and Core process regressions, `diff --check`, and
     installed-package smoke verification before commit/PR.
