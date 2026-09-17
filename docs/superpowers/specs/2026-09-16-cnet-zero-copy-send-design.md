# CNet retained-buffer zero-copy send design

Issue: #286

## Status

Design only. Production implementation starts only after the design review gate is accepted.

## Problem

`cnet_send()` and `cnet_sendv()` synchronously copy payload bytes into CNet-owned bounded command storage before returning success. That ownership rule is safe and remains unchanged, but the copy cost grows with payload size and is material at 64 KiB.

CNet already has pooled command storage. Salts already has reference-counted `mem_buffer_t` and external-buffer wrapping. The missing capability is an explicit ownership contract that lets CNet retain an existing payload buffer and submit those exact bytes to NativeIO without copying them into command staging.

## Zero-copy definition

For plaintext transports, zero-copy means:

> After successful admission, CNet performs no payload-byte copy between the caller-provided retained buffer and the buffer pointer passed to NativeIO.

This is application/CNet zero-copy. It does not claim kernel/network-stack zero-copy such as `MSG_ZEROCOPY`, registered-buffer DMA, page pinning, or NIC bypass.

For TLS, the guarantee is narrower: no CNet command-staging copy of plaintext. TLS record construction and encryption keep their existing buffers and are not described as end-to-end zero-copy.

## Goals

- Preserve `cnet_send()` and `cnet_sendv()` API and ownership semantics.
- Add one explicit contiguous-buffer zero-copy API.
- Retain exactly one CNet reference for each admitted zero-copy send.
- Allow the caller to release its own reference immediately after successful admission.
- Pass the original payload pointer to NativeIO on plaintext transports.
- Preserve bounded admission, one-write-pending-per-connection behavior, cancellation, timeout, close, callback, and shutdown semantics.
- Release retained ownership exactly once on every terminal path.
- Prove zero command-staging copies deterministically, not only by timing.

## Non-goals

- Changing legacy copied-send ownership.
- Public vectored zero-copy in Z1.
- Redesigning allocator size classes, ring buffers, dispatcher, scheduler, or coroutine lifetime.
- Adding platform-specific kernel zero-copy APIs.
- Claiming TLS ciphertext zero-copy.
- Increasing concurrent writes per connection.

## Public API

Z1 exposes exactly this API:

```c
int cnet_send_buffer(
    cnet_client *client,
    cnet_connection connection,
    mem_buffer_t *buffer);
```

`cnet/include/cnet/cnet.h` will include the existing public `salts_buffer.h`; Z1 will not introduce a second CNet-owned buffer abstraction.

The payload is the first `mem_buffer_used(buffer)` bytes beginning at `mem_buffer_const_data(buffer)`.

Validation rules:

- null client or buffer -> `SALTS_EINVAL`;
- used size `0` -> `SALTS_EINVAL`;
- used size greater than `max_send_bytes` -> `SALTS_EMSGSIZE`;
- stale connection -> existing stale-handle error;
- not connected, write pending, closing, or TLS upgrade pending -> existing `SALTS_EBUSY` behavior;
- bounded queue exhaustion -> existing bounded queue error.

### Caller contract

On `SALTS_OK`, CNet has accepted the payload under retained ownership and the caller may immediately release its own reference.

While the send is in flight, the caller must treat the buffer as immutable: payload bytes and buffer metadata that define the payload (`data`, `used`, `capacity`) must not be modified. Refcounting protects lifetime, not mutability.

On rejection, CNet leaves caller ownership unchanged and retains no lasting reference.

A terminal send may complete synchronously during admission. Therefore the required invariant is:

> CNet acquires one retained reference before publishing the accepted command and releases that reference only after the authoritative terminal path no longer needs the payload.

If terminal completion occurs synchronously, the CNet reference may also be released before `cnet_send_buffer()` returns; the caller's original reference keeps the argument valid for the duration of the call.

## Chosen ownership model

The command entry remains the sole CNet owner of the retained buffer for the complete logical send lifetime.

```text
caller mem_buffer_t
      |
      | retain once during accepted publish
      v
cnet_command_entry  [sole CNet owning reference]
      |
      | command view borrows pointer
      v
cnet_owner_request  [borrow only]
      |
      | NativeIO operation borrows same pointer
      v
NativeIO
      |
      | authoritative terminal completion
      v
cnet_command_queue_release()
      |
      | release retained mem_buffer_t exactly once
      v
final external ref determines actual free
```

This is preferred over moving an owning reference from command to request because it reuses the existing command token/generation as the single release authority. Request state and `session->tls_send_command` borrow the command view and never retain a second buffer reference.

