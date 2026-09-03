# CHTTP HTTP/1.1 Server Design

## Decision

CHTTP gains an HTTP/1.1 application server with explicit instances, bounded
static or named-segment routes, middleware chains, in-memory Cookie Sessions,
request-scoped views, response builders, and a background CNet progress owner.
Ordinary users register handlers and start or stop the server; they never call
a network poller.

The first supported transport is plain TCP. TLS, HTTP/2, HTTP/3, WebSocket,
streaming responses, asynchronous suspended handlers, and persistent or
distributed Session stores are not declared by this API. They can be added as
separate, fully specified layers without weakening the HTTP/1.1 lifecycle
below.

## Evidence and reference boundary

- Iris separates an application instance, route registration, request parsing,
  handler dispatch, and listener lifecycle. Its handler receives request and
  response objects scoped to one request.
- CHTTP already owns strict llhttp integration, HTTP method/header types, and a
  requests-style client that hides CNet polling.
- Before this design, CNet owned outgoing NativeIO connections but had no
  listener or accepted-socket admission path.

The server follows Iris's application-facing shape, but uses CHTTP names,
Salts error codes, hard capacities, CNet connection handles, and C11 ownership
rules. It does not copy Iris's global default application or unbounded route and
request allocation behavior.

## Candidate comparison

### Put native sockets directly in CHTTP

This is the smallest patch, but it makes CHTTP bypass CNet for incoming
connections and duplicates transport lifecycle code. It would also make the
claim that CHTTP is built on CNet false for half of the library.

### Add accept to every NativeIO backend

This gives one completion model for listening and connected sockets, but IOCP
AcceptEx, epoll, io_uring, and kqueue require a much larger backend change. The
HTTP/1.1 server does not need that expansion to preserve a single owner for the
connected data path.

### Add a CNet listener and adopted-connection admission

Selected. A small platform adapter owns a nonblocking TCP listener. Accepted
sockets are transferred into the existing CNet/NativeIO owner through the same
bounded command/session path as outgoing connections. The listener is polled in
short bounded slices by the same owner thread; connected reads and writes remain
NativeIO operations.

## Public CHTTP model

The public types are:

- `chttp_server`: one explicit server instance.
- `chttp_server_config`: bind address, CNet capacities and deadlines, route
  capacity, request limits, and response limits.
- `chttp_server_request_view`: method, target, path, version, headers, body,
  keep-alive decision, route params, and optional Session borrowed only during
  one handler invocation.
- `chttp_server_response`: opaque request-scoped response builder.
- `chttp_server_middleware` and `chttp_server_next`: ordered global/route
  middleware and a continuation that accepts exactly one call.
- `chttp_session`: request-scoped access to bounded server-side key/value state.
- `chttp_server_stats`: a thread-safe snapshot of admission and request counts.

The lifecycle is:

1. `chttp_server_init()` validates every capacity and preallocates all route,
   parser, body, header, and response storage.
2. `chttp_server_route()` or method conveniences copy static or `:name` route
   patterns, per-route middleware, and callback bindings. `chttp_server_use()`
   copies global middleware. Registration is rejected after start begins.
3. `chttp_server_start()` synchronously initializes CNet and binds the listener,
   then launches the background owner. Port zero asks the OS to select a port;
   `chttp_server_port()` reports it after start.
4. Handlers call `chttp_server_response_set_header()` and
   `chttp_server_reply()`. Both copy data into bounded request storage before
   returning.
5. `chttp_server_stop()` closes listener admission and waits up to the caller's
   timeout for accepted connections and CNet operations to drain. Timeout is
   retryable.
6. `chttp_server_destroy()` requires a stopped or never-started server and
   releases all control-plane storage.

Handler callbacks are ordered and non-concurrent on the server owner thread.
They must not block or call server stop/destroy. Castle-style templates and
static resources can be loaded before start and rendered synchronously. A true
asynchronous database/file/Actor path needs a future owning request token and
thread-safe resume mailbox; merely forwarding borrowed views to an executor is
invalid and is not implied here.

## Routing and protocol behavior

Routes use one `chttp_method` plus one origin-form path pattern. A complete
segment beginning with `:` binds a raw, non-percent-decoded named parameter;
static matches take precedence over parameter matches, then registration order
is stable. The query string is retained in `target` but excluded from `path`
matching. Duplicate method/path registration fails. A known path with another
method produces 405 plus Allow; an unknown path produces 404. HEAD falls back
to GET when no explicit HEAD route exists, sends no response body, and retains
the selected representation's Content-Length.

Global middleware wraps matched and built-in 404/405 dispatch. Route
middleware then runs in registration order. A middleware can call
`chttp_server_next_call()` once or short-circuit by completing the response.
Calling the same continuation twice returns `SALTS_EALREADY`.

