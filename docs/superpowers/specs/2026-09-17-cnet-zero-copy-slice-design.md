# CNet Retained Slice Zero-Copy Send Design

**Issue:** #292  
**Dependency:** #286  
**Status:** committed design; implementation is blocked until #286 Z1 retained-buffer ownership is validated

## 1. Purpose

CNet's first retained zero-copy phase (#286) intentionally sends one whole `mem_buffer_t`. That is the smallest ownership contract: successful admission retains one backing buffer, CNet stages only metadata, NativeIO borrows the same bytes, and the retained reference is released exactly once after the authoritative terminal send result.

This follow-up adds a **subrange** form of the same ownership model.

The motivating case is an application that already owns a larger shared buffer but needs to send only a header-free body, one frame payload, one record, or another contiguous region. Without a subrange API the application must either:

1. copy the region into a second `mem_buffer_t`, or
2. create another external wrapper around an interior pointer and manually reconstruct lifetime ownership.

Both defeat the purpose of the retained-buffer abstraction.

Salts already has the correct lifetime-bearing view type:

```c
struct mem_slice_s {
  char *data;
  size_t length;
  mem_buffer_t *buffer;
};
```

`mem_slice()` creates a view into a `mem_buffer_t` and retains the backing buffer; `mem_slice_release()` releases that slice-owned reference.

By contrast, `vstr` is deliberately a non-owning `{data,len}` view. `vstr` is useful for zero-copy parsing, slicing, searching, and inspection, but it is not sufficient to keep asynchronous CNet payload storage alive.

## 2. Scope classification

This is an architectural public-API follow-up rather than an internal micro-optimization because it adds a new asynchronous ownership surface.

The design is nevertheless intentionally narrow:

- one contiguous `mem_slice_t` only;
- reuse #286 retained-buffer ownership;
- no scatter/gather retained vectors;
- no new allocator or pool;
- no `vstr` ownership changes;
- no coroutine/direct-owner architecture changes;
- no kernel zero-copy claim.

## 3. Selected approach

### 3.1 Public API

Add:

```c
int cnet_send_slice(
    cnet_client *client,
    cnet_connection connection,
    const mem_slice_t *slice);
```

The `mem_slice_t` object is borrowed only for the synchronous duration of the admission call.

CNet never stores the caller's `mem_slice_t *`.

On successful admission CNet snapshots the range metadata and retains `slice->buffer` exactly once.

### 3.2 Why not `cnet_send_vstr()`

Do not add:

```c
int cnet_send_vstr(..., vstr payload);
```

A bare `vstr` contains no owner, refcount, release callback, or other lifetime token. An asynchronous API accepting only `vstr` would have to choose one of two bad contracts:

- silently copy the bytes, which is not the retained zero-copy path; or
- borrow caller memory after return, which would be unsafe and inconsistent with CNet's explicit ownership model.

The correct layering is:

```text
vstr
  borrowed byte/string view
  parsing / search / slicing / inspection

mem_slice_t
  retained contiguous subrange view
  carries backing mem_buffer_t lifetime

mem_buffer_t
  shared storage owner

CNet retained async send
  retains mem_buffer_t, never retains vstr
```

Existing `vstr_from_slice()` remains useful when the same slice bytes need string-style inspection without copying.

## 4. API semantics

### 4.1 Successful admission

For:

```c
int status = cnet_send_slice(client, connection, &slice);
```

if `status == SALTS_OK`:

1. the slice was validated as a non-empty range inside the current `used` bytes of its backing buffer;
2. CNet retained `slice.buffer` exactly once before returning;
3. CNet copied only bounded descriptor metadata, not payload bytes;
4. the caller may immediately call `mem_slice_release(&slice)`;
5. the caller may also release any other caller-owned backing-buffer references that are no longer needed;
6. the payload bytes and backing buffer metadata must remain immutable while CNet owns its reference;
7. CNet releases its retained reference exactly once after the logical send reaches its authoritative terminal and no NativeIO request can still reference those bytes.

The caller-owned slice reference and the CNet retained reference are independent.

Example lifetime:

```text
before call
  backing refcount = caller buffer ref + slice ref

successful cnet_send_slice
  backing refcount += one CNet ref

caller mem_slice_release immediately
  slice ref removed

NativeIO send in flight
  CNet ref keeps storage alive

terminal send completion
  CNet ref removed exactly once
```

