# CFlow SCXML content v3 implementation plan

## Task 1: bounded XML fragment serialization

- Add a caller-buffer XML child-fragment serializer to `XmlParser`; keep cxml
  private and preserve all raw child nodes including whitespace.
- Cover text, XML, mixed content, namespaces, empty fragments, exact capacity,
  malformed handles, and retained limits.

## Task 2: additive payload ABI v3 and compiler storage

- Add independent v3 content, entry, payload, send/invoke request, adapter,
  bundle, capability, requirement, and session-init declarations.
- Extend admission/emission with immutable inline fragments and structured
  simple CMeta locations. Keep scalar expression programs on v2 layouts.
- Validate v1/v2 prefix compatibility and fail v3-required attachment without
  a matching table.

## Task 3: runtime materialization and completion

- Materialize scalar, inline bytes, or structured staged-state views without
  ownership escape.
- Route send, delayed send, invoke, rejection, rollback, and donedata through
  the existing ticket/state transaction protocol.
- Add focused TinyTest coverage and update the exact README conformance claim.

## Verification

- Run `xml_parser_test`, all SCXML tests, C/C++ public-header tests,
  `git diff --check`, and full `win-release-user` CTest.
