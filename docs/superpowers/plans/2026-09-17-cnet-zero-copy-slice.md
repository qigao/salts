# CNet Retained Slice Zero-Copy Send Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `cnet_send_slice()` so callers can asynchronously send one canonical `mem_slice_t` subrange without copying payload bytes into CNet command storage, while CNet retains exactly one backing `mem_buffer_t` reference until the logical send terminal.

**Architecture:** Reuse #286's retained-buffer ownership path rather than adding a new payload kind. Extend the retained command descriptor from “whole buffer” to “retained owner + explicit view pointer + size”; whole-buffer `cnet_send_buffer()` becomes the zero-offset special case. `cnet_send_slice()` validates the public `mem_slice_t` defensively, snapshots its range, then enters the same client/shard/command/owner/TLS path as `cnet_send_buffer()`.

**Tech Stack:** C11, `mem_buffer_t`/`mem_slice_t`, CNet command/shard/client/owner/TLS layers, NativeIO, TinyTest, CMake presets, GitHub Actions release matrix.

**Spec:** `docs/superpowers/specs/2026-09-17-cnet-zero-copy-slice-design.md`

## Global Constraints

- Execution is blocked until #286 Z1 retained-buffer ownership is validated on an exact head and the implementation base contains `cnet_send_buffer()` plus the retained command payload mode.
- `cnet_send()`, `cnet_sendv()`, and `cnet_send_buffer()` public semantics remain unchanged.
- Add exactly one new public surface: `int cnet_send_slice(cnet_client *, cnet_connection, const mem_slice_t *)`.
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

### Task 0: Verify the #286 dependency before creating an implementation branch

**Files:** Evidence-only task. Do not modify source.

**Interfaces:**
- Consumes: #286 retained-buffer implementation.
- Produces: a recorded base SHA that is safe for #292 implementation.

- [ ] **Step 1: Verify the public retained-buffer API exists on the intended base**

Run:

```bash
git grep -n 'int cnet_send_buffer' -- cnet/include/cnet/cnet.h cnet/src/cnet_client.c
git grep -n 'CNET_COMMAND_PAYLOAD_RETAINED_BUFFER' -- cnet/src/cnet_command.h cnet/src/cnet_command.c
git grep -n 'cnet_owner_test_set_send_chunk_bytes' -- cnet/src/cnet_owner.h cnet/src/cnet_owner.c
```

Expected: all three concepts exist. If any is absent, stop: #286 has not reached the dependency state required by this plan.

- [ ] **Step 2: Verify #286 ownership/zero-copy contracts are green on that exact SHA**

Run the targets introduced/extended by #286:

```bash
cmake --preset linux-release-user -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON
cmake --build --preset linux-release-user --target \
  cnet_command_test cnet_command_profile_test cnet_send_buffer_api_test \
  cnet_owner_test cnet_owner_profile_test cnet_tls_test cnet_header_cpp_test
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_command(_profile)?_test|cnet_send_buffer_api_test|cnet_owner(_profile)?_test|cnet_tls_test|cnet_header_cpp_test)$' \
  --timeout 60 --output-on-failure
```

Expected: all applicable tests pass and retained-buffer profile coverage proves `payload_copy_calls == 0`.

- [ ] **Step 3: Record the implementation base**

Capture:

```bash
git rev-parse HEAD
```

Create the #292 implementation branch from exactly that SHA, not from the documentation branch if the documentation branch predates #286.

---

### Task 1: Extend the retained command representation from whole-buffer to retained-view

**Files:**
- Modify: `cnet/src/cnet_command.h`
- Modify: `cnet/src/cnet_command.c`
- Modify: `cnet/tests/cnet_command_test.c`
- Modify: `cnet/tests/cnet_command_profile_test.c`

**Interfaces:**
- Consumes: #286 `cnet_command.retained_buffer`, `CNET_COMMAND_PAYLOAD_RETAINED_BUFFER`, command-owned buffer retain/release, `cnet_command_view {data,size,_sequence}`.
- Produces: retained command publication with explicit `retained_data` subrange pointer while preserving one backing-buffer reference and zero command payload copies.

