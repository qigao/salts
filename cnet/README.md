# CNet

CNet is the connection-oriented layer above NativeIO. Applications see a
client, generation-checked connections, send/receive operations, explicit
progress polling, and ordered state notifications. NativeIO remains the raw,
threadless operating-system I/O backend; CFlow Actor and Reactive code continue
to depend on NativeIO directly.

CNet is built unconditionally. Its source-tree target is `salts_cnet`; installed
consumers link `Salts::CNet` and include `<cnet/cnet.h>`. The independent
WebSocket session API is declared by `<cnet/websocket.h>`.

## Base API

Include `<cnet/cnet.h>`, initialize one bounded `cnet_client_config`, then use:

- `cnet_connect` with `tcp://host:port`, `tls://host:port`, `udp://host:port`, or
  `pipe://name`;
- `cnet_send` to transfer one bounded payload copy into CNet;
- `cnet_sendv` to concatenate non-empty borrowed ranges directly into that
  same final bounded command slot without caller-side staging;
- `observer.on_send` to observe completion before admitting the next ordered
  write on that connection;
- `cnet_receive` to add explicit receive demand;
- `cnet_close` for one connection;
- `cnet_client_poll` to advance I/O and invoke callbacks on the caller;
- `cnet_client_stop` followed by `cnet_client_destroy` for shutdown.

TCP and Pipe deliver byte chunks. Connected UDP delivers one datagram per
receive callback. A receive view is borrowed only until its callback returns.
TLS delivers verified encrypted byte streams through the same send/receive
contract. The same header also exposes bound UDP, the KCP session engine, and
their unified packet endpoint; WebSocket remains in `<cnet/websocket.h>`. CNet parses
TCP, TLS, and UDP URIs through Salts UriParser and then applies
transport-specific constraints: network URIs require an explicit port and reject
userinfo, path, query, and fragment components instead of accepting truncated or
ambiguous input. Pipe is a scheme-specific IPC endpoint rather than a network
authority, so its bounded name after `pipe://` is preserved byte-for-byte.

## Socket tuning

`cnet_stream_socket_options` is the public, versioned TCP policy shared by
outgoing TCP/TLS connections and adopted listener sockets. Set it on a stopped
`cnet_client` with `cnet_client_set_stream_socket_options()`; listener owners use
`cnet_listener_init_ex()` with versioned `cnet_listener_options`. The policy
exposes OS receive/send buffers, keepalive enable plus idle/interval/probe count,
and linger. `cnet_datagram_config.reuse_port` exposes the same listener-port
sharing decision for UDP and the unified UDP/KCP packet endpoint.

Zero-valued buffer and timing fields preserve platform defaults. Keepalive
detail without `keepalive` is invalid; enabled linger with zero milliseconds is
an abortive close. CNet copies every policy into its owner command, so no caller
pointer is retained. Unsupported platform options return `SALTS_ENOTSUP` before
the socket is published, and invalid sizes or combinations fail without a
silent fallback.

## Unified UDP/KCP packet endpoint

`cnet_packet_endpoint` is the application-facing interface for bound UDP and
KCP. Select `CNET_PACKET_UDP` or `CNET_PACKET_KCP`; both use the same explicit
session open, copied send, poll, session close, stop, and destroy operations.
Sessions are generation-checked values indexed by a fixed-capacity `(peer,
conversation)` table. UDP requires conversation zero. Plain KCP requires a
non-zero conversation and retains message boundaries while adding ordering,
ACKs, retransmission, and fragmentation. Authenticated KCP is selected
explicitly with `CNET_KCP_SECURITY_PSK_V1`; callers open it with conversation
zero, observe `CONNECTING`, and receive `OPEN` only after the PSK handshake
derives the read-only conversation id.

An unknown inbound key is admitted only when `observer.on_admit` returns
`SALTS_OK`. This decision is synchronous on the poll owner; a missing callback
rejects unknown peers. Capacity exhaustion reports `SALTS_ENOBUFS` rather than
growing or evicting live sessions. `cnet_packet_send()` means that CNet copied
and admitted the message. It does not claim remote delivery: KCP can emit and
retransmit multiple UDP packets for one application message, so the facade does
not invent a per-message ACK callback. Asynchronous socket failures and KCP
output backpressure arrive through the generation-checked `on_error` callback.

The lower-level `cnet_datagram` API remains available for protocols that need
raw peer-addressed UDP. Each successful send retains its caller tag and reports
that tag exactly once in the terminal send callback. `cnet_kcp` remains
available as a socket-independent engine for applications with an existing
datagram transport. Its `output` callback is borrowed, `input` consumes one
borrowed wire packet synchronously, and `update/check` make timer ownership
explicit.

Plain `cnet_kcp` provides reliability, not confidentiality or peer
authentication. `cnet_secure_kcp` and the packet endpoint's explicit PSK v1
mode add the CoroNet-compatible authenticated handshake, XChaCha20-Poly1305
records, replay rejection, and Reed-Solomon FEC. There is no plaintext fallback
or wire sniffing. Unknown peers reach `on_admit` only after their client hello
passes a stateless PSK MAC check.

## WebSocket session engine

`<cnet/websocket.h>` provides a transport-independent RFC 6455 session after a
successful HTTP Upgrade. It handles text/binary messages, fragmentation,
ping/pong, close handshakes, client masking, strict UTF-8 validation, and role
masking rules. Input chunks may split or coalesce frames; event payloads are
borrowed only until their callback returns.

