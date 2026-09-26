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
- `cnet_send` to transfer one bounded payload copy into CNet;
- `cnet_sendv` to concatenate non-empty borrowed ranges directly into that
  same final bounded command slot without caller-side staging;
- `observer.on_send` to observe completion before admitting the next ordered
  write on that connection;
- `cnet_receive` to add explicit receive demand;
- `cnet_close` for one connection;
- `cnet_client_poll` to advance I/O and invoke callbacks on the caller;
- `cnet_client_stop` followed by `cnet_client_destroy` for shutdown.

`command_capacity` bounds the number of live copied commands,
`max_send_bytes` bounds one send, and `command_buffer_bytes` independently
bounds their aggregate copied payload. A zero aggregate budget preserves the
legacy `command_capacity * max_command_payload` bound. Set it explicitly when
large individual writes must coexist with a smaller retained-memory budget;
admission over either the slot or byte budget returns `SALTS_ENOBUFS`.

`event_capacity` likewise bounds event descriptors while `event_buffer_bytes`
bounds their aggregate copied payload. Event payloads and per-connection receive
buffers are allocated only while in use; configuring a large per-message bound
therefore no longer reserves its product with every event or connection slot.

TCP, VSOCK, and Pipe deliver byte chunks. Connected UDP delivers one datagram per
receive callback. A receive view is borrowed only until its callback returns.
TLS delivers verified encrypted byte streams through the same send/receive
contract. The same header also exposes bound UDP, the KCP session engine, and
their unified packet endpoint; WebSocket remains in `<cnet/websocket.h>`. CNet parses
TCP, TLS, and UDP URIs through Salts UriParser and then applies
transport-specific constraints: network URIs require an explicit port and reject
userinfo, path, query, and fragment components instead of accepting truncated or
ambiguous input. VSOCK uses a separate strict decimal `uint32` CID/port parser
and never enters DNS. Pipe is a scheme-specific IPC endpoint rather than a
network authority, so its bounded name after `pipe://` is preserved byte-for-byte.

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

### 独立 retained-send 对照

设置 `CNET_IO_BENCHMARK_SEND_COMPARE=1` 单独比较 `cnet_send` 与
`cnet_send_buffer`，不改变默认四驱动基线；其他值或同时设置 TRACE 均报错。
TCP 1/4/8/16/32/64 KiB，每组 5 次重复、32 次预热、512 次顺序 RTT。
两种方法使用相同的不可变 external `mem_buffer_t` 输入，分配和包装在预热之前完成，
每次 RTT 都等待一次发送完成及完整回包校验。单客户端线程拥有缓冲区引用，
CNet 只在 retained admission 后持有额外引用；输入不在运行中修改，排空销毁后检查
只剩调用方引用并释放。仅一条在途发送，容量和失败语义沿用主 benchmark，任何失败终止实验。

每个 repeat 含 A/B 两对未插桩运行，A/B 时间顺序交替，两对内部方法顺序相反，
同一 pass 的方法顺序按 payload/repeat 轮换。报告配对 p50 差的 median/MAD；
原始 CSV 还保留 p95、batch wall、线程 CPU/cycles 和全部 RTT，可检验噪声。
差值包含完整发送 admission、buffer 获取/引用计数和释放路径变化，**不等于 memcpy 独占成本**，
也不能直接拿这一组替换原来的 libuv/CNet 成绩。

CI 将这组结果独立保存为 `send.runs.csv`、`send.samples.csv` 和
`cnet-send-comparison.md`。例如使用已经配置运行库路径的 Release 可执行文件：

```powershell
$env:CNET_IO_BENCHMARK_SEND_COMPARE = '1'
$env:CNET_IO_BENCHMARK_OUTPUT = 'build/send'
./build/Msvc-Release/bin/cnet_io_benchmark.exe --no-color
if ($LASTEXITCODE -ne 0) { throw 'Send comparison failed' }
pwsh -File cnet/benchmarks/verify_io_benchmark.ps1 -Prefix build/send -SendComparison
Remove-Item Env:CNET_IO_BENCHMARK_SEND_COMPARE
Remove-Item Env:CNET_IO_BENCHMARK_OUTPUT
```

验证器必须使用 `-SendComparison`，严格检查 120 轮、61440 个样本及可复算分位数，
不接受混入 baseline/diagnostic 的记录或缺失组合。

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