- [ ] **Step 1: Write the retained-subrange command RED**

Extend the retained command helper in `cnet_command_test.c` to construct an explicit view:

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

Add a middle-range test:

```c
it("retains one backing buffer while exposing an interior retained view") {
  mem_pool_t pool;
  mem_buffer_t *buffer;
  cnet_command_view view = {0};
  const cnet_command_queue_config config = {.capacity = 2u, .max_payload_bytes = 64u};

  check_equal(mem_init(&pool, 0u), 0);
  buffer = mem_get_buffer(&pool, 32u);
  check_not_null(buffer);
  for (size_t i = 0u; i < 32u; ++i) mem_buffer_data(buffer)[i] = (char)i;
  mem_set_used(buffer, 32u);

  check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
  cnet_command command =
      make_retained_view_send(1u, buffer, mem_buffer_const_data(buffer) + 8u, 12u);
  check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
  check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));

  check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
  check_true(view.data == mem_buffer_const_data(buffer) + 8u);
  check_equal(view.size, 12u);
  check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
  check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));

  mem_buffer_release(buffer);
  check_equal(cnet_command_queue_close(&queue), SALTS_OK);
  check_equal(cnet_command_queue_destroy(&queue), SALTS_OK);
  mem_destroy(&pool);
}
```

Add fail-closed internal validation cases using manually forged retained descriptors:

```text
retained_data == NULL
retained_data before backing start
retained_data == backing start + used
size > used - offset
```

Each must return `SALTS_EINVAL` without changing the buffer refcount.

- [ ] **Step 2: Extend the retained profile RED**

In `cnet_command_profile_test.c`, publish an interior retained range and assert:

```c
check_equal(profile.publish_calls, UINT64_C(1));
check_equal(profile.payload_publish_calls, UINT64_C(1));
check_equal(profile.payload_copy_calls, UINT64_C(0));
check_equal(profile.payload_copy_ns, UINT64_C(0));
```

The taken command view must preserve exact pointer identity with the interior input pointer.

- [ ] **Step 3: Run RED**

```bash
cmake --build --preset linux-release-user --target cnet_command_test cnet_command_profile_test
```

Expected RED: `cnet_command.retained_data` does not exist. Do not accept an unrelated configure/link failure as RED evidence.

- [ ] **Step 4: Add explicit retained-view metadata**

In `cnet_command.h`, append to `cnet_command`:

```c
const void *retained_data;
```

Keep the payload kind unchanged; a slice is still `CNET_COMMAND_PAYLOAD_RETAINED_BUFFER`.

In `cnet_command.c`, extend `cnet_command_entry` with:

```c
const void *payload_data;
```

Add a fail-closed helper:

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

For retained SEND validation require:

```c
command->kind == CNET_COMMAND_SEND
command->data == NULL
command->segments == NULL
command->segment_count == 0u
cnet_command_retained_range_valid(command->retained_buffer,
                                  command->retained_data,
                                  command->size)
```

- [ ] **Step 5: Snapshot the view pointer into the command entry**

After all admission checks and after retaining the owner:

```c
entry->payload = payload;
entry->payload_data = retained ? command->retained_data
                               : (payload != NULL ? mem_buffer_const_data(payload) : NULL);
```

For copied commands, assign `payload_data` only after the copy has been created. For retained commands, never call `mem_set_used()` and never copy payload bytes.

Change `cnet_command_queue_take()` to:

```c
out_view->data = entry->size != 0u ? entry->payload_data : NULL;
```

On release, clear both:

```c
entry->payload = NULL;
entry->payload_data = NULL;
```

Continue releasing exactly one `entry->payload` reference and continue charging only copied payload bytes to `queued_bytes`.

- [ ] **Step 6: Preserve whole-buffer #286 behavior**

