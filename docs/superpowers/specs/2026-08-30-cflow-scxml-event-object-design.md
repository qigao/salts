# CFlow SCXML CMeta `_event` object

## Context

The CMeta profile previously exposed only `_event.name`, while SCXML 1.0
defines seven read-only fields and retains the selected Event during the
following eventless microsteps. External metadata already used session-owned,
mailbox-coupled rows, and v3 send/invoke adapters already shared a
format-neutral `cflow_scxml_content_view` contract.

## Decision

The owning session is the single fact source for the current Event. Selection
copies `name`, `type`, `sendid`, `origin`, `origintype`, `invokeid`, and scalar
data into session storage. It retains that snapshot through the complete
run-to-completion cycle and replaces it only when another Event is selected.
Missing optional fields are represented by empty CMeta strings.

External structured data uses the existing content-view boundary through the
additive `cflow_scxml_session_try_send_v3()` API. Admission requires the exact
compiled root CMeta schema. The session copy is bounded by
`CFLOW_SCXML_EVENT_DATA_CAPACITY`, has a complete copy/destroy lifecycle, and is
owned independently of caller mutation. Each queued external Event has one
metadata row, and the current Event has one separate slot.

Event classification is deterministic: raised, internal-send, and state
completion Events are `internal`; processor-generated error Events are
`platform`; externally admitted and invocation-completion Events are
`external`. Selecting a new Event clears unavailable optional metadata before
publishing its values.

## Alternatives

- Embedding JSON/XML nodes in the runtime would spread parser ownership,
  allocation, and format-specific errors into deterministic transition
  selection.
- A generic dynamic map would duplicate the compiled CMeta schema and weaken
  static location admission.
- Borrowing caller objects would permit mutation or use-after-free after the
  nonblocking send call returns.

These alternatives are rejected. CBind/CSerde may decode external formats into
the compiled root object before admission, but remains an application adapter.

## Compatibility, failure, and migration

The v1 and v2 public layouts and behavior are unchanged. V3 is additive and
rejects an invalid ABI, conflicting legacy/v3 data, unsupported content,
schema mismatch, missing lifecycle traits, or excess size before consuming a
mailbox or metadata row. Existing string users can remain on v2; structured
users migrate by constructing `cflow_scxml_event_metadata_v3` and setting
`data.kind` to `CFLOW_SCXML_CONTENT_CMETA`.

Rollback is removal of the v3 API and structured operand while retaining the
existing metadata rows. No persistent data or wire format is changed.

## Verification

Tests cover every Event field, internal/external/platform classification,
send/invoke/state-completion metadata, structured copy isolation, retention
through eventless microsteps, replacement without stale metadata, invalid
admission without capacity loss, unknown paths, and read-only assignment
rejection. Focused SCXML tests precede the full TurboUtils CTest suite.
