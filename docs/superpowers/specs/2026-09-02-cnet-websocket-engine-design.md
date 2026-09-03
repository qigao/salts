# CNet WebSocket Engine Design

**Date:** 2026-09-02
**Status:** accepted for implementation
**Scope:** RFC 6455 frame parser hardening and a transport-independent,
post-handshake CNet WebSocket session engine. HTTP/1.1 Upgrade, RFC 8441,
`ws://`/`wss://` URI admission, and CHTTP routes are separate follow-up work.

The same implementation change replaces CNet's private hand-written network
URI tokenization with the existing `Salts::UriParser`. CNet retains
transport policy validation, output adaptation, and byte-preserving extraction
of the scheme-specific Pipe IPC endpoint.

## Context and decision

CNet already owns ordered TCP/TLS byte streams and their single progress owner.
NativeIO intentionally exposes only operating-system I/O, while CHTTP owns HTTP
routing and header semantics. WebSocket framing, fragmentation, masking,
ping/pong, and the closing handshake are connection protocol state, so the
session engine belongs in CNet.

The engine reuses `tools/wsparser` as the only RFC 6455 frame syntax parser.
The parser remains an internal object target: no CNet public header exposes its
types, and installed static consumers do not acquire a new link dependency.
CHTTP will continue to use llhttp for HTTP syntax and will transfer an upgraded
ordered stream to this engine only after validating the Upgrade request.

## Public boundary

The additive public header is `<cnet/websocket.h>`. A `cnet_websocket` is a
caller-owned opaque value initialized with explicit limits, a role, one frame
write callback, and one event callback.

The public operations are:

- init/destroy and state/error queries;
- feed ordered byte-stream input and flush one retained output frame;
- send text, binary, fragments, ping, pong, and close;
- mark the underlying transport closed.

Input and output callbacks execute synchronously on the session owner. Event
payloads are borrowed only until the callback returns. The transport write
callback must copy the frame before returning `SALTS_OK`. If it returns
`SALTS_EBUSY`, the engine retains exactly that one bounded frame; later send
admission returns `SALTS_EBUSY` until `cnet_websocket_flush()` succeeds. No
unbounded output queue or implicit worker is created.

## State and ownership protocol

| Data/state | Owner | Bound | Lifetime and failure rule |
| --- | --- | --- | --- |
| input bytes | session | `max_buffered_input_bytes` | copied by `feed`; a rejected append changes nothing |
| one frame | input buffer view | `max_frame_bytes` plus 14-byte header | borrowed until frame consumption; never crosses a callback return |
| fragmented message | session | `max_message_bytes` | reset after its event callback; overflow fails with close code 1009 |
| pending output | session | one frame, `max_frame_bytes` plus 14 bytes | retained only after transport `SALTS_EBUSY`; `flush` is the only retry path |
| event view | callback | message/control bound | invalid when the callback returns |

All three allocations are fixed at initialization and never grow. Checked
arithmetic validates header overhead and the combined memory budget before any
allocation. The object is single-owner and not thread-safe. Cross-thread use
must enter the existing owner/executor mailbox; the WebSocket engine does not
introduce a second scheduler or queue.

The state is OPEN, CLOSING, CLOSED, or FAILED. Sending Close commits the session
to CLOSING and prohibits later data/control admission. Receiving Close emits one
close event and, if necessary, admits an echo Close. The CLOSING state is
committed before that callback. Receiving Close becomes CLOSED only after the
echo frame has transferred to the transport; a retained echo remains CLOSING
until `flush` succeeds. The transport adapter then closes the underlying stream.
A protocol, UTF-8, size, or transport-write failure records the first error as
the sole terminal fact.

## RFC behavior

The parser rejects non-zero RSV bits, reserved opcodes, non-canonical 16/64-bit
lengths, a set high bit in a 64-bit length, fragmented control frames, control
payloads larger than 125 bytes, NULL misuse, and `size_t` overflow.

The session additionally enforces role-specific masking: client-to-server
frames are masked and server-to-client frames are not. It supports control
frames between fragments, reassembles fragments in order, validates complete
text messages and Close reasons as strict UTF-8, validates Close code ranges,
and responds automatically to Ping and peer Close. Protocol errors use 1002,
invalid text uses 1007, and configured frame/message limits use 1009. Client
output masking keys come only from `salts_platform_secure_random()`; entropy
failure is propagated with no fallback.

These rules follow [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455), especially
Sections 5.2-5.6, 7.4, 8.1, and 10.4.

## Alternatives

1. **Put WebSocket in NativeIO.** Rejected because it would make an OS I/O
   backend own message protocol state and duplicate it for every backend.
2. **Put the engine only in CHTTP.** Rejected because HTTP/1.1 Upgrade and HTTP/2
   extended CONNECT should share one frame/session state machine.
3. **Import the CoroNet WebSocket session wholesale.** Rejected because its
   coroutine transport ownership and buffer policy do not match CNet. CoroNet
   remains a behavioral reference; only the in-repository parser is reused.
4. **Add a generic Executor facade.** Rejected for this change. Thread pool,
   coroutine executor, and CFlow executors keep separate types with similar
   lifecycle/admission semantics.

## Compatibility, migration, and rollback

The change is additive: existing CNet APIs, schemes, poll ownership, callback
ordering, and binary handles are unchanged. The new engine does not claim that
`ws://`, `wss://`, or CHTTP WebSocket routes exist. Later adapters will bind its
write callback to CNet/CHTTP pending-action state and call `flush` after send
completion.

Rollback removes the new header/source/tests and the private parser object from
CNet. No stored data, wire default, configuration format, or existing ABI needs
migration. Verification covers parser corpus cases, split/coalesced input,
fragment/control interleaving, UTF-8, limits, masking, close, backpressure,
C/C++ headers, installed consumption, Release, and ASan regression.

## URI parser reuse

`uri_parser` has no Core dependency, so its subdirectory is configured before
CNet and is no longer configured a second time by the aggregate parser
directory. `cnet_uri.c` calls `uri_parse()` for TCP/TLS/UDP and maps the generic
result to the existing `cnet_uri` value. It continues to enforce the CNet input
ceiling, supported schemes, required network port, forbidden userinfo/path/
query/fragment for TCP/TLS/UDP, and Pipe name capacity. Pipe remains a
scheme-specific IPC endpoint: its bounded bytes after `pipe://` are validated
and copied unchanged, because interpreting them as an RFC authority would alter
valid endpoint names such as `[service]` or `name:segment`.

Before reuse, the generic parser must reject rather than silently truncate an
oversized scheme, userinfo, host, path, query, or fragment, and it must reject
an empty port. This changes only malformed or ambiguous inputs that have no
safe round-trip representation. Existing valid `tcp`, `tls`, `udp`, IPv6, and
Pipe forms retain their CNet output. If this integration is rolled back, parser
hardening remains independently correct and the CNet adapter can be reverted
without changing public structures.
