# CHTTP HTTP/2 Deferred Response Design

## Decision

CHTTP extends the existing generation-checked `chttp_server_deferred` response
contract from HTTP/1.1 to ordinary HTTP/2 request streams without changing the
public handle layout, public reply shape, or introducing protocol fallback.

The core design is a transport-neutral internal deferred-control object. Public
`chttp_server_deferred.impl` always points to one stable
`chttp_server_deferred_control`; it never points directly to either an H1
connection or an H2 stream. This avoids a hidden tagged-pointer ABI and gives
worker-side `chttp_server_deferred_reply()` one ownership model across both
protocols.

HTTP/1.1 keeps one deferred control per connection. HTTP/2 allocates a fixed
pool of deferred controls per server-side H2 connection, with capacity exactly
`h2_stream_capacity`. An H2 stream claims one control only when its handler
successfully calls `chttp_server_response_defer()`. A canceled deferred lease may
outlive the protocol stream until the application eventually terminalizes its
work, but it does not keep that H2 stream slot unavailable except during the
short interval in which a worker is actively copying the deferred response.

No new public deferred-capacity setting is added. Deferred work remains bounded
by existing transport capacities:

- H1: at most one deferred lease per live connection;
- H2: at most `h2_stream_capacity` deferred leases per physical H2 connection,
  including canceled-but-not-yet-acknowledged application work.

The public deferred response remains an in-memory copied response. Deferred
streaming bodies, file responses, Session support, WebSocket opening deferral,
a generic task/future API, and automatic HTTP retry remain outside this change.

## Problem and evidence

CHTTP already has a useful H1 deferred-response contract:

1. a handler copies any request data needed after callback return;
2. `chttp_server_response_defer()` seals the handler response builder and
   publishes a generation-checked handle;
3. a worker may later call `chttp_server_deferred_reply()` from another thread;
4. the reply call copies headers/body into bounded CHTTP-owned storage and wakes
   the server owner;
5. the owner serializes and sends the response exactly once;
6. server stop waits for admitted deferred work before successful destruction.

The current implementation is intentionally H1-specific. The response builder
stores a direct `chttp_server_connection *`, the public deferred handle points
back to that connection, the deferred state/generation live on the connection,
and `chttp_server_response_defer()` rejects any non-H1 connection with
`SALTS_ENOTSUP`.

HTTP/2 requires a stronger internal lifetime model. A single physical
connection owns several concurrent protocol streams. An individual stream may
receive RST_STREAM or otherwise close while sibling streams continue normally.
The current application stream slots are fixed records that are reset and reused
after protocol close. Therefore a worker cannot safely hold a raw H2 stream
pointer as the public deferred lease identity: stream reset could race with the
worker copying response headers/body, and slot reuse could turn a stale handle
into access to an unrelated request generation.

Issue #214 is also an upstream requirement for TurboFlow's asynchronous HTTP
server migration. TurboFlow intends to copy graph-owned request data during the
handler, retain only the generation-checked CHTTP deferred handle, and complete
that handle after CFlow terminal execution. Until H2 deferred replies exist,
TurboFlow correctly fails fast for HTTP/2 async graph routes instead of falling
back to H1 or synchronous execution.

## Required invariant

The implementation is governed by one invariant:

> A deferred response is a server-owned, generation-checked lease independent of
> the transport callback lifetime. H1 connection reuse and H2 stream reuse may
> occur only when they cannot race with a worker-side response copy; transport
> cancellation terminalizes only the affected lease/stream and never sibling H2
> streams.

This yields four concrete obligations:

1. `chttp_server_deferred` never extends the lifetime of callback-borrowed
   request pointers.
2. Exactly one worker attempt at a time owns mutable deferred-copy storage.
3. The H2 owner may retire a PENDING stream immediately on RST_STREAM because no
   worker owns its request builder yet; a WRITING stream is quarantined only
   until that bounded copy exits.
4. Successful server stop cannot free any deferred-control storage while an
   application can still legally call `chttp_server_deferred_reply()` on an
   admitted handle.

## Goals

This change must provide:

- ordinary H2 handlers may call `chttp_server_response_defer()`;
- worker threads may call `chttp_server_deferred_reply()` without reentering the
  server owner;