Change the #286 internal whole-buffer command constructor to set:

```c
.retained_buffer = buffer,
.retained_data = mem_buffer_const_data(buffer),
.size = mem_buffer_used(buffer)
```

No public `cnet_send_buffer()` semantics change.

- [ ] **Step 7: Run GREEN and commit**

```bash
cmake --build --preset linux-release-user --target cnet_command_test cnet_command_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_command(_profile)?_test$' --output-on-failure
git add cnet/src/cnet_command.h cnet/src/cnet_command.c \
        cnet/tests/cnet_command_test.c cnet/tests/cnet_command_profile_test.c
git commit -m "feat(cnet): retain explicit zero-copy payload views"
```

Expected: copied, whole-buffer retained, and interior retained-view tests all pass; retained profile copy counters remain zero.

---

### Task 2: Add canonical `cnet_send_slice()` validation and client/shard admission

**Files:**
- Modify: `cnet/include/cnet/cnet.h`
- Modify: `cnet/src/cnet_client.c`
- Modify: `cnet/src/cnet_shards.h`
- Modify: `cnet/src/cnet_shards.c`
- Modify: `cnet/tests/cnet_send_buffer_api_test.c`
- Modify: `cnet/tests/cnet_header_cpp_test.cpp`

**Interfaces:**
- Consumes: Task 1 retained view command representation.
- Produces: public `cnet_send_slice()` and one internal retained-view shard helper used by both whole-buffer and slice sends.

- [ ] **Step 1: Write the public API compile RED**

In `cnet_send_buffer_api_test.c`, create one pool/external buffer and canonical slice, then call:

```c
mem_slice_t slice = mem_slice(buffer, 8u, 16u);
check_equal(cnet_send_slice(&client, connection, &slice), SALTS_OK);
```

In `cnet_header_cpp_test.cpp`, add only a signature check:

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

- [ ] **Step 3: Add the public declaration with exact ownership semantics**

Beside `cnet_send_buffer()` in `cnet/include/cnet/cnet.h`, add:

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

Do not add `vstr` to the CNet public header.

- [ ] **Step 4: Add one canonical retained-range validator in the client layer**

In `cnet_client.c`, add:

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

This helper performs no retain and no queue admission.

- [ ] **Step 5: Generalize the client send input to carry a retained view**

Extend `cnet_client_send_input` with:

```c
mem_buffer_t *retained_buffer;
const void *retained_data;
```

Replace the internal whole-buffer-only shard call with a retained-view call:

```c
if (input->retained_buffer != NULL)
  status = cnet_shards_send_retained(&impl->shards, internal,
                                     input->retained_buffer,
                                     input->retained_data,
                                     input->size);
```

All connection state, write-pending, TLS-upgrade, close, stale-handle, queue-full, and shutdown checks remain in their existing order after payload validation.

- [ ] **Step 6: Make whole-buffer send use the same retained-view path**

Implement `cnet_send_buffer()` as:

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

This preserves #286 semantics while sharing validation/plumbing.

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

Because validation occurs before `cnet_client_send_admit()`, invalid slice errors precede stale/busy/queue/shutdown errors exactly as specified.

- [ ] **Step 8: Replace the internal whole-buffer shard helper with one retained-view helper**

In `cnet_shards.h/.c`, define:

```c
int cnet_shards_send_retained(cnet_shards *shards,
                              cnet_shard_connection connection,
                              mem_buffer_t *buffer,
                              const void *data,
                              size_t size);
```

Implementation:

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

Remove the old internal `cnet_shards_send_buffer()` declaration/callers rather than keeping two internal ownership paths.

- [ ] **Step 9: Add validation/error-order tests before network settlement**

In `cnet_send_buffer_api_test.c`, after client initialization and before requiring a live connection, construct manual slices and assert:

```text
NULL slice                                  -> SALTS_EINVAL
slice.buffer == NULL                        -> SALTS_EINVAL
slice.data == NULL                          -> SALTS_EINVAL
slice.length == 0                           -> SALTS_EINVAL
slice.data before backing start             -> SALTS_EINVAL
slice.data == backing start + used          -> SALTS_EINVAL
slice.length > used - offset                -> SALTS_EINVAL
canonical slice length > max_send_bytes     -> SALTS_EMSGSIZE
```

For every rejection record `mem_buffer_ref_count()` before/after and require no change.

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

### Task 3: Prove subrange bytes and exact ownership through normal, partial, failure, and TLS terminals

**Files:**
- Modify: `cnet/tests/cnet_send_buffer_api_test.c`
- Modify: `cnet/tests/cnet_owner_test.c`
- Modify: `cnet/tests/cnet_tls_test.c`

**Interfaces:**
- Consumes: Task 2 public API and #286 retained command release authority plus `cnet_owner_test_set_send_chunk_bytes()`.
- Produces: deterministic evidence that slice sends keep one backing owner through every logical terminal and never leak adjacent bytes.

- [ ] **Step 1: Add the successful middle-subrange public ownership test**

Reuse the existing external-buffer free probe in `cnet_send_buffer_api_test.c`. Allocate 80 bytes with distinct guard bytes and a 64-byte payload:

```c
unsigned char *storage = (unsigned char *)malloc(80u);
for (size_t i = 0u; i < 80u; ++i) storage[i] = (unsigned char)i;
mem_buffer_t *buffer = mem_wrap_external(storage, 80u,
                                         cnet_send_buffer_test_free, &slice_free);
check_not_null(buffer);
mem_slice_t slice = mem_slice(buffer, 8u, 64u);
check_equal(slice.length, 64u);
check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));

check_equal(cnet_send_slice(&client, connection, &slice), SALTS_OK);
check_equal(mem_buffer_ref_count(buffer), UINT32_C(3));
mem_slice_release(&slice);
check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
mem_buffer_release(buffer);
check_equal(atomic_load_explicit(&slice_free.freed, memory_order_acquire), 0);
```

Drive to one `on_send` terminal, then require the free callback fired exactly once and the peer received bytes `storage[8..71]` only. The expected peer buffer must be copied before the external free callback can release `storage`.

- [ ] **Step 2: Add first-byte and final-byte range tests**

Using the same connected fixture, admit one-byte canonical slices at:

```text
offset 0, length 1
offset used - 1, length 1
```

For each, verify the peer receives exactly that byte and exactly one send callback reports size 1.

- [ ] **Step 3: Add rejection ownership regressions**

For valid canonical slices, verify the existing states return their original #286 errors without retaining:

```text
command queue full   -> SALTS_ENOBUFS
another write live   -> SALTS_EBUSY
stale connection     -> SALTS_ENOENT
client stopped       -> SALTS_ESHUTDOWN
```

Snapshot the backing refcount around every call and require equality on rejection.

- [ ] **Step 4: Extend the deterministic partial-send owner test with an interior retained view**

In the #286 retained owner test, create one backing buffer whose used bytes are eight guard bytes + four payload bytes + eight guard bytes. Publish:

```c
const void *payload = mem_buffer_const_data(buffer) + 8u;
cnet_command command = {.kind = CNET_COMMAND_SEND,
                        .connection = session,
                        .size = 4u,
                        .retained_buffer = buffer,
                        .retained_data = payload};
```

Before driving, call:

```c
check_equal(cnet_owner_test_set_send_chunk_bytes(&owner, 1u), SALTS_OK);
```

Require:

```text
peer receives exactly the 4 payload bytes, never guard bytes
one logical send event of size 4
backing ref remains held through the first three intermediate completions
backing ref releases exactly once after the logical terminal
```

Do not add a second chunk seam.

- [ ] **Step 5: Reuse #286 timeout/close ownership tests with a retained subrange**

Run the same forced timeout/close path once with `.retained_data` pointing inside the buffer and `.size` smaller than `mem_buffer_used(buffer)`. Require the external free callback remains zero until the authoritative terminal and becomes exactly one after cleanup.

