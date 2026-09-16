# CNet Retained-Buffer Zero-Copy Send Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `cnet_send_buffer()` so CNet can retain one caller-provided `mem_buffer_t`, submit the same payload bytes through the existing CNet/NativeIO path without command-staging memcpy, and release that retained reference exactly once on every terminal path.

**Architecture:** Keep the existing client -> shard -> bounded command queue -> owner -> NativeIO topology. The command entry is the sole CNet owner of the retained buffer; owner request state and TLS state borrow the command view only. Legacy `cnet_send()`/`cnet_sendv()` continue to allocate/copy queue-owned payloads. Zero-copy correctness is proven by pointer identity plus the existing payload-copy profiling counter, then measured separately in `cnet_io_benchmark`.

**Tech Stack:** C11, `mem_buffer_t`/`mem_pool_t`, CNet bounded command/session/owner state machines, NativeIO, TinyTest, CMake presets, GitHub Actions `native-io-release-benchmarks.yml`.

**Spec:** `docs/superpowers/specs/2026-09-16-cnet-zero-copy-send-design.md`

## Global Constraints

- `cnet_send()` and `cnet_sendv()` API and ownership semantics remain unchanged.
- Z1 adds exactly one public send surface: `int cnet_send_buffer(cnet_client *, cnet_connection, mem_buffer_t *)`.
- `cnet/include/cnet/cnet.h` includes the existing public `salts_buffer.h`; no second CNet buffer abstraction is introduced.
- Because the public CNet header exposes `mem_buffer_t`, `Salts::Core` becomes a PUBLIC dependency of both `salts_cnet` and `salts_cnet_profile` through `cnet_configure_target()`.
- One admitted retained send owns exactly one CNet buffer reference; the command entry is the sole owner.
- Request records, TLS state, dispatcher state, and callbacks never add a second retained-buffer reference.
- Retained payload bytes and defining metadata (`data`, `used`, `capacity`) are immutable while in flight.
- Retained sends consume command slots but do not consume copied `command_buffer_bytes` budget.
- No allocator redesign, ring buffer, public vectored zero-copy, coroutine redesign, scheduler change, NativeIO API change, or kernel zero-copy API is part of this plan.
- Plaintext zero-copy means `NativeIO operation.buffer == mem_buffer_const_data(original_buffer)` after successful admission.
- TLS only promises no command-staging plaintext copy; TLS ciphertext construction is unchanged.
- Performance evidence must retain the existing same-run A/A noise control and legacy copied CNet baseline.

---

### Task 1: Add Retained Payload Ownership to the Bounded Command Queue

**Files:**
- Modify: `cnet/src/cnet_command.h`
- Modify: `cnet/src/cnet_command.c`
- Modify: `cnet/tests/cnet_command_test.c`
- Modify: `cnet/tests/cnet_command_profile_test.c`
- Modify: `cnet/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mem_buffer_t *`, `mem_buffer_retain()`, `mem_buffer_release()`, `mem_buffer_const_data()`, `mem_buffer_used()`, `mem_buffer_ref_count()` from `salts_buffer.h`.
- Produces: retained-buffer `cnet_command` publication with the existing `cnet_command_view { data, size, _sequence }`; `payload_copy_calls == 0` for retained publication; copied publication behavior unchanged.

- [ ] **Step 1: Write the command-queue RED tests**

Add `#include <salts_buffer.h>` to both command tests and add this helper to each file that needs it:

```c
static cnet_command make_retained_send(uint32_t slot, mem_buffer_t *buffer) {
  cnet_command command = {0};
  command.kind = CNET_COMMAND_SEND;
  command.connection.slot = slot;
  command.connection.generation = 1u;
  command.size = mem_buffer_used(buffer);
  command.retained_buffer = buffer;
  return command;
}
```

Add to `cnet_command_test.c`:

