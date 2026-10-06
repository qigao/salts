# CNet

CNet is the connection-oriented layer above NativeIO. Applications see a
client, generation-checked connections, send/receive operations, explicit
progress polling, and ordered state notifications. NativeIO remains the raw,
threadless operating-system I/O backend; CFlow Actor and Reactive code continue
to depend on NativeIO directly.

The canonical lower-layer contract is [NativeIO execution and endpoint architecture](../native-io/ARCHITECTURE.md). CNet is an optional network/session semantic consumer: raw TCP/UDP/VSOCK/PIPE data paths do not require CNet, and CNet does not own NativeIO execution-style or terminal-completion truth.

CNet is built unconditionally. Its source-tree target is `salts_cnet`; installed
consumers link `Salts::CNet` and include `<cnet/cnet.h>`. The independent
WebSocket session API is declared by `<cnet/websocket.h>`.

## Base API

Include `<cnet/cnet.h>`, initialize one bounded `cnet_client_config`, then use:

- `cnet_connect` with `tcp://host:port`, `tls://host:port`, `udp://host:port`,
  `pipe://name`, or Linux `vsock://CID:PORT`;
- `cnet_send_buffer` for retained contiguous payload ownership;
- `cnet_send_slice` / `cnet_send_slicev` for retained subrange and scatter/gather ownership;
- `cnet_send_buffer_and_close` / `cnet_send_slicev_and_close` for retained final writes;
- `observer.on_send` to observe completion before admitting the next ordered
  write on that connection;
- `cnet_receive` to add explicit receive demand;
- `cnet_set_receive_slice_handler` to opt one connection into explicit
  application-owned receive slices without changing the legacy observer layout;
- `cnet_close` for one connection;
- `cnet_client_poll` to advance I/O and invoke callbacks on the caller;
- `cnet_client_stop` followed by `cnet_client_destroy` for shutdown.

## Execution ownership and multicore composition

One `cnet_client` is one connection/progress owner. The ordinary
`cnet_client_init()` + `cnet_client_poll()` mode remains caller-driven:
callbacks execute inline on that one poll owner and CNet creates no hidden
worker pool.

Multicore execution is **host-composed**, not an implicit multi-shard mode
inside one client. A runtime that already owns NativeIO progress lanes creates
one backend/client pair per owner lane with `cnet_client_init_external()`:

```text
owner lane 0
  NativeIO backend 0
      |
  cnet_client_init_external(client 0)
      |
  fixed connections + callbacks on lane 0

owner lane 1
  NativeIO backend 1
      |
  cnet_client_init_external(client 1)
      |
  fixed connections + callbacks on lane 1
```

The host selects the client/owner when a connection is admitted. That
connection never migrates. The existing data-plane calls stay unchanged:

```c
cnet_connect(client, ...);
cnet_send_buffer(client, connection, ...);
cnet_receive(client, connection, ...);
cnet_close(client, connection);
```

There is deliberately no public shard id, `shard_count`, CNet worker pool,
work stealing, or connection migration. CNet also does not need
`native_io_sharded` merely to obtain multicore scaling; independent fixed CNet
owners over direct NativeIO backends are sufficient.

For external-progress clients, the embedding owner performs the canonical host
loop:

```text
cnet_client_advance_external()
cnet_client_external_timeout()
native_io_backend_observe()
cnet_client_route_external_completion() for the observed batch
return to the host loop
```

Do not add a second `advance_external()` immediately after routing a successful
batch; the next loop iteration already performs the next control/session/deadline
pass.

The retained-TCP topology evidence behind this policy is:

- #680: two independent ordinary CNet owners scale real TCP work; 64 KiB
  distinct-core throughput reached about 2x the one-owner serial baseline;
- #684: one private shared multi-owner engine retained that owner-local scaling,
  proving the engine itself is not the bottleneck;
- #690: a hidden central-callback compatibility facade was rejected. Requiring
  each logical operation to cross a command hop and then return through central
  event hops retained only about 0.63-0.73x of the owner-affine distinct-core
  throughput and increased tail/CPU cost;
- #695/#713: host-driven external progress preserves the owner-affine class when
  the host loop is shaped correctly. Repeated epoll 64 KiB external/ordinary
  throughput was about 0.99x on one CPU/SMT and 1.04x on distinct cores;
  io_uring remained similarly near parity.

Therefore multicore CNet is a **throughput/concurrency execution policy**, not a
universal low-latency default. Small-message latency and CPU efficiency must
still be measured for the workload.

An io_uring-backed owner must create its NativeIO backend on the final owner
thread. NativeIO uses `IORING_SETUP_SINGLE_ISSUER`, and backend initialization
arms the wake poll; constructing the ring on one thread and later driving it
from another violates the kernel single-issuer contract.

