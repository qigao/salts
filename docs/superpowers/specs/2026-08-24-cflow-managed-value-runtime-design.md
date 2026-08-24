# CFlow Managed Value Runtime Design

## Scope

Add the first lifecycle-aware execution slice without changing the existing
typed callable ABI. Managed values may enter a normalized source-only Graph,
remain owned by Run while in flight, and be borrowed by Sink or a transactional
CMeta Collector. Existing byte results, Plans, channels, readiness sources,
coordination, SubRun, Relation, and typed operators remain trivial-only.

## Protocol

- A managed type provides COPY, MOVE, and DESTROY traits.
- A trivial type provides TRIVIAL_COPY and TRIVIAL_DESTROY.
- Run owns every live internal slot. A successful copy creates one live value;
  slot destruction calls the type destructor exactly once.
- Source `VALUE` and `VALUE_AND_DONE` construct one value in empty output
  storage. `WAIT`, `DONE`, and `ERROR` leave output storage empty.
- Source implementations advertise `CFLOW_SOURCE_CAP_CONSTRUCTS_VALUES`.
  Managed Run admission requires this capability; trivial sources remain
  compatible without it.
- A managed Range additionally advertises `CMETA_RANGE_CONSTRUCTS_VALUES`.
  Its `next` callback follows the same output construction rule.
- Sink and Collector borrow a live value only until their callback returns.
- Failed open does not move Source ownership. Copy failure, sink rejection,
  cancellation, and close destroy every live Run slot exactly once.

## Compatibility

The change adds capability flags and does not change public structure layouts.
Legacy Source and Range callbacks remain valid for trivial types. Byte-array
results and compiled Plans remain trivial-only. Managed operator graphs fail at
admission because the current callable ABI passes inputs by value and cannot
represent borrowed non-trivial inputs safely.

## Verification

- Count copy, move, and destroy calls for an owning heap value.
- Prove source-only managed Run success and source ownership transfer.
- Prove sink rejection and cancellation/close clean live values.
- Prove managed Range collection commits an independent owned result.
- Prove managed operator graphs and legacy managed sources fail before Source
  ownership transfer.
- Preserve existing CFlow/CMeta/TurboSTL tests and public-header compilation.
