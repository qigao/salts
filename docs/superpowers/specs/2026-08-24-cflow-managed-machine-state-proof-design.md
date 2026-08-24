# CFlow Managed Machine State Proof Design

## Decision

Model lifecycle-aware Machine state as a refinement of the existing commit
arbitration protocol. CMeta supplies semantic type identity and lifecycle
capabilities; CFlow remains the owner of runtime algorithms and storage.

This proof change does not implement managed C storage. It defines the resource
protocol that a later C implementation must refine.

## Scope

The model covers one committed state value, one optional staged target value,
and their resource ledger during copy, cancel, commit, and terminal disposal.
It excludes Event payload storage, observations, orthogonal regions,
communication, persistence, restart, and supervision.

## State

Each managed value has a semantic CMeta type and a unique resource token. The
resource ledger is the single fact source for whether that token is live or
destroyed. The control state contains:

- the existing orthogonal lifecycle and worker phase;
- an optional committed source value;
- an optional staged target value;
- constructed and destroyed counters;
- completed and cancelled Event counters; and
- the resource ledger.

`liveCount` is derived only from the source and staged slots. `Balanced` means
that every constructed value is either currently live in one slot or has been
destroyed.

## Operations

- Failed copy preserves the complete control state.
- Successful staging constructs one fresh target and enters `executing`.
- Cancellation before `beginCommit` destroys the staged target and preserves
  the committed source.
- `beginCommit` changes only the worker phase to `committing`.
- Commit destroys the old committed source, moves the target into the source
  slot, and accounts for one completed Event.
- Terminal disposal destroys the remaining committed source and clears the
  slot. A second disposal is rejected, proving the remaining resource cannot be
  destroyed twice through the protocol.

Every operation is pure in Lean. Later C code must map successful construction,
move, destruction, and mutex-protected commit to the corresponding transition.

## Proof obligations

1. Failed copy preserves state, ledger, and counters.
2. Cancellation before commit preserves the source token live and marks only
   the staged token destroyed.
3. Commit marks only the old source destroyed and makes the staged target the
   sole committed live value.
4. Both paths preserve resource balance.
5. Terminal disposal closes the balance with zero live slots.
6. A disposed control cannot dispose the committed value again.
7. Cancel-before-begin and begin-before-cancel retain the arbitration behavior
   already proved for byte-backed state.

## Compatibility and migration

The change adds Lean modules and test imports only. It changes no C symbol,
public structure, error code, Graph IR, generated header, or persisted format.
The later C implementation should first replace committed/staged raw byte
storage with lifecycle-aware slots while preserving the existing trivial fast
path and fail-fast admission behavior for unsupported descriptors.