`command_capacity` bounds the generic control/deferred command mailbox. `command_buffer_bytes`
bounds copied payloads that still belong to that control plane; stream data-plane sends do not
copy payload bytes into this mailbox. `max_send_bytes` remains the per-logical-send bound, while
retained stream writes are bounded by their fixed owner-local write slots. Capacity exhaustion
returns `SALTS_ENOBUFS`.

`event_capacity` likewise bounds event descriptors while `event_buffer_bytes`
bounds their aggregate copied payload. Event payloads and per-connection receive
buffers are allocated only while in use; configuring a large per-message bound
therefore no longer reserves its product with every event or connection slot.

TCP, VSOCK, and Pipe deliver byte chunks. Connected UDP delivers one datagram per
receive callback. The ordinary `observer.on_receive` view is borrowed only until
its callback returns. Applications that need receive bytes to outlive the callback
can install `cnet_set_receive_slice_handler()` for that connection. While
installed, owned delivery takes precedence over the borrowed callback; changing
the handler while receive demand is outstanding returns `SALTS_EBUSY`, so one
admitted demand cannot change lifetime contracts in flight.

For each connection CNet now keeps the receive scratch itself as a canonical
`mem_buffer_t` from `mem_global()`. Plaintext NativeIO reads and TLS plaintext
reads write directly into `mem_buffer_data()`; a RECEIVE event carries that same
backing through the dispatcher. The borrowed callback still sees only a
callback-scoped byte view. The owned handler creates a `mem_slice_t` over the
same backing, so there is no additional CNet payload memcpy between the producer
receive scratch and the public owned callback.

After successful publication, CNet reuses the same receive backing when nobody
retained it. If an owned callback or fallback event queue retains the backing,
the session moves that reference into one lazy spare slot and rearms on another
buffer, so future network progress cannot overwrite application-owned bytes.
For the common case where at most one slice remains live across callback return,
the two buffers then ping-pong: once the application releases the previous slice,
its spare refcount returns to one and CNet promotes it as the next active backing
without allocation. If multiple slices remain live concurrently, CNet drops only
its old spare reference and allocates another active buffer; user-owned slices
stay valid. Thus CNet itself caches at most one spare per session.

The fallback event queue retains canonical DATA backing rather than copying it
into its private payload pool, while non-DATA payloads such as ALPN keep the
existing copied path.

The application releases owned slices with `mem_slice_release()`. Because the
canonical backing belongs to the process-global Salts pool rather than the
client, an already delivered slice remains valid across later poll calls,
connection close, `cnet_client_stop()`, and `cnet_client_destroy()`. An empty
UDP datagram is represented by an empty slice with no backing reference.

This is a **CNet producer-to-callback zero-copy ownership path, not kernel
zero-copy**. The operating system still writes into user memory, and TLS still
performs the required ciphertext-to-plaintext transform before the canonical
plaintext backing is published.

TLS delivers verified encrypted byte streams through the same send/receive
contract. The same header also exposes bound UDP, the KCP session engine, and
their unified packet endpoint; WebSocket remains in `<cnet/websocket.h>`. CNet parses
TCP, TLS, and UDP URIs through Salts UriParser and then applies
transport-specific constraints: network URIs require an explicit port and reject
userinfo, path, query, and fragment components instead of accepting truncated or
ambiguous input. Bracketed network hosts must be valid IPv6 literals;
bracketed names, IPv4 addresses, and malformed IPv6 return `SALTS_EINVAL`,
while unsupported IPvFuture literals return `SALTS_ENOTSUP`. These failures
occur before connection admission or DNS lookup. VSOCK uses a separate strict
decimal `uint32` CID/port parser and never enters DNS. Pipe is a scheme-specific
IPC endpoint rather than a network authority, so its bounded name after
`pipe://` is preserved byte-for-byte.

## Socket tuning

`cnet_stream_socket_options` is the public, versioned TCP policy shared by
outgoing TCP/TLS connections and detached listener sockets consumed by that
final client owner. Set it on a stopped `cnet_client` with
`cnet_client_set_stream_socket_options()`; listener owners use
`cnet_listener_init_ex()` with versioned `cnet_listener_options`. The policy
exposes OS receive/send buffers, explicit `TCP_NODELAY` via `nodelay`,
keepalive enable plus idle/interval/probe count, and linger. Generic native
socket adoption preserves an externally configured live policy; only
`cnet_accepted_stream` handoff applies the final client's configured policy.
`cnet_datagram_config.reuse_port` exposes the same listener-port
sharing decision for UDP and the unified UDP/KCP packet endpoint.

Zero-valued buffer and timing fields preserve platform defaults. `nodelay=0`
keeps the platform's Nagle policy; `nodelay=1` requests `TCP_NODELAY` and is
appropriate for latency-sensitive protocols that may emit one logical frame
through multiple native write windows. Keepalive detail without `keepalive`
is invalid; enabled linger with zero milliseconds is an abortive close. CNet
copies every policy into its owner command, so no caller
pointer is retained. Unsupported platform options return `SALTS_ENOTSUP` before
the socket is published, and invalid sizes or combinations fail without a
silent fallback.


