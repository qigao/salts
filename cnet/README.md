# CNet (experimental)

CNet is the connection-oriented layer above NativeIO. Applications see only a
client, generation-checked connections, send/receive operations, and ordered
state notifications. NativeIO remains a raw, threadless operating-system I/O
backend; CFlow Actor and Reactive code continue to depend on NativeIO directly.

The target is currently gated by `CNET_ENABLE_EXPERIMENTAL` and is not installed.
Its source-tree target is `turbo_cnet_experimental`.

## Base API

Include `<cnet/cnet.h>`, initialize one bounded `cnet_client_config`, then use:

- `cnet_connect` with `tcp://host:port`, `udp://host:port`, or `pipe://name`;
- `cnet_send` to transfer one bounded payload copy into CNet;
- `cnet_receive` to add explicit receive demand;
- `cnet_close` for one connection;
- `cnet_client_stop` followed by `cnet_client_destroy` for service shutdown.

TCP and Pipe deliver byte chunks. Connected UDP delivers one datagram per
receive callback. A receive view is borrowed only until its callback returns.
TLS, WebSocket, and KCP are not exposed by this base header.

## Ownership and threads

Each I/O shard has one long-lived owner task and one NativeIO backend. The
session table is the sole connection-state fact source. Separate callback lanes
serialize callbacks for one connection while permitting different connections
to run concurrently. After NativeIO progress, the same owner transfers bounded
event leases directly into callback lanes; CNet does not start a separate
dispatcher worker. No user callback runs on an I/O owner or while an internal
lock is held.

The URI, observer, and send bytes are copied before their admitting call returns
success. A callback may call `cnet_send`, `cnet_receive`, or `cnet_close` for its
client. Calling `cnet_client_stop` or `cnet_client_destroy` from that callback
returns `TURBO_EBUSY` because either operation would otherwise wait on itself.

## Shutdown and errors

`cnet_client_stop(client, timeout_ms)` closes new admission, drains live
connections and terminal callbacks, and joins callback and owner workers.
`TURBO_ETIMEDOUT` is retryable and preserves the client. Destroying a client
before a successful stop returns `TURBO_EBUSY`.

Immediate connect validation failure clears the output handle and emits no
callback. Asynchronous failures emit exactly one `CNET_CONNECTION_FAILED` with
a stable stage string. TurboUtils status codes use `cnet_error.status`; a raw
platform status is normalized to `TURBO_EIO` and retained in
`cnet_error.native_status`.

The executable public contract is in `tests/cnet_api_test.c`; it covers TCP,
connected UDP, platform Pipe, callback reentrancy, stale handles, live drain,
timeout retry without duplicate terminal delivery, and multi-value receive
demand across request-slot reuse.

## Dispatch topology decision

The previous client used three scheduled stages: NativeIO owner, dispatcher
worker, then callback worker. The middle worker changed neither connection
state nor payload ownership; it only moved an event lease from the shard ring
to a callback lane. That extra queue and wake applied a fixed cost to every
small message.

The selected topology keeps two execution roles:

- one NativeIO owner per shard owns transport progress, request records,
  deadlines, and the session state machine;
- callback workers own user code and preserve per-connection callback order.

The owner invokes a nonblocking internal event sink after each drive. The sink
copies no payload, publishes the existing event lease to its callback lane, and
retains the lease on bounded backpressure. This removes the dispatcher thread
without moving user callbacks onto the I/O owner.

NativeIO coroutines were considered for the receive loop. They can make a
single owner's control flow linear, but they do not remove the required
owner-to-callback handoff and do not by themselves preserve CNet's deadline,
cancel, and terminal-record protocol. They therefore remain a NativeIO
implementation tool rather than a second CNet execution model.

This change has no public API migration: applications keep the same callback
threading, ownership, ordering, error, and shutdown contracts. The tradeoff is
that a full callback lane temporarily keeps its shard owner in bounded retry
instead of parking a separate dispatcher worker. Capacity remains fixed and no
fallback allocation is introduced. A rollback would restore the dedicated
dispatcher task while leaving the event and callback lease contracts intact.

## Benchmark

`cnet_io_benchmark` compares the libuv client API, direct NativeIO, and the CNet
public byte API against the same dedicated blocking loopback echo peer. Fixture,
connection, worker, and buffer setup stay outside the timed interval. All three
clients use identical payloads, warmups, samples, and persistent TCP/UDP
connections; runner order rotates per payload row. Output separates TCP and UDP
p50, p95, and round-trips-per-second tables. Every delta uses libuv as its
denominator. TCP covers 1/4/8/16/32/64 KiB; UDP uses the cross-platform common
single-datagram sizes 1/4/8 KiB. UDP CNet demand is admitted once before the
timed exchanges, so its data-path result does not include one redundant demand
command and owner wake per datagram.
