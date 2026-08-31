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
session table is the sole connection-state fact source. NativeIO completion
resumes an owner-affine coroutine, and the owner invokes the connection
callback inline without a callback queue or second worker. Callbacks for one
connection remain FIFO and non-overlapping; different shards may invoke
callbacks concurrently. No internal lock is held while user code runs.

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
connections and terminal callbacks, and joins the owner workers.
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

The previous client moved each owner event through a per-shard SPSC channel to
a callback worker. That handoff changed neither connection state nor payload
ownership and applied a fixed queue, wake, scheduling, and payload-copy cost to
every small message.

The selected topology keeps one execution role per shard:

- one NativeIO owner per shard owns transport progress, request records,
  deadlines, the session state machine, coroutine resumption, and callbacks.

The owner invokes its internal dispatcher after a coroutine processes a
completion. The dispatcher generation-checks the observer, invokes it inline,
and recycles terminal state after the callback returns. The borrowed receive
view therefore remains valid without another payload copy and expires when the
callback returns. User callbacks must not block; applications that need a
business executor must copy or retain their own data and dispatch explicitly.

Coroutines are an owner-local control-flow tool, not a second CNet execution
model. CNet keeps the session table as the state fact source and keeps deadline,
cancel, terminal-record, command ownership, and callback backpressure outside
the frame. NativeIO allocates frames lazily from a pool bounded by
`request_capacity` and reuses them after their first terminal completion; peak
concurrent awaits therefore bound both active and retained coroutine storage.
There is no owner-to-callback handoff. The MPSC command ring remains necessary
for public operations originating outside the owner thread. This changes the
experimental callback execution contract: callbacks now run on the owning I/O
shard and must remain nonblocking. It removes `callback_workers` from client
configuration and removes one bounded payload copy. No fallback allocation is
introduced.

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
