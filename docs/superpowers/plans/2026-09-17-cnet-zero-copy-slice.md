# CNet Retained Slice Zero-Copy Send Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `cnet_send_slice()` so callers can asynchronously send one canonical `mem_slice_t` subrange without copying payload bytes into CNet command storage, while CNet retains exactly one backing `mem_buffer_t` reference until the logical send terminal.

**Architecture:** Reuse #286's retained-buffer ownership path rather than adding a new payload kind. Extend the retained command descriptor from “whole buffer” to “retained owner + explicit view pointer + size”; whole-buffer `cnet_send_buffer()` becomes the zero-offset special case. `cnet_send_slice()` validates the public `mem_slice_t` defensively, snapshots its range, then enters the same client/shard/command/owner/TLS path as `cnet_send_buffer()`.

**Tech Stack:** C11, `mem_buffer_t`/`mem_slice_t`, CNet command/shard/client/owner/TLS layers, NativeIO, TinyTest, CMake presets, GitHub Actions release matrix.

**Spec:** `docs/superpowers/specs/2026-09-17-cnet-zero-copy-slice-design.md`

## Global Constraints

- Execution is blocked until #286 Z1 retained-buffer ownership is validated on an exact head and the implementation base contains `cnet_send_buffer()` plus the retained command payload mode.
- `cnet_send()`, `cnet_sendv()`, and `cnet_send_buffer()` public semantics remain unchanged.
- Add exactly one public send surface: `int cnet_send_slice(cnet_client *, cnet_connection, const mem_slice_t *)`.
- `vstr` remains a non-owning borrowed view; do not add `cnet_send_vstr()`.
- A successful slice admission retains exactly one additional backing `mem_buffer_t` reference before returning.
- The caller-owned `mem_slice_t` object is borrowed only during `cnet_send_slice()` and is never stored by CNet.
- The command entry remains the sole CNet owner of the retained backing buffer for the logical send; request/TLS/NativeIO state borrow the command view only.
- Snapshot the admitted raw view pointer and length at admission. Do not reconstruct the view later from mutable `buffer->used`, `buffer->capacity`, or caller-owned `mem_slice_t` storage.
- Validate the public slice relation with integer-address/subtraction-based checked arithmetic; do not subtract unrelated C pointers.
- Invalid/non-canonical slice is `SALTS_EINVAL`; canonical oversize is `SALTS_EMSGSIZE`; only then apply existing stale/busy/queue/shutdown admission checks.
- No retain may survive a rejected admission.
- Retained slice sends consume command/request/write-pending bounds but do not consume copied `command_buffer_bytes`.
- No secondary `mem_buffer_t`, payload pool, hidden per-send heap allocation, retained-vector API, allocator redesign, or kernel zero-copy claim.
- Plaintext zero-copy means the command/NativeIO payload pointer is the exact admitted `slice->data`; TLS still performs its normal record/ciphertext construction.
- Reuse #286's deterministic payload-copy counter and `cnet_owner_test_set_send_chunk_bytes()` seam; do not create a second partial-send test mechanism.
- #293 (`mem_slice()` overflow hardening) is independent and not a blocker because `cnet_send_slice()` validates public slice fields defensively.

---

### Task 0: Verify the #286 dependency and freeze the implementation base

**Files:** Evidence-only task. Do not modify source.

**Interfaces:**
- Consumes: validated #286 retained-buffer implementation.
- Produces: local ref `refs/cnet-slice/base`, used by every final diff/audit command in this plan.

- [ ] **Step 1: Verify the retained-buffer dependency exists on the intended base**

```bash
git grep -n 'int cnet_send_buffer' -- cnet/include/cnet/cnet.h cnet/src/cnet_client.c
git grep -n 'CNET_COMMAND_PAYLOAD_RETAINED_BUFFER' -- cnet/src/cnet_command.h cnet/src/cnet_command.c
git grep -n 'cnet_owner_test_set_send_chunk_bytes' -- cnet/src/cnet_owner.h cnet/src/cnet_owner.c
```

Expected: all three concepts exist. If any command returns no match, stop execution because #286 has not reached the required dependency state.

- [ ] **Step 2: Verify #286 ownership/zero-copy contracts on the same SHA**

