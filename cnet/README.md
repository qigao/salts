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
session table is the sole connection-state fact source. Each shard owns one
SPSC callback channel, and callback workers consume a fixed set of those
channels. This preserves FIFO delivery for every connection on a shard and
permits different shards to run callbacks concurrently. After NativeIO
progress, the owner copies the event descriptor and bounded payload directly
into its callback channel; CNet does not start a separate dispatcher worker or
publish the normal path through an intermediate event ring. No user callback
runs on an I/O owner or while an internal lock is held.

TCP connect, send, and receive operations execute as owner-affine NativeIO
coroutines. A public caller never resumes a frame: it only publishes an MPSC
command and wakes the shard. The shard starts `native_io_coroutine_await()`, and
its own NativeIO observation resumes the same frame with the matching terminal
completion. Cancellation marks the awaited request but retains the command,
payload, request record, and frame until `CANCELLED` or the competing terminal
completion is observed.

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
state nor payload ownership; it only moved an event from the shard ring to a
callback queue. That extra queue and wake applied a fixed cost to every small
message.

The selected topology keeps two execution roles:

- one NativeIO owner per shard owns transport progress, request records,
  deadlines, and the session state machine;
- callback workers own user code and preserve per-connection callback order.

The owner invokes a nonblocking internal event sink after each drive. The sink
copies the payload once into a fixed-capacity per-shard SPSC channel before
returning; this copy is the callback-lifetime boundary that lets the owner reuse
its NativeIO receive storage. A full channel leaves the event pending with the
owner, stops read rearming, and returns bounded backpressure. Callback workers
drain at most 32 entries from one shard before checking the next shard assigned
to that worker. This removes the dispatcher thread and MPSC callback queue
without moving user callbacks onto the I/O owner.

Coroutines are an owner-local control-flow tool, not a second CNet execution
model. CNet keeps the session table as the state fact source and keeps deadline,
cancel, terminal-record, command ownership, and callback backpressure outside
the frame. NativeIO allocates frames lazily from a pool bounded by
`request_capacity` and reuses them after their first terminal completion; peak
concurrent awaits therefore bound both active and retained coroutine storage.
The context switch does not remove the required owner-to-callback handoff, so
benchmark results still include that SPSC publication and callback wake.

This change has no public API migration: applications keep the same callback
threading, ownership, ordering, error, and shutdown contracts. The tradeoff is
that a full callback channel temporarily keeps its event pending on the shard
owner. Capacity remains fixed, payloads have one bounded callback-boundary
copy, and no fallback allocation is introduced. The MPSC command ring remains
necessary because public operations may originate from arbitrary threads.

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