```c
it("retains one buffer without copying payload bytes") {
  mem_pool_t pool;
  mem_buffer_t *buffer;
  cnet_command_view view = {0};
  const cnet_command_queue_config config = {.capacity = 2u, .max_payload_bytes = 64u};

  check_equal(mem_init(&pool, 0u), 0);
  buffer = mem_get_buffer(&pool, 32u);
  check_not_null(buffer);
  memset(mem_buffer_data(buffer), 0x5a, 32u);
  mem_set_used(buffer, 32u);
  const void *original = mem_buffer_const_data(buffer);

  check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
  cnet_command command = make_retained_send(1u, buffer);
  check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
  check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
  mem_buffer_release(buffer);

  check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
  check_true(view.data == original);
  check_equal(view.size, 32u);
  check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
  mem_destroy(&pool);
}
```

Add rejection coverage that checks refcounts before/after `SALTS_ENOBUFS` and `SALTS_ESHUTDOWN`. Add stale release coverage by copying a valid view before first release, then verify the copied token returns `SALTS_EINVAL` on the second release.

Add to `cnet_command_profile_test.c`:

```c
it("records zero payload copies for retained publication") {
  mem_pool_t pool;
  mem_buffer_t *buffer;
  cnet_command_queue_profile profile = {0};
  cnet_command_view view = {0};
  const cnet_command_queue_config config = {.capacity = 2u, .max_payload_bytes = 4096u};

  check_equal(mem_init(&pool, 0u), 0);
  buffer = mem_get_buffer(&pool, 4096u);
  check_not_null(buffer);
  mem_set_used(buffer, 4096u);

  check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
  check_equal(cnet_command_queue_profile_begin(&queue), SALTS_OK);
  cnet_command command = make_retained_send(1u, buffer);
  check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
  check_equal(cnet_command_queue_profile_take(&queue, &profile), SALTS_OK);
  check_equal(profile.publish_calls, UINT64_C(1));
  check_equal(profile.payload_publish_calls, UINT64_C(1));
  check_equal(profile.payload_copy_calls, UINT64_C(0));

  mem_buffer_release(buffer);
  check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
  check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
  mem_destroy(&pool);
}
```

- [ ] **Step 2: Run the RED and confirm the missing retained descriptor is the failure**

```bash
cmake --preset linux-release-user -DBUILD_TESTS=ON
cmake --build --preset linux-release-user --target cnet_command_test cnet_command_profile_test
```

Expected RED: compile failure because `cnet_command.retained_buffer` does not exist. No unrelated configure/link failure is acceptable as RED evidence.

- [ ] **Step 3: Implement the minimal retained command representation**

In `cnet_command.h`, include `salts_buffer.h`, add:

```c
typedef enum cnet_command_payload_kind {
  CNET_COMMAND_PAYLOAD_NONE = 0,
  CNET_COMMAND_PAYLOAD_COPIED,
  CNET_COMMAND_PAYLOAD_RETAINED_BUFFER
} cnet_command_payload_kind;
```

Extend `cnet_command` by appending:

```c
mem_buffer_t *retained_buffer;
```

Keep `cnet_command_view` unchanged.

In `cnet_command.c`, `cnet_command_entry` already owns `mem_buffer_t *payload`; reuse that field. Add only:

```c
cnet_command_payload_kind payload_kind;
size_t copied_payload_bytes;
```

Validation for `CNET_COMMAND_SEND`/`CNET_COMMAND_SEND_CLOSE` must enforce exactly one payload source. Retained mode is valid only for `CNET_COMMAND_SEND`:

```c
const bool retained = command->retained_buffer != NULL;
if (retained) {
  return command->kind == CNET_COMMAND_SEND &&
         command->data == NULL && command->segments == NULL && command->segment_count == 0u &&
         command->size != 0u &&
         command->size == mem_buffer_used(command->retained_buffer) &&
         mem_buffer_const_data(command->retained_buffer) != NULL;
}
```

For copied commands, preserve current scalar/vector validation and require `retained_buffer == NULL`.

Before the copied-payload byte-budget check:

```c
const bool retained = command->retained_buffer != NULL;
const size_t copied_bytes = retained ? 0u : command->size;
```

Use `copied_bytes` for `payload_capacity_bytes` and `queued_bytes`. After all fallible validation/capacity/state checks have succeeded:

```c
if (retained) {
  payload = mem_buffer_retain(command->retained_buffer);
  entry->payload_kind = CNET_COMMAND_PAYLOAD_RETAINED_BUFFER;
  entry->copied_payload_bytes = 0u;
} else if (command->size != 0u) {
  payload = mem_get_buffer(&impl->payload_pool, command->size);
  if (payload == NULL) {
    cnet_command_record_rejection(impl, command->size);
    return SALTS_ENOMEM;
  }
  entry->payload_kind = CNET_COMMAND_PAYLOAD_COPIED;
  entry->copied_payload_bytes = command->size;
}
```

Assign the existing `entry->payload = payload` once. For retained mode, do not call `mem_set_used()` and do not run either scalar or vector `memcpy`. `cnet_command_queue_take()` continues to expose:

```c
out_view->data = entry->size != 0u ? mem_buffer_const_data(entry->payload) : NULL;
```

On release, subtract `entry->copied_payload_bytes` from `queued_bytes`, call `mem_buffer_release(entry->payload)` exactly once, clear `payload_kind`, `copied_payload_bytes`, and `payload`, then return the slot to `free_slots`.

Profiling behavior is fixed as:

```text
payload_publish_calls: copied + retained payload commands
payload_copy_calls:    copied commands only
payload_copy_ns:       copied memcpy region only
```

- [ ] **Step 4: Give Task 1 tests direct Core access before Core becomes public in Task 2**

In `cnet/tests/CMakeLists.txt`, change exactly these two LIBS lines:

```text
cnet_command_test:         salts_cnet Salts::TinyTest
                       ->  salts_cnet Salts::Core Salts::TinyTest

cnet_command_profile_test: salts_cnet_profile Salts::TinyTest
                       ->  salts_cnet_profile Salts::Core Salts::TinyTest
```

- [ ] **Step 5: Run GREEN and legacy command regressions**

```bash
cmake --build --preset linux-release-user --target cnet_command_test cnet_command_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_command(_profile)?_test$' --output-on-failure
```

Expected: retained tests pass; existing copied/vector/budget tests pass; the existing copied profile still reports `payload_copy_calls == 1`.

- [ ] **Step 6: Commit Task 1**

```bash
git add cnet/src/cnet_command.h cnet/src/cnet_command.c \
        cnet/tests/cnet_command_test.c cnet/tests/cnet_command_profile_test.c \
        cnet/tests/CMakeLists.txt
git commit -m "feat(cnet): retain zero-copy command payloads"
```

---

### Task 2: Add `cnet_send_buffer()` Through the Existing Client and Shard Path

**Files:**
- Modify: `cnet/include/cnet/cnet.h`
- Modify: `cnet/CMakeLists.txt`
- Modify: `cnet/src/cnet_client.c`
- Modify: `cnet/src/cnet_shards.h`
- Modify: `cnet/src/cnet_shards.c`
- Modify: `cnet/tests/cnet_api_test.c`
- Modify: `cnet/tests/cnet_header_cpp_test.cpp`

**Interfaces:**
- Consumes: retained `cnet_command` from Task 1.
- Produces: `cnet_send_buffer(client, connection, buffer)` with the same connection/write-pending admission state machine as `cnet_send()` and one retained command reference on success.

- [ ] **Step 1: Write the public API RED**

In `cnet_api_test.c`, add an external free probe:

```c
typedef struct cnet_api_test_buffer_probe {
  atomic_int freed;
} cnet_api_test_buffer_probe;

static void cnet_api_test_buffer_free(void *data, void *user) {
  cnet_api_test_buffer_probe *probe = (cnet_api_test_buffer_probe *)user;
  free(data);
  atomic_fetch_add_explicit(&probe->freed, 1, memory_order_release);
}
```

Add a TCP send test using the file's existing loopback listener/client helpers. Allocate the external payload before admission:

```c
unsigned char *storage = (unsigned char *)malloc(64u);
check_not_null(storage);
memset(storage, 0x2a, 64u);
mem_buffer_t *buffer = mem_wrap_external(storage, 64u, cnet_api_test_buffer_free, &buffer_probe);
check_not_null(buffer);

check_equal(cnet_send_buffer(&client, connection, buffer), SALTS_OK);
mem_buffer_release(buffer);
check_equal(atomic_load_explicit(&buffer_probe.freed, memory_order_acquire), 0);
```

The `freed == 0` assertion is made immediately after admission/caller release and before the owner processes the send terminal. Then drive/poll until the peer has received all 64 bytes and the existing `on_send` callback reports 64 bytes. At that settled point require `freed == 1`. Do not assert that the free callback occurs after `on_send`: current owner semantics release the command when the NativeIO send terminal is settled, before publishing/dispatching `CNET_EVENT_SEND`.

Add validation for null buffer/client, zero-used buffer, over-`max_send_bytes`, stale connection, and a second write while the first is pending. Rejected calls must leave the buffer refcount unchanged.

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset linux-release-user --target cnet_api_test cnet_header_cpp_test
```

Expected RED: compile/link failure because `cnet_send_buffer()` is absent.

- [ ] **Step 3: Add the public declaration and promote Core to a public CNet dependency**

In `cnet/include/cnet/cnet.h` add:

```c
#include <salts_buffer.h>
```

Beside `cnet_send()`/`cnet_sendv()`, add:

```c
/**
 * Retains one non-empty immutable buffer on successful admission and sends the
 * first mem_buffer_used(buffer) bytes without copying them into CNet command
 * storage. The caller may release its reference immediately after SALTS_OK,
 * but must not mutate data/used/capacity while the logical send is in flight.
 * Rejection retains no lasting reference.
 */
int cnet_send_buffer(cnet_client *client, cnet_connection connection, mem_buffer_t *buffer);
```

In `cnet/CMakeLists.txt`, replace the current dependency grouping with:

```cmake
target_link_libraries(
  ${target_name}
  PUBLIC Salts::NativeIO Salts::Concurrency Salts::Core
  PRIVATE Salts::UriParser Salts::Platform Salts::CSTL monocypher salts_reed
          ${cnet_cares_link_items} kcp::kcp c-ares::cares
          OpenSSL::SSL OpenSSL::Crypto
          $<$<PLATFORM_ID:Windows>:crypt32>)
```

This applies to both `salts_cnet` and `salts_cnet_profile` because both use `cnet_configure_target()`.

- [ ] **Step 4: Plumb retained buffers through client admission**

Extend `cnet_client_send_input`:

```c
typedef struct cnet_client_send_input {
  const void *data;
  const cnet_const_buffer *segments;
  mem_buffer_t *retained_buffer;
  size_t size;
  size_t segment_count;
  bool close_after_send;
} cnet_client_send_input;
```

In `cnet_client_send_admit()` use this exact selection order:

```c
if (input->retained_buffer != NULL)
  status = cnet_shards_send_buffer(&impl->shards, internal, input->retained_buffer, input->size);
else if (input->segments != NULL)
  status = cnet_shards_sendv(&impl->shards, internal, input->segments, input->segment_count,
                             input->size);
else if (input->close_after_send)
  status = cnet_shards_send_and_close(&impl->shards, internal, input->data, input->size);
else
  status = cnet_shards_send(&impl->shards, internal, input->data, input->size);
```

Implement:

```c
int cnet_send_buffer(cnet_client *client, cnet_connection connection, mem_buffer_t *buffer) {
  cnet_client_impl *impl = cnet_client_get(client);
  size_t size;
  cnet_client_send_input input;

  if (impl == NULL || buffer == NULL) return SALTS_EINVAL;
  size = mem_buffer_used(buffer);
  if (size == 0u || mem_buffer_const_data(buffer) == NULL) return SALTS_EINVAL;
  if (size > impl->max_send_bytes) return SALTS_EMSGSIZE;
  input = (cnet_client_send_input){.retained_buffer = buffer, .size = size};
  return cnet_client_send_admit(impl, connection, &input);
}
```

Do not retain in `cnet_send_buffer()`; the successful queue publication remains the single retain point.

- [ ] **Step 5: Add shard plumbing without changing shard ownership semantics**

In `cnet_shards.h` add:

```c
int cnet_shards_send_buffer(cnet_shards *shards, cnet_shard_connection connection,
                            mem_buffer_t *buffer, size_t size);
