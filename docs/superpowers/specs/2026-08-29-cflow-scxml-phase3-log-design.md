# CFlow SCXML Phase 3 Log Executable Content Design

**Issue:** [qigao/turbo-utils#122](https://github.com/qigao/turbo-utils/issues/122)

**Date:** 2026-08-29

**Reference semantics:** [W3C SCXML 1.0](https://www.w3.org/TR/scxml/#log)

## Scope

This increment adds the SCXML `log` executable element to the optional
`TurboUtils::CFlowScxml` null-data-model frontend. A `log` step emits its
optional `label` through TurboUtils tlog at DEBUG level and then continues the
same executable block. It is legal in every currently supported executable
owner and inside conditional partitions.

The increment does not add a value-expression language. W3C SCXML Appendix
B.1 defines no value expressions for the null data model, so a present `expr`
attribute is rejected during compilation. Assignment, iteration, system
variables, and non-null data models remain outside this increment.

## Standards-derived behavior

W3C SCXML 1.0 section 4.7 makes both `label` and `expr` optional, permits no
children, leaves the display mechanism platform-dependent, and requires log
execution to have no side effects on document interpretation. Appendix B.1
defines no value-expression language for the null data model.

Consequently this frontend:

- accepts `log` with an optional unqualified `label` and no `expr`;
- treats a missing label as the empty string;
- rejects a present `expr` with `CFLOW_SCXML_UNSUPPORTED_FEATURE` at that
  attribute;
- rejects element/text children and unsupported unqualified attributes;
- emits one DEBUG record in document order when the step executes; and
- never converts logger absence, filtering, queue pressure, or dropped output
  into a Statechart failure.

## Existing boundaries and evidence

- CFlow Statechart remains format-neutral and does not depend on tlog.
- `TurboUtils::CFlowScxml` is a static optional frontend currently linking
  `TurboUtils::CFlow` and `TurboUtils::XmlParser`.
- tlog is exported by `TurboUtils::Core`; `TLOG_DEBUG` uses
  `tlog_peek_default()`, so it neither constructs a default logger nor emits
  when none is configured.
- SCXML programs own immutable executable steps after the XML document is
  destroyed. Any retained label therefore needs program-owned storage.
- The existing `max_name_bytes` limit is the public bound for retained SCXML
  strings and is the correct shared budget for state names, event names, and
  log labels.

## Alternatives

### Add a public host callback

Rejected for this increment after user direction. It offers per-instance sink
selection but adds public compile/runtime configuration solely for a
platform-dependent debug facility. tlog is already a repository-owned logging
boundary and can satisfy the null-model feature without a new API.

### Log inside the native CFlow runtime

Rejected. The native runtime must not know XML elements or acquire a tlog
dependency. Only the SCXML contextual executable callback recognizes and emits
the `LOG` step.

### Use INFO and auto-create the default logger

Rejected. Statecharts may execute actions at high frequency. DEBUG honors the
logging emission budget and `tlog_peek_default()` avoids hidden logger
lifecycle, console output, allocation, and shutdown ownership.

## Chosen design

Add `SCXML_ELEMENT_LOG` to XML admission and `SCXML_STEP_LOG` to the immutable
block IR. Admission counts `label.size + 1` with checked arithmetic. The build
allocates one bounded label buffer, copies each decoded XML label with a NUL
terminator during emission, and transfers the buffer to the owning program.
Each log step points only into that stable program buffer.

At execution, the contextual SCXML block callback performs:

```c
TURBO_LOG_DEBUG(tlog_peek_default(), "cflow.scxml", step->label);
```

The macro is deliberately not followed by a flush or status check. Logger
filtering and delivery are observability behavior, not Statechart transaction
state. The callback then advances to the next step exactly as it does for a
successful raise or conditional.

The application owns the default logger. tlog synchronizes access to the
default pointer but does not retain the pointed-to logger, so an installed
logger must remain alive until every executor that could run an SCXML `log`
step is quiescent. Concurrent replacement followed by destruction while a
default-log call is in flight violates the tlog object-lifetime contract; the
SCXML frontend neither acquires ownership nor attempts to repair that invalid
lifecycle.

An executable block containing at least one log step adds `CMETA_EFFECT_IO` to
its existing `CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL` descriptor. It
retains deterministic/no-alias properties because document-order calls and
state output are deterministic even though the external sink may filter or
drop DEBUG records.

## Ownership and limits

- XML input and XML views remain borrowed only during compilation.
- The program exclusively owns the label buffer and frees it after all
  instances using program bindings are quiescent.
- Step label pointers are immutable and valid exactly as long as the program.
- Every label consumes decoded byte length plus one terminator from
  `max_name_bytes`; state/event names and labels share that one hard limit.
- All additions and allocations use the compiler's checked arithmetic and
  transactional cleanup path.

## Dependency and compatibility impact

- **MED — dependency:** `TurboUtils::CFlowScxml` gains a private/link-only
  dependency on `TurboUtils::Core`, because Core owns tlog. Public headers do
  not expose Core types. `TurboUtils::CFlow` remains independent of XML and
  tlog.
- **MED — accepted syntax:** documents containing label-only `log` change from
  unsupported to executable. A present `expr` remains fail-fast unsupported
  under the null data model.
- **LOW — log volume:** each executed element attempts one DEBUG record.
  Production filtering/backpressure belongs to the application-configured
  logger; the frontend never samples, retries, flushes, or promotes the level.
- **LOW — ABI:** no public struct or function changes. Internal step layout and
  the static frontend's private dependency change only require normal relink.

## Verification

Focused TinyTest coverage captures a real tlog callback sink and proves exact
label bytes, component, DEBUG level, document order, conditional selection,
and continuation into later executable content. Separate cases prove a missing
default logger is a successful no-op, `expr` is rejected, children and unknown
attributes are rejected, and combined retained strings respect
`max_name_bytes`.

Verification then runs the CFlowScxml target/test, tlog tests, enabled and
disabled installed-package consumers, and the complete MSVC Release suite.
The package test must demonstrate that the new private/link-only Core
dependency is exported and resolved without changing the public SCXML header.
