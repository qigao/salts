# CHTTP HTTP/2 Deferred Response Design Amendment 2

## Status

This amendment is normative for:

- `docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design.md`
  at commit `7919744358c7f01f44491c5be6b9e952a5dd3da0`;
- `docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design-amendment.md`
  at commit `77558901b4a77988b87e8979cbfffd06fa5fd8e6`.

It corrects the owner-side lifetime after a worker has successfully published a
copied HTTP/2 deferred response. All previously approved public API, admission,
cancellation, backpressure, Session/WebSocket, and downstream graph-ordering
decisions remain unchanged.

## Problem

The original design models a successful worker completion as:

```text
READY
  -> server owner submits/drops response
  -> IDLE
```

That is safe for HTTP/1.1 because the owner serializes the complete response into
the connection-owned outbound byte buffer before releasing the deferred reply
builder.

It is not safe for HTTP/2. The private H2 protocol engine records an outbound
in-memory response body as a borrowed pointer and explicitly requires that body
to outlive the send. `chttp_h2_proto_submit_response_ex()` therefore does not
transfer ownership of the deferred builder's body bytes into the protocol
engine.

If an H2 deferred control were reset to IDLE immediately after successful submit,
`chttp_server_response_builder_reset()` could free the body allocation while the
protocol stream still retains that pointer for later DATA frames. That would
create a use-after-free and could also allow the same control/builder storage to
be reused by an unrelated deferred request before the first response finished
sending.

## Correct H2 post-submit ownership

HTTP/2 adds one owner-side state:

```text
IDLE
  |
  | handler defer succeeds
  v
PENDING
  |
  | worker owns bounded copy
  v
WRITING
  |
  +-- retryable copy error, live transport --> PENDING
  +-- transport cancellation -------------> CANCELED
  `-- copied response ready ---------------> READY
                                                |
                                                | owner successfully submits
                                                v
                                            SUBMITTED
                                                |
                                                | exact H2 stream terminal close
                                                v
                                               IDLE

PENDING -- transport cancel before writer --> CANCELED
CANCELED -- application terminal reply attempt --> IDLE
READY -- stream closes before submit --> IDLE after dropping copied response
SUBMITTED -- stream reset/close/connection termination --> IDLE after protocol no longer borrows body
```

`SUBMITTED` is an internal H2 transport-ownership state. It does not re-open an
application lease: the caller's public `chttp_server_deferred` was already
consumed when the worker published READY.

## Builder lifetime

For H2:

- the deferred control owns the copied deferred reply builder through PENDING,
  WRITING, READY, and SUBMITTED;
- successful `chttp_h2_proto_submit_response_ex()` changes READY to SUBMITTED but
  does not reset or reuse that builder;
- the application stream keeps its link to that control while SUBMITTED;
- the H2 stream-close/terminal path resets the control-owned builder only after
  the protocol has detached/closed the exact stream and can no longer read the
  borrowed body;
- only then does the control become IDLE and reusable.

The control generation does not advance at READY -> SUBMITTED. It advances only
when a later handler claims the now-IDLE control for a new deferred lease.

For H1:

- the existing owner serializes the response into connection-owned outbound
  bytes;
- after successful serialization the deferred reply builder is no longer
  borrowed by transport code;
- H1 may therefore return its control to IDLE at the same point as the current
  implementation.

This protocol-specific release point is intentional; the public deferred lease
semantics remain unified even though the wire transports consume copied response
storage differently.

## Cancellation and close behavior

The existing PENDING/WRITING/READY cancellation rules remain unchanged with one
addition for SUBMITTED:

### PENDING

RST_STREAM/peer close wins PENDING -> CANCELED, detaches the stream, and permits
application stream-slot reuse immediately. The control stays occupied until the
application terminalizes the canceled handle.

### WRITING

The owner records cancellation and quarantines only the affected application
stream slot until the worker exits the bounded copy. The worker consumes the
application lease with `SALTS_ECANCELED`; the owner then releases the stream
slot/control safely.

### READY

The application handle is already consumed. If the stream closes before the
owner submits the response, the owner drops/reset the copied builder and returns
the control to IDLE. No application callback/result is generated.

### SUBMITTED

The application handle is already consumed and the protocol may still borrow the
body. RST_STREAM, peer stream close, connection failure, or normal END_STREAM
completion terminalizes the stream. The stream-close path then resets the copied
builder and returns the control to IDLE. No worker-side cancellation result is
possible because application ownership was already transferred at READY.

Sibling streams remain independent in every state.

## H2 response submission helper

The H2 server response helper must support an explicit builder argument instead
of assuming every response comes from `stream->request_state.response_builder`.
Conceptually:

```c
static int chttp_h2_server_submit_response_from(
    chttp_h2_server_stream *stream,
    chttp_server_response_builder *builder);
