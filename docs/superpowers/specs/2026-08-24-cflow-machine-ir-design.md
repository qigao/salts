# CFlow Typed Machine IR Design

**Status:** Approved for implementation under GitHub issue #63  
**Date:** 2026-08-24

## Context

CFlow has a typed Event schema and bounded MPSC Mailbox, while Graph and Plan
describe collection pipelines. Neither is the semantic state-machine IR needed
by the execution-model roadmap. Machine must be defined and validated before
issue #64 binds it to Resumable and SerialExecutor.

This phase adds no scheduler, event loop, callback invocation, serialization,
or runtime instance. It defines the immutable facts that those layers consume.

## Decision

`cflow_machine_definition` is a borrowed construction input containing finite
arrays of state, Event, guard, action, and transition declarations. A successful
`cflow_machine_build` copies, sorts, validates, and atomically publishes an
opaque immutable `cflow_machine`. A failed build leaves the output empty. A
non-empty output is rejected so a caller cannot accidentally overwrite a live
Machine.

The Machine owns copied declaration arrays. It borrows every CMeta type
descriptor until `cflow_machine_destroy`. Stable numeric identifiers, rather
than descriptor addresses or array positions, are the semantic identities.

## Semantic schema

### States

Each non-zero state ID has one value type and one kind:

- `ACTIVE`: accepts transitions;
- `DONE`: successful absorbing terminal state;
- `ERROR`: error absorbing terminal state.

The initial state must name a declared state. An initial terminal state is
valid. Empty state sets are invalid. Every declared state must be reachable
from the initial state through transition targets; otherwise the definition is
rejected instead of retaining dead semantic facts.

### Events

Machine reuses `cflow_event_type`: each non-zero Event ID maps to one valid,
non-empty CMeta payload type. IDs are unique. Machine copies the schema rows and
borrows their descriptors. Mailbox ownership and payload-trait restrictions
remain the Mailbox's concern; Machine IR validates semantic type identity only.

### Guards

A non-zero guard ID declares its source-state type, Event ID and Event payload
type. Guards belong to the admitted deterministic fragment only when they are
pure, deterministic, total, and no-alias. Runtime bindings added by #64 must
match this declaration exactly.

Guard ID zero is the built-in unconditional guard and has no declaration row.

### Actions

A non-zero action ID declares source-state, Event, and target-state types,
CMeta effects/properties, and one observation mode:

- `NONE`: state transition only;
- `VALUE`: emit one value of the declared output type;
- `EVENT`: emit one declared Machine Event.

Action declarations may carry `CMETA_EFFECT_MAY_FAIL`. Runtime bindings added
by #64 must match the declaration. Action ID zero is the built-in identity/no-
observation action and is valid only when source and target state types are
equal.

### Transitions

A transition names source state, input Event, optional guard, optional action,
target state, and a numeric priority. Transitions are normalized by
`(source, event, priority)`.

For one `(source, event)` pair, priorities must be unique. At execution, the
lowest numeric priority whose guard is enabled wins. Multiple enabled guards
at different priorities are therefore ordered, while equal-priority ambiguity
is rejected at build time. Terminal states cannot own outgoing transitions.

Every guard and action declaration must be referenced. This prevents the
immutable Machine from becoming a second, partially live fact source.

## Small-step contract

The Lean semantic evaluator receives an already typed Event, a guard valuation,
and an action result for the selected action.

1. Candidate transitions match the current state and Event ID.
2. The lowest-priority enabled candidate is selected.
3. Entering the step consumes the Event exactly once.
4. With no enabled transition, the state value is unchanged and the Machine
   enters an error terminal outcome.
5. On action success, observations are appended in order, the target state is
   committed once, the state observation is emitted, and a DONE/ERROR target
   emits its terminal observation.
6. On action failure, no target state is committed, the source state remains
   authoritative, the first action error is observed, and the Machine enters
   the error terminal outcome.
7. Terminal configurations admit no later step.

The observable trace contains emitted values, emitted Events, committed state
IDs, errors, and successful termination. Scheduler identities and serialization
bytes are intentionally absent.

## Validation and normalization

Build uses temporary allocations only. It validates counts and checked byte
arithmetic before dereferencing input arrays, then copies and sorts each finite
domain. Validation covers:

- null/count disagreement and allocation overflow;
- zero or duplicate IDs;
- invalid state/Event/guard/action type descriptors;
- unknown initial/source/target/Event/guard/action IDs;
- state/Event/source/target type mismatch;
- invalid guard effects/properties and invalid action observation declarations;
- outgoing transitions from terminal states;
- duplicate `(source, event, priority)` rows;
- unreachable states and unused guard/action rows.

Only after every check passes is the temporary representation assigned to the
caller. Destroy is idempotent and requires control-plane quiescence; immutable
Machine reads are safe concurrently when borrowed descriptors remain alive.

## C and Lean schema alignment

Lean owns the finite enum manifest for state kinds, action observation kinds,
and schema version. `cflow-machine-schema-gen` renders
`cflow/include/cflow/generated/machine_schema.h`; C enums replay those rows.
CI runs the generator in `--check` mode, and C conformance tests assert the
generated counts/version. This makes schema drift a build-time failure without
claiming a full C memory-model refinement theorem.

## Resource protocol

- Data unit: one immutable declaration row or transition row.
- Fact source: the successfully built `cflow_machine` arrays.
- Input lifetime: arrays are borrowed only during build; descriptors are
  borrowed through destroy.
- Owner: one caller owns and destroys the Machine; readers borrow it.
- Topology: construction/destruction are single-owner control-plane operations;
  post-build queries are read-only and may be concurrent.
- Capacity: all arrays are exact-sized, checked allocations; no post-build
  growth or allocation occurs.
- Backpressure: not applicable in semantic IR; Event capacity belongs to
  Mailbox.
- Failure: explicit `cflow_machine_status`; failed build publishes nothing.
- Shutdown: destroy requires readers quiescent and clears the handle.
- Observation: query functions expose canonical sorted rows; execution traces
  are defined in Lean and refined by #64.

## Compatibility and migration

Existing Graph, Plan, Event/Mailbox, and runtime APIs are unchanged. Machine is
a sibling semantic IR, not a replacement for Plan. Issue #64 will bind Machine
declarations to callbacks and existing execution primitives; it must not add
function pointers, scheduler ownership, or protocol formats to this core.

## Verification

C tests cover canonical valid construction, empty definitions, every unknown
reference/type mismatch class, equal-priority ambiguity, terminal outgoing
edges, unreachable states, unused declarations, error-capable actions,
transactional failure, immutable queries, and C++ header compatibility. Lean
tests cover selection order, no-transition error, action failure consumption,
terminal absorption, state/Event typing preservation, determinism, and trace
contents. Release, ASan, complete CTest, generator checks, and `lake test` form
the delivery gate.