```

In `cnet_shards.c` add:

```c
int cnet_shards_send_buffer(cnet_shards *shards, cnet_shard_connection connection,
                            mem_buffer_t *buffer, size_t size) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  const cnet_command command = {.kind = CNET_COMMAND_SEND,
                                .connection = connection.session,
                                .size = size,
                                .retained_buffer = buffer};
  if (impl == NULL || buffer == NULL || size == 0u) return SALTS_EINVAL;
  if (size > impl->max_command_payload_bytes) return SALTS_EMSGSIZE;
  return cnet_shards_publish(impl, connection, &command);
}
```

- [ ] **Step 6: Keep C++ header compatibility explicit**

In `cnet_header_cpp_test.cpp` add:

```cpp
static int (*const send_buffer_signature)(cnet_client *, cnet_connection, mem_buffer_t *) =
    &cnet_send_buffer;
```

- [ ] **Step 7: Run GREEN**

```bash
cmake --build --preset linux-release-user --target \
  cnet_command_test cnet_command_profile_test cnet_api_test cnet_header_cpp_test
ctest --test-dir build/linux-gcc-release \
  -R '^cnet_(command|command_profile|api|header_cpp)_test$' --output-on-failure
```

Expected: immediate caller release is safe; peer bytes are correct; busy/stale/rejected sends do not acquire a lasting reference; legacy sends remain green.

- [ ] **Step 8: Commit Task 2**

```bash
git add cnet/include/cnet/cnet.h cnet/CMakeLists.txt \
        cnet/src/cnet_client.c cnet/src/cnet_shards.h cnet/src/cnet_shards.c \
        cnet/tests/cnet_api_test.c cnet/tests/cnet_header_cpp_test.cpp
git commit -m "feat(cnet): add retained-buffer send API"
```

---

### Task 3: Prove Exactly-Once Release Across Partial Sends, Timeout/Close, and TLS

**Files:**
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`
- Modify: `cnet/tests/cnet_tls_test.c`

**Interfaces:**
- Consumes: Task 1 command token/generation release authority and Task 2 `cnet_send_buffer()`.
- Produces: deterministic evidence that partial stream completions keep the base retained buffer alive, timeout/close release exactly once, and TLS uses the same retained command view without adding ownership.

- [ ] **Step 1: Add deterministic partial-send and timeout RED tests**

Under `#if defined(CNET_INTERNAL_TESTING)` in `cnet_owner_test.c`, add a retained send case using the existing owner/session/loopback setup. Publish:

```c
cnet_command command = {
    .kind = CNET_COMMAND_SEND,
    .connection = session,
    .size = mem_buffer_used(buffer),
    .retained_buffer = buffer,
};
check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
mem_buffer_release(buffer);
```

Configure a 2-byte test send chunk for a 7-byte payload. Require the peer to receive all 7 bytes. Before each drive that can complete another chunk, require the external free callback still to be zero while bytes remain. After the final NativeIO send terminal is processed and the command is released, require exactly one free. The later `CNET_EVENT_SEND` publication/dispatch is not the ownership-release boundary.

Add a write-timeout case using the existing deterministic owner clock: admit a retained command, advance beyond `write_timeout_ms`, drive cancellation/terminal settlement, and require exactly one free.

- [ ] **Step 2: Run RED and confirm the missing send-chunk seam**

```bash
cmake --build --preset linux-release-user --target cnet_owner_profile_test
ctest --test-dir build/linux-gcc-release -R '^cnet_owner_profile_test$' --output-on-failure
```

Expected RED: the test-only chunk control is absent.

- [ ] **Step 3: Add a test-only chunk seam that is compiled out of production CNet**

In `cnet_owner_impl`, under `#if defined(CNET_INTERNAL_TESTING)` add:

```c
size_t test_send_chunk_bytes;
```

In `cnet_owner.h`, under the same define add:

```c
int cnet_owner_test_set_send_chunk_bytes(cnet_owner *owner, size_t bytes);
```

Implement:

```c
int cnet_owner_test_set_send_chunk_bytes(cnet_owner *owner, size_t bytes) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  if (impl == NULL) return SALTS_EINVAL;
  impl->test_send_chunk_bytes = bytes;
  return SALTS_OK;
}
```

In `cnet_owner_coroutine_entry()`, keep `request->operation` as the full remaining logical operation. Submit a local copy:

```c
native_io_operation submitted = request->operation;
#if defined(CNET_INTERNAL_TESTING)
if ((request->role == CNET_OWNER_REQUEST_SEND || request->role == CNET_OWNER_REQUEST_TLS_WRITE) &&
    impl->test_send_chunk_bytes != 0u && submitted.length > impl->test_send_chunk_bytes)
  submitted.length = impl->test_send_chunk_bytes;
#endif
status = native_io_coroutine_await(coroutine, &submitted, &completion);
```

After completion, keep the existing update of `request->operation.buffer` and `request->operation.length` by `completion.bytes`. This produces real repeated NativeIO submissions in the test static library while the production shared library contains no test field or branch.

- [ ] **Step 4: Add TLS retained-buffer lifetime coverage**

In `cnet_tls_test.c`, reuse the successful TLS fixture that currently sends `plaintext_request`. Add one retained-buffer case:

```c
mem_pool_t pool;
mem_buffer_t *buffer;
check_equal(mem_init(&pool, 0u), 0);
buffer = mem_get_buffer(&pool, sizeof(plaintext_request) - 1u);
check_not_null(buffer);
memcpy(mem_buffer_data(buffer), plaintext_request, sizeof(plaintext_request) - 1u);
mem_set_used(buffer, sizeof(plaintext_request) - 1u);
check_equal(cnet_send_buffer(&client, client_connection, buffer), SALTS_OK);
mem_buffer_release(buffer);
```

Verify the existing TLS peer receives the same plaintext request and the normal send callback fires once. Add an externally wrapped variant and assert its free callback is zero immediately after admission/caller release before owner progress, then eventually exactly one after the TLS send terminal has settled. Do not assert that free occurs after the public send callback; command release may precede event dispatch. Do not add a TLS-specific copy counter: Task 1's command-queue retained-mode test is transport-independent and already proves command staging performs zero payload copies.

- [ ] **Step 5: Run owner/TLS GREEN and terminal regressions**

```bash
cmake --build --preset linux-release-user --target \
  cnet_owner_test cnet_owner_profile_test cnet_tls_test cnet_api_test cnet_api_profile_test
ctest --test-dir build/linux-gcc-release \
  -R '^cnet_(owner|owner_profile|tls|api|api_profile)_test$' --output-on-failure
```

Expected: normal, partial, timeout/close, and TLS retained paths release exactly once; legacy copied/TLS behavior remains unchanged.

- [ ] **Step 6: Commit Task 3**

```bash
git add cnet/src/cnet_owner.h cnet/src/cnet_owner.c \
        cnet/tests/cnet_owner_test.c cnet/tests/cnet_tls_test.c
git commit -m "test(cnet): prove retained send terminal ownership"
```

---

### Task 4: Add a Separate Zero-Copy CNet Benchmark Variant

**Files:**
- Modify: `cnet/benchmarks/cnet_io_benchmark.c`

**Interfaces:**
- Consumes: Task 2 `cnet_send_buffer()` and Task 1 copy counters.
- Produces: legacy CNet copy and retained-buffer CNet zero-copy rows in the same benchmark execution, using the existing A/A null control and backend matrix.

- [ ] **Step 1: Add benchmark RED for an explicit driver identity**

Extend the driver enum:

```c
typedef enum io_bench_driver {
  IO_BENCH_LIBUV = 0,
  IO_BENCH_NATIVE_IO,
  IO_BENCH_NATIVE_IO_COROUTINE,
  IO_BENCH_CNET,
  IO_BENCH_CNET_ZERO_COPY
} io_bench_driver;
```