```bash
cmake --preset linux-release-user -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON
cmake --build --preset linux-release-user --target \
  cnet_command_test cnet_command_profile_test cnet_send_buffer_api_test \
  cnet_owner_test cnet_owner_profile_test cnet_tls_test cnet_header_cpp_test
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_command(_profile)?_test|cnet_send_buffer_api_test|cnet_owner(_profile)?_test|cnet_tls_test|cnet_header_cpp_test)$' \
  --timeout 60 --output-on-failure
```

Expected: all applicable tests pass and retained-buffer command profiling proves `payload_copy_calls == 0`.

- [ ] **Step 3: Freeze the exact base locally**

```bash
git update-ref refs/cnet-slice/base "$(git rev-parse HEAD)"
git rev-parse refs/cnet-slice/base
```

Create the implementation branch from the same `HEAD`. The documentation branch is not the implementation base if it predates #286.

---

### Task 1: Extend retained commands from whole-buffer ownership to explicit retained views

**Files:**
- Modify: `cnet/src/cnet_command.h`
- Modify: `cnet/src/cnet_command.c`
- Modify: `cnet/tests/cnet_command_test.c`
- Modify: `cnet/tests/cnet_command_profile_test.c`

**Interfaces:**
- Consumes: #286 `cnet_command.retained_buffer`, `CNET_COMMAND_PAYLOAD_RETAINED_BUFFER`, command-owned retain/release, `cnet_command_view {data,size,_sequence}`.
- Produces: `cnet_command.retained_data` and exact retained subrange pointer identity with the existing payload kind.

- [ ] **Step 1: Write the command-queue RED**

In `cnet_command_test.c`, define:

```c
static cnet_command make_retained_view_send(uint32_t slot, mem_buffer_t *buffer,
                                            const void *data, size_t size) {
  cnet_command command = {0};
  command.kind = CNET_COMMAND_SEND;
  command.connection.slot = slot;
  command.connection.generation = 1u;
  command.size = size;
  command.retained_buffer = buffer;
  command.retained_data = data;
  return command;
}
```

Add a middle-range test that allocates a 32-byte backing buffer, publishes bytes `[8,20)`, and requires:

```c
check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
check_true(view.data == mem_buffer_const_data(buffer) + 8u);
check_equal(view.size, 12u);
check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));
```

Add forged retained descriptors for these exact invalid cases:

```text
retained_data == NULL
retained_data address below backing start
retained_data address at backing start + used
size > used - offset
```

Every invalid publish returns `SALTS_EINVAL` and leaves the backing refcount unchanged.

- [ ] **Step 2: Write the profiling RED**

In `cnet_command_profile_test.c`, publish an interior retained range and require:

```c
check_equal(profile.publish_calls, UINT64_C(1));
check_equal(profile.payload_publish_calls, UINT64_C(1));
check_equal(profile.payload_copy_calls, UINT64_C(0));
check_equal(profile.payload_copy_ns, UINT64_C(0));
check_true(view.data == interior_pointer);
```

- [ ] **Step 3: Run RED**

```bash
cmake --build --preset linux-release-user --target cnet_command_test cnet_command_profile_test
```

Expected first RED: `cnet_command.retained_data` is absent. An unrelated configure/link failure is not valid RED evidence.

- [ ] **Step 4: Add explicit retained-view metadata**

Append to `cnet_command` in `cnet_command.h`:

```c
const void *retained_data;
```

Extend `cnet_command_entry` in `cnet_command.c`:

```c
const void *payload_data;
```

Add:

```c
static bool cnet_command_retained_range_valid(mem_buffer_t *buffer,
                                              const void *data,
                                              size_t size) {
  const void *base_ptr;
  uintptr_t base;
  uintptr_t start;
  uintptr_t delta;
  size_t used;
  size_t offset;

  if (buffer == NULL || data == NULL || size == 0u) return false;
  base_ptr = mem_buffer_const_data(buffer);
  used = mem_buffer_used(buffer);
  if (base_ptr == NULL || used == 0u) return false;
  base = (uintptr_t)base_ptr;
  start = (uintptr_t)data;
  if (start < base) return false;
  delta = start - base;
  if (delta > (uintptr_t)SIZE_MAX) return false;
  offset = (size_t)delta;
  if (offset >= used) return false;
  return size <= used - offset;
}
```