### 4.2 Rejected admission

If admission fails before ownership transfer:

- CNet acquires no lasting reference;
- caller/slice ownership is unchanged;
- no send terminal callback is produced for the rejected operation.

### 4.3 Mutability rule

While CNet owns its retained reference, the caller must not mutate:

```text
payload bytes covered by the admitted slice
backing buffer data pointer
backing buffer used
backing buffer capacity
```

This is the same immutability rule as #286 whole-buffer retained sends.

Unrelated bytes outside the admitted slice are not read by CNet, but changing the backing allocation or metadata while CNet holds the slice is forbidden.

## 5. Canonical slice validation

`mem_slice_t` is a public struct. A caller can therefore manually construct or corrupt one. CNet must validate the owner/range relation before retaining anything.

### 5.1 Required validation

Reject with `SALTS_EINVAL` unless all are true:

```text
slice != NULL
slice->buffer != NULL
slice->data != NULL
slice->length > 0
mem_buffer_const_data(slice->buffer) != NULL
mem_buffer_used(slice->buffer) > 0
slice start lies inside [backing_start, backing_start + used)
slice length fits entirely inside the remaining used bytes
```

A canonical implementation may compute:

```text
base_addr  = integer address of backing data
slice_addr = integer address of slice data
used       = backing used bytes

require slice_addr >= base_addr
offset = slice_addr - base_addr
require offset < used
require slice_length <= used - offset
```

All address and length arithmetic must be overflow-checked.

Do not subtract unrelated C pointers directly to discover an offset. The validation must fail closed for forged pointers.

### 5.2 Error ordering

Freeze the public error ordering:

1. invalid/null/non-canonical slice -> `SALTS_EINVAL`;
2. canonical slice whose `length > max_send_bytes` -> `SALTS_EMSGSIZE`;
3. then apply the existing #286 connection/write/admission checks and preserve their existing errors (`SALTS_ENOENT`, `SALTS_EBUSY`, `SALTS_ENOBUFS`, `SALTS_ESHUTDOWN`, etc.).

No reference may be retained until all fallible validation and queue/capacity checks that can reject synchronously have passed.

### 5.3 Existing `mem_slice()` overflow concern

The current `mem_slice()` implementation clamps using an `offset + length` comparison. A pathological huge length can therefore be considered independently for hardening, but **this CNet design does not depend on that helper being perfect**.

`cnet_send_slice()` must validate `length <= used - offset` with subtraction-based checked arithmetic, so a malformed or overflow-created public slice cannot escape the CNet admission boundary.

Changing `mem_slice()` itself is outside this issue unless a dedicated correctness test demonstrates that change is necessary and it is tracked separately.

## 6. Internal ownership representation

Reuse #286's retained-buffer payload kind. Do not create a second ownership mode for slices.

Conceptually a retained slice command contains:

```text
payload kind     = retained buffer
owner            = mem_buffer_t *
view data        = const byte *
view size        = size_t
copied byte cost = 0
```

The implementation may store an offset instead of a raw view pointer if that preserves pointer identity and does not depend on mutable metadata after admission. What is mandatory is:

- exactly one retained backing-buffer reference;
- exact slice pointer/length semantics;
- no secondary `mem_buffer_t` allocation;
- no payload memcpy into command storage.

### 6.1 Sole-owner invariant

Retain the #286 rule:

> The command entry is the sole CNet owner of the retained backing buffer for the entire logical send.

The request/TLS/NativeIO layers borrow the retained view. They do not add independent retained references.

This avoids ownership transfer complexity and double-release risk.

### 6.2 Whole-buffer sends remain a special case

`cnet_send_buffer()` is equivalent to a retained view whose data pointer is the beginning of the buffer and whose size is `mem_buffer_used(buffer)`.

The slice implementation must therefore extend the retained-view metadata without changing #286 whole-buffer semantics.

Do not duplicate two command-queue implementations for whole-buffer and slice sends.

## 7. Data path

### 7.1 Plain stream path

```text
caller mem_slice_t
    |
    | validate owner/range
    | retain backing buffer once
    v
bounded CNet command descriptor
    |
    | view pointer + remaining length
    v
CNet owner request
    |
    | NativeIO borrows same bytes
    v
partial/terminal completions
    |
    v
one logical send terminal
    |
    v
command entry releases backing ref once
```

### 7.2 Partial stream sends

A partial completion advances only the logical view cursor:

```text
view_data      += bytes_completed
remaining_size -= bytes_completed
```

The retained backing owner does not change.

Requirements:

- one CNet logical send;
- one write-pending interval;
- one deadline;
- one final `on_send` event;
- one retained CNet buffer reference;
- zero release on intermediate partial completions;
- exactly one release after the logical terminal.

### 7.3 UDP boundary

Do not broaden #286 transport semantics in this issue.

If #286's public retained-buffer API is stream-only in its final implementation, slice sends remain stream-only.

If #286 already exposes the retained path to a transport where CNet's public API supports it safely, this issue may reuse that exact transport set. It must not create a new datagram API family merely because `mem_slice_t` exists.

## 8. TLS boundary

The zero-copy claim is identical to #286:

- no CNet **command-staging plaintext payload copy**;
- retained slice plaintext can be consumed directly by the TLS owner;
- TLS record encryption and ciphertext buffering remain expected;
- no kernel/network-stack zero-copy claim.

TLS must borrow the retained view. It does not take another backing-buffer reference.

## 9. Backpressure and boundedness

A retained slice consumes the same bounded command/write/request resources as a whole-buffer retained send.

It does not consume copied `command_buffer_bytes` because CNet does not stage payload bytes.

The implementation must not add:

- an unbounded retained-slice list;
- a new unbounded owner queue;
- a second payload pool;
- hidden heap allocation per slice send.

The existing command capacity remains the hard admission bound unless #286's final retained-buffer design introduces a stricter explicit retained-send bound. Slice sends must reuse whichever bound #286 establishes.

## 10. Interaction with `vstr`

`vstr` can reduce copies around this API, but only as a borrowed inspection layer.

Example:

```c
mem_slice_t payload = mem_slice(buffer, payload_offset, payload_length);
vstr text = vstr_from_slice(&payload);   /* no copy */
/* parse/inspect text */
int status = cnet_send_slice(client, connection, &payload);
mem_slice_release(&payload);             /* legal immediately after SALTS_OK */
```

No conversion from `vstr` back into an owning async send is added.

If an application starts from arbitrary borrowed `vstr` bytes and needs async zero-copy, it must first associate those bytes with a lifetime owner, for example through an existing `mem_buffer_t` / `mem_slice_t` or `mem_wrap_external()` owner contract.

## 11. Error and cleanup behavior

All terminal paths converge on the same #286 retained-buffer release point.

The following must release CNet's retained backing reference exactly once:

```text
normal send terminal
partial send then terminal
write timeout
explicit close/cancel
peer failure
TLS failure
client stop/drain
synchronous backend terminal where supported
```

The following must not release a live request incorrectly:

```text
stale CNet handle
stale NativeIO generation
synthetic/duplicate completion
completion for a recycled request slot
```

Rejected admission never owns and therefore never releases a CNet reference.

## 12. Deterministic zero-copy proof

Timing is not sufficient evidence.

Reuse #286's command-staging copy instrumentation.

For a retained slice send:

```text
payload_copy_calls delta = 0
payload_copy_ns delta    = 0
```

The command view presented to the owner must have pointer identity equal to the admitted slice's `data` pointer and size equal to `slice.length`.

Legacy copied `cnet_send()` must still exercise the copy instrumentation.

Whole-buffer `cnet_send_buffer()` must remain zero-copy after slice support is added.

## 13. Testing strategy

### 13.1 Public contract tests

Cover:

- NULL slice -> `SALTS_EINVAL`;
- NULL owner -> `SALTS_EINVAL`;
- NULL data -> `SALTS_EINVAL`;
- zero length -> `SALTS_EINVAL`;
- pointer before backing start -> `SALTS_EINVAL`;
- pointer at/after backing used end -> `SALTS_EINVAL`;
- length exceeding `used - offset` -> `SALTS_EINVAL`;
- valid canonical range exceeding `max_send_bytes` -> `SALTS_EMSGSIZE`;
- stale/busy/closing/queue-full errors preserve #286 behavior and do not retain.

### 13.2 Ownership tests

Use both pool-owned and `mem_wrap_external()` buffers.

Required assertions:

- `mem_slice()` adds its normal slice reference;
- successful `cnet_send_slice()` adds exactly one additional CNet reference;
- caller can immediately `mem_slice_release()`;
- external free callback does not fire while CNet is in flight;
- terminal send releases CNet's reference once;
- failed admission leaves refcount unchanged;
- duplicate/stale terminal cannot release twice.