Update `io_bench_driver_name()` so `IO_BENCH_CNET_ZERO_COPY` returns `"cnet-zero-copy"`. Add the new enum to every driver-count loop, setup/destroy switch, rotating comparison order, and report table that currently handles `IO_BENCH_CNET`.

Build immediately after only the enum/name/order edits. Expected RED: setup/exchange logic returns the benchmark's unsupported/invalid status for the new driver because retained fixture/send handling does not exist yet.

- [ ] **Step 2: Allocate the retained benchmark payload outside timed admission**

Extend `io_bench_cnet`:

```c
mem_pool_t send_pool;
mem_buffer_t *send_buffer;
bool send_pool_initialized;
bool zero_copy;
```

During fixture setup for `IO_BENCH_CNET_ZERO_COPY`:

```c
if (mem_init(&fixture->cnet.send_pool, 0u) != 0) return SALTS_ENOMEM;
fixture->cnet.send_pool_initialized = true;
fixture->cnet.send_buffer = mem_get_buffer(&fixture->cnet.send_pool, payload_size);
if (fixture->cnet.send_buffer == NULL) return SALTS_ENOMEM;
memcpy(mem_buffer_data(fixture->cnet.send_buffer), payload, payload_size);
mem_set_used(fixture->cnet.send_buffer, payload_size);
fixture->cnet.zero_copy = true;
```

Hold this benchmark-owned reference across all warmup and measured exchanges. CNet adds/releases one temporary reference per accepted send.

Destroy only after CNet terminal drain:

```c
mem_buffer_release(fixture->cnet.send_buffer);
fixture->cnet.send_buffer = NULL;
if (fixture->cnet.send_pool_initialized) {
  mem_destroy(&fixture->cnet.send_pool);
  fixture->cnet.send_pool_initialized = false;
}
```

- [ ] **Step 3: Change only the CNet send-admission call**

Where the existing CNet exchange records `send_admission_ns`, use:

```c
status = fixture->zero_copy
             ? cnet_send_buffer(&fixture->client, fixture->connection, fixture->send_buffer)
             : cnet_send(&fixture->client, fixture->connection, sent, length);
```

Do not alter server behavior, receive demand, polling, callback timing, payload checking, warmup count, measured exchange count, backend selection, or A/A control.

- [ ] **Step 4: Preserve deterministic copy proof in diagnostic output**

For zero-copy profile repeats, require `command_queue_payload_copy_calls == 0`. Keep the legacy copied row and its existing copy timing. Report the retained row separately for 1/4/8/16/32/64 KiB TCP.

Primary decision surface remains Windows IOCP TCP 64 KiB. Linux epoll/io_uring and macOS kqueue are cross-platform regression evidence.

- [ ] **Step 5: Build and smoke-test locally**

```bash
cmake --preset linux-release-user -DBUILD_BENCHMARKS=ON -DBUILD_TESTS=ON
cmake --build --preset linux-release-user --target \
  cnet_io_benchmark cnet_command_test cnet_command_profile_test cnet_api_test
CNET_IO_BENCHMARK_BACKEND=epoll build/linux-gcc-release/bin/cnet_io_benchmark
```

On a Linux host with io_uring support:

```bash
CNET_IO_BENCHMARK_BACKEND=io_uring build/linux-gcc-release/bin/cnet_io_benchmark
```

Expected: both `cnet` and `cnet-zero-copy` rows appear; the retained diagnostic reports zero command payload-copy calls; benchmark comparison rows remain uninstrumented.

- [ ] **Step 6: Commit Task 4**

```bash
git add cnet/benchmarks/cnet_io_benchmark.c
git commit -m "bench(cnet): compare retained zero-copy sends"
```

---

### Task 5: Document the Contract and Run the Full Exact-Head Verification Gate

**Files:**
- Modify: `cnet/README.md`
- Read/verify only: `.github/workflows/native-io-release-benchmarks.yml`

**Interfaces:**
- Consumes: Tasks 1-4 implementation and benchmark.
- Produces: user-facing ownership documentation plus exact-head correctness/performance evidence for #286.