Retained SEND validation becomes:

```c
return command->kind == CNET_COMMAND_SEND &&
       command->data == NULL &&
       command->segments == NULL && command->segment_count == 0u &&
       cnet_command_retained_range_valid(command->retained_buffer,
                                         command->retained_data,
                                         command->size);
```

- [ ] **Step 5: Snapshot the retained view into the queue entry**

After all synchronous admission checks:

```c
entry->payload = payload;
entry->payload_data = retained ? command->retained_data
                               : (payload != NULL ? mem_buffer_const_data(payload) : NULL);
```

For retained mode, never call `mem_set_used()` and never execute the scalar/vector payload `memcpy` path.

Change take to:

```c
out_view->data = entry->size != 0u ? entry->payload_data : NULL;
```

On release clear:

```c
entry->payload = NULL;
entry->payload_data = NULL;
```

Release exactly one retained backing reference and continue charging only copied bytes to `queued_bytes`.

- [ ] **Step 6: Preserve #286 whole-buffer semantics**

Every whole-buffer retained command must now set:

```c
.retained_buffer = buffer,
.retained_data = mem_buffer_const_data(buffer),
.size = mem_buffer_used(buffer)
```

`cnet_send_buffer()` behavior and refcount contract do not change.

- [ ] **Step 7: Run GREEN and commit**

```bash
cmake --build --preset linux-release-user --target cnet_command_test cnet_command_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_command(_profile)?_test$' --output-on-failure
git add cnet/src/cnet_command.h cnet/src/cnet_command.c \
  cnet/tests/cnet_command_test.c cnet/tests/cnet_command_profile_test.c
git commit -m "feat(cnet): retain explicit zero-copy payload views"
```

Expected: copied, whole-buffer retained, and interior retained-view command tests all pass; retained copy counters remain zero.

---

### Task 2: Add canonical `cnet_send_slice()` validation and admission

**Files:**
- Modify: `cnet/include/cnet/cnet.h`
- Modify: `cnet/src/cnet_client.c`
- Modify: `cnet/src/cnet_shards.h`
- Modify: `cnet/src/cnet_shards.c`
- Modify: `cnet/tests/cnet_send_buffer_api_test.c`
- Modify: `cnet/tests/cnet_header_cpp_test.cpp`

**Interfaces:**
- Consumes: Task 1 retained-view command representation.
- Produces: public `cnet_send_slice()` and one internal `cnet_shards_send_retained()` path shared by whole-buffer and slice sends.

- [ ] **Step 1: Write the public compile RED**

In `cnet_send_buffer_api_test.c`, construct a canonical slice and reference the missing API:

```c
mem_slice_t slice = mem_slice(buffer, 8u, 16u);
check_equal(cnet_send_slice(&client, connection, &slice), SALTS_OK);
```

In `cnet_header_cpp_test.cpp` add:

```cpp
using cnet_send_slice_signature =
    int (*)(cnet_client *, cnet_connection, const mem_slice_t *);
cnet_send_slice_signature slice_send = &cnet_send_slice;
(void)slice_send;
```

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset linux-release-user --target cnet_send_buffer_api_test cnet_header_cpp_test
```

Expected RED: `cnet_send_slice` is undeclared.

- [ ] **Step 3: Add the public declaration**

Beside `cnet_send_buffer()` in `cnet/include/cnet/cnet.h` add:

```c
/**
 * Retains the backing buffer of one non-empty canonical mem_slice_t on
 * successful admission and sends exactly slice->length bytes beginning at
 * slice->data without copying them into CNet command storage. The slice object
 * itself is borrowed only for this call. The caller may immediately release
 * the slice after SALTS_OK, but must not mutate admitted bytes or backing
 * data/used/capacity while the logical send remains in flight.
 */
int cnet_send_slice(cnet_client *client, cnet_connection connection,
                    const mem_slice_t *slice);
