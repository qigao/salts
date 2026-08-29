# CFlow SCXML Core Frontend Design

**Issue:** [qigao/turbo-utils#122](https://github.com/qigao/turbo-utils/issues/122)

**Date:** 2026-08-29

**Reference semantics:** [W3C SCXML 1.0](https://www.w3.org/TR/scxml/)

## Scope

This phase adds two independently linkable TurboUtils libraries:

- `TurboUtils::XmlParser`, a bounded owning XML DOM facade over a private cxml
  implementation; and
- `TurboUtils::CFlowScxml`, an optional SCXML Core frontend that validates and
  compiles accepted XML into the existing format-neutral CFlow Statechart IR.

The frontend covers `scxml`, `state`, `parallel`, `transition`, `initial`,
`final`, `history`, `onentry`, and `onexit`. It supports the SCXML null data
model. Conditions, executable content, multiple transition targets, wildcard
event descriptors, `send`, `invoke`, script, data expressions, and every
element outside this list are rejected during compilation with stable source
diagnostics. There is no fallback and no full-SCXML-conformance claim.

Existing CFlow and parser targets, headers, statechart semantics, and public
ABI remain unchanged. `TurboUtils::CFlow` does not acquire an XML dependency.

## Evidence and boundary decision

- **Fact:** `cflow_statechart_build()` already copies, normalizes, validates,
  and atomically publishes the native Statechart rows.
- **Fact:** the native IR already represents compound, parallel, initial,
  final, shallow/deep history, eventless/event/completion transitions, and
  ordered entry/exit action references.
- **Fact:** the cxml DOM `pos` member is a document-order counter. Its lexer
  tracks a token line but does not retain columns or byte offsets on DOM nodes.
- **Fact:** cxml exposes concrete node structs, global configuration, and
  stderr-oriented parse failures. Those contracts must not cross an installed
  TurboUtils API boundary.
- **Inference:** directly linking cxml from CFlow would reverse the intended
  dependency and make the format-neutral runtime depend on one syntax. A
  separate frontend target preserves the current core and permits other input
  formats later.

The dependency direction is therefore:

```text
private cxml -> TurboUtils::XmlParser
TurboUtils::XmlParser + TurboUtils::CFlow -> TurboUtils::CFlowScxml
TurboUtils::CFlow -> TurboUtils::CMeta
```

`CSerde` and `CBind` are not dependencies. They describe application data
values and typed bindings, not XML element/attribute syntax. A later data-model
phase may add a separate adapter without coupling the syntax parser to CSerde.

## Alternatives

### Parse SCXML in TurboParser

Rejected. TurboUtils owns both the parser engines and CFlow after the repository
move, while TurboParser is now a thin consumer. A TurboUtils-to-TurboParser
dependency would be cyclic at the package level.

### Expose cxml as the public XML API

Rejected. This would expose third-party layouts, allocation rules, global
configuration, and error behavior. It would also make source-location fixes an
ABI commitment to a private dependency.

### Compile directly into Machine

Rejected. Machine cannot represent parallel configurations, history, or the
Statechart run-to-completion semantics required by issue #122.

### Thin XML facade plus an owning SCXML program

Chosen. The XML facade isolates cxml. The SCXML program owns a published native
Statechart plus deterministic state/event name maps needed by runtime callers.

## Public XML contract

`xml_parser/xml_parser.h` exposes opaque document and node handles. Parsing
takes an explicit byte pointer and length; embedded NUL bytes are rejected.
The caller supplies limits, or selects documented bounded defaults. The parser
copies the input so every returned string view and source location remains
valid until document destruction.

The document is the sole fact source. Nodes and attribute handles are borrowed
views into it. They are invalid after `turbo_xml_document_destroy()`. The API
is single-threaded during construction and immutable/read-only after successful
publication; concurrent read access is allowed only while the owner guarantees
the document remains alive.

Each element and attribute has a one-based line and column plus a zero-based
UTF-8 byte offset. The vendored cxml lexer/parser retains these fields while
building its DOM. Syntax failure returns the first failing token location and a
stable TurboUtils diagnostic instead of relying on cxml stderr text.

Limits cover input bytes, nodes, attributes, depth, and total retained string
bytes. Adapter-owned allocation sizes use checked arithmetic. Exceeding a limit
is distinct from malformed XML and allocation failure. Failed parse leaves a
zero-initialized output document empty.

## Public SCXML contract

`cflow/scxml.h` exposes an opaque owning `cflow_scxml_program`. Compilation
takes XML bytes, explicit limits, and a zero-initialized output. On success the
program owns:

- one immutable `cflow_statechart`;
- the exact copied source-independent state-name to numeric-ID mapping; and
- the exact event-name to numeric-ID mapping.

The Statechart accessor returns a borrowed pointer invalidated by program
destruction. Name lookup is length-aware. IDs are assigned deterministically in
depth-first document order, with zero reserved by CFlow. Synthetic root and
initial pseudo-state IDs participate in native IR order but are not exposed as
user-declared names.

The null data model uses `cmeta_type_bool` as a one-byte inert storage witness.
Its initial value and all name-created external Event payloads are `false`.
Helper accessors return the initial state pointer and construct a borrowed
`cflow_event_view`; the CFlow runtime copies both at admission. No user-visible
data mutation or expression evaluation is implied.

Compilation is transactional. Temporary XML/row/name storage is private; only
after semantic validation succeeds does `cflow_statechart_build()` publish the
native IR. Any error destroys all temporary state and leaves an empty program.
Diagnostics contain a stable status, source location, and bounded message.

## Structural lowering

The `<scxml>` element becomes the native root state. `<state>` is atomic when it
has no state-like children and compound otherwise. `<parallel>`, `<final>`, and
`<history type="shallow|deep">` map directly to their native kinds.

Every compound state receives exactly one native initial pseudo-child:

1. an explicit `<initial><transition target="..."/></initial>` is retained;
2. otherwise an `initial="id"` attribute creates a synthetic pseudo-child; or
3. otherwise the first direct state-like child is the default target.

If a compound has no direct state-like child, it is classified atomic and an
`initial` declaration is invalid. A history node must contain exactly one
target-bearing eventless default transition for this phase, matching the
native IR invariant.

An SCXML transition without `event` is eventless. A single ordinary event name
becomes a typed native event using the null CMeta witness. Several whitespace-
separated ordinary event names lower to several otherwise identical native
transitions in source token order. `done.state.<id>` becomes a native completion
trigger. Wildcards and multiple targets are rejected because the current
native row has exact one-event and at-most-one-target fields.

Empty `onentry` and `onexit` elements are admitted and preserve the structural
location. Any executable child or non-whitespace body is rejected as
unsupported. This makes the Phase 2 boundary explicit instead of silently
discarding behavior reserved for Phase 3.

## XML and SCXML admission

The XML layer requires well-formed XML and namespace-correct names. The SCXML
layer additionally requires:

- root expanded name `{http://www.w3.org/2005/07/scxml}scxml`;
- `version="1.0"`;
- absent `datamodel` or `datamodel="null"`;
- unique nonempty XML IDs for declared state/history/final elements;
- targets resolving to declared IDs;
- legal parents and children for each supported element;
- no unknown SCXML elements or attributes whose semantics would be lost; and
- native Statechart validation success after lowering.

Unknown foreign-namespace attributes are ignored as extension metadata;
unknown elements are rejected because their behavior cannot be preserved.

## Error semantics

Statuses distinguish invalid arguments, limits, allocation, XML syntax,
namespace/version/data-model errors, duplicate/unknown IDs, invalid structure,
unsupported features, and native-IR rejection. The first error detected by the
deterministic admission pipeline wins; within one validation phase, traversal
uses document order. Messages are owned by the caller-provided diagnostic
structure and never point into cxml storage.

Failures are logged nowhere in the library. The API boundary returns enough
context for the consumer to decide how and where to log.

## Build, install, and compatibility

`TurboUtils::XmlParser` is a regular parser target. Its public link interface
contains only first-party targets required by its public header; cxml is
private and is not installed or exported.

`CFLOW_ENABLE_SCXML` controls `TurboUtils::CFlowScxml` and defaults to `OFF`.
When disabled, neither the frontend target nor its header/install artifact is
provided. When enabled, the target publicly links only `TurboUtils::CFlow` and
`TurboUtils::XmlParser`. Package verification tests both the disabled boundary
and an enabled installed consumer. Existing consumers linking only CFlow see no
new transitive XML dependency.

## Verification

Verification covers:

- XML locations for elements, attributes, and malformed tokens;
- embedded NUL, zero/one/exact-limit/limit+1, nesting, node and attribute
  boundaries;
- namespace/version/null-data-model admission;
- all supported structural elements and transition forms;
- default, attribute, and explicit initial lowering;
- duplicate IDs, unknown targets, invalid parents and unsupported constructs;
- deterministic name-to-ID mappings;
- compiled Statechart initialization, event dispatch, and active-configuration
  traces; and
- build-tree and installed-package dependency contracts.

Focused XML and SCXML tests run first, then existing Statechart tests, parser
tests, complete CFlow tests, and installed-package verification. MSVC Release
is the local baseline; Linux/macOS CI provides the remaining platform evidence.
