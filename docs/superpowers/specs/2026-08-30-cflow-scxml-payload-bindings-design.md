# CFlow SCXML bounded payload bindings design

## Scope

This change adds the next bounded part of the selected SCXML 1.0 `cmeta`
profile:

- external `<send>` payloads produced by `namelist`, `<param>`, or one scalar
  `<content expr>`;
- `<invoke>` payloads produced by `namelist`, `<param>`, or one scalar
  `<content expr>`;
- versioned Event I/O and invocation adapter callbacks that receive the
  resulting typed payload without exposing CMeta implementation types.

The W3C rules are the source of truth. `namelist` entries remain in token
order, `<param>` entries follow them in document order for `send`, and
duplicates are retained. `send` content is mutually exclusive with
`namelist` and `<param>`. `invoke` content is mutually exclusive with `src`,
`srcexpr`, `namelist`, and `<param>`; `invoke` `namelist` and `<param>` are
also mutually exclusive. A `param` has one nonempty `name` and exactly one of
`expr` or `location`.

This batch does not approximate the remaining features. Inline text/XML
object content, internal named-object `_event.data`, dynamically generated
`send`/`invoke` IDs and `idlocation`, external resource loading, and late data
binding remain explicit unsupported features. Dynamic IDs need a re-entry
counter and transactional location write; exact XML content needs a
lossless source-range contract from the XML layer. They are separate changes.

Normative references:

- <https://www.w3.org/TR/scxml/#send>
- <https://www.w3.org/TR/scxml/#invoke>
- <https://www.w3.org/TR/scxml/#param>
- <https://www.w3.org/TR/scxml/#content>

## Compatibility boundary

The v1 request structs, adapter tables, `cflow_scxml_session_config`, and v1
session initialization entry points retain their layout and behavior. They
continue to run programs without payload requirements.

Payload support is additive:

- a public format-neutral scalar value (`bool`, signed, unsigned, floating,
  or UTF-8 string);
- a payload view whose kind is either scalar content or an ordered list of
  named scalar entries;
- `cflow_scxml_send_request_v2` and
  `cflow_scxml_invoke_start_request_v2`, each embedding its v1 request as the
  first member and adding the payload view;
- independent v2 Event I/O and invocation adapter tables;
- one versioned `cflow_scxml_session_adapters_v2` bundle passed to new session
  initialization entry points alongside the unchanged base config.

The v2 bundle is explicit dependency injection, not a service locator. A
corresponding v1 adapter field in the base config must be null when a v2
adapter is supplied. Programs with `CFLOW_SCXML_REQUIREMENT_PAYLOAD` fail
session admission unless every required transport adapter is v2 and declares
the payload capability. This keeps statically known external payload failures
at the configuration boundary. One compatibility exception preserves existing
dynamic internal sends: scalar content paired with `targetexpr` does not set the
payload requirement because the target is unknown during admission. If that
expression resolves to an external target under v1, execution raises
`error.execution` before any adapter callback; a v2 payload-capable adapter is
required to publish it.

## Compile-time representation

Every admitted payload item becomes one immutable descriptor containing a
retained name and one precompiled CMeta value expression. A `namelist` token
uses the token bytes as both the public name and expression source. A `param`
uses its `name`; `expr` uses the general value compiler, while `namelist` and
`location` must first pass the bounded read-only CMeta location compiler before
the corresponding value-read expression is emitted.

Effect and invocation descriptors own an offset/count span into one program
payload-descriptor array. Scalar content continues to use one expression
program directly. Counts, retained name bytes, and expression rows are
admitted in the existing two-pass compiler with checked arithmetic. The
compile-time hard ceiling `CFLOW_SCXML_PAYLOAD_MAX_ENTRIES` bounds one
operation and is overrideable at build time.

The compiler validates structure before allocating IR. Invalid combinations
return `CFLOW_SCXML_INVALID_STRUCTURE`; a payload construct outside the CMeta
profile remains `CFLOW_SCXML_UNSUPPORTED_FEATURE`; expression type/path and
configured expression limits retain their existing diagnostics.

## Runtime and adapter protocol

The immutable program is the fact source for names and expression programs.
The staged CMeta state is the fact source for `send` values; the committed
session state supplied to the stable hook is the fact source for `invoke`
values. Each expression is evaluated exactly once immediately before its
prepare callback.

The session owns one payload scratch array sized to the program's maximum
per-operation entry count. The SerialExecutor is the sole writer. The view is
borrowed only for the prepare callback:

- names borrow immutable program storage;
- string values borrow the current state until the callback returns;
- numeric and boolean values are copied into scratch entries;
- no callback may retain any pointer without copying it.

The adapter must reserve capacity and copy retained data during `prepare`.
`ACCEPTED` transfers one move-only ticket; the session calls exactly one of
`commit` or `discard`. The existing native effect journal remains the single
transaction fact source for `send`. Invocation start keeps the current stable
hook contract and updates the fixed invocation registry only around a valid
adapter ticket.

## Capacity, concurrency, and shutdown

Data unit: one scalar content value or one ordered named scalar entry.
Ownership: program owns descriptors/names; session owns scratch; adapter owns
only copies it explicitly makes. Borrow invalidation point: return from the
prepare callback. Thread topology: single session SerialExecutor producer and
one synchronous adapter callback; adapter-internal concurrency is outside the
session contract.

The entry ceiling is hard and compile-time configurable. No payload path
grows during execution. Expression strings remain subject to the configured
CMeta string limit. Full adapter queues return the existing `FULL` status;
there is no retry, fallback, overwrite, or unbounded allocation. Close and
quiescence semantics are identical to v1.

## Error and rollback semantics

Payload evaluation or type failure raises `error.execution`. For `send`, the
current executable block aborts and its staged state and effect tickets are
discarded together. Adapter execution/communication/full/closed results keep
their current mapping. An accepted callback without both ticket functions is
a fatal invalid contract.

For `invoke`, argument evaluation failure terminates that invocation start,
marks its fixed row failed, increments the existing failure statistic, and
enqueues `error.execution`. No partially materialized payload is published.

## Tests and migration

Focused TinyTest cases cover public ABI validation, exact ordering and
duplicates, all scalar kinds, `param location`, content exclusivity, v1
session rejection, adapter full/error behavior, ticket commit/discard, send
rollback, and invoke start payloads. Existing v1 adapter tests are unchanged
and remain the compatibility regression set.

Callers that do not use payloads require no changes. Payload callers opt into
the v2 adapter bundle and copy any data needed after callback return. Rollback
is removal of the additive v2 entry points and payload descriptors; no stored
data or wire format is migrated.