```

Do not include or expose `vstr` from CNet.

- [ ] **Step 4: Add one defensive retained-view validator in the client layer**

```c
static int cnet_client_validate_retained_view(const cnet_client_impl *impl,
                                              mem_buffer_t *buffer,
                                              const void *data,
                                              size_t size) {
  const void *base_ptr;
  uintptr_t base;
  uintptr_t start;
  uintptr_t delta;
  size_t used;
  size_t offset;

  if (impl == NULL || buffer == NULL || data == NULL || size == 0u)
    return SALTS_EINVAL;
  base_ptr = mem_buffer_const_data(buffer);
  used = mem_buffer_used(buffer);
  if (base_ptr == NULL || used == 0u) return SALTS_EINVAL;
  base = (uintptr_t)base_ptr;
  start = (uintptr_t)data;
  if (start < base) return SALTS_EINVAL;
  delta = start - base;
  if (delta > (uintptr_t)SIZE_MAX) return SALTS_EINVAL;
  offset = (size_t)delta;
  if (offset >= used || size > used - offset) return SALTS_EINVAL;
  if (size > impl->max_send_bytes) return SALTS_EMSGSIZE;
  return SALTS_OK;
}
```

This helper retains nothing.

- [ ] **Step 5: Generalize internal client send input**

Extend `cnet_client_send_input` with:

```c
mem_buffer_t *retained_buffer;
const void *retained_data;
```

In `cnet_client_send_admit()` use:

```c
if (input->retained_buffer != NULL)
  status = cnet_shards_send_retained(&impl->shards, internal,
                                     input->retained_buffer,
                                     input->retained_data,
                                     input->size);
```

Do not alter existing connection state/write-pending/TLS-upgrade/close admission ordering.

- [ ] **Step 6: Route whole-buffer send through the same retained-view helper**

`cnet_send_buffer()` becomes:

```c
int cnet_send_buffer(cnet_client *client, cnet_connection connection,
                     mem_buffer_t *buffer) {
  cnet_client_impl *impl = cnet_client_get(client);
  const void *data;
  size_t size;
  int status;

  if (impl == NULL || buffer == NULL) return SALTS_EINVAL;
  data = mem_buffer_const_data(buffer);
  size = mem_buffer_used(buffer);
  status = cnet_client_validate_retained_view(impl, buffer, data, size);
  if (status != SALTS_OK) return status;
  return cnet_client_send_admit(
      impl, connection,
      &(cnet_client_send_input){.retained_buffer = buffer,
                                .retained_data = data,
                                .size = size});
}
```

- [ ] **Step 7: Implement `cnet_send_slice()` with frozen error ordering**

```c
int cnet_send_slice(cnet_client *client, cnet_connection connection,
                    const mem_slice_t *slice) {
  cnet_client_impl *impl = cnet_client_get(client);
  int status;

  if (impl == NULL || slice == NULL) return SALTS_EINVAL;
  status = cnet_client_validate_retained_view(impl, slice->buffer,
                                              slice->data, slice->length);
  if (status != SALTS_OK) return status;
  return cnet_client_send_admit(
      impl, connection,
      &(cnet_client_send_input){.retained_buffer = slice->buffer,
                                .retained_data = slice->data,
                                .size = slice->length});
}
```

Validation therefore resolves `SALTS_EINVAL` / `SALTS_EMSGSIZE` before stale/busy/queue/shutdown admission results.

- [ ] **Step 8: Replace the internal whole-buffer shard helper with one retained-view helper**

Declare in `cnet_shards.h`:

```c
int cnet_shards_send_retained(cnet_shards *shards,
                              cnet_shard_connection connection,
                              mem_buffer_t *buffer,
                              const void *data,
                              size_t size);
```

Implement in `cnet_shards.c`:

```c
int cnet_shards_send_retained(cnet_shards *shards,
                              cnet_shard_connection connection,
                              mem_buffer_t *buffer,
                              const void *data,
                              size_t size) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  const cnet_command command = {.kind = CNET_COMMAND_SEND,
                                .connection = connection.session,
                                .size = size,
                                .retained_buffer = buffer,
                                .retained_data = data};
  if (impl == NULL || buffer == NULL || data == NULL || size == 0u)
    return SALTS_EINVAL;
  if (size > impl->max_command_payload_bytes) return SALTS_EMSGSIZE;
  return cnet_shards_publish(impl, connection, &command);
}
```

Delete the old internal `cnet_shards_send_buffer()` declaration and callers so there is only one retained ownership path.

- [ ] **Step 9: Add defensive validation/error-order tests**

Before requiring a live connection, manually construct slices and assert:

```text
NULL slice                                  -> SALTS_EINVAL
slice.buffer == NULL                        -> SALTS_EINVAL
slice.data == NULL                          -> SALTS_EINVAL
slice.length == 0                           -> SALTS_EINVAL
slice.data address below backing start      -> SALTS_EINVAL
slice.data address == backing start + used  -> SALTS_EINVAL
slice.length > used - offset                -> SALTS_EINVAL
canonical slice length > max_send_bytes     -> SALTS_EMSGSIZE
```

For each rejection, snapshot `mem_buffer_ref_count()` before/after and require no change.

- [ ] **Step 10: Run GREEN and commit**

```bash
cmake --build --preset linux-release-user --target \
  cnet_command_test cnet_command_profile_test cnet_send_buffer_api_test cnet_header_cpp_test
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_command(_profile)?_test|cnet_send_buffer_api_test|cnet_header_cpp_test)$' \
  --output-on-failure
