# CNet

CNet is the connection-oriented layer above NativeIO. Applications see a
client, generation-checked connections, send/receive operations, explicit
progress polling, and ordered state notifications. NativeIO remains the raw,
threadless operating-system I/O backend; CFlow Actor and Reactive code continue
to depend on NativeIO directly.

CNet is built unconditionally. Its source-tree target is `turbo_cnet`; installed
consumers link `TurboUtils::CNet` and include `<cnet/cnet.h>`. The independent
WebSocket session API is declared by `<cnet/websocket.h>`.

## Base API

Include `<cnet/cnet.h>`, initialize one bounded `cnet_client_config`, then use:

- `cnet_connect` with `tcp://host:port`, `tls://host:port`, `udp://host:port`, or
  `pipe://name`;
- `cnet_send` to transfer one bounded payload copy into CNet;
- `observer.on_send` to observe completion before admitting the next ordered
  write on that connection;
- `cnet_receive` to add explicit receive demand;
- `cnet_close` for one connection;
- `cnet_client_poll` to advance I/O and invoke callbacks on the caller;
- `cnet_client_stop` followed by `cnet_client_destroy` for shutdown.

TCP and Pipe deliver byte chunks. Connected UDP delivers one datagram per
receive callback. A receive view is borrowed only until its callback returns.
TLS delivers verified encrypted byte streams through the same send/receive
contract. WebSocket and KCP are not exposed by this base header. CNet parses
TCP, TLS, and UDP URIs through TurboUtils UriParser and then applies
transport-specific constraints: network URIs require an explicit port and reject
userinfo, path, query, and fragment components instead of accepting truncated or
ambiguous input. Pipe is a scheme-specific IPC endpoint rather than a network
authority, so its bounded name after `pipe://` is preserved byte-for-byte.

## WebSocket session engine

`<cnet/websocket.h>` provides a transport-independent RFC 6455 session after a
successful HTTP Upgrade. It handles text/binary messages, fragmentation,
ping/pong, close handshakes, client masking, strict UTF-8 validation, and role
masking rules. Input chunks may split or coalesce frames; event payloads are
borrowed only until their callback returns.

Initialization allocates fixed-capacity input, reassembled-message, and
single-frame output storage. `max_frame_bytes`, `max_message_bytes`, and
`max_buffered_input_bytes` are mandatory. A write callback returning
`TURBO_EBUSY` retains exactly one complete frame; the owner calls
`cnet_websocket_flush()` before feeding or sending more data. Other write errors
move the session to `CNET_WEBSOCKET_FAILED` and are available through
`cnet_websocket_last_error()`. A peer Close commits `CLOSING` before its event
callback and becomes `CLOSED` only after any retained echo Close is transferred.

The engine owns protocol state but performs no socket I/O and creates no thread.
It is single-owner and can therefore be driven by a CNet callback, Executor,
Actor mailbox, or another ordered byte-stream adapter. CHTTP remains responsible
for HTTP/1.1 Upgrade routing/header validation and future HTTP/2 extended
CONNECT. `cnet_connect()` does not currently accept `ws://` or `wss://`, and
CHTTP does not yet expose WebSocket routes.

The following complete adapter example sends one server-side text frame into a
bounded transport sink:

```c
#include <cnet/websocket.h>

#include <string.h>

typedef struct frame_sink {
  unsigned char bytes[270];
  size_t size;
} frame_sink;

static int write_frame(void *user, const uint8_t *data, size_t size) {
  frame_sink *sink = (frame_sink *)user;
  if (size > sizeof(sink->bytes)) return TURBO_EMSGSIZE;
  memcpy(sink->bytes, data, size);
  sink->size = size;
  return TURBO_OK;
}

static void on_event(void *user, cnet_websocket *websocket,
                     const cnet_websocket_event *event) {
  (void)user;
  (void)websocket;
  (void)event;
}

int main(void) {
  cnet_websocket websocket = {0};
  frame_sink sink = {0};
  cnet_websocket_config config = {
      .size = sizeof(config),
      .role = CNET_WEBSOCKET_SERVER,
      .max_frame_bytes = 256,
      .max_message_bytes = 512,
      .max_buffered_input_bytes = 1024,
      .write = write_frame,
      .on_event = on_event,
      .user = &sink,
  };
  int status = cnet_websocket_init(&websocket, &config);
  if (status == TURBO_OK) status = cnet_websocket_send_text(&websocket, "hello", 5);
  (void)cnet_websocket_destroy(&websocket);
  return status == TURBO_OK && sink.size != 0 ? 0 : 1;
}
```

## TLS transport

TLS is an opt-in bounded transport implemented by CNet over the same NativeIO
TCP endpoints. Set both `cnet_client_config.tls_io_buffer_bytes` (at least
`CNET_TLS_MIN_IO_BUFFER_BYTES`) and `tls_handshake_timeout_ms` to admit TLS
connections. Leaving both zero preserves a TLS-free client and makes a
`tls://` connect fail with `TURBO_ENOTSUP`.

`cnet_connect()` accepts an optional `cnet_tls_client_config`. NULL uses the
platform trust store and the URI host as the verified identity. An explicit
configuration can select CA file/path, client certificate/key, SNI/identity,
and an ordered ALPN offer. Certificate-chain and hostname/IP verification are
mandatory; CNet exposes no insecure mode and never retries `tls://` as
plaintext. Configuration strings are consumed synchronously during admission.