## Bounded write ownership

Ordinary stream-send payload lifetime lives in the dedicated owner-local write
queue. The queue has a fixed global slot bound plus per-connection FIFO chains.
Every public stream data-plane write retains caller-provided `mem_buffer_t` ownership
directly or retains the unique backing buffers referenced by canonical `mem_slice_t`
ranges. CNet does not maintain a copied stream-payload queue or copied-byte budget.

`cnet_send_slicev()` preserves scatter/gather ownership without flattening.
`cnet_send_slicev_and_close()` uses the same retained ownership for the final
logical write and closes only after that vector settles. CNet validates
1..`CNET_RETAINED_VECTOR_MAX` canonical `mem_slice_t` ranges (currently 32),
retains each unique backing buffer once, copies only fixed range descriptors
into the owner-local write slot, and preserves those ranges until the single
logical terminal completion.

For plaintext stream transports, the retained ranges flow into NativeIO
scatter/gather. One NativeIO submission exposes at most
`NATIVE_IO_VECTOR_MAX` spans (currently 16); a larger CNet logical vector
advances through successive native span windows without flattening or
publishing an intermediate CNet send terminal.

TLS keeps the same retained-vector ownership but necessarily transforms
plaintext before NativeIO sees it. CNet feeds retained spans to the TLS engine
in order, advances only accepted spans, then sends the generated ciphertext
through the normal TLS/NativeIO path. It does not flatten plaintext into copied
CNet storage. TLS record and receive-callback boundaries are intentionally not
preserved as vector boundaries. Small, highly fragmented TLS vectors may cost
more TLS write/record operations; callers should avoid unnecessary
fragmentation when throughput matters. UDP retained slicev remains unsupported;
there is no hidden flatten fallback.

The larger logical range bound remains fixed-memory. On a 64-bit build, raising
the retained range/owner arrays from 16 to 32 adds approximately 512 bytes per
owner-local write slot. Write slots are preallocated at
`write_capacity_per_shard` (derived from the client command capacity), so the
incremental metadata is approximately
`512 * command_capacity * shard_count` bytes and does not grow with payload
size or runtime duration. For a 16-slot shard this is about 8 KiB.

## Canonical session-state authority

The generation-checked `cnet_session_table` is the sole CNet connection
lifecycle authority. The client record does not mirror CONNECTED/OPEN as an
independent boolean. Steady-state send/receive admission delegates lifecycle
validation to `cnet_shards`, which checks the canonical session state.

The remaining client booleans are deliberately narrower:

- `active` owns the public-handle/observer slot mapping, not connection state;
- `write_pending` is the temporary one-write admission reservation tracked
  for #479;
- receive demand is a bounded delivery counter, not lifecycle state;
- close/TLS command-pending flags only reserve same-owner command admission
  until #478 removes that mailbox hop.

TLS inspection and upgrade readiness query the canonical session state instead
of a duplicated client CONNECTED flag. Terminal NativeIO/session state remains
authoritative throughout close, failure and TLS-handshake transitions.

## Linux VSOCK

CNet supports plaintext Linux `AF_VSOCK` `SOCK_STREAM` over the epoll and
io_uring NativeIO backends. An outbound URI has exactly the form
`vsock://CID:PORT`; both values are unsigned decimal 32-bit integers in host
byte order. Signs, whitespace, userinfo, paths, queries, fragments, overflow,
`CNET_VSOCK_CID_ANY`, and `CNET_VSOCK_PORT_ANY` are rejected for outbound
connections. Non-Linux platforms return `SALTS_ENOTSUP` before reserving a
connection or publishing a callback, with no TCP or Pipe fallback.

Servers use a versioned `cnet_vsock_listener_config` with
`cnet_listener_init_vsock()`. The ANY constants are valid for listener bind;
`cnet_listener_vsock_local()` queries the current full-width CID and port with
`getsockname()` on every call. `cnet_listener_accept_vsock_peer()` returns a
copied `cnet_vsock_peer`; the shorter `cnet_listener_accept_vsock()` omits that
metadata. The common wait/close/destroy lifecycle is unchanged. TCP/TLS accept
functions reject a VSOCK listener, and VSOCK accept functions reject a TCP
listener.

`cnet_client_adopt_vsock()` consumes a connected native VSOCK stream. Accepted
and directly adopted sockets transfer exactly once: failed admission closes the
native socket and leaves the public connection zero. CNet applies the same
bounded byte send/receive demand, one-write-at-a-time, timeout, cancellation,
callback ordering, and stop/drain rules as TCP. The current stream tuning API
expresses TCP policy, so any requested receive/send buffer, keepalive, or linger
setting is rejected for VSOCK with `SALTS_ENOTSUP` instead of being ignored.

