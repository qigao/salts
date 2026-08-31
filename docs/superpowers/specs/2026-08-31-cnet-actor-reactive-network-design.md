# CNet Actor/Reactive Network Design

## Status

- Tracking issue: [#194](https://github.com/qigao/turbo-utils/issues/194)
- Decision scope: a new TurboUtils network module; no production API is added by
  this design change.
- Public module name: `CNet`
- Public CMake targets: `TurboUtils::CNet` and `TurboUtils::CNetCFlow`
- Public C headers: `<cnet/cnet.h>` and `<cnet/cflow.h>`

## Background

NativeIO is deliberately a raw, owner-driven operation layer. Its public
contract accepts already-created native sockets or pipes, submits bounded
read/write operations, and returns terminal completions. It does not create a
thread, own a user socket, resolve a host, connect a transport, perform a
protocol handshake, or publish application events. This boundary is stated in
`native-io/README.md` and enforced by `native-io/include/turbo/native_io.h`.

CFlow already binds NativeIO completions to a bounded I/O Actor and a Reactive
Publisher through `cflow_io_native_adapter`. That adapter deliberately receives
caller-owned `native_io_operation` values; it is not a connection or protocol
implementation. `cflow/include/cflow/io_native_adapter.h` also preserves a
single NativeIO owner and separates the Publisher owner from Subscriber
execution.

The missing capability is a network session layer. Applications should not
assemble OS sockets, NativeIO endpoints, TLS state, WebSocket framing, KCP
timers, Actor acknowledgements, and Reactive demand merely to perform four
application operations:

1. connect;
2. send;
3. receive;
4. observe connection state.

CNet provides that layer. It is intended to replace the capabilities of the
separate coroutine networking stack, but it imports none of that stack's
source, types, ABI, coroutine runtime, or event loop.

## Decision

Add an independent `cnet/` root module above NativeIO. CNet owns connections,
transport composition, protocol state, bounded payload storage, and public
connection state. NativeIO remains the only owner of native operation progress.
Optional CFlow adapters expose the same CNet session as Actor control events and
a demand-aware Reactive receive Publisher.

```text
Application
    |
    +-------------------- connect / send / receive / state
    v
                  CNet session core
                 /        |         \
        Actor control   direct      Reactive receive
          projection   diagnostics    projection
                 \        |         /
                  one authoritative session record
                               |
                      NativeIO operations
                               |
             IOCP / epoll / io_uring / kqueue / blocking
```

The direct projection exists for deterministic tests, diagnostics, and the
performance denominator. It is not a separate network implementation.

## Why This Is Not NativeIO or CFlow

| Candidate | Result | Reason |
|---|---|---|
| Add connection/protocol APIs to NativeIO | Rejected | It would mix OS operation progress with DNS, handshakes, messages, callbacks, and policy. NativeIO would cease to be reusable as a raw backend. |
| Put network protocols inside CFlow | Rejected | CFlow owns typed execution, Actor, demand, and scheduling. TCP/TLS/WS/KCP semantics are not Graph or Actor semantics and would make CFlow network-specific. |
| Add CNet above both layers | Selected | It preserves `CNet -> NativeIO`, while the optional `CNetCFlow -> CNet + CFlow` adapter supplies Actor/Reactive projections without a dependency cycle. |

The dependency graph is fixed:

```text
TurboUtils::CNet       -> TurboUtils::NativeIO
TurboUtils::CNet       -> TurboUtils::Concurrency
TurboUtils::CNetCFlow  -> TurboUtils::CNet
TurboUtils::CNetCFlow  -> TurboUtils::CFlow
TurboUtils::CFlow      -> TurboUtils::NativeIO
TurboUtils::NativeIO   -> TurboUtils::Platform
```

NativeIO and CFlow cannot include CNet headers or link CNet targets.

## Public Concepts

### Handles and states

`cnet_client` is the network service boundary. `cnet_connection` is a
generation-checked value handle, never an OS descriptor and never a pointer to
an internal session.

```c
typedef struct cnet_client {
    void *impl;
} cnet_client;

typedef struct cnet_connection {
    uint32_t slot;
    uint32_t generation;
} cnet_connection;

typedef enum cnet_connection_state {
    CNET_CONNECTION_CONNECTING = 1,
    CNET_CONNECTION_CONNECTED,
    CNET_CONNECTION_CLOSING,
    CNET_CONNECTION_CLOSED,
    CNET_CONNECTION_FAILED
} cnet_connection_state;

typedef enum cnet_message_kind {
    CNET_MESSAGE_BYTES = 1,
    CNET_MESSAGE_DATAGRAM,
    CNET_MESSAGE_TEXT,
    CNET_MESSAGE_BINARY
} cnet_message_kind;
```

Internal states are more precise than public states:

```text
FREE
  -> RESERVED
  -> RESOLVING
  -> TRANSPORT_CONNECTING
  -> PROTOCOL_HANDSHAKING
  -> OPEN
  -> DRAINING
  -> TERMINAL
  -> FREE(next generation)
```

The public mapping is:

| Internal state | Public state |
|---|---|
| `RESERVED`, `RESOLVING`, `TRANSPORT_CONNECTING`, `PROTOCOL_HANDSHAKING` | `CONNECTING` |
| `OPEN` | `CONNECTED` |
| `DRAINING` | `CLOSING` |
| normal `TERMINAL` | `CLOSED` |
| failed `TERMINAL` | `FAILED` |

One session record is the only source of truth. NativeIO request slots,
protocol codec state, Actor events, and Reactive values are derived state. They
cannot independently change or infer the public connection state.

### Observer contract

```c
typedef struct cnet_receive_view {
    const void *data;
    size_t size;
    cnet_message_kind kind;
} cnet_receive_view;

typedef struct cnet_error {
    int status;
    int native_status;
    const char *stage;
} cnet_error;

typedef void (*cnet_state_fn)(
    void *user,
    cnet_connection connection,
    cnet_connection_state state,
    const cnet_error *error);

typedef void (*cnet_receive_fn)(
    void *user,
    cnet_connection connection,
    const cnet_receive_view *view);

typedef struct cnet_observer {
    cnet_state_fn on_state;
    cnet_receive_fn on_receive;
    void *user;
} cnet_observer;
```

`cnet_error` is non-NULL only for `FAILED`. `stage` is an owned, stable CNet
constant such as `resolve`, `connect`, `tls-handshake`, `websocket-handshake`,
`read`, `write`, or `shutdown`. Callbacks are serialized for one connection and
never run while an internal lock is held. Different connections may run
callbacks concurrently. The observer and its `user` context must be valid when
`cnet_connect` is called because asynchronous notification may race the caller
after successful admission.

State and data delivery for one connection is ordered: `CONNECTED` precedes the
first receive callback, `CLOSING` precedes a normal terminal callback, and no
callback occurs after `CLOSED` or `FAILED`. A callback may call `cnet_send`,
`cnet_receive`, or `cnet_close`; those calls only publish commands. It cannot
call `cnet_client_stop` or `cnet_client_destroy`, because either operation would
wait for the currently executing callback.

The receive view is borrowed through the callback only. Saving its pointer
after the callback returns is invalid. A later retained-buffer API, if needed,
must use an explicit retain/release ownership transfer and is outside this
contract.

### Client configuration

```c
typedef struct cnet_client_config {
    native_io_backend_kind backend;
    size_t io_shards;
    size_t callback_workers;
    size_t connection_capacity;
    size_t command_capacity_per_shard;
    size_t request_capacity_per_shard;
    size_t completion_batch_capacity;
    size_t data_event_capacity_per_shard;
    size_t payload_chunk_bytes;
    size_t payload_chunk_count;
    size_t max_message_bytes;
    size_t max_queued_bytes_per_connection;
    uint32_t connect_timeout_ms;
    uint32_t shutdown_timeout_ms;
} cnet_client_config;
```

Every capacity must be positive and checked for multiplication/addition
overflow before allocation. `max_message_bytes` cannot exceed the representable
payload-pool budget. CNet fails initialization rather than silently reducing a
capacity or selecting another backend.

CNet uses TurboUtils `turbo_threadpool` for its configured I/O owner shards and
callback workers; it does not implement another thread pool. Each I/O shard has
exactly one long-lived owner task and one NativeIO backend. Connections are
assigned to a shard once and never migrate while live. `callback_workers` are
separate from I/O owner workers so slow business callbacks cannot stop socket
progress.

### Connection options

```c
typedef struct cnet_tls_options {
    const char *server_name;
    const char *ca_file;
    const char *client_certificate_file;
    const char *client_private_key_file;
} cnet_tls_options;

typedef struct cnet_websocket_options {
    const char *subprotocol;
    size_t max_header_bytes;
    size_t max_frame_bytes;
    size_t max_message_bytes;
} cnet_websocket_options;

enum {
    CNET_KCP_PSK_BYTES = 32
};

typedef struct cnet_kcp_fec_options {
    uint16_t data_shards;
    uint16_t parity_shards;
    uint16_t max_payload_bytes;
    uint16_t receive_group_count;
} cnet_kcp_fec_options;

typedef struct cnet_kcp_options {
    uint8_t pre_shared_key[CNET_KCP_PSK_BYTES];
    uint16_t mtu;
    uint16_t send_window;
    uint16_t receive_window;
    uint16_t interval_ms;
    uint16_t handshake_retry_ms;
    uint8_t fast_resend;
    bool disable_congestion_window;
    cnet_kcp_fec_options fec;
} cnet_kcp_options;

typedef struct cnet_connect_options {
    const char *uri;
    cnet_observer observer;
    cnet_tls_options tls;
    cnet_websocket_options websocket;
    cnet_kcp_options kcp;
} cnet_connect_options;
```

Typed option groups avoid an unbounded string-key map. Strings are copied into
bounded session-owned storage during successful admission; callers may release
the option structure after `cnet_connect` returns.

TLS verification is enabled by default. An empty `server_name` derives the name
from the URI host. Disabling certificate or hostname verification is not part
of the initial API. KCP requires a non-zero 32-byte PSK and valid Reed-Solomon
dimensions; raw KCP, unauthenticated KCP, and FEC `NONE` are not valid modes.
`cnet_connect` copies the KCP key into session-owned secret storage and wipes it
when the session reaches terminal state.

### Application operations

```c
int cnet_client_init(
    cnet_client *client,
    const cnet_client_config *config);

int cnet_connect(
    cnet_client *client,
    const cnet_connect_options *options,
    cnet_connection *out_connection);

int cnet_send(
    cnet_client *client,
    cnet_connection connection,
    const void *data,
    size_t size);

int cnet_receive(
    cnet_client *client,
    cnet_connection connection,
    size_t demand);

int cnet_close(
    cnet_client *client,
    cnet_connection connection);

int cnet_client_stop(
    cnet_client *client,
    uint32_t timeout_ms);

int cnet_client_destroy(cnet_client *client);
```

The application data plane remains connect, send, receive, and state
notification. Client init/stop/destroy are explicit resource lifecycle, not
additional transport models.

`cnet_connect` validates and copies the request, reserves a generation handle,
and publishes one bounded command. `TURBO_OK` means admission succeeded, not
that the connection is already established. Immediate validation or capacity
failure leaves `out_connection` zero and produces no callback.

`cnet_send` copies the payload once into CNet's bounded payload pool before
returning success. Success means CNet owns that copy; it does not mean that the
peer received or acknowledged it. An asynchronous write failure preserves the
first error and moves the connection to `FAILED`. Empty sends are rejected with
`TURBO_EINVAL`. Sending before `CONNECTED` returns `TURBO_EBUSY`; CNet does not
hide connection latency behind an implicit pre-connect queue.

`cnet_receive` adds application receive demand. One unit permits one delivered
byte chunk or one complete message according to transport semantics. Demand
uses checked saturating rejection: overflow returns `TURBO_EOVERFLOW` without
changing prior demand. CNet does not arm another application receive when
demand is zero, except for bounded protocol-control reads required for
handshake, ping/pong, close, acknowledgements, or KCP retransmission.

`cnet_close` stops new send and receive admission for the handle. A second close
while draining returns `TURBO_EALREADY`. A stale or recycled handle returns
`TURBO_ENOENT`. Normal close emits `CLOSING` then exactly one `CLOSED`; any
failure emits exactly one `FAILED` and never emits `CLOSED` afterward.

`cnet_client_stop` stops new connections, requests close for all live sessions,
drains or cancels accepted NativeIO requests within the bounded deadline,
settles callbacks, and stops owner tasks. Timeout returns `TURBO_ETIMEDOUT` and
preserves the client for another stop attempt. `destroy` returns `TURBO_EBUSY`
until stop has reached quiescence.

## Transport Semantics

| URI scheme | Composition | Receive unit | `CONNECTED` means |
|---|---|---|---|
| `tcp://host:port` | TCP -> NativeIO | byte chunk | TCP connection completed |
| `udp://host:port` | connected UDP -> NativeIO | one datagram | local UDP socket is associated with the peer; it does not prove peer reachability |
| `pipe://name` | platform byte pipe -> NativeIO | byte chunk | named/FIFO endpoint opened |
| `tls://host:port` | TLS -> TCP -> NativeIO | decrypted byte chunk | TCP and verified TLS handshake completed |
| `ws://host:port/path` | WebSocket -> TCP -> NativeIO | complete text/binary message | TCP and HTTP Upgrade completed |
| `wss://host:port/path` | WebSocket -> TLS -> TCP -> NativeIO | complete text/binary message | TCP, verified TLS, and HTTP Upgrade completed |
| `kcp://host:port` | KCP -> authenticated AEAD record -> Reed-Solomon FEC -> connected UDP -> NativeIO + owner timer | complete KCP message | the authenticated PSK handshake completed, session/FEC keys were derived, and the KCP conversation was created from the session epoch |

TCP, TLS, and Pipe preserve ordered byte-stream semantics but do not promise
that receive callback boundaries match send call boundaries. UDP preserves one
datagram per receive. WebSocket reassembles fragments subject to the configured
message limit. KCP runs in message mode; stream mode is excluded from the first
contract.

KCP uses the authenticated wire contract as an explicit CNet protocol, not as
behavior attributed to upstream KCP. Client/server nonces and a server session
epoch are authenticated with the configured PSK. Directional AEAD keys and an
FEC authentication key are derived from that handshake. The non-zero KCP
conversation ID is derived from the session epoch, so it is not a public
configuration field. Data follows this exact order:

```text
application message -> KCP segment -> AEAD record -> FEC shard -> UDP datagram
```

Receive reverses the order. FEC covers encrypted/authenticated KCP records and
cannot expose unauthenticated recovered plaintext.

CNet preserves the existing authenticated KCP wire version used by the
replacement target: `TKSH` authenticated handshake frames, `TKSR` secure data
records, and `TKF1` authenticated FEC frames. Compatibility is a wire-level
contract only; CNet does not link or include the other runtime. Checked-in
golden packets cover
handshake derivation, encrypted data, parity recovery, replay rejection, and
tamper rejection so later implementations cannot silently fork the protocol.

## Execution and Communication Protocol

### Thread topology

For each shard:

```text
producer threads (MPSC)
        |
        | claim -> copy command/payload -> publish -> native_io_backend_wake
        v
fixed command ring
        |
single CNet/NativeIO owner task
        |
        +-> session state + protocol codecs + timer heap
        +-> NativeIO submit/observe/cancel
        +-> fixed event ring
                         |
                    callback workers / CFlow adapter
```

The command ring uses TurboUtils `disruptor` in an MPSC-to-single-consumer
topology. It carries descriptors and payload leases, never caller pointers.
Each session has fixed storage for its monotonic public state notifications, so
event pressure cannot discard a terminal notification. Receive events use a
separate bounded ring and carry a payload-pool lease. When that ring is full,
the owner stops arming application reads while continuing the bounded protocol
control required for shutdown. The owner is the only writer of session and
protocol state.

The notification paths are not second state machines. Consumers can delay
delivery but cannot change the authoritative session record. CNet retains the
session slot until its terminal state notification and all borrowed payload
leases have been delivered and released.

### Payload pool and copy boundary

CNet preallocates fixed payload chunks. A send claim reserves the command slot
and all required chunks before copying any byte. If either reservation fails,
the function releases the partial claim and returns `TURBO_ENOBUFS`; no command
is published. A successful claim copies each application byte exactly once into
CNet-owned chunks.

NativeIO currently accepts one contiguous buffer per operation. The first CNet
milestone may submit one chunk at a time while preserving per-session write
lane order. A later NativeIO scatter/gather operation is permitted only as a
separate raw-operation enhancement with direct NativeIO tests and benchmarks;
it cannot be simulated by extra CNet payload copies.

Receive buffers are claimed by the owner before NativeIO submission. NativeIO
borrows the claimed storage until terminal completion. CNet then feeds protocol
codecs in place and publishes either a borrowed slice or a bounded reassembled
message lease. The lease returns to the pool after the callback or Reactive
`on_next` completes.

### Claim terminal states

Every successful claim has exactly one public terminal path:

| Claim | Terminal outcomes |
|---|---|
| connection slot | `CLOSED` notification or `FAILED` notification, then recycle |
| command slot | consumed, rejected with an asynchronous state error, or cancelled during stop |
| payload lease | submitted then completed/cancelled, or released before publication |
| NativeIO request | one observed completion, including cancellation |
| Reactive demand unit | one delivered value, cancellation, or terminal error/completion |

No path can abandon a claimed slot during shutdown.

## Actor and Reactive Projections

`TurboUtils::CNetCFlow` contains adapters, not another CNet implementation.

### Actor projection

- Connect, close, timeout, and terminal errors become typed control events.
- One retained Actor reference is associated with a connection binding.
- Actor acknowledgement releases only the event token; it does not release the
  CNet session slot or NativeIO request.
- Payload chunks are not copied into the Actor mailbox. Actor actions that need
  data receive a bounded buffer lease or coordinate with the Reactive stream.
- Actor failure requests CNet close; it cannot mutate CNet state directly.

### Reactive projection

- One connection receive publisher moves into one Subscription.
- Downstream demand is translated to `cnet_receive` demand; it counts delivered
  chunks/messages, not NativeIO read attempts or protocol frames.
- The Publisher owns its adapter state and borrows the CNet client/connection
  until close.
- Subscriber operators and callbacks run on a CFlow Worker Scheduler distinct
  from the CNet/NativeIO owner.
- Cancellation stops demand, requests connection close only when configured as
  an owning subscription, drains outstanding delivery, and acknowledges every
  accepted event exactly once.

Actor and Reactive may observe one connection concurrently. Actor receives the
control projection and Reactive receives the data projection; neither is a
second socket driver.

## Protocol Libraries

Protocol dependencies must be I/O-neutral:

- TLS uses BoringSSL with memory BIOs. BoringSSL exposes memory BIOs and
  caller-managed buffer ownership, so encrypted bytes remain in CNet/NativeIO
  buffers: <https://boringssl.googlesource.com/boringssl/+/HEAD/include/openssl/bio.h>.
- WebSocket framing uses Wslay's low-level frame API. Wslay performs no I/O and
  supports an external event loop, but explicitly does not implement the HTTP
  opening handshake: <https://github.com/tatsuhiro-t/wslay>.
- WebSocket Upgrade parsing uses llhttp's generated C parser; it performs no
  network I/O: <https://github.com/nodejs/llhttp>.
- KCP uses the upstream algorithm core. Upstream requires caller-provided UDP
  output and clock/update calls and performs no system calls:
  <https://github.com/skywind3000/kcp>.
- KCP FEC uses TurboUtils's independently vendored `reed/gf256.c` and
  `reed/gf256.h`. They retain their provenance from `xtaci/libkcp` commit
  `824a449f6c966f247a8c7c2109e069c2383f360c` and Daniel Fu's MIT license.
- KCP keyed BLAKE2b, XChaCha20-Poly1305, constant-time verification, and secret
  wiping use the existing private TurboUtils Monocypher target. No primitive is
  reimplemented in CNet.

BoringSSL, c-ares, llhttp, and KCP enter through the existing vcpkg manifest.
Wslay is not in the baseline registry used by this repository; admission
therefore requires a pinned overlay port containing upstream URL/commit,
checksum, MIT license, and an unmodified-source statement. CNet wraps all
third-party types in private translation units. BoringSSL contains C++ objects,
so CNet includes one private C++ linkage translation unit and links its C API
target with the configured C++ runtime, matching the established repository
integration. No third-party type or error code enters installed headers.

`reed/gf256` is migrated into TurboUtils rather than referenced through a
sibling repository path. Its public-to-CNet API is extended with a
caller-provided reconstruction workspace so FEC recovery does not allocate on
the I/O owner. Initialization may allocate its bounded codec matrix before the
session opens; encode/decode/reconstruct use CNet-owned shard and workspace
storage afterward.

Upstream KCP allocates segments through a process-global allocator hook, which
cannot enforce a per-client or per-session byte budget. The CNet KCP feature
therefore uses a pinned overlay patch that adds allocator context to each KCP
control block and preallocates its segment, acknowledgement, and protocol
buffers from a session-owned bounded pool. The patch is recorded separately
from upstream source with its removal condition. KCP is not enabled until the
patched behavior passes allocation-exhaustion and upstream conformance tests.

Hostnames use c-ares with its supported asynchronous event-thread mode. That
resolver owns only DNS query sockets; it never receives a CNet session socket
or NativeIO endpoint. Resolver completion copies a bounded address result into
the owning shard's command ring and then calls NativeIO wake. Cancellation
invalidates the query generation, and late resolver callbacks can only release
their result. This is a separate resolver fact source, not a second owner of a
session socket. c-ares also documents an external-event-loop integration for
applications that later require one: <https://c-ares.org/docs/ares_process_fds.html>.

BoringSSL and the admitted protocol libraries may allocate inside their private
implementations. CNet itself performs no owner-hot-path allocation; it bounds
all application, command, reassembly, and KCP storage it controls and reports
third-party allocation behavior separately in protocol benchmarks.

## Error Semantics

Synchronous API failures use Turbo error codes and do not mutate the destination
on validation/admission failure. Asynchronous errors preserve the first useful
failure in the session, record its stage, and publish `FAILED` exactly once.
Native status is diagnostic and never replaces the portable Turbo status.

Important mappings include:

| Condition | Result |
|---|---|
| invalid URI/options/zero demand/empty payload | `TURBO_EINVAL` |
| stale generation handle | `TURBO_ENOENT` |
| command, connection, request, callback, or payload capacity full | `TURBO_ENOBUFS` |
| checked size/demand arithmetic overflow | `TURBO_EOVERFLOW` |
| send before `CONNECTED` | `TURBO_EBUSY` |
| send/receive after close admission | `TURBO_ESHUTDOWN` |
| second close while draining | `TURBO_EALREADY` |
| explicit backend or transport unavailable | `TURBO_ENOTSUP` |
| bounded connect/shutdown deadline elapsed | `TURBO_ETIMEDOUT` |
| destroy before quiescence | `TURBO_EBUSY` |

There is no implicit transport downgrade, certificate-verification bypass,
backend fallback, message truncation, retry with unbounded allocation, or error
logging followed by success.

## Security Boundaries

- TLS certificate-chain and hostname verification are mandatory by default.
- TLS private keys remain in BoringSSL-owned objects and are never logged.
- URI, host, pipe name, HTTP headers, WebSocket frame/message lengths, UDP
  datagram lengths, and KCP options have explicit maximums.
- WebSocket client masking, control-frame constraints, UTF-8 validation, close
  codes, fragmentation, and Upgrade header validation are delegated to the
  admitted codec/parser and covered by protocol corpus tests.
- CNet KCP always provides PSK authentication, AEAD confidentiality/integrity,
  replay-window checks, and authenticated Reed-Solomon FEC. It does not provide
  public-key identity or forward secrecy; documentation cannot imply either.
- Callbacks receive bounded summaries; payload contents, credentials, and keys
  do not enter logs.

## Compatibility and Migration

This design does not change current NativeIO or CFlow public behavior. CNet is a
new target, so existing consumers do not link it unless requested. The initial
implementation remains behind `CNET_ENABLE_EXPERIMENTAL=OFF` and does not add
installed targets or headers until TCP, connected UDP, Pipe, state/error
semantics, Actor projection, Reactive projection, and shutdown conformance pass
on Windows, Linux, and macOS.

TLS, WS/WSS, and KCP are separately selectable CNet features. Requesting a
disabled scheme fails during URI admission with `TURBO_ENOTSUP`; CNet never
downgrades `wss` to `ws` or `tls` to `tcp`.

Replacement of the separate coroutine network stack is a consumer migration,
not a source, ABI, or runtime dependency in TurboUtils. The authenticated KCP
wire contract is deliberately preserved and verified with independent golden
vectors; the C/C++ application API is not treated as compatible. Consumer API
migration requires a later, independent decision after CNet is stable.

Rollback is safe before installation: disable the experimental option and
remove the uninstalled module. After installation, semantic versioning and the
normal TurboUtils export set apply; rollback cannot silently reuse handles or
change a URI scheme's semantics.

## Verification

### Conformance

One fake transport suite drives every state transition, error stage, capacity
boundary, stale handle, reentrant callback rejection, close race, stop timeout,
and exact-once terminal invariant deterministically.

Transport integration tests cover:

- TCP byte-stream fragmentation, half-close, reset, connect refusal, and
  timeout;
- UDP datagram boundaries, zero-length datagrams at the protocol boundary, and
  unreachable diagnostics where the platform exposes them;
- Windows named pipe and POSIX pipe byte-stream behavior;
- TLS verification success/failure, partial records, renegotiation policy, and
  orderly/abrupt shutdown;
- WS/WSS fragmented messages, control frames, invalid masks/lengths/UTF-8,
  Upgrade rejection, and close handshake;
- KCP authenticated handshake retry/timeout, wrong PSK, replay, tampered
  data/FEC shards, loss, duplication, reordering, recovery within parity limits,
  failure beyond parity limits, timer advancement, window exhaustion,
  conversation mismatch, drain, and compatibility with checked-in `TKSH`,
  `TKSR`, and `TKF1` golden packets.

Actor tests assert typed state delivery and exact acknowledgement. Reactive
tests assert demand, cancellation, callback thread separation, no delivery at
zero demand, and borrowed-view expiry. Sanitizer runs cover callback shutdown,
pool reuse, and stale handles.

### Benchmarks

Every benchmark row uses the same OS backend, transport, peer, payload, request
window, sample count, and thread topology. Direct NativeIO is the raw I/O
denominator. Direct CNet isolates session/protocol overhead. Actor/CNet and
Reactive/CNet then isolate their respective control and demand costs.

Report separate tables by transport and payload for p50/p95/p99 latency,
operations per second, MiB/s, CPU time, payload copies, pool high-water marks,
command batch size, NativeIO submit/observe time, protocol time, and callback
dispatch time. Compare p50 with p50 and p95 with p95; no percentile mixing is
valid.

No performance optimization is accepted without a profile showing its stage.
The initial targets are correctness gates rather than promises: no hidden
allocation after initialization on the NativeIO owner hot path, one application
payload copy for the basic send API, and bounded batch processing.

## Consequences

- **HIGH — fact:** CNet requires NativeIO async connect support or an equally
  explicit CNet platform transport adapter. A synchronous connect on the I/O
  owner would stall every connection on the shard. The implementation plan adds
  raw `TCP_CONNECT` completion support to NativeIO while preserving borrowed
  socket ownership.
- **HIGH — fact:** TLS, WebSocket, and KCP add supply-chain and security surface.
  They remain private, pinned dependencies with independent protocol tests.
- **HIGH — inference:** One authoritative session record prevents Actor,
  Reactive, protocol, and OS completion state from diverging. Correctness still
  depends on enforcing owner-only mutation and exact terminal delivery in code.
- **MED — computation:** payload memory is approximately
  `payload_chunk_bytes * payload_chunk_count` plus per-chunk metadata and
  alignment. Configuration validation must report the complete retained-memory
  budget before starting workers.
- **MED — fact:** Actor and Reactive add scheduling and acknowledgement costs.
  Direct CNet remains measurable so those costs are never attributed to
  NativeIO or hidden by mismatched benchmarks.
- **LOW — migration:** the new CMake targets and headers are additive. No current
  consumer changes until it opts into CNet.