Initialization allocates fixed-capacity input, reassembled-message, and
single-frame output storage. `max_frame_bytes`, `max_message_bytes`, and
`max_buffered_input_bytes` are mandatory. A write callback returning
`SALTS_EBUSY` retains exactly one complete frame; the owner calls
`cnet_websocket_flush()` before feeding or sending more data. Other write errors
move the session to `CNET_WEBSOCKET_FAILED` and are available through
`cnet_websocket_last_error()`. A peer Close commits `CLOSING` before its event
callback and becomes `CLOSED` only after any retained echo Close is transferred.

The engine owns protocol state but performs no socket I/O and creates no thread.
It is single-owner and can therefore be driven by a CNet callback, Executor,
Actor mailbox, or another ordered byte-stream adapter. CHTTP remains responsible
for HTTP/1.1 Upgrade routing/header validation and HTTP/2 extended CONNECT.
`cnet_connect()` does not accept `ws://` or `wss://`; applications use CHTTP's
WebSocket routes and clients, which adapt those sessions onto this engine.

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
  if (size > sizeof(sink->bytes)) return SALTS_EMSGSIZE;
  memcpy(sink->bytes, data, size);
  sink->size = size;
  return SALTS_OK;
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
  if (status == SALTS_OK) status = cnet_websocket_send_text(&websocket, "hello", 5);
  (void)cnet_websocket_destroy(&websocket);
  return status == SALTS_OK && sink.size != 0 ? 0 : 1;
}
```

## TLS transport

TLS is an opt-in bounded transport implemented by CNet over the same NativeIO
TCP endpoints. Set both `cnet_client_config.tls_io_buffer_bytes` (at least
`CNET_TLS_MIN_IO_BUFFER_BYTES`) and `tls_handshake_timeout_ms` to admit TLS
connections. Leaving both zero preserves a TLS-free client and makes a
`tls://` connect fail with `SALTS_ENOTSUP`.

The repository manifest selects BoringSSL. CMake consumes its conventional
`find_package(OpenSSL REQUIRED)` compatibility targets only as private build
dependencies; Salts neither exports those targets nor installs BoringSSL.

`cnet_connect()` accepts either a one-shot `cnet_tls_client_config` or a reusable
`cnet_tls_client`; the two fields are mutually exclusive. NULL uses the platform
trust store and the URI host as the verified identity. An explicit configuration
can select CA file/path, client certificate/key, SNI/identity, and an ordered
ALPN offer. `cnet_tls_client_init()` builds an immutable BoringSSL context and
consumes all input synchronously. A successful connect retains that context, so
the public wrapper may be destroyed after admission while the connection remains
valid. Certificate-chain and hostname/IP verification are mandatory; CNet
exposes no insecure mode and never retries `tls://` as plaintext.

After CONNECTED, `cnet_tls_negotiated_alpn()` copies the selected protocol. It
can be called from the CONNECTED callback because CNet records ALPN before
invoking user code. No overlap returns `SALTS_ENOENT`; protocol layers such as
HTTP/2 must treat that result as a policy decision rather than assume `h2`.
`cnet_tls_peer_certificate_sha256()` copies the verified peer leaf certificate
fingerprint while the TLS connection remains open; a server session whose peer
did not present a client certificate returns `SALTS_ENOENT`.

Servers that need transport identity can use `cnet_listener_accept_peer()` or
`cnet_listener_accept_tls_peer()`. They preserve the existing accept lifecycle
while also returning an owning `cnet_stream_peer` value containing the remote
IPv4/IPv6 address, scope id and host-order port. The original accept helpers
remain source-compatible wrappers when endpoint metadata is not needed.

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

The adapter follows BoringSSL's OpenSSL-compatible
[BIO pair](https://boringssl.googlesource.com/boringssl/+/HEAD/include/openssl/bio.h)
and [hostname validation](https://boringssl.googlesource.com/boringssl/+/HEAD/include/openssl/ssl.h)
contracts;
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
success. For `cnet_sendv`, both the descriptor array and its immutable backing
ranges are borrowed only during the call; successful admission has copied their
ordered concatenation and `on_send` reports its total size once. Empty ranges
are rejected so segment count is bounded by the configured byte limit. A
callback may call `cnet_send`, `cnet_sendv`, `cnet_receive`, or `cnet_close` for
its client. Calling `cnet_client_poll`, `cnet_client_stop`, or
`cnet_client_destroy` recursively from that callback returns `SALTS_EBUSY`.
Each connection admits one write at a time; another send returns `SALTS_EBUSY`
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
terminal callbacks settle. `SALTS_ETIMEDOUT` is retryable and preserves the
client. Destroying a client before successful stop returns `SALTS_EBUSY`.
If progress has already recorded a fatal error, stop keeps that first error as
its return value while still driving close/recycle to quiescence. The caller
must still attempt `cnet_client_destroy`; it succeeds when cleanup completed,
even though stop reported the terminal diagnostic. Listener bind and accept
failures use portable Salts status codes such as `SALTS_EADDRINUSE`.

Immediate connect validation failure clears the output handle and emits no
callback. Asynchronous failures emit exactly one `CNET_CONNECTION_FAILED` with
a stable stage string. Salts status codes use `cnet_error.status`; a raw
platform status is normalized to `SALTS_EIO` and retained in
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