### 13.3 Range tests

Send and verify exact peer bytes for:

- first-byte range;
- middle range;
- final-byte range;
- large middle range;
- a backing buffer with live bytes before and after the slice.

The peer must never receive adjacent bytes outside the admitted range.

### 13.4 Partial-send tests

Use the existing deterministic send-chunk seam where available.

For a four-byte slice forced into one-byte NativeIO submissions:

```text
one logical request
three resubmits after first submit
one final send event
one retained backing reference for the entire logical send
zero intermediate release
```

### 13.5 Regression tests

Keep green:

```text
cnet_send
cnet_sendv
cnet_send_buffer
TLS send behavior
close/timeout/stop contracts
C++ public header compile
```

## 14. Performance evidence

The primary value of this API is eliminating **application-side subrange staging** while preserving #286's command-stage zero-copy path.

Do not require `cnet_send_slice()` to outperform `cnet_send_buffer()`; both should ultimately drive the same retained-buffer machinery.

Performance checks should instead prove:

1. admission cost does not scale with slice payload bytes because CNet performs no payload copy;
2. slice send latency/throughput does not materially regress relative to whole-buffer retained send for the same bytes;
3. a representative application-side `copy subrange -> new buffer -> cnet_send_buffer()` path is measurably more expensive when the copied staging work is included, if such a benchmark is added.

The deterministic copy counter remains the authoritative zero-copy proof.

## 15. Alternatives considered

### A. `cnet_send_vstr()`

Rejected. `vstr` has no ownership token and cannot safely outlive the call without either copying or introducing unsafe borrowed async semantics.

### B. `cnet_send_buffer_range(buffer, offset, length)`

Viable and intrinsically easier to validate, but it duplicates an ownership-bearing range abstraction Salts already exposes as `mem_slice_t`.

The selected `mem_slice_t` API better composes with existing zero-copy parsing and slice lifetime operations, provided CNet validates the public struct defensively.

### C. Create a new CNet-specific retained view type

Rejected. It would duplicate `mem_slice_t` and create another ownership vocabulary without adding capability.

### D. Retained scatter/gather now

Rejected. It expands one backing owner into N owners, complicates boundedness, partial progress, cleanup, and TLS interaction. It remains a later independent phase after single-slice ownership is proven.

## 16. Acceptance criteria

Implementation is acceptable only when all are true:

- #286 whole-buffer retained ownership is already validated;
- `cnet_send_slice()` is public and the `mem_slice_t` object is borrowed only during admission;
- successful admission retains exactly one additional backing-buffer reference;
- caller can immediately `mem_slice_release()` after success;
- malformed/forged/out-of-range slices fail before retain;
- valid oversize slices return `SALTS_EMSGSIZE`;
- command staging performs zero payload-byte copies;
- owner receives the exact admitted slice pointer and length;
- partial sends keep the same backing owner until the logical terminal;
- timeout/close/failure/stop release exactly once;
- stale/duplicate completions cannot double-release;
- no extra `mem_buffer_t` is allocated for the slice;
- no `cnet_send_vstr()` is introduced;
- `cnet_send`, `cnet_sendv`, and `cnet_send_buffer` semantics stay unchanged;
- exact-head correctness passes Windows IOCP, Linux epoll, Linux io_uring, and macOS kqueue.

## 17. Non-goals

- changing `mem_slice_t` ABI;
- changing `vstr` into an owning type;
- adding borrowed-lifetime async APIs;
- retained scatter/gather vectors;
- allocator or slab redesign;
- ring-buffer redesign;
- kernel zero-copy;
- TLS ciphertext zero-copy;
- owner/coroutine/direct-NativeIO architecture work;
- merging #286 and #292 into one implementation PR.

## 18. Sequencing

1. Complete #286 Z1 whole-buffer retained send ownership.
2. Validate #286 deterministic zero-copy and terminal-release evidence.
3. Implement #292 by extending the same retained-buffer command representation with a validated view range.
4. Run full exact-head cross-platform correctness.
5. Only after #292 is stable, consider a separate retained scatter/gather design.

## 19. Design principle

> `vstr` removes copies while describing bytes. `mem_slice_t` makes a byte range retainable. `mem_buffer_t` owns the storage. CNet asynchronous zero-copy must retain the owner, not the view.