Phase 1 does not provide TLS-over-VSOCK. Passing TLS policy with a `vsock://`
URI is invalid because a CID is not a certificate identity. VSOCK is only a
transport and does not itself authenticate or encrypt application data. Live
migration disconnects an active stream and may change the local CID; CNet
reports the normal terminal stream failure and does not reconnect implicitly.
The Linux-only `cnet_vsock_integration_test` exercises ANY-CID bind, local-CID
connect, accept, ownership, and bidirectional bytes, and is explicitly skipped
when the kernel or sandbox does not expose AF_VSOCK.

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
growing or evicting live sessions. `cnet_packet_send()` remains admission-only:
it means that CNet copied and admitted the message and does not claim transport
completion or remote delivery.

Consumers that need authoritative per-message settlement use
`cnet_packet_endpoint_init_ex()` with `cnet_packet_terminal_config`, then call
`cnet_packet_send_tagged()`. The terminal configuration is size/versioned and
reserves a fixed logical-operation capacity before the endpoint is published.
Pool exhaustion returns `SALTS_ENOBUFS`; a failed admission never produces a
callback. The opaque caller tag is copied and returned exactly once. UDP settles
from its NativeIO datagram terminal. KCP, authenticated KCP, and FEC settle only
after KCP's cumulative acknowledgement point passes the logical message's last
segment; individual emitted or retransmitted UDP packet completions never settle
the logical send. Tagged KCP rejects stream mode because it cannot retain these
message boundaries. Explicit close/stop cancels unacknowledged KCP sends with
`SALTS_ECANCELED`, drains UDP native terminals, and dispatches logical terminals
before the corresponding CLOSED state. Asynchronous socket failures and KCP
output backpressure also remain visible through the generation-checked
`on_error` callback.

The lower-level `cnet_datagram` API remains available for protocols that need
raw peer-addressed UDP. Each successful send retains its caller tag and reports
that tag exactly once in the terminal send callback. `cnet_kcp` remains
available as a socket-independent engine for applications with an existing
datagram transport. Its `output` callback is borrowed, `input` consumes one
borrowed wire packet synchronously, and `update/check` make timer ownership
explicit.

Each `cnet_kcp_send()` call may add at most 127 new segments after any
stream-mode tail merge. Exceeding this protocol limit returns `SALTS_EMSGSIZE`
before retaining input or changing an already queued stream tail; the separate
conservative `send_segment_capacity` admission bound remains unchanged.

Plain `cnet_kcp` provides reliability, not confidentiality or peer
authentication. `cnet_secure_kcp` and the packet endpoint's explicit PSK v1
mode add the CoroNet-compatible authenticated handshake, XChaCha20-Poly1305
records, replay rejection, and Reed-Solomon FEC. There is no plaintext fallback
or wire sniffing. Unknown peers reach `on_admit` only after their client hello
passes a stateless PSK MAC check.

FEC `max_payload_bytes` must not exceed 65533: parity shards include the
two-byte data length and must fit the 16-bit wire length field. Packet endpoint
datagram send/receive limits must cover `max_payload_bytes + 48` bytes (30-byte
header, two-byte parity length metadata, and 16-byte MAC); undersized limits
are rejected during initialization.

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

The repository manifest selects the canonical GmSSL overlay. CMake consumes
`GmSSL::GmSSL` only as a private implementation dependency; Salts never
exports that target in CNet's public CMake link interface. The overlay packages
GmSSL statically, so the Windows native SDK ships no private TLS-provider DLL
beside `cnet.dll`.

`cnet_connect()` accepts either a one-shot `cnet_tls_client_config` or a reusable
`cnet_tls_client`; the two fields are mutually exclusive. NULL uses the platform
trust store and the URI host as the verified identity. An explicit configuration
can select CA file/path, client certificate/key, SNI/identity, and an ordered
ALPN offer. `cnet_tls_client_init()` builds immutable GmSSL provider contexts and
consumes all input synchronously. A successful connect retains that context, so
the public wrapper may be destroyed after admission while the connection remains
valid. Certificate-chain and hostname/IP verification are mandatory; CNet
exposes no insecure mode and never retries `tls://` as plaintext.

After CONNECTED, `cnet_tls_negotiated_alpn()` copies the selected protocol. It
can be called from the CONNECTED callback because CNet records ALPN before
invoking user code. No overlap returns `SALTS_ENOENT`; protocol layers such as
HTTP/2 must treat that result as a policy decision rather than assume `h2`.

The TLS protocol version and cipher suite remain automatically negotiated.
`cnet_tls_negotiated_version()` and `cnet_tls_negotiated_cipher()` expose the
effective session metadata after CONNECTED without exposing the underlying TLS
implementation. The policy remains TLS 1.2 minimum with no maximum cap, so a
TLS 1.3-capable peer is negotiated automatically while TLS 1.2 peers remain
supported.
`cnet_tls_peer_certificate_sha256()` copies the verified peer leaf certificate
fingerprint while the TLS connection remains open; a server session whose peer
did not present a client certificate returns `SALTS_ENOENT`.
`cnet_tls_server_end_point_binding()` exposes the distinct RFC 5929
`tls-server-end-point` semantic value for SASL-style channel binding. It hashes
the exact verified peer leaf DER certificate with the certificate signature
digest, upgrading MD5/SHA-1 signatures to SHA-256 as required by RFC 5929.
This API is intentionally separate from `cnet_tls_export_channel_binding()`,
which remains the RFC 9266-style `EXPORTER-Channel-Binding` contract; callers
must not substitute one binding type for the other.

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

