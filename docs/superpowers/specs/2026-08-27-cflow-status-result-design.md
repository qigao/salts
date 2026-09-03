# CFlow unified status/result vocabulary

## Context

Issue #125 requires new CFlow interfaces to report common failure classes
without replacing the exact protocol states already exposed by Executor,
Mailbox, Machine, Statechart, and Actor. Those domain enums are state-machine
outputs: collapsing or renumbering them would lose information and break
existing users. The synchronous Stream terminals currently expose only
`bool + const char **`, so callers cannot branch on failure class without
matching diagnostic text.

## Decision

Add `cflow_status` as an additive cross-layer result category. It covers
success, invalid input, type mismatch, unsupported semantics, bounded capacity,
allocation, cancellation, closure, execution failure, and non-blocking
backpressure. Existing domain enums remain authoritative inside their own
protocols; adapters may map them to `cflow_status` only at a higher-level API
boundary.

`cflow_status_result` contains only the status category. Its canonical text is
obtained with `cflow_status_string()` or
`cflow_status_result_message()`. Both return library-owned static storage valid
for the process lifetime. This deliberately prevents a Result from retaining a
Runtime, Source, or callback diagnostic whose owner may disappear when a
synchronous adapter closes its temporary Run.

The five bound-Stream terminals gain `_result` variants returning
`cflow_status_result` while continuing to write their value output only on
success. Existing `bool + out_error` functions remain source-compatible and
retain their detailed diagnostic behavior. Both surfaces call the same
status-producing implementation; the legacy surface projects `OK` to `true`
and all other statuses to `false`.

The three owned byte adapters follow the same rule:
`cflow_eval_array_result()`, `cflow_eval_stream_result()`, and
`cflow_eval_stream_limit_result()` return `cflow_status_result`, clear their
`cflow_result` output before admission, and transfer its allocation only on
success. Their existing `bool` forms remain compatibility projections over the
same execution. Container exposes the bounded Stream form as
`to_array_result()` while retaining `to_array()`.

## Classification

- invalid Stream, null output, invalid callable, or occupied output handle:
  `INVALID_ARGUMENT`;
- predicate signature mismatch: `TYPE_MISMATCH`;
- byte sink item bound exceeded: `CAPACITY_EXCEEDED`;
- byte sink allocation failure: `ALLOCATION_FAILED`;
- managed/non-trivial Graph admitted to a byte result: `UNSUPPORTED`;
- missing value lifecycle traits: `UNSUPPORTED`;
- count overflow: `CAPACITY_EXCEEDED`;
- failure to allocate retained `find_first` storage: `ALLOCATION_FAILED`;
- predicate, visitor, value construction, Source, Graph, Scheduler, or Runtime
  failure: `EXECUTION_ERROR`.

`CANCELLED`, `CLOSED`, and `WOULD_BLOCK` are part of the common vocabulary for
subsequent runtime/control adapters. A terminal's private cancellation after a
decisive `any_match`, `all_match`, or `find_first` value remains successful and
therefore reports `OK`.

## Compatibility and ownership

No existing enum value, struct layout, or function signature changes. The new
header and functions are additive. Structured Results never own memory and
need no destroy function. Their message accessors never expose detailed
foreign diagnostics. Legacy `out_error` remains borrowed and must not be freed;
library-generated text has static storage, while externally supplied Runtime
text follows that provider's contract.

## Verification

Tests must establish exact status classification, canonical message lifetime,
unchanged detailed legacy errors, output commit-on-success behavior, C++ header
types, Container forwarding, and installed-package consumption. Focused Release
and AddressSanitizer tests precede the adjacent CFlow/Container suites.