After CONNECTED, `cnet_tls_negotiated_alpn()` copies the selected protocol. It
can be called from the CONNECTED callback because CNet records ALPN before
invoking user code. No overlap returns `TURBO_ENOENT`; protocol layers such as
HTTP/2 must treat that result as a policy decision rather than assume `h2`.

Servers initialize one reusable `cnet_tls_server`, accept sockets with
`cnet_listener_accept_tls()`, and destroy the public context after closing
admission. Accepted sessions retain their context, but accept and destroy on
the same wrapper must not overlap. Optional client authentication requires an
explicit CA source and validates the client certificate during the handshake.

Each TLS session owns two fixed-capacity BIO directions and two fixed-capacity
I/O scratch buffers. Handshake, encrypted reads/writes, ALPN, cancellation,
and `close_notify` stay on the CNet progress owner; TLS creates no worker
thread. Handshake timeout is reported with stage `handshake`, malformed or
truncated TLS never falls back to plaintext, and user close during a handshake
cancels the in-flight transport without publishing CONNECTED.

The adapter follows OpenSSL's [BIO pair](https://docs.openssl.org/3.5/man3/BIO_new_bio_pair/)
and [hostname validation](https://docs.openssl.org/3.5/man3/SSL_set1_host/) contracts;
ALPN wire behavior follows [RFC 7301](https://www.rfc-editor.org/rfc/rfc7301).

## Ownership and progress

One client owns one session engine and one NativeIO backend. CNet creates no
I/O worker thread. The application repeatedly calls `cnet_client_poll`; that
call drains bounded commands, observes NativeIO, resumes owner-affine
coroutines, and invokes callbacks before returning. Calls to poll must not
overlap. Callbacks for the client are FIFO and non-concurrent, and no internal
lock is held while user code runs.

The core client is single-thread-owned: connect/send/receive/close and poll are
issued by that owner or by its inline callback. Cross-thread producers use an
external bounded mailbox and wake policy; CNet does not silently create that
thread or queue topology.

A blocking poll continues through internal-only send completions until it
delivers a public callback or reaches its timeout. A zero timeout performs one
nonblocking progress pass. This keeps completion batching internal instead of
forcing the application to call poll once per backend completion.

TCP connect, send, and receive currently execute as NativeIO coroutines. The
poll owner starts `native_io_coroutine_await()`, and the same caller's NativeIO
observation resumes the frame with its generation-checked terminal completion.
Callback-issued send/receive/close commands enter the same bounded local queue;
they require no operating-system wake or owner-thread handoff. Cancellation
retains the payload, request record, and frame until a terminal completion is
observed.

The URI, observer, and send bytes are copied before their admitting call returns
success. A callback may call `cnet_send`, `cnet_receive`, or `cnet_close` for its
client. Calling `cnet_client_poll`, `cnet_client_stop`, or
`cnet_client_destroy` recursively from that callback returns `TURBO_EBUSY`.
Each connection admits one write at a time; another send returns `TURBO_EBUSY`
until its send event is observed. `cnet_send_and_close()` reserves the final
write and immediately closes further send/receive admission.

Hostname resolution uses c-ares' external-event-loop integration. The same
poll owner checks its bounded DNS socket set without blocking and advances
c-ares timers; no resolver thread or synchronous DNS fallback is created.
While at least one hostname query is active, a NativeIO wait is capped to a
1 ms fairness quantum so DNS and transport completions both make bounded
progress. Numeric TCP/UDP addresses and Pipe endpoints do not enter this path.

## Shutdown and errors

`cnet_client_stop(client, timeout_ms)` closes admission and drives the same
caller-owned loop until connections, NativeIO requests, coroutines, and
terminal callbacks settle. `TURBO_ETIMEDOUT` is retryable and preserves the
client. Destroying a client before successful stop returns `TURBO_EBUSY`.
If progress has already recorded a fatal error, stop keeps that first error as
its return value while still driving close/recycle to quiescence. The caller
must still attempt `cnet_client_destroy`; it succeeds when cleanup completed,
even though stop reported the terminal diagnostic. Listener bind and accept
failures use portable Turbo status codes such as `TURBO_EADDRINUSE`.

Immediate connect validation failure clears the output handle and emits no
callback. Asynchronous failures emit exactly one `CNET_CONNECTION_FAILED` with
a stable stage string. TurboUtils status codes use `cnet_error.status`; a raw
platform status is normalized to `TURBO_EIO` and retained in
`cnet_error.native_status`.

The executable contracts are in `tests/cnet_api_test.c` and
`tests/cnet_tls_test.c`; they cover caller-owned callback execution, TCP,
connected UDP, platform Pipe, callback reentrancy, stale handles, live drain,
receive demand across request-slot reuse, verified TLS, ALPN, mTLS, partial
records, handshake timeout/cancel, accepted sockets, and clean close.

## Benchmark

`cnet_io_benchmark` compares libuv, direct NativeIO, NativeIO coroutines, and
the CNet public byte API against matched dedicated blocking loopback echo
peers. Every payload uses five independent repeats. Each repeat recreates its
client and peer, runs the same warmup and measured round trips, and rotates the
four driver orders. The report aggregates per-repeat metrics by median and
reports paired deltas with median absolute deviation (MAD); it never pools
independent runs into one latency distribution.

CNet progress is driven by the benchmark caller, so its timed path contains no
owner-thread scheduling or wake handoff. CNet-only stage clocks run in separate
diagnostic passes and therefore do not bias the direct latency/rate comparison.
Output separates TCP and UDP p50/p95 latency and throughput by payload size,
then reports CNet send admission, poll, callback, and public
polls-per-round-trip medians and MAD.