- one public handle type and one public reply API for H1 and H2;
- generation checking for every deferred lease;
- copied headers/body within existing configured bounds;
- retryable response-copy failures while the transport is still live;
- explicit transport cancellation behavior;
- sibling-stream isolation for RST_STREAM, errors, and deferred backpressure;
- deterministic server stop/drain semantics;
- h2c and TLS ALPN `h2` parity;
- no silent protocol fallback.

## Explicit non-goals

This issue does not add:

- deferred streaming body sources;
- deferred regular-file responses;
- Session-backed deferred handlers;
- HTTP/1.1 or HTTP/2 retry;
- a cancellation callback from CHTTP into the application worker;
- a public deferred-cancel/abandon API;
- WebSocket opening deferral for H1 Upgrade or RFC 8441 Extended CONNECT;
- unbounded control queues or unbounded response buffering;
- a new H2-only public handle type;
- a generic future/promise/task abstraction.

The application remains responsible for eventually terminalizing every admitted
piece of asynchronous work. If the transport has already disappeared, its later
`chttp_server_deferred_reply()` call releases the lease and reports that
terminal transport condition instead of sending a response.

## Candidate comparison

### Make `chttp_server_deferred.impl` point to either a connection or a stream

Rejected. `deferred_reply()` would need to infer which private type the pointer
represents, effectively creating a hidden tagged pointer in a public ABI. More
importantly, an H2 stream slot is reset/reused while a worker may still hold the
handle, so the pointer itself is not a sufficient generation-safe lifetime
boundary.

### Pin every deferred H2 stream until the application replies

Rejected as the default model. It is simple, but a peer can issue RST_STREAM
against deferred requests and consume the server's H2 stream capacity until
unrelated application work eventually finishes. The accepted application work
may remain bounded and outstanding, but transport concurrency should be released
as soon as no worker is reading the stream-owned builder.

### Add a stable deferred-control lease separate from the H2 stream slot

Selected. Public handles always point to stable deferred controls. H2 stream
records and deferred controls have independent reuse generations. A PENDING
stream can detach from a canceled control immediately; only a WRITING stream is
briefly quarantined because the worker is actively reading its base response
builder.

## Public API compatibility

The public layout remains unchanged:

```c
typedef struct chttp_server_deferred {
  void *impl;
  uint32_t generation;
  uint32_t reserved;
} chttp_server_deferred;
```

`reserved` remains reserved and zero. No public tag, stream id, connection id,
or slot index is encoded into the handle.

The existing functions remain the only public surface:

```c
int chttp_server_response_defer(chttp_server_response *response,
                                chttp_server_deferred *out_deferred);

int chttp_server_deferred_reply(chttp_server_deferred *deferred,
                                const chttp_server_deferred_response *response);
```

The documentation changes from "HTTP/1.1 response" to "HTTP response" and
states the H2 semantics explicitly. The C ABI layout and function signatures do
not change.

`chttp_server_deferred_response` remains an in-memory copied response. Existing
response headers set before `defer()` are retained. Mutation through the
handler-scoped response after successful defer remains rejected with
`SALTS_EALREADY`.

## Internal deferred control

A private `chttp_server_deferred_control` becomes the sole fact source for one
public deferred lease. It contains, conceptually:

```text
server owner pointer
lease generation
atomic lease state
atomic cancel-requested flag
transport kind: H1 or H2
transport owner/link metadata
pointer to the original request response builder while attached
private deferred reply builder
H2 stream generation/link when applicable
```

The exact private fields may differ, but these ownership rules are fixed:

- the control record itself has server lifetime;
- public `impl` always points to the control record;
- `generation` increments on every new lease and never uses zero;
- the handler/request response builder remains transport-owned;
- the private deferred reply builder is control-owned;
- only the worker that owns WRITING mutates the deferred reply builder;
- only the server owner submits or drops a READY response;
- cancellation flags/state transitions are atomic and never require a worker to
  touch H2 protocol state.

The current direct `chttp_server_response_builder.connection` coupling should be
replaced by a private defer-target boundary. An H1 response builder resolves to
the connection's control; an H2 response builder resolves to the current stream
and claims a free H2 deferred control when `defer()` is called. This internal
boundary is explicitly protocol-aware; the public handle is not.

## H1 control placement

HTTP/1.1 retains one stable control per `chttp_server_connection`. This preserves
the existing capacity and stop model:

- only one handler may be deferred on that exclusive connection;
- the parser remains paused while the deferred lease is active;
- the connection slot is not reused until the lease is terminal;
- a disconnected deferred connection remains an outstanding application lease
  until the worker eventually calls `chttp_server_deferred_reply()`;
- stop waits for that lease exactly as it does today.

The H1 implementation may migrate its current `deferred_state`,
`deferred_generation`, reply builder, and disconnect flag into the common
control, but this change must not weaken existing H1 tests or behavior.

## H2 deferred-control pool

Each private `chttp_h2_server_connection` owns a fixed array of
`h2_stream_capacity` deferred controls. The pool is created with the H2 server
connection object and therefore remains stable across physical-connection reuse
until server destruction.

A control is claimed only when an ordinary H2 handler successfully calls
`chttp_server_response_defer()`. Synchronous H2 streams do not reserve deferred
controls.

Consequences:

- a live H2 connection can have at most `h2_stream_capacity` outstanding
  deferred application leases;
- a canceled lease may remain in the control pool until application work
  terminalizes it, while its original H2 stream slot is already available;
- synchronous sibling requests remain usable even if the control pool is full;
- a new handler attempting `defer()` when every control is occupied receives
  `SALTS_ENOBUFS` and no handle is published;
- the handler may then synchronously return an application-chosen 429/503 or
  another bounded response;
- no new public capacity field is needed.

This is intentional backpressure on outstanding asynchronous application work,
not on ordinary H2 protocol concurrency.

## H2 stream generation

The private H2 stream slot gains a nonzero generation counter that advances on
every new stream acquisition and is not cleared by ordinary stream reset or
physical H2 connection prepare.

A deferred control attached to H2 captures both:

- the stable stream-slot address/index;
- the stream generation for the request that called `defer()`.

READY submission requires the captured generation to still match the stream.
This is a defensive invariant in addition to the state machine: no response may
be submitted into a later stream generation even if a future bug incorrectly
reuses a slot too early.

HTTP/2 stream ids alone are not the application generation key because the
private H2 connection object itself is reused across physical CNet connection
generations.

## Lease state machine

The common conceptual states are:

```text
IDLE
  |
  | handler defer succeeds
  v
PENDING
  |
  | worker wins deferred_reply ownership
  v
WRITING
  |
  +-- retryable copy error and transport still live --> PENDING
  |
  +-- transport canceled while/after copy -----------> CANCELED
  |
  `-- copied response ready --------------------------> READY
                                                         |
                                                         | server owner submit/drop
                                                         v
                                                        IDLE

PENDING -- transport cancel before writer --> CANCELED
CANCELED -- application acknowledges via deferred_reply --> IDLE
```

`CANCELED` means the protocol can no longer accept the deferred response, but an
application may still hold the generation-checked public lease. The control
therefore stays allocated until that lease is terminalized or until a worker
that already owned WRITING observes cancellation and consumes it.

A successful READY publication consumes/clears the caller's public handle. The
application has then released its lease; only owner-side submission remains.

## `chttp_server_response_defer()` semantics

For an ordinary H2 handler:

1. validate that the call is inside the active CHTTP callback for this server;
2. reject a replied/already-deferred builder;
3. retain the existing rule that Session-backed requests are unsupported;
4. reject WebSocket opening callbacks as outside this feature;
5. claim one IDLE deferred control from the current H2 connection pool;
6. increment/publish a nonzero control generation;
7. attach the current stream pointer/slot and stream generation;
8. publish the base response builder as immutable for deferred-copy purposes;
9. set the handler builder's `deferred` flag;
10. publish `{control, generation}` to the caller only after all preceding steps
    succeed;
11. transition the control to PENDING with release ordering.

No H2 response is submitted when the handler returns with
`response_builder.deferred == true`.

Immediate failure publishes no handle and leaves the handler free to send a
normal synchronous response unless the builder had already been terminalized by
another operation.

## H2 dispatch integration

The ordinary H2 dispatch path changes at exactly one semantic boundary. Today it
runs the shared route/middleware/handler chain and then immediately calls H2
response submission. After this design:

```text
headers/body complete
        |
        v
shared request dispatch
        |
        +-- builder.replied ------> submit ordinary H2 response
        |
        +-- builder.deferred -----> keep stream admitted; submit nothing yet
        |
        `-- neither -------------> existing default/handler behavior
```

The deferred stream remains an active server request until one of these occurs:

- a worker publishes READY and the owner submits the response;
- the peer resets/closes the stream;
- the physical connection terminates;
- server shutdown drains or ultimately terminates the connection according to
  existing H2 rules.

The H2 submit helper should accept the control-owned deferred reply builder
without moving that builder into public/request state. Header filtering,
Content-Length generation, HEAD behavior, response statistics, and HPACK/output
bounds remain the same as synchronous H2 response submission.

## Worker-side `chttp_server_deferred_reply()`

`chttp_server_deferred_reply()` is thread-safe and performs no H2 protocol
operation. Its ownership algorithm is:

1. validate handle and response input;
2. load the control through `impl` and verify the lease generation;
3. if the matching lease is already CANCELED, consume/clear the passed handle and
   return `SALTS_ECANCELED`;
4. CAS PENDING -> WRITING; another writer or already-published reply yields the
   existing duplicate/in-progress error behavior;
5. reset the control-owned work builder;
6. copy existing handler response headers from the attached base builder;
7. copy/replace supplied deferred headers;
8. copy content type/body through the existing bounded reply builder;
9. if a copy/validation/capacity failure occurs while transport is still live,
   reset the work builder and return the lease to PENDING so the caller may
   retry;
10. if transport cancellation was requested at any point after WRITING was
    acquired, cancellation wins over a retryable copy error: consume/clear the
    handle, publish terminal cancellation, wake the owner if a quarantined H2
    stream must be released, and return `SALTS_ECANCELED`;
11. otherwise publish READY with release ordering, clear the public handle, and
    wake the server owner.

The worker never calls nghttp2/H2 protocol functions, changes stream state,
resets request state, or touches sibling streams.

## Retryable versus terminal reply results

The existing important property is preserved: a response-copy failure does not
lose the lease.

While the matching transport remains usable, failures such as invalid supplied
response metadata, response-header capacity exhaustion, body-size overflow, or
allocation failure leave the handle retryable and the control in PENDING.

Transport cancellation is different. Once the exact lease can no longer send a
response, `SALTS_ECANCELED` is terminal for that handle and the call consumes the
passed handle. A later copied/stale handle for an already recycled generation
returns `SALTS_ENOENT`.

A copied duplicate handle racing the original may observe:

- `SALTS_EALREADY` while the matching lease is WRITING or already READY but not
  yet recycled;
- `SALTS_ENOENT` after generation recycle.

No API promises that a copied stale handle can recover historical terminal
cause after its generation has been recycled.

## H2 RST_STREAM and stream-close races

RST_STREAM must affect exactly one application stream and one deferred lease.
The owner-side behavior depends on control state.

### PENDING

The owner wins PENDING -> CANCELED before any worker reads the base response
builder. It then detaches the control from the H2 stream and resets/releases the
stream slot immediately. The canceled control remains stable for the
application's eventual terminal `deferred_reply()` call.

Sibling streams continue normally.

### WRITING

The worker owns the base builder and control reply builder. The owner must not
reset/reuse the application stream slot. Instead it atomically records
cancellation and marks that stream slot quarantined after protocol close.

The worker finishes the bounded copy attempt, observes cancellation, discards
any copied response, consumes the handle with `SALTS_ECANCELED`, publishes that
the writer has left, and wakes the owner. The owner then resets/releases the
quarantined stream slot.

This WRITING interval is the only case where protocol close temporarily delays
application stream-slot reuse.

### READY

The application handle is already consumed. If the peer closes/resets before
owner submission, the owner drops the copied response, resets the control, and
releases the stream. Nothing is reported back to the application because its
successful `deferred_reply()` already transferred response ownership to CHTTP.

## H2 control-pool cancellation accounting

A CANCELED control whose application handle has not yet been terminalized stays
occupied. This is deliberate: accepted asynchronous application work still
exists and its public handle must remain safe until successful server stop.

Such a canceled control does not hold an H2 protocol stream or block synchronous
sibling requests. It only reduces the number of additional H2 handlers that can
successfully call `defer()` on that connection. This makes abandoned application
work visible as bounded backpressure instead of hidden memory growth.

## Physical connection close

If an H2 physical connection fails or closes, every attached PENDING deferred
stream on that connection becomes CANCELED. WRITING leases use the same
cancel-requested handoff described above. READY responses are dropped.