git add cnet/include/cnet/cnet.h cnet/src/cnet_client.c \
  cnet/src/cnet_shards.h cnet/src/cnet_shards.c \
  cnet/tests/cnet_send_buffer_api_test.c cnet/tests/cnet_header_cpp_test.cpp
git commit -m "feat(cnet): admit retained zero-copy slices"
```

---

### Task 3: Prove exact subrange bytes and terminal ownership

**Files:**
- Modify: `cnet/tests/cnet_send_buffer_api_test.c`
- Modify: `cnet/tests/cnet_owner_test.c`
- Modify: `cnet/tests/cnet_tls_test.c`

**Interfaces:**
- Consumes: Task 2 API, #286 command release authority, and #286 `cnet_owner_test_set_send_chunk_bytes()`.
- Produces: deterministic normal/partial/failure/TLS ownership evidence for retained slices.

- [ ] **Step 1: Add one successful external middle-slice public test**

Allocate 80 distinct bytes, wrap them externally, and create a 64-byte slice at offset 8. Copy the expected 64 bytes into a local test array before any release callback can free the external allocation.

Require this refcount sequence:

```text
wrapped buffer                         1
mem_slice(buffer, 8, 64)              2
successful cnet_send_slice            3
caller mem_slice_release              2
caller mem_buffer_release             1  (CNet only)
logical terminal                      0  (free callback exactly once)
```

The peer must receive exactly the copied expected 64-byte middle range and one `on_send` callback must report size 64.

- [ ] **Step 2: Add first-byte and final-byte range tests**

With a live connection, send canonical slices:

```text
offset 0, length 1
offset used - 1, length 1
```

For each, verify exactly one byte on the peer and one send callback of size 1.

- [ ] **Step 3: Add rejection ownership regressions**

For valid canonical slices, verify these existing connection/admission states preserve #286 errors and do not retain:

```text
command queue full -> SALTS_ENOBUFS
another write live -> SALTS_EBUSY
stale connection   -> SALTS_ENOENT
client stopped     -> SALTS_ESHUTDOWN
```

The backing refcount before and after every rejected call must be identical.

- [ ] **Step 4: Extend the #286 deterministic partial-send owner test**

Create backing bytes:

```text
8 guard bytes | 4 payload bytes | 8 guard bytes
```

Publish:

```c
const void *payload = mem_buffer_const_data(buffer) + 8u;
cnet_command command = {.kind = CNET_COMMAND_SEND,
                        .connection = session,
                        .size = 4u,
                        .retained_buffer = buffer,
                        .retained_data = payload};
```

Set:

```c
check_equal(cnet_owner_test_set_send_chunk_bytes(&owner, 1u), SALTS_OK);
```

Require four one-byte NativeIO submissions but one logical send event, exact peer bytes, zero guard-byte leakage, and one retained backing reference until the logical terminal.

- [ ] **Step 5: Re-run #286 timeout/close ownership logic using an interior retained view**

Use a backing buffer whose `used` size exceeds the command view size. Point `.retained_data` inside it, trigger the existing deterministic timeout/close path, and require the external free callback remains zero until authoritative terminal cleanup and becomes exactly one afterward.

- [ ] **Step 6: Extend TLS retained-send coverage**

Send an interior retained view through the existing TLS retained path. Verify the application peer receives only the slice bytes and command profiling still reports zero plaintext command-stage payload copies. Do not assert ciphertext or kernel zero-copy.

- [ ] **Step 7: Run GREEN and commit**

```bash
cmake --build --preset linux-release-user --target \
  cnet_send_buffer_api_test cnet_owner_test cnet_owner_profile_test cnet_tls_test
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_send_buffer_api_test|cnet_owner(_profile)?_test|cnet_tls_test)$' \
  --timeout 60 --output-on-failure