- [ ] **Step 1: Document copied vs retained send semantics**

Add this contract table to `cnet/README.md`:

```text
API               admission ownership               command payload copy
cnet_send          copies bytes before SALTS_OK      yes
cnet_sendv         concatenates/copies before OK     yes
cnet_send_buffer   retains mem_buffer_t on OK        no
```

Document these exact rules:

```text
- caller may release its own mem_buffer_t reference immediately after SALTS_OK;
- caller must not mutate data/used/capacity while the logical send is in flight;
- rejection retains no lasting reference;
- command_capacity bounds retained in-flight buffer count;
- command_buffer_bytes continues to bound copied command storage only;
- TLS promise is command-staging zero-copy, not ciphertext/kernel zero-copy.
```

- [ ] **Step 2: Run the full local CNet test closure**

```bash
cmake --preset linux-release-user -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON
cmake --build --preset linux-release-user --target \
  cnet_websocket_parser_test cnet_websocket_test cnet_session_test \
  cnet_command_test cnet_uri_test cnet_transport_test cnet_owner_test \
  cnet_owner_profile_test cnet_event_test cnet_resolver_test cnet_shards_test \
  cnet_dispatcher_test cnet_api_test cnet_api_profile_test cnet_header_cpp_test \
  cnet_tls_test cnet_datagram_test cnet_kcp_test cnet_secure_kcp_test \
  cnet_packet_endpoint_test cnet_stop_contract_test cnet_benchmark_stats_test \
  cnet_io_benchmark_config_test cnet_io_benchmark
ctest --test-dir build/linux-gcc-release -R '^cnet_' --output-on-failure
```

Expected: all CNet tests green.

- [ ] **Step 3: Verify the diff stays inside the approved boundary**

```bash
git diff master...HEAD -- cnet \
  docs/superpowers/specs/2026-09-16-cnet-zero-copy-send-design.md \
  docs/superpowers/plans/2026-09-16-cnet-zero-copy-send.md
git diff --check master...HEAD
```

Reject the implementation if it introduces allocator size-class changes, a second queue/worker, public vectored zero-copy, coroutine lifecycle redesign, NativeIO API changes, or kernel zero-copy claims.

- [ ] **Step 4: Commit documentation**

```bash
git add cnet/README.md
git commit -m "docs(cnet): document retained zero-copy sends"
```

- [ ] **Step 5: Push the exact head and open a draft PR linked to #286**

Use branch `design/cnet-zero-copy-send-286`. The PR body must contain:

```text
Implements #286 Z0/Z1/Z2.

Correctness gates:
- legacy copied send semantics unchanged
- command entry sole retained owner
- exact-once release tests
- deterministic zero-copy proof: pointer identity + payload_copy_calls == 0
- Linux/Windows/macOS exact-head runtime coverage

Performance gate:
- copied CNet vs retained CNet vs NativeIO direct
- same-run A/A control
- Windows IOCP 64 KiB primary
```

- [ ] **Step 6: Require the existing release benchmark matrix on the exact PR head**

`.github/workflows/native-io-release-benchmarks.yml` already triggers on `cnet/**`. Require successful exact-head jobs for:

```text
epoll / Ubuntu 24.04 GCC
io_uring / Ubuntu 24.04 GCC
IOCP / Windows 2022 MSVC
kqueue / macOS 15 AppleClang
```

Do not substitute a different SHA's run. Capture the run ID, exact head SHA, and benchmark artifact IDs/digests in #286.

- [ ] **Step 7: Record exactly one #286 decision**

After reading the exact-head artifacts, post exactly one of:

```text
zero-copy validated and measurable
zero-copy valid but below noise floor
zero-copy contract/benchmark invalid
```

`zero-copy validated and measurable` requires semantic proof plus a signal larger than the same-run A/A noise floor on the predeclared IOCP 64 KiB decision surface. `zero-copy valid but below noise floor` applies when semantic gates pass but the performance delta is not noise-resolved. `zero-copy contract/benchmark invalid` applies to ownership divergence, nonzero retained-path command copies, cross-platform correctness failure, or a confounded benchmark.
