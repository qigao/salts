# CHTTP HTTP/2 WebSocket Session Pool Design

**Date:** 2026-09-03

## Context

The existing requests-style WebSocket client owns one transport and one WebSocket session. RFC
8441 permits an HTTP/2 connection to carry multiple Extended CONNECT WebSocket streams alongside
ordinary HTTP/2 streams. Applications need that multiplexing without driving CNet or an HTTP/2
poller themselves.

## Decision

Add an HTTP/2-only `chttp_websocket_pool` facade. The pool owns exactly one CNet connection and one
HTTP/2 protocol engine. Each successful `chttp_websocket_pool_open()` allocates one fixed-capacity
slot and creates one RFC 8441 Extended CONNECT stream. The existing single-session
`chttp_websocket_client` remains unchanged and source/ABI compatible.

Alternatives considered:

- A collection of existing clients would create one TCP/TLS connection per WebSocket and would not
  provide HTTP/2 multiplexing.
- Extending `chttp_websocket_client` to change from one session to many would break its lifecycle
  and handle model.
- A new pool facade keeps the current API stable and makes shared-connection ownership explicit.

## Public API

```c
typedef struct chttp_websocket_pool { void *impl; } chttp_websocket_pool;

typedef struct chttp_websocket_session {
  uint32_t slot;
  uint32_t generation;
} chttp_websocket_session;

typedef struct chttp_websocket_pool_config {
  size_t size;
  chttp_websocket_client_config client;
  size_t session_capacity;
} chttp_websocket_pool_config;
```

The operations are `init`, `open`, `send_text`, `send_binary`, `send_ping`, `send_pong`, `receive`,
`close`, and `destroy`. `open` reuses `chttp_websocket_connect_options`; the protocol must be
`CHTTP_HTTP_2`. The first open fixes the pool origin, transport security, TLS profile, and physical
connection. Later opens may use different paths or queries, but must name the same normalized
origin and use the same TLS profile.

The API is requests-style and blocks internally until the requested operation completes or its
deadline expires. The user never calls a poller. The pool is single-owner and rejects a reentrant or
concurrent operation with `SALTS_EBUSY`.

## Ownership and bounded data path

| Resource | Owner | Lifetime and invalidation |
| --- | --- | --- |
| CNet client/connection, TLS profile reference, H2 engine | pool | `init` through successful `destroy` |
| WebSocket parser, event ring, event payload slab, pending frame buffer | session slot | slot acquisition through close/failure/release |
| `chttp_websocket_session` | caller | generation-checked value handle; stale after close or slot reuse |
| received event view | session slot | invalid after the next operation on the same pool |

Worst-case slot, event, payload, frame, HTTP/2, HPACK, and transport storage is reserved during pool
initialization. Opening a slot initializes its CNet WebSocket parser with the already validated hard
bounds and allocates a bounded HTTP/2 header-descriptor array. `session_capacity`, every event ring,
every payload slab, HTTP/2 input/output, HPACK, and CNet capacities are hard bounds. Arithmetic is
checked before allocation. Local capacity or peer
`MAX_CONCURRENT_STREAMS` exhaustion returns `SALTS_ENOBUFS`; no second physical connection and no
unbounded fallback are created.

The topology is single-threaded/single-owner: public operations synchronously drive CNet, whose
callbacks run on that same owner. No mutex or lock-free queue is required. A stream callback scans
the bounded slot array by stream id; slot state is the single fact source.

## State and failure model

Pool states are disconnected, connected, terminal, and stopping. Slot states are free, opening,
open, closing, and terminal.

- A response status other than 200 fails only the opening stream and is returned together with its
  HTTP status.
- Frame parse errors, event overflow, RST_STREAM, or premature END_STREAM terminate only the
  affected slot.
- Connection failure, GOAWAY that excludes a stream, or protocol-connection failure terminates all
  affected active slots.
- Closing one session sends its RFC 6455 Close frame and then ends only its HTTP/2 stream. Sibling
  sessions and the physical connection remain usable.
- Destroy stops admission, closes active sessions within the shared deadline, then closes the
  physical transport. A timeout leaves the pool allocated so the caller may retry destruction.

HTTP/2 flow-control credit is returned after each DATA callback has synchronously copied complete
events into the owning slot's bounded queue. Event-queue overflow resets that stream rather than
pausing the whole multiplexed connection.

## Compatibility, migration, and rollback

This is an additive C ABI change using opaque storage and value handles. Existing
`chttp_websocket_client_*` users require no migration. Applications that need multiplexing replace
one client per WebSocket with one pool plus session handles; H1 remains on the existing client.

The implementation is isolated in a new source file and can be rolled back by removing the new
symbols and source entry without changing existing behavior. No third-party dependency is added.

## Verification

Tests must demonstrate:

1. Two paths open as two RFC 8441 sessions on one accepted TCP connection.
2. Messages remain associated with their session.
3. Closing one session leaves the sibling usable.
4. Capacity and peer concurrency limits fail explicitly.
5. Stale handles are rejected after close/reuse.
6. Invalid protocol/origin/configuration fails before changing shared state.
7. Existing H1, H2, WS, and WSS tests remain green.