```

Synchronous dispatch calls it with the request-state builder. Deferred owner
progress calls it with the control-owned builder.

The helper may encode/copy header metadata as it already does, but it must not
move or reset the deferred body builder on successful H2 submission. The
control remains SUBMITTED until stream close.

Streaming/source responses remain outside deferred scope, so deferred H2
submission always uses the bounded in-memory body path and never installs a
control-owned source callback.

## Stream/control link while SUBMITTED

The H2 application stream retains:

- its nonzero stream-slot generation;
- its pointer/index to the deferred control;
- the control generation/link established by the original lease.

The terminal stream-close path verifies this link before releasing SUBMITTED
storage. A stale stream generation may never reset a control belonging to a
later request.

Conversely, while SUBMITTED the control is not available to a new `defer()` even
though the application public handle is gone. This is required transport
backpressure: the response body is still owned by the server and borrowed by the
H2 sender.

## Capacity consequence

An H2 deferred control counts as occupied while in:

```text
PENDING | WRITING | READY | SUBMITTED | CANCELED
```

Only IDLE is claimable.

This means `h2_stream_capacity` bounds both outstanding application leases and
submitted deferred responses whose body bytes are still in transport use. That
is consistent with the existing hard bound and avoids hidden per-response
storage.

Synchronous H2 requests still do not consume deferred controls.

## Stop and GOAWAY consequence

Successful server stop still requires every deferred control to be IDLE.

A SUBMITTED control normally reaches IDLE when its stream reaches terminal close
while the existing H2 GOAWAY/drain logic progresses. A finite stop timeout may
therefore return `SALTS_ETIMEDOUT` if a submitted deferred response cannot drain
before the deadline; the server and its borrowed body storage remain valid for
a later stop retry.

Connection teardown must not destroy H2 deferred-control storage until every
protocol stream has been terminalized and no SUBMITTED control can still be
borrowed by the protocol engine.

## Required verification additions

The implementation plan must add a regression that would fail if the deferred
builder were released immediately after H2 submit:

1. create an H2 deferred response body larger than one forced DATA send chunk;
2. publish the deferred reply successfully;
3. let owner progress submit the response but keep the stream mid-send;
4. prove the deferred control remains non-IDLE/SUBMITTED while output remains;
5. continue transport progress until the client receives the exact complete
   body;
6. prove the stream then closes and the control becomes reusable;
7. submit a later deferred request and require the same bounded control capacity
   to admit it safely.

The test must not depend on timing sleeps. It may use the existing private H2
send-chunk/test controls or another deterministic private seam.

Also retain the already-required READY-before-submit cancellation test so READY
and SUBMITTED release points remain distinct.

## Compatibility

No public struct, function signature, capacity field, error code, or ABI changes.
`SUBMITTED` is private implementation state only.

The downstream TurboFlow ownership amendment remains unchanged: reserve the
CHTTP deferred lease before admitting asynchronous graph work, and terminalize
that lease on every graph outcome. A successful `chttp_server_deferred_reply()`
still means application response ownership has transferred to CHTTP even though
CHTTP may retain the private copied body until the H2 stream finishes sending.