- [ ] **Step 6: Extend TLS retained-send coverage**

In `cnet_tls_test.c`, send an interior retained view through the same TLS command path and assert the application peer receives only the slice bytes. CNet command profiling must still report zero plaintext command-stage copies for that send. Do not assert ciphertext zero-copy.

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

### Task 4: Add retained-slice benchmark/proof rows without changing the zero-copy claim

**Files:**
- Modify: `cnet/benchmarks/cnet_io_benchmark.c`
- Modify: `cnet/tests/cnet_benchmark_stats_test.c` only if a new paired-summary helper is required; otherwise do not touch it.

**Interfaces:**
- Consumes: #286 retained-buffer benchmark driver and existing paired median/MAD/A-A machinery.
- Produces: retained whole-buffer vs retained slice comparison for identical payload bytes, with independent admission metrics.

- [ ] **Step 1: Add a slice send mode to the existing retained CNet benchmark fixture**

Do not add another CNet implementation. Extend the retained benchmark fixture with:

```c
typedef enum io_bench_cnet_retained_mode {
  IO_BENCH_CNET_RETAINED_BUFFER = 1,
  IO_BENCH_CNET_RETAINED_SLICE
} io_bench_cnet_retained_mode;
```

For the slice mode allocate one backing buffer with 16 guard bytes before and after the measured payload. Create:

```c
mem_slice_t slice = mem_slice(buffer, 16u, payload_size);
```

The measured admission call is only:

```c
cnet_send_slice(&fixture->client, fixture->connection, &slice)
```

Create/release the slice outside the timed admission interval. Keep the backing buffer and immutable bytes alive for the sample exactly as the retained-buffer driver already does.

- [ ] **Step 2: Add deterministic benchmark correctness checks**

Before recording a sample, require the echoed payload equals the exact interior slice bytes. Guard bytes are not included in the expected payload.

Profile passes must require:

```text
retained buffer payload_copy_calls == 0
retained slice  payload_copy_calls == 0
```

Legacy copied CNet remains nonzero where the #286 benchmark already proves that behavior.

- [ ] **Step 3: Add paired retained-buffer vs retained-slice reporting**

For 1/4/8/16/32/64 KiB TCP rows, report:

```text
retained buffer admission ns median/MAD
retained slice admission ns median/MAD
slice vs buffer p50 paired median/MAD
slice vs buffer p95 paired median/MAD
slice vs buffer rate paired median/MAD
```

Reuse the existing same-run repeat ordering and `cnet_benchmark_summarize_paired_delta()` helper. Do not hard-code a required speedup: the expected result is comparable retained-path behavior, not “slice must beat buffer.”

- [ ] **Step 4: Define the evidence interpretation in the report text**

The benchmark output must say:

```text
Retained-slice zero-copy is established by pointer identity and zero command payload-copy counters.
Timing compares retained-view overhead only; it does not claim TLS ciphertext or kernel/network-stack zero-copy.
```

- [ ] **Step 5: Run the benchmark locally for correctness**

```bash
cmake --build --preset linux-release-user --target cnet_io_benchmark cnet_benchmark_stats_test
ctest --test-dir build/linux-gcc-release -R '^cnet_benchmark_stats_test$' --output-on-failure
CNET_IO_BENCHMARK_BACKEND=epoll build/linux-gcc-release/bin/cnet_io_benchmark > /tmp/cnet-slice-benchmark.md
```

Expected: benchmark exits zero, slice rows exist, zero-copy counters are zero, and no guard-byte mismatch occurs.

- [ ] **Step 6: Commit**

```bash
git add cnet/benchmarks/cnet_io_benchmark.c cnet/tests/cnet_benchmark_stats_test.c
git commit -m "bench(cnet): measure retained slice sends"
```

If `cnet_benchmark_stats_test.c` was not changed, omit it from `git add`.

---

### Task 5: Close public documentation and regression coverage

