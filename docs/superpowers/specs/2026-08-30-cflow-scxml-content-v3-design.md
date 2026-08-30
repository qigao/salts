# CFlow SCXML bounded content ABI v3 design

## Decision

Issue #178 is implemented as an additive adapter ABI v3. Existing v1/v2
tables and scalar payload layouts remain byte-for-byte unchanged. Programs
that contain inline XML/text or a non-scalar CMeta content location require a
v3 table and fail session admission when only v1/v2 is supplied.

The public boundary never exposes `turbo_xml_node` or cxml objects. Inline
content is compiled into one immutable UTF-8 fragment owned by the SCXML
program:

- an empty or text-only child sequence is `TEXT_UTF8`;
- a sequence containing an element, comment, or processing instruction is
  `XML_UTF8`;
- XML serialization preserves child order, qualified names, namespace
  declarations, attributes, text, comments, and processing instructions;
- `expr` and inline children are mutually exclusive.

Structured CMeta content is a callback-scoped pair of a validated
`cmeta_data_desc` and an object address in the staged Machine state. It is not
flattened, serialized, cloned, or treated as an untyped `void *`. The adapter
must copy/serialize it into its reservation ticket if it survives the callback.
Only a simple dotted CMeta location may produce a structured view; arbitrary
expressions retain the existing scalar evaluator.

## Public representation

ABI v3 introduces a separate content value rather than extending the v2
scalar struct, because changing the size of `cflow_scxml_payload_value` would
break v2 entry-array stride.

```c
typedef enum cflow_scxml_content_kind {
    CFLOW_SCXML_CONTENT_INVALID = 0,
    CFLOW_SCXML_CONTENT_SCALAR,
    CFLOW_SCXML_CONTENT_TEXT_UTF8,
    CFLOW_SCXML_CONTENT_XML_UTF8,
    CFLOW_SCXML_CONTENT_CMETA
} cflow_scxml_content_kind;

typedef struct cflow_scxml_content_view {
    cflow_scxml_content_kind kind;
    cflow_scxml_payload_value scalar;
    const char *bytes;
    size_t byte_count;
    const cmeta_data_desc *schema;
    const void *object;
} cflow_scxml_content_view;
```

The v3 named-entry and payload structs use this view. Scalar values are copied
from the v2 representation. Exactly one representation is live for each kind;
all pointers are borrowed only for the prepare callback. Names remain ordered
and duplicates remain legal.

Event-I/O and invocation adapter v3 tables use v3 request types. A v3 session
adapter bundle and `*_init_v3` entry points are additive. The existing v1/v2
session entry points never reinterpret a table as another ABI.

## Ownership and transaction protocol

Inline bytes are program-owned and immutable until program destruction.
Structured object pointers refer to the current staged state and expire when
the callback returns; they may also be invalidated by rollback or publication.
The schema graph is borrowed from CMeta compile options and retains its
existing requirement to outlive the program.

Adapter prepare remains a pure reservation step. It runs without session or
runtime locks, receives callback-scoped views, and returns one move-only
ticket. Runtime publication precedes ticket commit. Rejection stages the same
execution/communication Event as scalar payloads. Fatal contract or effect
journal failure discards accepted tickets and publishes no partial state or
external effect.

## Bounds and errors

The XML parser remains the source of node, attribute, depth, retained-string,
and input bounds. Serialized fragments additionally share
`cflow_scxml_limits.max_name_bytes`; all accounting uses checked arithmetic.
Program storage owns the exact admitted byte count plus NUL. Structured views
do not traverse or allocate; their descriptor and fixed C storage are already
bounded by CMeta validation and the Statechart `max_storage_bytes` contract.

Malformed XML fails in the XML parser. Empty content is an admitted zero-byte
text view. Unknown inline constructs, `expr` plus children, invalid structured
locations, serialization mismatch, and capacity overflow fail compilation
without a partial program. Runtime scalar/materialization errors keep the
existing transactional `error.execution` behavior.

## Donedata boundary

Inline text/XML donedata is copied into the existing bounded completion data
surface and therefore must fit `CFLOW_SCXML_EVENT_METADATA_CAPACITY`.
Structured donedata is retained as a schema/object view for the read-only
`_event.data` object completed by issue #181; #178 establishes its compiled
descriptor and lifetime but does not invent a string flattening.

## Compatibility and conformance

The exact named Event table remains finite. No parser DOM type crosses the
adapter boundary. v1/v2 source and binary layouts remain unchanged, scalar
content behavior remains unchanged, and inline/structured programs fail fast
without v3. This implements the selected bounded SCXML content profile; it
does not claim arbitrary executable DOM mutation, script objects, or an
unbounded XML data model.

