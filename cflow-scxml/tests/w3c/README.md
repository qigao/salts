# W3C-derived SCXML regression fixtures

These fixtures are local transformations of selected documents from the
[W3C SCXML 1.0 Implementation Report test suite](https://www.w3.org/Voice/2013/scxml-irp/).
They test only the named assertions below. Passing them is not W3C
certification and is not, by itself, a claim of complete SCXML conformance.

| Local fixture | Upstream source | Assertion preserved |
| --- | --- | --- |
| `test144.scxml` | [test144.txml](https://www.w3.org/Voice/2013/scxml-irp/144/test144.txml) | Events produced by `raise` are appended to the internal queue in execution order. |
| `test355.scxml` | [test355.txml](https://www.w3.org/Voice/2013/scxml-irp/355/test355.txml) | With no root `initial`, the first child state in document order is selected. |
| `test375.scxml` | [test375.txml](https://www.w3.org/Voice/2013/scxml-irp/375/test375.txml) | Multiple `onentry` handlers execute in document order. |
| `test377.scxml` | [test377.txml](https://www.w3.org/Voice/2013/scxml-irp/377/test377.txml) | Multiple `onexit` handlers execute in document order. |
| `test416.scxml` | [test416.txml](https://www.w3.org/Voice/2013/scxml-irp/416/test416.txml) | Entering a compound state's final child generates `done.state.<id>`. |
| `test417.scxml` | [test417.txml](https://www.w3.org/Voice/2013/scxml-irp/417/test417.txml) | Completing every region generates the parallel state's `done.state.<id>` event. |
| `test419.scxml` | [test419.txml](https://www.w3.org/Voice/2013/scxml-irp/419/test419.txml) | An enabled eventless transition is selected before a queued internal event. |

The upstream `.txml` files use a `conf:` vocabulary consumed by the W3C test
generation pipeline. Every local transformation replaces `conf:pass` and
`conf:fail` with ordinary SCXML `final` states named `pass` and `fail`, removes
test-generation metadata, selects the null datamodel, and keeps the executable
structure that observes the assertion.

Tests 144, 375, and 377 replace wildcard failure transitions, which are
outside the current TurboUtils profile, with the finite exact events that
represent an ordering error at each observation state. Tests 416 and 417 omit
the upstream one-second timeout `send`; it is only a liveness safety net, while
the local harness directly fails any run that does not reach `pass`. Test 419
keeps the queued internal event as the failure witness, replaces the wildcard
with that exact event, and omits the additional external `send`; the retained
event is sufficient to distinguish eventless-transition precedence.

The upstream suite page offers the tests under the
[W3C Test Suite License](https://www.w3.org/copyright/test-suite/) or the
[W3C 3-clause BSD License](https://www.w3.org/Consortium/Legal/2015/copyright-software-and-document).
Keep this provenance and transformation record with any copied or extended
fixture set.

Copyright © 2013 World Wide Web Consortium. W3C liability, trademark, and
document-use rules are governed by the selected license above.