**Files:**
- Modify: `cnet/README.md`
- Test: existing CNet contract suite.

**Interfaces:** No new runtime interface beyond `cnet_send_slice()`.

- [ ] **Step 1: Document the ownership layers**

Add a concise retained-send section to `cnet/README.md` stating:

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

Also state that retained sends do not consume copied `command_buffer_bytes` and that TLS still performs normal encryption/ciphertext buffering.

- [ ] **Step 2: Audit forbidden surface**

Run:

```bash
git grep -n 'cnet_send_vstr' -- cnet || true
git grep -n -E 'retained_slice_queue|slice_payload_pool|CNET_COMMAND_PAYLOAD_RETAINED_SLICE' -- cnet || true
```

Expected: no matches. There must be no second slice payload kind, queue, or pool.

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

Expected: all applicable tests pass; VSOCK retains its existing unsupported-host skip contract.

- [ ] **Step 4: Commit**

```bash
git add cnet/README.md
git commit -m "docs(cnet): describe retained slice sends"
```

---

### Task 6: Exact-head cross-platform verification and final #292 evidence

**Files:** Evidence-only task. Any source failure returns to the task that owns that behavior.

**Interfaces:** Produces merge-readiness evidence; does not modify runtime code.

- [ ] **Step 1: Audit the final implementation boundary**

Run:

```bash
git diff --stat <dependency-base-sha>...HEAD
git diff --name-only <dependency-base-sha>...HEAD
git grep -n 'cnet_send_slice' -- cnet/include/cnet/cnet.h cnet/src/cnet_client.c
git grep -n 'CNET_COMMAND_PAYLOAD_RETAINED_SLICE' -- cnet || true
git grep -n 'cnet_send_vstr' -- cnet || true
```

Expected: one public slice API, reuse of `CNET_COMMAND_PAYLOAD_RETAINED_BUFFER`, and no `cnet_send_vstr()`.

- [ ] **Step 2: Verify deterministic ownership/proof tests on the exact head**

Run:

```bash
cmake --build --preset linux-release-user --target \
  cnet_command_test cnet_command_profile_test cnet_send_buffer_api_test \
  cnet_owner_test cnet_owner_profile_test cnet_tls_test cnet_header_cpp_test \
  cnet_io_benchmark
ctest --test-dir build/linux-gcc-release \
  -R '^(cnet_command(_profile)?_test|cnet_send_buffer_api_test|cnet_owner(_profile)?_test|cnet_tls_test|cnet_header_cpp_test)$' \
  --timeout 60 --output-on-failure
```

Expected: pointer/range/refcount/copy-counter/partial/TLS tests all pass.

- [ ] **Step 3: Require one exact-head release matrix**

Record the same implementation head for:

```text
Windows IOCP
Linux epoll
Linux io_uring
macOS kqueue
```

All four jobs must pass build, NativeIO/CNet contracts, benchmark, and artifact upload before #292 is considered complete.

- [ ] **Step 4: Inspect the Windows IOCP artifact**

Confirm the artifact contains the existing #286 copied/retained evidence plus retained-slice rows. Record:

```text
head SHA
workflow run ID
four job IDs
four artifact IDs/digests
64 KiB retained-buffer admission median/MAD
64 KiB retained-slice admission median/MAD
64 KiB slice-vs-buffer p50/p95/rate paired median/MAD
same-run A/A noise envelope
```

The merge gate is not “slice must be faster.” It is:

```text
correctness green
zero-copy deterministic proof green
slice admission remains payload-size-independent within measurement noise
no material aggregate p95/rate regression versus retained-buffer send
```

- [ ] **Step 5: Record final evidence on #292 and stop before merge**

Comment on #292 with the exact head/run/jobs/artifacts, deterministic zero-copy proof, ownership results, and performance interpretation. Explicitly state that #293 remains an independent helper-hardening issue and that `cnet_send_vstr()` was not introduced.

Do not merge automatically.