git add cnet/tests/cnet_send_buffer_api_test.c \
  cnet/tests/cnet_owner_test.c cnet/tests/cnet_tls_test.c
git commit -m "test(cnet): prove retained slice terminal ownership"
```

---

### Task 4: Measure retained slice overhead against whole-buffer retained sends

**Files:**
- Modify: `cnet/benchmarks/cnet_io_benchmark.c`

**Interfaces:**
- Consumes: #286 retained-buffer benchmark path plus existing paired median/MAD and same-run A/A machinery.
- Produces: retained whole-buffer vs retained-slice rows for identical payload bytes.

- [ ] **Step 1: Add a retained send-mode enum**

```c
typedef enum io_bench_cnet_retained_mode {
  IO_BENCH_CNET_RETAINED_BUFFER = 1,
  IO_BENCH_CNET_RETAINED_SLICE
} io_bench_cnet_retained_mode;
```

The slice fixture allocates 16 guard bytes before and after the measured payload and creates:

```c
mem_slice_t slice = mem_slice(buffer, 16u, payload_size);
```

Create/release the slice outside the timed admission interval. The measured admission call is only `cnet_send_slice()`.

- [ ] **Step 2: Add benchmark correctness assertions**

Before accepting each measured sample, require echoed bytes equal the exact interior slice bytes. The guard bytes are never part of expected output.

Profile passes require:

```text
retained buffer payload_copy_calls == 0
retained slice  payload_copy_calls == 0
```

Keep the existing copied CNet row unchanged.

- [ ] **Step 3: Add paired retained-buffer vs retained-slice summaries**

For TCP 1/4/8/16/32/64 KiB, report:

```text
retained buffer admission ns median/MAD
retained slice admission ns median/MAD
slice vs buffer p50 paired median/MAD
slice vs buffer p95 paired median/MAD
slice vs buffer rate paired median/MAD
```

Use the existing repeat order and `cnet_benchmark_summarize_paired_delta()`; do not add another statistics implementation.

- [ ] **Step 4: Add exact evidence wording**

The report must include:

```text
Retained-slice zero-copy is established by pointer identity and zero command payload-copy counters.
Timing compares retained-view overhead only; it does not claim TLS ciphertext or kernel/network-stack zero-copy.
```

- [ ] **Step 5: Run benchmark correctness locally**

```bash
cmake --build --preset linux-release-user --target cnet_io_benchmark
CNET_IO_BENCHMARK_BACKEND=epoll build/linux-gcc-release/bin/cnet_io_benchmark \
  > /tmp/cnet-slice-benchmark.md
```

Expected: exit zero, slice rows exist for every payload size, guard-byte checks pass, and retained-slice copy counters are zero.

- [ ] **Step 6: Commit**

```bash
git add cnet/benchmarks/cnet_io_benchmark.c
git commit -m "bench(cnet): measure retained slice sends"
```

---

### Task 5: Close documentation and full Linux regressions

**Files:**
- Modify: `cnet/README.md`

**Interfaces:** No new runtime interface beyond `cnet_send_slice()`.

- [ ] **Step 1: Document the ownership layers**

Add a retained-send section with these exact distinctions:

```text
cnet_send / cnet_sendv
  copied admission; caller bytes reusable immediately

cnet_send_buffer
  retains the whole mem_buffer_t used range

cnet_send_slice
  borrows the mem_slice_t object during admission, retains its backing mem_buffer_t,
  snapshots slice data/length, and sends only that retained subrange

vstr
  non-owning borrowed view only; not an asynchronous CNet ownership token
