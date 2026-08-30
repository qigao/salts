# W3C-derived SCXML regression fixtures

These fixtures are local transformations of selected documents from the
[W3C SCXML 1.0 Implementation Report test suite](https://www.w3.org/Voice/2013/scxml-irp/).
They test only the named assertions below. Passing them is not W3C
certification and is not, by itself, a claim of complete SCXML conformance.

| Local fixture | Upstream source | Assertion preserved |
| --- | --- | --- |
| `test355.scxml` | [test355.txml](https://www.w3.org/Voice/2013/scxml-irp/355/test355.txml) | With no root `initial`, the first child state in document order is selected. |
| `test375.scxml` | [test375.txml](https://www.w3.org/Voice/2013/scxml-irp/375/test375.txml) | Multiple `onentry` handlers execute in document order. |

The upstream `.txml` files use a `conf:` vocabulary consumed by the W3C test
generation pipeline. The local transformation replaces `conf:pass` and
`conf:fail` with ordinary SCXML `final` states named `pass` and `fail`, removes
test-generation metadata, and keeps the executable structure that observes
the assertion. Test 375 replaces wildcard failure transitions, which are
outside the current TurboUtils profile, with exact `event1`/`event2` failure
transitions. This still detects either reversal: `event2` reaching the first
state or `event1` reaching the second state enters `fail`.

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