A dead physical connection has no surviving sibling streams, so all its protocol
streams terminate, but stable deferred controls remain valid until each
application lease is consumed.

For simple lifecycle and parity with H1, a `chttp_server_connection` slot whose
old H2 connection still owns non-IDLE deferred controls is not reused for a new
physical peer. This avoids mixing canceled work from one physical connection
generation with another peer on the same server connection record. The bounded
cost is one network connection slot per disconnected peer with outstanding
application work, matching the existing H1 deferred-disconnect model.

## GOAWAY and server stop

Server stop does not cancel admitted deferred work.

Existing H2 shutdown sends GOAWAY and drains already accepted streams. A
PENDING/WRITING/READY deferred stream is already accepted and therefore remains
eligible to complete normally after server-initiated GOAWAY. The owner submits a
READY deferred response while the connection is draining, subject to the same
protocol/output rules as any other in-flight response.

`chttp_server_stop()` succeeds only after both are true:

1. ordinary H1/H2 connections/streams have reached the existing shutdown-ready
   state;
2. every stable H1/H2 deferred control is IDLE.

A canceled control still held by application work therefore keeps stop from
success. A finite stop timeout returns `SALTS_ETIMEDOUT` without destroying the
server or invalidating handles. The application may finish/cancel its own work,
call `chttp_server_deferred_reply()` to consume each lease, and retry stop.

This preserves the existing public rule that a deferred handle must not outlive
a successful server stop.

Peer-sent GOAWAY alone does not manufacture cancellation for an otherwise live
stream. Terminal behavior follows actual stream reset/close or connection
termination reported by the H2 protocol layer.

## Owner progress

The background server owner remains the only thread that submits deferred H1 or
H2 responses.

The existing `chttp_server_deferred_progress()` is extended conceptually to:

- process H1 READY controls;
- process READY H2 controls on active H2 connections;
- release H2 stream quarantines after WRITING cancellation exits;
- clean owner-side terminal state after dropped READY responses;
- preserve fair bounded scanning across configured connection/stream
  capacities.

A worker publishes READY/canceled-writer completion and calls the existing CNet
wake mechanism. No new unbounded MPSC queue is required for #214.

## Memory ordering and thread ownership

The state machine must use explicit acquire/release ordering:

- handler initializes generation/link/base-builder fields before release
  publication of PENDING;
- worker acquires PENDING before reading those fields;
- worker completes all work-builder writes before release publication of READY;
- owner acquires READY before reading the copied builder;
- owner publishes cancellation atomically;
- WRITING prevents owner reset of the attached base builder/stream slot;
- worker checks cancellation before returning a retryable failure or publishing
  READY.

Only one writer may own WRITING. The owner never mutates the control reply
builder while WRITING, and the worker never mutates protocol state.

## Response bounds

No new response-size contract is introduced.

Deferred response headers use the existing
`max_response_header_count/max_response_header_bytes` limits. Deferred in-memory
body uses `max_buffered_response_body_bytes`; aggregate body allocation remains
subject to the existing server buffer accounting. The final H2 wire response
must fit the same HPACK/output bounds as synchronous responses.

Control records are fixed by H2 stream capacity. Their private reply builders may
initialize storage lazily on first WRITING use; such allocation failure is a
retryable deferred-reply failure while the transport remains live. Lazy
initialization avoids eagerly doubling per-stream response-header storage on
servers that never use H2 deferral while preserving a hard maximum number of
builders.

## Session behavior

This change does not expand Session semantics.

`chttp_server_response_defer()` continues to return `SALTS_ENOTSUP` when the
active request exposes a server Session. That rule applies consistently to H1
and H2. A later independent design may define copied transactional Session state
for asynchronous completion, but #214 does not do so.

The internal call to session begin/abort/finish continues to obey existing
request lifecycle rules even when Session capacity is zero.

## JWT admission interaction

The JWT authentication-admission contract merged in #213 is unchanged.

JWT verification still happens before H2 body admission and before the handler.
A protected deferred handler may inspect `request->jwt_claims` during its
callback, but those claims remain callback-borrowed. If asynchronous graph work
needs identity, the application copies the required subject/audience/other
values before returning from the handler.

The deferred control does not extend `jwt_claims` lifetime and does not expose
CJWT ownership to a worker. The request-state JWT owner may remain internally
alive while the stream is live, but stream cancellation/reset destroys it
normally; no worker may depend on that storage.