Consequences:

- synchronous completion uses the normal command release path;
- partial stream sends retain the base buffer until the whole logical send terminates;
- stale/double release is rejected by command-entry state/generation validation before touching the buffer reference;
- TLS can keep its existing borrowed command-view shape.

## Command representation

Add an internal payload ownership kind:

```c
typedef enum cnet_command_payload_kind {
  CNET_COMMAND_PAYLOAD_NONE = 0,
  CNET_COMMAND_PAYLOAD_COPIED,
  CNET_COMMAND_PAYLOAD_RETAINED_BUFFER
} cnet_command_payload_kind;
```

A send command uses exactly one payload mode.

```text
COPIED
  input bytes
    -> queue allocates payload buffer
    -> memcpy
    -> entry owns copied mem_buffer_t

RETAINED_BUFFER
  caller mem_buffer_t
    -> queue retains once
    -> entry stores retained mem_buffer_t pointer
    -> no command payload allocation
    -> no payload memcpy
```

For retained mode, the command view returned by `cnet_command_queue_take()` has:

```text
view.data == mem_buffer_const_data(retained_buffer)
view.size == captured mem_buffer_used(retained_buffer)
```

The size and base pointer are captured at admission; later caller mutation is forbidden by the public contract.

## Admission ordering and rollback

A rejected publish must never leak a retained reference.

Required ordering:

1. validate command and size;
2. check queue admission-open state;
3. check free command slot;
4. check retained-send boundedness;
5. select free entry;
6. retain the caller buffer;
7. initialize the entry fully as `QUEUED` with retained payload mode;
8. publish the slot to the queue.

There must be no fallible step after retain that can return without either publishing the entry or releasing the retained reference.

## Boundedness and backpressure

Zero-copy bypasses copied `command_buffer_bytes`, but not bounded admission.

Z1 uses existing command capacity as the retained-buffer count bound. A send remains represented by a live command entry until terminal completion, and every payload is independently bounded by `max_send_bytes`.

Therefore:

```text
retained zero-copy buffer count <= command_capacity
retained zero-copy payload bytes <= command_capacity * max_send_bytes
```

The second quantity is caller/external memory temporarily retained by CNet, not CNet-allocated command payload memory.

`command_buffer_bytes` continues to mean copied pending-command storage only. No unbounded retained-buffer side list is introduced.

## Existing path integration

The retained-buffer path goes through the current layers:

```text
cnet_send_buffer()
  -> cnet_client_send_admit()
  -> cnet_shards_send_buffer()
  -> cnet_shards_publish()
  -> cnet_command_queue_publish()
  -> cnet_owner_process_commands()
  -> cnet_owner_send()
  -> cnet_owner_start_request()
  -> NativeIO
```

The connection-record checks and `write_pending` transition remain identical to copied send. There is no second queue, worker, scheduler, or fast-path topology.

## Plaintext transports

For TCP, UDP, pipe, and vsock plaintext sends:

```text
command_view.data == mem_buffer_const_data(original_buffer)
NativeIO operation.buffer == command_view.data
```

Partial stream sends advance the NativeIO operation pointer within the original retained buffer exactly as the current request path advances copied payload pointers. The command entry retains the base buffer until the full logical send reaches an authoritative terminal state.

UDP keeps existing all-or-error behavior; partial datagram completion remains an error.

## TLS boundary

For TLS, `session->tls_send_command` continues to borrow a command view while the command entry owns the retained reference.

`cnet_tls_write()` consumes plaintext directly from that retained view. Existing TLS ciphertext buffers remain unchanged.

The retained plaintext command is released only when existing TLS send semantics establish that no subsequent CNet operation references the plaintext command view.

No claim is made that TLS record/ciphertext construction is zero-copy.

## Terminal and failure paths

All terminal paths converge on `cnet_command_queue_release()` for the admitted command.

Required cases:

- normal send completion;
- synchronous completion during spawn/submit;
- partial stream send then success;
- partial stream send then failure;
- request cancellation;
- request timeout;
- peer close / connection abort;
- client close with a send pending;
- backend shutdown/drain;
- stale or duplicate native completion.

Z1 does not add a zero-copy `send_and_close` API.

Invariant:

```text
for each admitted retained-buffer command:
  exactly one valid cnet_command_queue_release(token)
  => exactly one release of the CNet-retained mem_buffer_t reference
```

A second release with the same token must fail state/generation validation before buffer release.

## External buffers

Buffers created with `mem_wrap_external()` are supported.

For an external buffer with a free callback:

- successful admission adds one CNet reference;
- caller may release immediately;
- callback cannot run while CNet still needs the payload;
- callback runs after terminal release when the final reference reaches zero;
- rejected admission adds no lasting reference, so caller release may run the callback immediately.

This is both supported behavior and a deterministic ownership test surface.

## Deterministic zero-copy proof

Timing is not proof of zero-copy.

Reuse the existing command-queue payload-copy profiling counters:

- legacy copied publish increments `payload_copy_calls`;
- retained-buffer publish leaves `payload_copy_calls` unchanged.

Also assert structurally in unit tests that the retained command view's `data` pointer is the original `mem_buffer_const_data()` pointer.

The retained path must not allocate a replacement command payload buffer.

## Tests

### Command queue

- retained publish succeeds with zero payload-copy calls;
- view pointer equals original buffer pointer;
- caller reference can be released before queue take/release;
- queue release drops the retained reference exactly once;
- full queue rejects without retaining;
- closed queue rejects without retaining;
- stale/double release cannot release again;
- copied publish behavior remains unchanged.

### Public CNet

- zero-copy TCP send completes and reports normal `on_send` size;
- caller releases immediately after `SALTS_OK`;
- external free callback fires only after terminal send;
- busy write rejects without ownership change;
- stale connection rejects without ownership change;
- close/failure/timeout releases ownership;
- partial stream send keeps buffer alive through all partial completions;
- legacy send tests remain unchanged.

### TLS

- retained plaintext remains alive until TLS no longer references the command view;
- command-stage payload-copy count stays zero;
- existing TLS ciphertext behavior is unchanged.

### Runtime matrix

Exact-head correctness on Linux epoll, Linux io_uring, Windows IOCP, and macOS kqueue.

## Benchmark

Add a separate retained-buffer CNet driver/variant to `cnet_io_benchmark`; do not replace the legacy CNet row.

Primary decision surface:

- TCP plaintext;
- 64 KiB primary payload;
- Windows IOCP primary;
- Linux epoll/io_uring and macOS kqueue regression coverage;
- persistent connection;
- same payload contents;
- existing warmup/repeat protocol;
- same-run A/A control.

Keep 1/4/8/16/32 KiB rows to show scaling.

Report separately:

- legacy CNet copy path;
- CNet retained-buffer zero-copy path;
- NativeIO direct path;
- zero-copy send admission/control cost;
- command payload-copy calls/bytes where available;
- zero-copy vs copied p50/p95/rate paired median/MAD;
- same-run A/A noise floor.

Retained buffers are allocated and filled outside the timed send-admission region. Otherwise caller-side construction cost would replace the removed CNet copy and confound the comparison.

## Decision gate

The issue closes with exactly one result:

- `zero-copy validated and measurable`;
- `zero-copy valid but below noise floor`;
- `zero-copy contract/benchmark invalid`.

A performance win is not required to establish the semantic capability, but merging the production API requires the ownership contract and deterministic zero-copy proof to pass on all supported runtime platforms.

## Implementation boundaries

Expected Z1 production touch points:

- `cnet/include/cnet/cnet.h`
- `cnet/src/cnet_client.c`
- `cnet/src/cnet_shards.h`
- `cnet/src/cnet_shards.c`
- `cnet/src/cnet_command.h`
- `cnet/src/cnet_command.c`
- `cnet/src/cnet_owner.c` only if lifetime handling requires a change
- command/API/TLS tests

Benchmark work is Z2 and touches `cnet/benchmarks/cnet_io_benchmark.c` separately.

No allocator redesign or NativeIO API change is expected.

## Acceptance criteria

- Legacy copied APIs and semantics are unchanged.
- `cnet_send_buffer()` is the sole new Z1 public send surface.
- `cnet/cnet.h` uses the existing public `mem_buffer_t` type.
- Successful admission retains exactly one CNet reference before accepted publication.
- Rejected admission leaves ownership unchanged.
- In-flight payload bytes and defining buffer metadata are immutable by contract.
- Command staging performs zero payload-byte copies for retained mode.
- Plaintext NativeIO receives the original payload pointer.
- Command entry remains the sole CNet retained-reference owner.
- Request/TLS state borrows without adding ownership.
- Every terminal path releases exactly once.
- Duplicate/stale release cannot double-release.
- Admission remains hard bounded.
- Pool-owned and externally wrapped buffers are covered.
- Linux/Windows/macOS correctness is green at exact head.
- Benchmark retains A/A noise control and the legacy copy baseline.
- Final issue records one of the three predeclared decisions.