Protocols such as SMTP, IMAP, and POP3 can upgrade an already connected
plaintext TCP stream without reconnecting. After the application has completed
its plaintext negotiation and observed the final send/receive completion, call
`cnet_start_tls()` on the client side and `cnet_start_tls_server()` on the server
side. The connection handle remains unchanged. CNet publishes
`CNET_CONNECTION_TLS_HANDSHAKING`, rejects new send/receive work during the
transition, then publishes CONNECTED again only after certificate and hostname
verification succeeds. A handshake error is terminal and never restores the
plaintext stream.

TLS upgrade admission is intentionally strict: the stream must be a quiescent,
connected `tcp://` session with no pending send, receive, close, or earlier
upgrade. `cnet_start_tls_options` is consumed synchronously. A one-shot config
is converted to an immutable context before admission; a reusable client/server
context is retained by the bounded command and may be destroyed by the caller
after success. Queue exhaustion is reported immediately, and the existing
client TLS buffer and handshake-timeout bounds apply unchanged.

Each TLS session owns bounded ciphertext input/output rings plus fixed-capacity
I/O scratch buffers. GmSSL consumes and emits ciphertext only through CNet's
external-I/O callbacks; NativeIO remains the sole transport/socket authority.
Handshake, encrypted reads/writes, ALPN, cancellation, and `close_notify`
stay on the CNet progress owner; TLS creates no worker thread. Handshake timeout is reported with stage `handshake`, malformed or
truncated TLS never falls back to plaintext, and user close during a handshake
cancels the in-flight transport without publishing CONNECTED.

