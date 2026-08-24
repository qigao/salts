# CFlow Executor Protocol Proof Design

## Decision

Model Executor admission, execution, and shutdown as a finite accounting
protocol, then compose its accepted/start transitions with the existing Machine
worker phases. The model is a specification for later runtime hardening; this
change does not alter the C Executor interface or implementation.

## Scope

The protocol covers bounded non-blocking admission, queued and active work,
serial execution, task completion or cancellation, shutdown admission closure,
drain and cancel-pending policies, and callback-context blocking restrictions.
It excludes task payloads, task ordering, priorities, timers, communication,
operating-system scheduling, persistence, restart, and supervision.

## State and ownership

The Executor task ledger is the single fact source for task accounting. Each
accepted task occupies exactly one ledger entry whose phase is `queued`,
`running`, `completed`, or `cancelled`:

- `accepted` is ledger length;
- `queued` and `running` are derived non-terminal counts;
- `completed` and `cancelled` are derived, mutually exclusive terminal counts;
- `rejectedFull`, `rejectedClosed`, and `rejectedWouldBlock` record explicit
  failed admissions;
- `capacity` bounds only `queued`, matching the current thread-pool queue; and
- lifecycle is `open`, `closing`, or `closed`.

`Conserved` means
`accepted = queued + running + completed + cancelled`. `Bounded` means
`queued <= capacity`. Serial executors additionally require `running <= 1`.
These predicates describe safety, not temporal progress.

## Operations and error semantics

- `tryPost` accepts only while open and below queue capacity; otherwise it
  returns a distinct full or closed result without changing task accounting.
- Blocking `post` from the same Executor callback context returns `wouldBlock`
  when capacity is full; `waitIdle` from that context always returns
  `wouldBlock`. The protocol never models actual self-wait as valid.
- `start` moves one queued task to running. Serial start is rejected while one
  task is already running.
- `finish` moves one running task to completed.
- `beginShutdown` atomically ends admission and enters `closing`.
- Drain shutdown preserves queued tasks for later start/finish settlement.
- Cancel-pending shutdown moves every queued task to `cancelled`; already
  running tasks still finish.
- `close` succeeds only when queued and running are both zero.

Rejected operations preserve the task ledger. They may increment the
corresponding rejection counter so diagnostics remain attached to the same
state without becoming a second task fact source.

## Machine composition

The existing Machine worker phase is refined through a paired state:

- Machine `idle -> scheduled` occurs only with Executor `tryPost = accepted`;
- Machine `scheduled -> executing` occurs only with Executor `start = started`;
- admission rejection leaves the Machine idle; and
- finishing the Executor task does not replace Machine commit arbitration,
  which remains owned by `MachineRuntime`.

This composition proves that `scheduled` has an accepted queued task and that
`executing` has a started running task. It does not introduce communication or
make Executor own Machine domain state.

## Proof obligations

1. Initial state is bounded, conserved, serial-safe, and quiescent.
2. Accepted admission preserves boundedness and conservation.
3. Full, closed, and would-block rejection preserve accepted-task accounting.
4. Start and finish preserve conservation; serial start never creates two
   active tasks.
5. Drain and cancel-pending shutdown reject subsequent admission.
6. Cancel-pending settlement and drain settlement each leave every accepted
   task in exactly one of queued, running, completed, or cancelled.
7. Closed state is quiescent.
8. Machine scheduling/execution phases are reachable only through Executor
   acceptance/start transitions.

No unconditional liveness theorem is claimed. A later liveness model may prove
progress only under explicit drive/fairness and task-termination premises.

## Compatibility, migration, and rollback

The change adds Lean modules, executable proof tests, and documentation only.
It changes no C symbol, ABI, Graph IR, error code, build dependency, generated
header, or serialized format. A later C migration can refine each backend
against this protocol independently. Reverting these formal files fully rolls
back the change because no runtime path consumes them.

## Verification

Run focused model/proof/test builds, then serial `lake build` and `lake test`.
Scan changed Lean modules for `sorry`, `axiom`, `admit`, or `unsafe`, run
`git diff --check`, and synchronize CodeGraph. Lake is authoritative where the
current CodeGraph analyzer cannot derive Lean test impact.
