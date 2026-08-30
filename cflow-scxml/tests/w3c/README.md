# W3C-derived SCXML regression fixtures

These fixtures are local transformations of documents from the
[W3C SCXML 1.0 Implementation Report test suite](https://www.w3.org/Voice/2013/scxml-irp/).
The inventory follows the upstream 10 March 2015 report: 200 assertions expand
to 202 test documents because assertion 403 has three starts. Of those
documents, 168 are mandatory and 34 optional. TurboUtils currently executes 36
local PASS transformations, records 132 mandatory documents as UNSUPPORTED,
and records all 34 optional-profile documents as N/A. Passing this corpus is
not W3C certification and is not, by itself, a claim of complete SCXML
processor conformance.

`manifest.tsv` is the single machine-readable corpus fact source. Every
upstream start document has one row. Its nine tab-separated columns record the
local ID and fixture path, applicability, status, feature, exact upstream
source, expected result, transformation, and rationale. The harness rejects
duplicate IDs, fixture paths, and sources; malformed or empty columns;
applicability/status mismatches; missing PASS fixtures; inconsistent expected
results; undocumented source origins; and an inventory other than 202 rows,
168 mandatory rows, and 34 optional rows. The prose below explains PASS
transformations and profile boundaries but does not override manifest facts.

Status meanings are strict:

- `PASS` means the local fixture is executed and must deterministically reach
  its `pass` terminal, or the equivalent single-pass terminal checked by a
  specialized strict adapter harness.
- `UNSUPPORTED` means a mandatory upstream document remains outside the
  implemented or testable TurboUtils profile. The row states the missing
  assertion rather than silently omitting it.
- `N/A` is reserved for optional profiles that TurboUtils does not claim,
  currently ECMAScript, XPath, and BasicHTTP-specific behavior.

| Local fixture | Upstream source | Assertion preserved |
| --- | --- | --- |
| `test144.scxml` | [test144.txml](https://www.w3.org/Voice/2013/scxml-irp/144/test144.txml) | Events produced by `raise` are appended to the internal queue in execution order. |
| `test147.scxml` | [test147.txml](https://www.w3.org/Voice/2013/scxml-irp/147/test147.txml) | An `if` executes only the first partition whose condition is true. |
| `test148.scxml` | [test148.txml](https://www.w3.org/Voice/2013/scxml-irp/148/test148.txml) | An `if` executes its `else` partition when every condition is false. |
| `test149.scxml` | [test149.txml](https://www.w3.org/Voice/2013/scxml-irp/149/test149.txml) | An `if` executes no partition when every condition is false and no `else` exists. |
| `test158.scxml` | [test158.txml](https://www.w3.org/Voice/2013/scxml-irp/158/test158.txml) | Elements in one executable-content block execute in document order. |
| `test159.scxml` | [test159.txml](https://www.w3.org/Voice/2013/scxml-irp/159/test159.txml) | An execution error prevents the remaining elements of the same block from executing. |
| `test179.scxml` | [test179.txml](https://www.w3.org/Voice/2013/scxml-irp/179/test179.txml) | Evaluated `send/content` bytes reach the host Event I/O boundary unmodified. |
| `test223.scxml` | [test223.txml](https://www.w3.org/Voice/2013/scxml-irp/223/test223.txml) | `invoke/@idlocation` receives the generated invocation ID before completion is processed. |
| `test224.scxml` | [test224.txml](https://www.w3.org/Voice/2013/scxml-irp/224/test224.txml) | The generated invocation ID has `stateid.platformid` form at both the CMeta location and adapter boundary. |
| `test355.scxml` | [test355.txml](https://www.w3.org/Voice/2013/scxml-irp/355/test355.txml) | With no root `initial`, the first child state in document order is selected. |
| `test375.scxml` | [test375.txml](https://www.w3.org/Voice/2013/scxml-irp/375/test375.txml) | Multiple `onentry` handlers execute in document order. |
| `test376.scxml` | [test376.txml](https://www.w3.org/Voice/2013/scxml-irp/376/test376.txml) | An execution error aborts only its `onentry` block; a later independent handler still executes. |
| `test377.scxml` | [test377.txml](https://www.w3.org/Voice/2013/scxml-irp/377/test377.txml) | Multiple `onexit` handlers execute in document order. |
| `test378.scxml` | [test378.txml](https://www.w3.org/Voice/2013/scxml-irp/378/test378.txml) | An execution error aborts only its `onexit` block; a later independent handler still executes. |
| `test387.scxml` | [test387.txml](https://www.w3.org/Voice/2013/scxml-irp/387/test387.txml) | An unset shallow or deep history slot enters its declared default stored configuration. |
| `test399.scxml` | [test399.txml](https://www.w3.org/Voice/2013/scxml-irp/399/test399.txml) | Event descriptor unions, token prefixes, token boundaries, `.*`, and `*` select exactly the intended transitions. |
| `test579.scxml` | [test579.txml](https://www.w3.org/Voice/2013/scxml-irp/579/test579.txml) | Unset history transition content executes after the parent's `onentry` and initial-transition content. |
| `test580.scxml` | [test580.txml](https://www.w3.org/Voice/2013/scxml-irp/580/test580.txml) | A history pseudo-state never appears in the active configuration. |
| `test576.scxml` | [test576.txml](https://www.w3.org/Voice/2013/scxml-irp/576/test576.txml) | Root `initial` IDREFS enter both deeply nested non-default siblings of one parallel state. |
| `test403a.scxml` | [test403a.txml](https://www.w3.org/Voice/2013/scxml-irp/403/test403a.txml) | Transition selection prefers descendant sources, then document order, and falls through disabled conditions. |
| `test404.scxml` | [test404.txml](https://www.w3.org/Voice/2013/scxml-irp/404/test404.txml) | States execute `onexit` content in exit order before transition content. |
| `test405.scxml` | [test405.txml](https://www.w3.org/Voice/2013/scxml-irp/405/test405.txml) | Selected transition content executes in document order after all required exits. |
| `test406.scxml` | [test406.txml](https://www.w3.org/Voice/2013/scxml-irp/406/test406.txml) | Transition content executes before states enter in parent-before-child, document order. |
| `test407.scxml` | [test407.txml](https://www.w3.org/Voice/2013/scxml-irp/407/test407.txml) | A state's `onexit` content executes when the state leaves the active configuration. |
| `test409.scxml` | [test409.txml](https://www.w3.org/Voice/2013/scxml-irp/409/test409.txml) | A state leaves the active configuration after its own `onexit` and before an ancestor's `onexit`. |
| `test411.scxml` | [test411.txml](https://www.w3.org/Voice/2013/scxml-irp/411/test411.txml) | A state enters the active configuration immediately before its own `onentry`. |
| `test412.scxml` | [test412.txml](https://www.w3.org/Voice/2013/scxml-irp/412/test412.txml) | Initial-transition content executes after the parent's `onentry` and before the child's `onentry`. |
| `test416.scxml` | [test416.txml](https://www.w3.org/Voice/2013/scxml-irp/416/test416.txml) | Entering a compound state's final child generates `done.state.<id>`. |
| `test417.scxml` | [test417.txml](https://www.w3.org/Voice/2013/scxml-irp/417/test417.txml) | Completing every region generates the parallel state's `done.state.<id>` event. |
| `test419.scxml` | [test419.txml](https://www.w3.org/Voice/2013/scxml-irp/419/test419.txml) | An enabled eventless transition is selected before a queued internal event. |
| `test421.scxml` | [test421.txml](https://www.w3.org/Voice/2013/scxml-irp/421/test421.txml) | Unmatched internal events are removed until one enables a transition or the internal queue is empty. |
| `test503.scxml` | [test503.txml](https://www.w3.org/Voice/2013/scxml-irp/503/test503.txml) | A targetless transition has an empty exit set. |
| `test504.scxml` | [test504.txml](https://www.w3.org/Voice/2013/scxml-irp/504/test504.txml) | An external transition exits every active proper descendant of the source/target LCCA. |
| `test505.scxml` | [test505.txml](https://www.w3.org/Voice/2013/scxml-irp/505/test505.txml) | An internal transition from a compound state to a proper descendant retains the source state. |
| `test506.scxml` | [test506.txml](https://www.w3.org/Voice/2013/scxml-irp/506/test506.txml) | An internal transition whose target is not a proper descendant uses external transition-domain semantics. |
| `test533.scxml` | [test533.txml](https://www.w3.org/Voice/2013/scxml-irp/533/test533.txml) | An internal transition from a non-compound source uses external transition-domain semantics. |

The upstream `.txml` files use a `conf:` vocabulary consumed by the W3C test
generation pipeline. Every local transformation replaces `conf:pass` and
`conf:fail` with ordinary SCXML `final` states named `pass` and `fail`, removes
test-generation metadata, selects the null datamodel, and keeps the executable
structure that observes the assertion.

Tests 144, 147, 148, 149, 158, 375, 377, 404, 405, 406, and 412 replace wildcard
failure transitions with finite exact events so the local `pass`/`fail` finals
observe each expected ordering path directly. Test
147 and 148 replace generator counters with a post-conditional event that
observes both the selected partition and the absence of any later partition.
Test 149 uses the same post-conditional event to prove that no partition ran,
and test 158 retains the upstream two-event document-order trace. Test 412 also
removes redundant generator-level parent sentinels while retaining the complete
three-event observation chain. Tests 405, 406, 412, 416, and 417
omit the upstream one-second timeout `send`; it is only a liveness safety net,
while the local harness directly fails any run that does not reach `pass`.
Tests 399 and 576 retain the upstream event-descriptor and root multi-target
structures respectively; they only remove generator metadata and timeout
failure sends that the local synchronous harness does not need.
Test 419 keeps the queued internal event as the failure witness, replaces the
wildcard with that exact event, and omits the additional external `send`; the
retained event is sufficient to distinguish eventless-transition precedence.
Test 403a replaces generator counters with two queued events: the first checks
descendant and document-order priority, and the second checks condition
fallthrough to an ancestor. Tests 409 and 411 retain their `In(state)` timing
checks but replace generator pass/fail operations and timeout sends with exact
internal success and failure events. Tests 503, 505, 506, and 533 replace exit
counters and wildcard observers with finite ordered `onexit` event chains. A
missing, extra, or misordered exit either reaches `fail` or prevents the harness
from observing completion. Their upstream timeout sends are omitted because the
local harness already requires each run to terminate in `pass` without a runtime
error.

Tests 376 and 378 replace the generator counter with a `second.block` event.
Their test-only owning sessions inject `CFLOW_SCXML_ADAPTER_ERROR_EXECUTION`
for the first handler's `send`, then require `error.execution` followed by the
event from the later independent handler. Test 159 uses the same deterministic
adapter failure, rejects an event from the remainder of the failing block, and
accepts only the witness raised by the next independent `onentry` block. Test
387 preserves both unset-history targets and replaces wildcard failures with
the finite wrong leaf-entry events; its timeout send is omitted because the
harness requires terminal completion. Test 579 replaces the generator counter
and timeout with a finite two-pass event trace. The first pass requires
parent-entry `event1`, initial-transition `event2`, and unset-history `event3`
in order. Stored-history reentry requires `event1`, `event2`, and a leaf-entry
witness while rejecting `event3`, proving that the history default content is
suppressed after the slot is set. Test 580 keeps exact `In(sh1)` guards at
child, parent, exited, and restored observation points, replacing only generator
pass/fail operations with local terminal states and internal events. Test 407
replaces the exit counter with one exact internal exit event. Test 421
retains the four internal events and matches only the third and fourth, which
directly observes the named internal-queue draining assertion; the upstream
external-send failure witness is outside that assertion and is omitted. Test
504 replaces five counters with the two complete reverse-document exit traces
produced by its external transitions. Exact observers require both parallel
regions and their parallel parent to exit twice, and the containing state to
exit once.

Test 179 replaces upstream self-delivery with the versioned v3 host Event I/O
adapter. The fixture still evaluates literal `content` when `send` executes;
the adapter is the external-service boundary and requires the exact UTF-8 bytes
`123` before the only terminal path can complete. Tests 223 and 224 replace the
invoked child processor with the versioned host invoke adapter. The adapter
observes the committed generated ID and reports completion through that same
invocation token. The fixtures independently require the writable CMeta
`idlocation` to be nonempty and exactly `s0.1`, so removing the generated child
does not weaken either binding or `stateid.platformid` witness.

The late-binding implementation does not justify weakening upstream test 280:
TurboUtils uses caller-supplied typed CMeta storage, so a declared field exists
before its state-local initializer runs. Reads before first entry therefore
observe the caller value instead of the upstream generated datamodel's
unbound-location error. Test 280 remains explicitly `UNSUPPORTED`; the local
late-binding transaction, first-entry, re-entry, history, and rollback tests
remain implementation tests rather than being relabeled as W3C PASS.

## Current state-membership profile

Null-model and CMeta `In(id)` expressions admit any ID-bearing SCXML state,
including history pseudo-states. The W3C `<initial>` element has no attributes
and cannot be named. The native Statechart active configuration remains the
single fact source: pseudo-states are never active, so a declared history query
evaluates to false. Unknown IDs still fail program admission.

## Current CMeta system-event profile

CMeta sessions expose the complete read-only `_event` profile from
[SCXML 1.0 section 5.10.1](https://www.w3.org/TR/2015/REC-scxml-20150901/#InternalStructureofEvents):
`name`, `type`, `sendid`, `origin`, `origintype`, `invokeid`, and `data`.
`type` is `internal` for raised, internal-send, and state-completion Events,
`platform` for processor-generated error Events, and `external` for admitted
external and invocation-completion Events. Missing optional metadata is the
empty CMeta string; selecting the next Event clears metadata not supplied by
that Event instead of retaining stale values.

The selected Event remains current through all eventless microsteps in the
same run-to-completion cycle. Initial eventless work has no current Event and
therefore fails evaluation when it reads `_event`. Scalar/text/XML data is
exposed as a bounded string. `cflow_scxml_session_try_send_v3()` additionally
copies structured CMeta data whose descriptor is exactly the compiled session
root, allowing typed paths such as `_event.data.order.count`. The copy is owned
by the session until the next Event is selected or the session is destroyed.
Because a structured value is not a string, reading it as bare `_event.data`
fails evaluation instead of silently substituting an empty value.
The fixed storage cost is bounded by
`external_event_capacity * CFLOW_SCXML_EVENT_DATA_CAPACITY`, plus one current
Event slot and row metadata.

Format parsing remains outside the SCXML runtime. An embedding application may
use CBind/CSerde to convert JSON, XML, YAML, or another format into the compiled
root CMeta object, then admit that object through the v3 API. This keeps codecs
and their errors out of transition selection. Invalid envelopes, unsupported
content, schema mismatches, bare/unknown `_event` paths, and every write to an
`_event` location fail fast without a compatibility fallback.

The upstream suite page offers the tests under the
[W3C Test Suite License](https://www.w3.org/copyright/test-suite/) or the
[W3C 3-clause BSD License](https://www.w3.org/Consortium/Legal/2015/copyright-software-and-document).
Keep this provenance and transformation record with any copied or extended
fixture set.

Copyright © 2013 World Wide Web Consortium. W3C liability, trademark, and
document-use rules are governed by the selected license above.