Strict llhttp request parsing supports fragmented messages, Content-Length and
chunked bodies, HTTP/1.0, HTTP/1.1, sequential keep-alive requests, and bounded
pipelined messages present in one receive callback. HTTP/1.1 requires one
non-empty Host header. An HTTP/1.1 `Expect: 100-continue` receives an interim
response and other HTTP/1.1 expectations produce 417; HTTP/1.0 expectations
are ignored. Until a WebSocket route exists, Upgrade invitations are ignored
and the request is handled as ordinary HTTP, without sending an incomplete 426.
The request target is fragment-free origin-form. HTTP/1.1 accepts only the exact
`chunked` transfer coding; an unsupported coding produces 501 and HTTP/1.0
Transfer-Encoding produces 400. Because the public request view does not expose
trailers separately, every non-empty trailer section is rejected instead of
being merged into ordinary headers. Malformed syntax produces 400, an oversized
target 414, oversized body or chunk-extension framing 413, oversized headers
431, and an unsupported HTTP version 505. Protocol errors close after the error
response is sent.

Responses serialize a status line, Connection, optional Content-Type, copied
application headers, and the permitted body. Content-Length is emitted except
for 204 and 304 responses. Application code cannot override Content-Length,
Connection, or Transfer-Encoding. Header names and values reject
control-character injection.

## Data and memory protocol

### Data unit and fact source

One accepted TCP connection maps to one CNet generation-checked handle and one
CHTTP slot. The CNet handle is the connection identity; the slot's llhttp state
is the only source of truth for the current request. The immutable route and
middleware table is the only source of truth for dispatch. A Session record is
the sole source of truth for its server-side values; the Cookie contains only
its 128-bit CSPRNG identifier.

### Ownership and lifetime

- The server copies its configuration and route paths during control-plane
  calls.
- CNet owns an accepted socket after adopted admission succeeds; the listener
  closes it on admission failure.
- Parser callback fragments are copied into per-slot target, header, and body
  storage.
- Request pointers expire when the handler returns. They must not cross a
  callback, thread handoff, coroutine suspension, parser reset, or slot reuse.
- Route parameters and Session values obey the same handler-scoped borrowed
  lifetime. Session setters copy strings into fixed record storage.
- Response helpers copy headers and body before returning. `cnet_send()` or the
  atomic send-and-close operation copies the serialized response before the
  slot wire buffer is reused.

### Topology and ordering

There is one CHTTP owner thread, one CNet progress owner, one listener, and up
to `connection_capacity` connection slots. Handler callbacks never overlap.
Per-connection CNet writes retain FIFO order, so responses follow parsed
request order. Route registration and destroy are quiescent control-plane
operations.

### Capacity and backpressure

Every growing resource has a hard configured bound: routes, connections,
CNet commands, CNet requests, event batches, receive chunks, target bytes,
request and response header counts/bytes, request and response bodies, route
parameters, global/route middleware, Session records/entries/key/value bytes,
chunk-extension lines, raw request-line/header/trailer framing, and the
serialized response. Configuration validation uses checked arithmetic and
rejects impossible aggregate sizes.

A full CNet connection or command capacity rejects and closes the newly
accepted socket. A full HTTP parser or response builder produces a protocol
error where possible, then closes the connection. No path converts bounded
pressure into unbounded allocation.

Expired Session records are reclaimed before allocation. If every Session slot
is still live, a new `chttp_session_set()` returns `SALTS_ENOBUFS`; it does not
silently evict live application state. `chttp_session_invalidate()` clears the
record and emits an expired Cookie. Session state is owner-thread-only and does
not require a handler-side lock.

### Failure and shutdown

Listener bind, allocation, parser setup, route registration, and thread startup
fail before publishing a running server. Once start succeeds, transport and
protocol failures are isolated to their connection and recorded in stats.
Internal CNet progress failure stops admission and becomes the server's terminal
status.

Stop changes the server state exactly once: running to stopping to stopped. It
first closes listener admission, then requests connection shutdown, drives
terminal callbacks, destroys the CNet owner on its own thread, and signals
waiters. A timed-out caller does not detach or free live state and may retry.

## Compatibility and migration

Existing CHTTP client types and functions are unchanged. The shared
`chttp_method` and `chttp_header` types are reused. Existing CNet function
signatures remain source compatible; listener and send-and-close functions are
additive, and `cnet_observer.on_send` is appended after the pre-existing fields
so positional initializers keep their meaning. Close admission now immediately
enforces the documented closing semantics: a second close returns
`SALTS_EALREADY`, while later send/receive admission returns `SALTS_EBUSY` even
before the owner polls the first close. Installed C and C++ consumers compile
the new declarations.

## Verification

- CNet unit and real-socket tests cover listener validation, ephemeral port,
  accepted-socket adoption, bounded admission, data exchange, and send-then-
  close ordering.
- CHTTP parser and server tests cover fragmented input, methods, headers,
  bodies, 100-continue, limits, static/dynamic precedence, named params,
  middleware ordering/continuation, 404/405, HEAD, keep-alive reuse, Session
  continuity/capacity/invalidation, connection close, start/stop, and no caller
  poll.
- Existing CNet, CHTTP, and CRPC tests guard adjacent behavior.
- Installed C/C++ consumers and header tests guard package/API compatibility.
- Release and sanitizer builds verify cleanup and use-after-free boundaries.
