# W3C-derived SCXML regression fixtures

These fixtures are local transformations of selected documents from the
[W3C SCXML 1.0 Implementation Report test suite](https://www.w3.org/Voice/2013/scxml-irp/).
They test only the named assertions below. Passing them is not W3C
certification and is not, by itself, a claim of complete SCXML conformance.

| Local fixture | Upstream source | Assertion preserved |
| --- | --- | --- |
| `test144.scxml` | [test144.txml](https://www.w3.org/Voice/2013/scxml-irp/144/test144.txml) | Events produced by `raise` are appended to the internal queue in execution order. |
| `test147.scxml` | [test147.txml](https://www.w3.org/Voice/2013/scxml-irp/147/test147.txml) | An `if` executes only the first partition whose condition is true. |
| `test148.scxml` | [test148.txml](https://www.w3.org/Voice/2013/scxml-irp/148/test148.txml) | An `if` executes its `else` partition when every condition is false. |
| `test149.scxml` | [test149.txml](https://www.w3.org/Voice/2013/scxml-irp/149/test149.txml) | An `if` executes no partition when every condition is false and no `else` exists. |
| `test158.scxml` | [test158.txml](https://www.w3.org/Voice/2013/scxml-irp/158/test158.txml) | Elements in one executable-content block execute in document order. |
| `test159.scxml` | [test159.txml](https://www.w3.org/Voice/2013/scxml-irp/159/test159.txml) | An execution error prevents the remaining elements of the same block from executing. |
| `test355.scxml` | [test355.txml](https://www.w3.org/Voice/2013/scxml-irp/355/test355.txml) | With no root `initial`, the first child state in document order is selected. |
| `test375.scxml` | [test375.txml](https://www.w3.org/Voice/2013/scxml-irp/375/test375.txml) | Multiple `onentry` handlers execute in document order. |
| `test376.scxml` | [test376.txml](https://www.w3.org/Voice/2013/scxml-irp/376/test376.txml) | An execution error aborts only its `onentry` block; a later independent handler still executes. |
| `test377.scxml` | [test377.txml](https://www.w3.org/Voice/2013/scxml-irp/377/test377.txml) | Multiple `onexit` handlers execute in document order. |
| `test378.scxml` | [test378.txml](https://www.w3.org/Voice/2013/scxml-irp/378/test378.txml) | An execution error aborts only its `onexit` block; a later independent handler still executes. |
| `test387.scxml` | [test387.txml](https://www.w3.org/Voice/2013/scxml-irp/387/test387.txml) | An unset shallow or deep history slot enters its declared default stored configuration. |
| `test403a.scxml` | [test403a.txml](https://www.w3.org/Voice/2013/scxml-irp/403a/test403a.txml) | Transition selection prefers descendant sources, then document order, and falls through disabled conditions. |
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

Tests 144, 147, 148, 149, 158, 375, 377, 404, 405, 406, and 412 replace wildcard failure
transitions, which are outside the current TurboUtils profile, with the finite
exact events that represent an ordering error at each observation state. Test
147 and 148 replace generator counters with a post-conditional event that
observes both the selected partition and the absence of any later partition.
Test 149 uses the same post-conditional event to prove that no partition ran,
and test 158 retains the upstream two-event document-order trace. Test 412 also
removes redundant generator-level parent sentinels while retaining the complete
three-event observation chain. Tests 405, 406, 412, 416, and 417
omit the upstream one-second timeout `send`; it is only a liveness safety net,
while the local harness directly fails any run that does not reach `pass`.
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
387 preserves both unset-history
targets and replaces wildcard failures with the finite wrong leaf-entry events;
its timeout send is omitted because the harness requires terminal completion.
Test 407 replaces the exit counter with one exact internal exit event. Test 421
retains the four internal events and matches only the third and fourth, which
directly observes the named internal-queue draining assertion; the upstream
external-send failure witness is outside that assertion and is omitted. Test
504 replaces five counters with the two complete reverse-document exit traces
produced by its external transitions. Exact observers require both parallel
regions and their parallel parent to exit twice, and the containing state to
exit once.

## Current CMeta system-event profile

CMeta expressions admit the read-only string `_event.name` only while the
native Statechart contextual callback carries the Event that triggered the
current transition quantum. This covers event transition guards and the
exit/transition/entry executable content run by that quantum. Initial and
later eventless work has no `_event` binding.

This is deliberately a partial profile, not the complete `_event` object from
[SCXML 1.0 section 5.10.1](https://www.w3.org/TR/2015/REC-scxml-20150901/#InternalStructureofEvents).
The `type`, `sendid`, `origin`, `origintype`, `invokeid`, and `data` fields, as
well as retention of the last selected Event across later eventless
microsteps, remain unsupported. Bare `_event`, unsupported fields, and writes
to `_event` locations fail admission; reading `_event.name` without a current
Event fails evaluation.

The upstream suite page offers the tests under the
[W3C Test Suite License](https://www.w3.org/copyright/test-suite/) or the
[W3C 3-clause BSD License](https://www.w3.org/Consortium/Legal/2015/copyright-software-and-document).
Keep this provenance and transformation record with any copied or extended
fixture set.

Copyright © 2013 World Wide Web Consortium. W3C liability, trademark, and
document-use rules are governed by the selected license above.