The adapter uses GmSSL's bounded `TLS_IO` callback contract and performs DNS
hostname or IP-address SAN verification without transferring socket ownership
to the provider. ALPN wire behavior follows
[RFC 7301](https://www.rfc-editor.org/rfc/rfc7301).

## Ownership and progress

One client owns one session engine and one NativeIO backend. CNet creates no
I/O worker thread. The application repeatedly calls `cnet_client_poll`; that
call drains bounded commands, observes NativeIO direct completions, routes those
completions through the CNet owner, and invokes callbacks before returning.
Calls to poll must not overlap. Callbacks for the client are FIFO and
non-concurrent, and no internal lock is held while user code runs.

The core client is single-thread-owned: connect/send/receive/close and poll are
issued by that owner or by its inline callback. Cross-thread producers use an
external bounded mailbox and wake policy; CNet does not silently create that
thread or queue topology.

A blocking poll continues through internal-only send completions until it
delivers a public callback or reaches its timeout. A zero timeout performs one
nonblocking progress pass. This keeps completion batching internal instead of
forcing the application to call poll once per backend completion.

CNet's stream owner uses NativeIO direct completion ownership internally for
TCP, VSOCK, and Pipe stream requests. A successful first submission stores the
generation-checked NativeIO request in the bounded CNet request record; observed
completions are routed back to that exact live record before CNet publishes the
logical event. Owner-issued receive demand bypasses the client/shard admission
locks and command queue: it is recorded directly in the canonical owner and
placed on the bounded owner-local rearm queue for the next poll. Callback-issued
receive remains deferred through the command queue because the owner may still
be routing later completions in the current NativeIO batch; this prevents a
callback from reusing a CNet request slot still named by that batch. A
quiescent owner-issued close likewise bypasses the generic command queue: it
commits DRAINING immediately and schedules bounded owner-local close work, but
CLOSING/CLOSED callbacks are still emitted only from the next poll. A close
with earlier send/receive/TLS/connect work remains on the FIFO command path so
ordering is preserved. No path requires an operating-system wake or owner-thread
handoff.

Cancellation is a request, not terminal evidence. CNet retains the request and
any owned payload until the matching terminal NativeIO completion is observed;
an `SALTS_EALREADY` cancellation result therefore does not recycle ownership.
Partial stream writes remain one logical CNet send and may require multiple
NativeIO submissions before the single terminal send event is published.
NativeIO coroutine APIs remain available to other consumers and to the
standalone NativeIO coroutine benchmark; CNet simply no longer creates one
coroutine per stream I/O.

The URI and observer configuration are copied before their admitting call returns
success. Stream send payload bytes are not copied into CNet: successful
`cnet_send_buffer()`, `cnet_send_slice()`, and `cnet_send_slicev()` admission
retains the required backing ownership until the logical send terminal. A callback
may admit another retained send, call `cnet_receive`, or call `cnet_close` for
its client. Calling `cnet_client_poll`, `cnet_client_stop`, or
`cnet_client_destroy` recursively from that callback returns `SALTS_EBUSY`.
Plain non-TLS connections admit multiple logical writes into a fixed-capacity
owner-local FIFO. Admission is bounded by write slots; capacity exhaustion returns
`SALTS_ENOBUFS`. NativeIO progresses one stream write head at a time where the
transport requires serialization, and `on_send` callbacks remain FIFO.
Retained final-send APIs close further public send/receive admission immediately
after successful admission and begin transport/TLS close only after the final
logical write settles.

Hostname resolution uses c-ares' external-event-loop integration. The same
poll owner checks its bounded DNS socket set without blocking and advances
c-ares timers; no resolver thread or synchronous DNS fallback is created.
While at least one hostname query is active, a NativeIO wait is capped to a
1 ms fairness quantum so DNS and transport completions both make bounded
progress. Numeric TCP/UDP addresses, VSOCK endpoints, and Pipe endpoints do not
enter this path.

### Client data-path ownership

Connection records and ordinary client admission are owned by the same CNet
progress thread. Receive/send callbacks, connection mapping, connect admission,
TLS admission/inspection and callback-issued send/receive/close therefore do
not acquire the client control mutex. That mutex is reserved for
poll/profile/wake/stop/destroy overlap handling, where `cnet_client_wake()`
is the only operation permitted from a non-owner thread.

This does not make command publication multi-threaded: cross-thread application
admission still requires an external mailbox, and callback-issued operations
retain their explicit deferred paths when completion-batch safety requires it.

This owner-local layout is also the CNet side of the NativeIO coarse-handoff
contract. Ordinary send/receive/close progress does not cross a NativeIO owner
boundary and does not need an owner-to-owner acknowledgement. A future sharded
CNet facade may route an owned command to a connection's fixed owner, but it
must keep subsequent data-plane progress there instead of bouncing each
completion back through another NativeIO shard.

### Dispatcher ownership

The client dispatcher is a same-owner callback bridge, not a synchronization
boundary. Registration, direct event preparation, inline observer invocation,
terminal recycle, drain and profiling all run on the CNet progress owner, so
the dispatcher owns no mutex. Its fallback lane retains bounded atomic
driving/pending guards, and first-error publication remains atomic; those
mechanisms do not make the dispatcher a cross-thread execution runtime.

## Shutdown and errors

`cnet_client_stop(client, timeout_ms)` closes admission and drives the same
caller-owned loop until connections, NativeIO requests, and terminal callbacks
settle. `SALTS_ETIMEDOUT` is retryable and preserves the
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
Linux VSOCK validation/integration, connected UDP, platform Pipe, callback
reentrancy, stale handles, live drain,
receive demand across request-slot reuse, verified TLS, ALPN, mTLS, partial
records, handshake timeout/cancel, accepted sockets, and clean close.

## Benchmark

设置 `CNET_IO_BENCHMARK_RECEIVE_COMPARE=1` 会运行独立的 receive ownership
comparison，使用同一 CNet TCP echo harness 比较 `borrowed callback + consumer
memcpy` 与 `producer-owned slice retain + bounded backing rotation`，覆盖 64 B、64 KiB、
1 MiB。owned benchmark 会让 slice 跨 callback return 存活，直到下一 receive callback
或本次 poll 返回后才 release，因此实际覆盖 retained owner 触发的 receive-buffer rotation，
而不是只测 callback 内立即释放。该实验用于量化 producer-owned receive 的成本/收益；
这里的 zero-copy 只指
CNet receive backing → public owned callback 之间不再 payload memcpy，不代表
kernel/TLS transform zero-copy，也不混入主 libuv/NativeIO 排名表。

`cnet_io_benchmark` 比较 libuv、NativeIO direct、NativeIO coroutine 和 CNet
public byte API。每个客户端使用独立 blocking loopback echo peer；每个 payload
运行 5 轮，每轮各有 32 次预热和 512 次串行 RTT。RT/s 是往返次数，**不是并发饱和吞吐**。
不测每次 I/O 的 deadline；超时语义由 contract tests 验证。

### 成绩、噪声与诊断分离

每轮以未插桩 A/B 四驱动组夹住诊断组，四驱动顺序轮换，A/B 的前后顺序逐轮交换。
所有平台、所有驱动都收集 A/A 对照；主成绩只使用 A 组，以逐轮配对的 median/MAD
报告相对 **libuv** 的 p50、p95、RT/s 差值。MAD 不是置信区间，不自动把大于 5% 的
单次结果判为回归。A/A 与插桩扰动和差距相当时，应在受控宿主复测。

诊断报告替代旧的重复 inclusive 宽表和逐轮闭合明细：

- 线程 CPU、wall-CPU 估计、A/A 波动、插桩组相对周围 A/B 的均值偏移。
  CPU 来自已有 libuv 的 [`uv_getrusage_thread`](https://docs.libuv.org/en/v1.x/misc.html#c.uv_getrusage_thread)，
  在整个测量 batch 两端读取，不包含 echo 线程。Windows CPU 时间记账较粗，摘要改用
  [`QueryThreadCycleTime`](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime)
  的线程 cycles/RT，不把 cycles 换算成时间；粗粒度 CPU ns 仍保留在 CSV，wall-CPU 标为
  unresolved，不把量化误差解释成等待。调用不支持或失败时报错。
- 四驱动采用同一外层边界：start API、drive API、payload 校验、剩余 harness 时间。
  CNet 的校验嵌套于 poll，从 drive 中扣除；其他驱动的校验在 drive 之后。
  CNet start 是发送入队，NativeIO start 是提交，libuv start 是接收启用和发送，
  coroutine start 是任务启动；**这些边界职责不同，不是同语义内部阶段**。
  CNet 的实际提交在 drive 内，drive 仍混合等待和分发，不能直接归因为 CPU 成本。
- 阶段差值是逐轮配对后的**算术均值差**，可加和到同批诊断均值差。
  不拿诊断均值解释另一批未插桩 p50/p95；剩余时间保留，不用代数闭合宣称根因已解释。
- CNet 内部只保留一张紧凑证据表：固定控制、发送复制、NativeIO 提交/重提交、observe、
  请求/完成数。这是自身成本，不是相对 libuv 的因果结论。

计时区间没有文件写入或逐事件日志。固定大小的样本数组在 benchmark 中保留全部数据，
空间为 O(payload 数 × 4 驱动 × 3 组 × 5 轮 × 512 样本)，实际字节数在报告开头打印。
不修改生产 CNet/NativeIO ABI、队列、生命周期或调度；仍使用既有私有 profiling target。

### 原始证据与复算

设置 `CNET_IO_BENCHMARK_OUTPUT` 为输出路径前缀（父目录必须已存在），全部计时结束后写：

- `<prefix>.runs.csv`：backend/protocol/payload/driver/pass/repeat 标识、RT 数、batch wall、
  客户线程 CPU、Windows 线程 cycles、p50/p95、上下文切换和外层 API 调用次数。
- `<prefix>.samples.csv`：相同标识加原始 sample 编号，每次 RTT 的 wall 和诊断阶段数据。
  未插桩阶段、Windows/macOS 不支持的上下文切换字段留空，不能视为零。

前缀指定的两个文件会覆盖；写入/关闭失败使测试失败。未设置前缀时仅输出摘要。
CI 保存 CSV，不把所有样本灌入 step summary，并运行
`pwsh -File cnet/benchmarks/verify_io_benchmark.ps1 -Prefix <prefix>`，核对 540 轮、
276480 个样本、逐样本非重叠阶段、计数以及从样本重算的 p50/p95。
慢样本的阶段只能与**同一诊断轮次、同一 sample 编号**关联。

### retained/owned 基准

默认四驱动基线中的 CNet 使用 `cnet_send_buffer`；CI 只把 retained/owned
路径与 NativeIO direct 比较和设门禁。CNet copied-send benchmark mode 已随
copied stream API 一起移除。NativeIO 自身仍可保留 flatten-vs-SG 诊断，用于隔离
payload memcpy 与 vectored submit 成本，但这不代表 CNet 存在 copied data plane。

Linux `cnet_scaling_benchmark` 每个场景保留 5 次配对测量。
`verify_scaling_benchmark.ps1` 对 16 连接、32/64 KiB 场景的 owner residual、
observer framework 和 client poll wrapper，分别以 5 次中位数检查
`2/1/1 us/op` 上限，并报告 MAD 和最大值。单次超限保留警告，持续超限仍失败；
该门禁衡量典型开销，不保证每次运行都低于上限。墙钟计时可能包含调度停顿，
仅凭单个尖峰不能归因于框架回归。非有限值、负耗时和嵌套区间错误仍逐行拒绝，
不参与中位数聚合。NativeIO 配对吞吐/延迟的 MAD 稳定性门禁独立保留。

### Linux 系统调用证据

`CNET_IO_BENCHMARK_TRACE=<driver>:<protocol>:<bytes>` 单独运行一个未插桩 workload，
只输出 trace 身份与测量区间标记，**不输出性能成绩**。driver 为
`libuv|native|coroutine|cnet`，protocol 为 `tcp|udp`，bytes 为 TCP 1–65536 或
UDP 1–8192。非法配置报错，不降级到默认 workload。

CI 在成绩完成后使用 [`strace`](https://strace.io/) 收集 TCP 1/32/64 KiB、UDP 8 KiB
各驱动独立 trace，按线程分文件。`summarize_io_trace.ps1` 只统计客户端
`IO_BENCH_MEASURE_BEGIN/END` 之间的系统调用，排除 setup、warmup、cleanup 和 echo 线程；
生成 `syscalls.csv`、`summary.csv`，分别输出每 RTT 的数据调用、epoll poll、io_uring_enter、直接 epoll_ctl、
EAGAIN 和空 epoll 返回次数；另列 `poll+ppoll/RT` 和空 `poll+ppoll/RT`。
io_uring 后端对 ring/wake fd 的 `poll` 等待必须采集，不能只数 `io_uring_enter`。
超时的 `0 (Timeout)` 与裸 `0` 都计作空返回。解析失败、缺少区间、缺少客户端文件均报错。

用它检查“是否多做了注册/提交/空等待”，不要把 ptrace 下的调用耗时当作未插桩成本。
`io_uring_enter` 次数也不是 SQE 数。libuv 可能通过 io_uring 提交 epoll 控制操作，
不能把“直接 epoll_ctl 为零”解释为“没有注册成本”（参见
[libuv 1.51 Linux 实现](https://github.com/libuv/libuv/blob/v1.51.0/src/unix/linux.c)）。
trace 是独立复跑，不能关联先前未插桩的单个慢样本。
CPU 函数栈以及 blocked/runnable 等待分离仍需具备权限的 profiling host 上采集
[`perf record`](https://man7.org/linux/man-pages/man1/perf-record.1.html) /
[`perf sched`](https://man7.org/linux/man-pages/man1/perf-sched.1.html)；普通 CI 明确不宣称已采集。
例如在已用 `linux-release-user` 构建且已配置运行库路径的宿主执行：

```sh
CNET_IO_BENCHMARK_BACKEND=epoll CNET_IO_BENCHMARK_TRACE=native:tcp:32768 \
  strace -ff -ttt -T -s 128 -o native-tcp-32768.trace \
  build/linux-gcc-release/bin/cnet_io_benchmark --no-color
```

更换驱动重复同一 workload。只有 syscall 次数、CPU/调度证据与未插桩复测相互印证后，
才把候选原因升级为根因；否则报告保留“未解释”，不凭阶段表直接优化生产路径。

### TLS receive cadence attribution

Linux `cnet_tls_public_owner_benchmark` 的私有 profiling 构建可对一次精确 echo
记录有界事件时间线。trace 模式要求 `echo`、`owners=1`、`fresh` 和一次 operation，
避免把多个逻辑操作混入同一 session/request 时间线：

```sh
CNET_TLS_PUBLIC_TRACE=1 \
CNET_TLS_PUBLIC_OWNER_OPS=1 \
CNET_TLS_PUBLIC_NODELAY=1 \
CNET_TLS_PUBLIC_RECEIVE_DEMAND=single \
  build/linux-gcc-release/bin/cnet_tls_public_owner_benchmark \
  echo 32768 1 fresh
```

stderr 的 JSON lines 按统一单调时钟合并 client/server 事件，包含 TLS ciphertext
write submit、socket write completion、NativeIO read arm/completion、TLS record decrypt、
plaintext publish、completion bytes 以及 session/request/endpoint identity。已经在 trace
启用前 arm 的 TLS read 使用 request record 保存的原始 submit 时间，不把 trace 启用时间
误当成 read-arm 时间。固定 trace buffer 满额会令 benchmark 失败，不静默丢事件。
正常 benchmark 未设置 `CNET_TLS_PUBLIC_TRACE` 时不记录逐事件时间线；安装的
`Salts::CNet` target 也不包含该私有 profiling 路径。


### TLS logical write ownership

Steady-state TLS payload ownership uses the same bounded write FIFO as plain
streams. A plaintext write slot remains the logical owner while GmSSL may
generate one or more ciphertext NativeIO writes; ciphertext requests borrow
only the TLS scratch buffer and never own application plaintext. The logical
slot settles only after all ciphertext generated for that plaintext has reached
terminal success. The next TLS logical head starts on a later owner drive.

A final TLS write carries the same close-after-send marker. Further public
admission closes immediately, while `close_notify` starts only after the final
plaintext has been accepted and all of its ciphertext has flushed.


## Coroutine ownership boundary

CNet remains a caller-driven, single-owner session/request state machine above NativeIO. It does
not create a private coroutine executor, I/O thread pool, or second await/result registry.

The dependency boundary is intentional:

```text
CNet session state
      |
      v
NativeIO request/completion authority
      |
      +-> Direct / Coroutine / Sharded execution style
```

NativeIO coroutine frames are an optional execution-position mechanism owned by NativeIO. They do
not replace CNet's session lifecycle, write FIFO, TLS state, or terminal callback contract.
Application code that wants coroutine orchestration may use the generic Coroutine Executor above
CNet, but the executor must return to the connection owner before mutating CNet session state.

Current owner tests require that ordinary CNet NativeIO requests do not retain NativeIO coroutine
frames. This is deliberate: adding a coroutine frame around the existing CNet owner state machine
would duplicate execution state without removing an I/O hop. A future CNet coroutine-facing adapter
requires a concrete consumer and paired evidence before it can change this boundary.

Cross-owner work remains explicit and bounded. CNet never hides live connection migration,
work-stealing, or an implicit worker-pool hop behind its public send/receive APIs.
