# CFlow Inline Reactive I/O Implementation Plan

1. Add failing Scheduler tests for the public inline initializer, immediate
   execution, delayed rejection, shutdown, and descriptor finalization.
2. Change the existing I/O Publisher synchronous-drive tests to require drive
   credit coalescing instead of a reentrant `SALTS_EBUSY` callback, and observe
   the focused test failure.
3. Implement the bounded-state Inline Scheduler in `scheduler.c` and expose its
   contract in `scheduler.h`; admit internal task descriptors so finalize runs
   exactly once.
4. Suppress external drive invocation while the Publisher owner is active;
   retain the existing tail-generation handoff.
5. Bind the NativeIO adapter benchmark to the bounded caller-driven Scheduler
   and batched NativeIO/Publisher drive composition. Preserve Direct as the only
   performance baseline and preserve payload/operation accounting.
6. Update CFlow documentation with execution-policy and callback-thread rules.
7. Build and run focused Scheduler/I/O Publisher tests, the adapter benchmark,
   then the adjacent CFlow regression set on Windows. Record Linux as a merge
   gate if it cannot be run locally.