```

State that retained sends do not consume copied `command_buffer_bytes` and TLS still performs normal encryption/ciphertext buffering.

- [ ] **Step 2: Audit forbidden surface**

```bash
git grep -n 'cnet_send_vstr' -- cnet || true
git grep -n -E 'retained_slice_queue|slice_payload_pool|CNET_COMMAND_PAYLOAD_RETAINED_SLICE' -- cnet || true
```

Expected: no matches.

- [ ] **Step 3: Run the full Linux CNet regression set**

```bash
cmake --build --preset linux-release-user --target \
  cnet_websocket_parser_test cnet_websocket_test cnet_session_test cnet_command_test \
  cnet_command_profile_test cnet_uri_test cnet_transport_test cnet_owner_test \
  cnet_owner_profile_test cnet_event_test cnet_resolver_test cnet_shards_test \
  cnet_dispatcher_test cnet_api_test cnet_api_profile_test cnet_send_buffer_api_test \
  cnet_header_cpp_test cnet_tls_test cnet_datagram_test cnet_kcp_test \
  cnet_secure_kcp_test cnet_packet_endpoint_test cnet_stop_contract_test \
  cnet_vsock_integration_test cnet_benchmark_stats_test cnet_io_benchmark_config_test
ctest --test-dir build/linux-gcc-release \
  -R '^cnet_.*_test$' --timeout 60 --output-on-failure
```

Expected: all applicable tests pass; VSOCK keeps its existing unsupported-host skip behavior.

- [ ] **Step 4: Commit**

```bash
git add cnet/README.md
git commit -m "docs(cnet): describe retained slice sends"
```

---

### Task 6: Exact-head cross-platform verification and final evidence

**Files:** Evidence-only task. Any source failure returns to the task that owns that behavior.

**Interfaces:** Produces #292 merge-readiness evidence; performs no runtime modification.

- [ ] **Step 1: Audit the final implementation boundary against the frozen dependency base**

```bash
git diff --stat refs/cnet-slice/base...HEAD
git diff --name-only refs/cnet-slice/base...HEAD
git grep -n 'cnet_send_slice' -- cnet/include/cnet/cnet.h cnet/src/cnet_client.c
git grep -n 'CNET_COMMAND_PAYLOAD_RETAINED_SLICE' -- cnet || true
git grep -n 'cnet_send_vstr' -- cnet || true
```

Expected: one public slice API, reuse of `CNET_COMMAND_PAYLOAD_RETAINED_BUFFER`, no slice-specific payload kind, and no `cnet_send_vstr()`.

- [ ] **Step 2: Verify deterministic ownership/zero-copy tests on the exact head**

```bash
cmake --build --preset linux-release-user --target \
  cnet_command_test cnet_command_profile_test cnet_send_buffer_api_test \
  cnet_owner_test cnet_owner_profile_test cnet_tls_test cnet_header_cpp_test \
  cnet_io_benchmark
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_command(_profile)?_test|cnet_send_buffer_api_test|cnet_owner(_profile)?_test|cnet_tls_test|cnet_header_cpp_test)$' \
  --timeout 60 --output-on-failure
```

Expected: range, pointer identity, refcount, copy-counter, partial-send, timeout/close, TLS, and header tests all pass.

- [ ] **Step 3: Require one exact-head release matrix**

Record the same implementation SHA for:

```text
Windows IOCP
Linux epoll
Linux io_uring
macOS kqueue
```

All four jobs must pass build, NativeIO/CNet contracts, benchmark, and artifact upload.

- [ ] **Step 4: Inspect Windows IOCP performance evidence**

Record:

```text
head SHA
workflow run ID
four job IDs
four artifact IDs and SHA-256 digests
64 KiB retained-buffer admission median/MAD
64 KiB retained-slice admission median/MAD
64 KiB slice-vs-buffer p50/p95/rate paired median/MAD
same-run A/A noise envelope
```

The performance gate is:

```text
correctness green
zero-copy deterministic proof green
slice admission remains payload-size-independent within measurement noise
no material aggregate p95/rate regression versus retained-buffer send
```

Do not require slice sends to outperform whole-buffer retained sends.

- [ ] **Step 5: Record final evidence on #292 and stop before merge**

The #292 comment must include exact head/run/jobs/artifacts, deterministic zero-copy proof, ownership results, performance interpretation, and these two explicit statements:

```text
#293 remains an independent mem_slice() helper-hardening issue.
No cnet_send_vstr() or borrowed-lifetime asynchronous API was introduced.
```

Do not merge automatically.