## WebSocket behavior

Ordinary HTTP deferred response semantics do not apply to H1 WebSocket Upgrade
or RFC 8441 Extended CONNECT opening callbacks.

`chttp_server_response_defer()` is unsupported from a WebSocket opening callback
and returns `SALTS_ENOTSUP`. WebSocket opening already has its own handshake and
captured-session lifetime model; mixing HTTP deferred leases into that model is
outside #214.

This explicit rejection is preferable to accidentally allowing H1 Upgrade to
enter an H1 deferred path that the WebSocket handshake code does not own.

## Error contract

The intended public results are:

### `chttp_server_response_defer()`

- `SALTS_OK`: lease published; handler response sealed;
- `SALTS_EINVAL`: invalid wrapper/output arguments;
- `SALTS_EBUSY`: not called from the active server callback or conflicting
  callback ownership;
- `SALTS_EALREADY`: response already replied/deferred or the request already owns
  a deferred lease;
- `SALTS_ENOTSUP`: Session-backed request or WebSocket opening context;
- `SALTS_ENOBUFS`: no H2 deferred control is available;
- other existing initialization/storage errors only if required before lease
  publication; no partially published handle on failure.

### `chttp_server_deferred_reply()`

- `SALTS_OK`: response ownership transferred to CHTTP; handle cleared;
- `SALTS_EINVAL`: invalid handle/response input or invalid response metadata;
- `SALTS_EALREADY`: matching lease currently WRITING/already transferred but not
  recycled;
- `SALTS_ENOENT`: stale/recycled generation;
- `SALTS_ECANCELED`: exact lease lost its transport before transfer; terminal
  handle consumed;
- bounded copy/allocation errors such as `SALTS_ENOBUFS`, `SALTS_EMSGSIZE`, or
  `SALTS_ENOMEM`: retryable while transport remains live; handle unchanged.

If cancellation races with an otherwise retryable copy error, cancellation is
authoritative and `SALTS_ECANCELED` wins.

## Sibling-stream isolation

The design requires a same-connection regression proving all of the following
at once:

1. stream A enters deferred PENDING;
2. stream B completes an ordinary synchronous response while A is pending;
3. RST_STREAM on A does not reset the H2 connection or B;
4. a later stream C can execute on the same connection after A's protocol slot
   is retired;
5. A's old application handle remains generation-safe and terminalizes without
   affecting B/C;
6. a WRITING cancellation race also releases only A after the writer exits.

Connection-level failure may of course terminate all sibling streams; that is
not confused with stream-scoped RST behavior.

## Capacity behavior tests

A specific H2 control-capacity test is required so deferred-work capacity is not
accidentally conflated with stream capacity:

- configure a small `h2_stream_capacity`;
- create deferred streams and cancel their protocol streams while retaining the
  application handles, leaving controls CANCELED but stream slots reusable;
- open a new ordinary synchronous stream and prove it still succeeds;
- attempt another `defer()` when every control is occupied and require
  `SALTS_ENOBUFS`;
- terminalize one canceled handle;
- retry on a later stream and require `defer()` to succeed.

This demonstrates explicit bounded application backpressure without damaging
ordinary HTTP/2 service.

## Required deterministic race coverage

Timing sleeps are insufficient for the WRITING cancellation contract. Tests must
have a private deterministic synchronization seam or equivalent barrier that
can hold `deferred_reply()` after it owns WRITING but before it publishes its
result.

The test then issues RST_STREAM, proves the stream slot is quarantined rather
than reused during WRITING, releases the worker, requires terminal cancellation,
and proves the slot becomes reusable afterward.

The seam is test/private only; no public hook is introduced.

## Verification matrix

At minimum the implementation must prove:

### H1 compatibility

- existing deferred success path unchanged;
- cross-thread reply unchanged;
- retryable response-copy failure unchanged;
- duplicate/stale generation behavior unchanged;
- disconnect and stop/drain behavior remain bounded;
- existing H1 parser pipelining/deferred tests remain green.

### H2 h2c

- happy-path deferred GET/POST response;
- worker-thread reply;
- existing response headers retained across defer;
- response header override/addition in deferred reply;
- body/header capacity failures remain retryable;
- sibling synchronous stream progresses while one stream is PENDING;
- PENDING RST releases stream slot and later handle returns terminal cancel;
- deterministic WRITING/RST race;
- READY response submission exactly once;
- duplicate handle and stale generation;
- deferred-control pool exhaustion/recovery;
- physical connection close with outstanding deferred work;
- stop timeout while a lease is outstanding, then successful retry after lease
  terminalization;
- server-initiated GOAWAY drains accepted deferred streams;
- synchronous siblings remain usable after stream-scoped cancellation.

### H2 TLS ALPN

Repeat the essential happy-path, sibling isolation, RST cancellation, and stop
cases over TLS with ALPN fixed to `h2`. No h2c/H1 fallback is allowed.

### Integration boundaries

- JWT-protected H2 route may defer after copying any needed claims;
- Session-backed H1/H2 defer remains `SALTS_ENOTSUP`;
- WebSocket opening defer remains `SALTS_ENOTSUP`;
- public C header compiles as C11;
- public header compiles from C++;
- Linux, Windows, and macOS exact-head release tests pass.

## Production file boundaries

Expected implementation scope is limited to CHTTP:

- `chttp/include/chttp/chttp.h` — documentation only unless a private-neutral
  comment requires synchronization;
- `chttp/src/chttp_server_runtime.h` — common deferred-control/target state;
- `chttp/src/chttp_server_response.c` — protocol-neutral defer/reply lease logic;
- `chttp/src/chttp_server.c` — H1 target/control integration, owner progress,
  stop/outstanding-work accounting;
- `chttp/src/chttp_h2_server.h` — private H2 deferred progress/cancel boundary;
- `chttp/src/chttp_h2_server.c` — control pool, stream generation/quarantine,
  deferred dispatch, response submit/drop, RST/connection-close integration;
- CHTTP tests and README.

CNet, CFlow, TurboFlow, CJWT, and H2 protocol-core semantics should not need
production changes for #214. If implementation discovers that the private H2
protocol layer lacks a required exact stream-close signal, that is an
architecture escalation and must stop for review rather than expanding scope
silently.

## Downstream TurboFlow contract

After #214, TurboFlow #7 may treat H1 and H2 ordinary HTTP handlers uniformly:

```text
CHTTP handler
  -> copy graph-owned request fields
  -> attempt graph admission
  -> if graph admission fails: synchronous bounded 429/503
  -> if graph admission succeeds: chttp_server_response_defer()
  -> retain only owned graph data + chttp_server_deferred
  -> graph terminal completion calls chttp_server_deferred_reply()
```

TurboFlow must still ensure every accepted graph run reaches a terminal reply
attempt for success, error, timeout, cancellation, and shutdown. A transport
that disappeared produces terminal `SALTS_ECANCELED`; TurboFlow must release its
job and must not retry through a different HTTP protocol or connection.

## Compatibility and versioning

This is an additive protocol-capability extension of an existing pre-v1 public
function. It does not change the size/layout of `chttp_server_deferred`,
`chttp_server_deferred_response`, or `chttp_server_config`, and does not add a new
public symbol.

No ABI bump is required by this design. Documentation and tests must stop saying
that `chttp_server_response_defer()` is H1-only once H2 parity is implemented.

## Rollback boundary

The implementation is rollback-safe because the public shape does not change.
Before downstream TurboFlow depends on H2 deferral, the H2-specific internal
control pool/progress code can be removed and the existing explicit
`SALTS_ENOTSUP` behavior restored without changing H1 deferred semantics.

No compatibility fallback layer is added.

## Completion criteria

Issue #214 is complete only when all of the following are true:

- ordinary H2 handlers can defer and complete from another thread;
- one stable generation-checked public handle contract covers H1 and H2;
- H2 stream slot reuse cannot race worker-side response copying;
- PENDING cancellation releases the stream slot without invalidating the
  application handle;
- WRITING cancellation quarantines only the affected stream until the bounded
  writer exits;
- sibling streams survive RST_STREAM and deferred-capacity exhaustion;
- canceled/stale/duplicate/retryable-copy outcomes are explicit and tested;
- server GOAWAY/stop/drain waits for admitted deferred leases without destroying
  valid handles;
- h2c and TLS ALPN h2 parity tests pass;
- H1 deferred behavior remains green;
- public C/C++ header tests and README are updated;
- Linux, Windows, and macOS exact-head validation passes;
- no streaming deferred body, Session expansion, WebSocket defer, fallback, or
  unrelated transport capability is included.
