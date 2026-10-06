#include "cnet_write_queue.h"
#include "tinytest.h"

#include <cmeta_buffer.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(CNET_RETAINED_VECTOR_MAX >= 32u,
               "CNet logical retained vector must cover 32 ranges");

typedef struct cnet_write_queue_free_probe {
  atomic_int freed;
} cnet_write_queue_free_probe;

static void cnet_write_queue_test_free(void *data, void *user_data) {
  cnet_write_queue_free_probe *probe = (cnet_write_queue_free_probe *)user_data;
  free(data);
  atomic_fetch_add_explicit(&probe->freed, 1, memory_order_release);
}

static mem_buffer_t *cnet_write_queue_external(size_t size, unsigned char value,
                                               cnet_write_queue_free_probe *probe) {
  unsigned char *data = (unsigned char *)malloc(size);
  mem_buffer_t *buffer;
  if (data == NULL) return NULL;
  memset(data, value, size);
  buffer = mem_wrap_external(data, size, cnet_write_queue_test_free, probe);
  if (buffer == NULL) free(data);
  return buffer;
}

static mem_buffer_t *cnet_write_queue_bytes(const void *data, size_t size) {
  mem_buffer_t *buffer;
  if (data == NULL || size == 0u) return NULL;
  buffer = mem_get_buffer(mem_global(), size);
  if (buffer == NULL) return NULL;
  memcpy(mem_buffer_data(buffer), data, size);
  mem_set_used(buffer, size);
  return buffer;
}

spec("CNet bounded write ownership queue") {
  it("preserves retained payloads and independent per-connection FIFO order") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {2u, 4u, 16u};
    const cnet_session_handle first = {1u, 7u};
    const cnet_session_handle second = {2u, 3u};
    const unsigned char a[] = {1u, 2u, 3u};
    const unsigned char b[] = {4u, 5u};
    const unsigned char d[] = {8u, 9u};
    mem_buffer_t *a_buffer = cnet_write_queue_bytes(a, sizeof(a));
    mem_buffer_t *b_buffer = cnet_write_queue_bytes(b, sizeof(b));
    mem_buffer_t *d_buffer = cnet_write_queue_bytes(d, sizeof(d));
    cnet_write_handle handle = {0};
    cnet_write_view view = {0};

    check_true(a_buffer != NULL);
    check_true(b_buffer != NULL);
    check_true(d_buffer != NULL);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_buffer(&queue, first, a_buffer, false, &handle), SALTS_OK);
    check_true(cnet_write_handle_valid(handle));
    check_equal(cnet_write_queue_enqueue_buffer(&queue, first, b_buffer, true, &handle), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_buffer(&queue, second, d_buffer, false, &handle), SALTS_OK);
    mem_buffer_release(a_buffer);
    mem_buffer_release(b_buffer);
    mem_buffer_release(d_buffer);

    check_equal(cnet_write_queue_peek(&queue, first, &view), SALTS_OK);
    check_equal(view.size, sizeof(a));
    check_equal(view.remaining, sizeof(a));
    check_equal(((const unsigned char *)view.data)[0], 1u);
    check_false(view.close_after_send);
    check_equal(cnet_write_queue_advance(&queue, &view, 1u), SALTS_OK);
    check_equal(view.offset, 1u);
    check_equal(view.remaining, sizeof(a) - 1u);
    check_equal(((const unsigned char *)view.data)[0], 2u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);

    check_equal(cnet_write_queue_peek(&queue, first, &view), SALTS_OK);
    check_equal(view.size, sizeof(b));
    check_true(view.close_after_send);
    check_equal(((const unsigned char *)view.data)[0], 4u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);

    check_equal(cnet_write_queue_peek(&queue, second, &view), SALTS_OK);
    check_equal(view.size, sizeof(d));
    check_equal(((const unsigned char *)view.data)[1], 9u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(cnet_write_queue_peek(&queue, first, &view), SALTS_ETIMEDOUT);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("retains external buffers only after successful bounded admission") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 1u, 8u};
    const cnet_session_handle connection = {1u, 1u};
    cnet_write_queue_free_probe first_free;
    cnet_write_queue_free_probe rejected_free;
    mem_buffer_t *first;
    mem_buffer_t *rejected;
    cnet_write_handle handle = {0};
    cnet_write_view view = {0};

    atomic_init(&first_free.freed, 0);
    atomic_init(&rejected_free.freed, 0);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);

    first = cnet_write_queue_external(4u, 0x5au, &first_free);
    rejected = cnet_write_queue_external(4u, 0x33u, &rejected_free);
    check_true(first != NULL);
    check_true(rejected != NULL);
    check_equal(mem_buffer_ref_count(first), UINT32_C(1));
    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, first, false, &handle),
                SALTS_OK);
    check_equal(mem_buffer_ref_count(first), UINT32_C(2));
    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, rejected, false, &handle),
                SALTS_ENOBUFS);
    check_equal(mem_buffer_ref_count(rejected), UINT32_C(1));

    mem_buffer_release(first);
    check_equal(atomic_load_explicit(&first_free.freed, memory_order_acquire), 0);
    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(atomic_load_explicit(&first_free.freed, memory_order_acquire), 1);

    mem_buffer_release(rejected);
    check_equal(atomic_load_explicit(&rejected_free.freed, memory_order_acquire), 1);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("discards retained queued tails while preserving an active FIFO head") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 4u, 8u};
    const cnet_session_handle connection = {1u, 9u};
    const unsigned char a = 1u;
    const unsigned char b = 2u;
    const unsigned char d = 3u;
    mem_buffer_t *a_buffer = cnet_write_queue_bytes(&a, 1u);
    mem_buffer_t *b_buffer = cnet_write_queue_bytes(&b, 1u);
    mem_buffer_t *d_buffer = cnet_write_queue_bytes(&d, 1u);
    cnet_write_handle first = {0};
    cnet_write_handle second = {0};
    cnet_write_handle third = {0};
    cnet_write_view view = {0};
    size_t count = 0u;
    size_t discarded = 0u;

    check_true(a_buffer != NULL);
    check_true(b_buffer != NULL);
    check_true(d_buffer != NULL);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, a_buffer, false, &first), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, b_buffer, false, &second), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, d_buffer, false, &third), SALTS_OK);
    mem_buffer_release(a_buffer);
    mem_buffer_release(b_buffer);
    mem_buffer_release(d_buffer);
    check_equal(cnet_write_queue_count(&queue, connection, &count), SALTS_OK);
    check_equal(count, (size_t)3u);

    check_equal(cnet_write_queue_discard(&queue, connection, true, &discarded), SALTS_OK);
    check_equal(discarded, (size_t)2u);
    check_equal(cnet_write_queue_count(&queue, connection, &count), SALTS_OK);
    check_equal(count, (size_t)1u);
    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_equal(view.handle.slot, first.slot);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);

    a_buffer = cnet_write_queue_bytes(&a, 1u);
    b_buffer = cnet_write_queue_bytes(&b, 1u);
    check_true(a_buffer != NULL);
    check_true(b_buffer != NULL);
    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, a_buffer, false, &first), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, b_buffer, false, &second), SALTS_OK);
    mem_buffer_release(a_buffer);
    mem_buffer_release(b_buffer);
    check_equal(cnet_write_queue_cancel_tail(&queue, connection, second), SALTS_OK);
    check_equal(cnet_write_queue_count(&queue, connection, &count), SALTS_OK);
    check_equal(count, (size_t)1u);
    check_equal(cnet_write_queue_discard(&queue, connection, false, &discarded), SALTS_OK);
    check_equal(discarded, (size_t)1u);
    check_equal(cnet_write_queue_count(&queue, connection, &count), SALTS_OK);
    check_equal(count, (size_t)0u);

    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("retains canonical slice ranges and advances only inside the subrange") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 2u, 16u};
    const cnet_session_handle connection = {1u, 5u};
    cnet_write_queue_free_probe free_probe;
    mem_buffer_t *buffer;
    mem_slice_t slice;
    cnet_write_handle handle = {0};
    cnet_write_view view = {0};

    atomic_init(&free_probe.freed, 0);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    buffer = cnet_write_queue_external(8u, 0u, &free_probe);
    check_true(buffer != NULL);
    for (size_t index = 0u; index < 8u; ++index)
      mem_buffer_data(buffer)[index] = (char)(index + 1u);
    slice = mem_slice(buffer, 2u, 4u);
    check_equal(slice.length, (size_t)4u);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));

    check_equal(cnet_write_queue_enqueue_slice(&queue, connection, &slice, false, &handle),
                SALTS_OK);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(3));
    mem_slice_release(&slice);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
    mem_buffer_release(buffer);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 0);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_equal(view.size, (size_t)4u);
    check_equal(view.remaining, (size_t)4u);
    check_equal(((const unsigned char *)view.data)[0], 3u);
    check_equal(((const unsigned char *)view.data)[3], 6u);
    check_equal(cnet_write_queue_advance(&queue, &view, 2u), SALTS_OK);
    check_equal(view.offset, (size_t)2u);
    check_equal(view.remaining, (size_t)2u);
    check_equal(((const unsigned char *)view.data)[0], 5u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 1);

    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("rejects forged or out-of-range slices without retaining") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 1u, 16u};
    const cnet_session_handle connection = {1u, 6u};
    cnet_write_queue_free_probe free_probe;
    mem_buffer_t *buffer;
    cnet_write_handle handle = {1u, 1u};
    mem_slice_t forged;

    atomic_init(&free_probe.freed, 0);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    buffer = cnet_write_queue_external(8u, 0x44u, &free_probe);
    check_true(buffer != NULL);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));

    forged = (mem_slice_t){mem_buffer_data(buffer) + 8u, 1u, buffer};
    check_equal(cnet_write_queue_enqueue_slice(&queue, connection, &forged, false, &handle),
                SALTS_EINVAL);
    check_false(cnet_write_handle_valid(handle));
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));

    forged = (mem_slice_t){mem_buffer_data(buffer) + 6u, 3u, buffer};
    check_equal(cnet_write_queue_enqueue_slice(&queue, connection, &forged, false, &handle),
                SALTS_EINVAL);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));

    forged = (mem_slice_t){mem_buffer_data(buffer), 0u, buffer};
    check_equal(cnet_write_queue_enqueue_slice(&queue, connection, &forged, false, &handle),
                SALTS_EINVAL);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));

    mem_buffer_release(buffer);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 1);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("retains bounded vector slices without copying and rebuilds partial spans") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 2u, 32u};
    const cnet_session_handle connection = {1u, 12u};
    cnet_write_queue_free_probe first_free;
    cnet_write_queue_free_probe second_free;
    mem_buffer_t *first;
    mem_buffer_t *second;
    mem_slice_t slices[3];
    cnet_write_handle handle = {0};
    cnet_write_view view = {0};
    native_io_buffer_span spans[NATIVE_IO_VECTOR_MAX] = {{0}};
    size_t span_count = 0u;
    size_t span_bytes = 0u;
    cnet_write_queue_stats stats = {0};

    atomic_init(&first_free.freed, 0);
    atomic_init(&second_free.freed, 0);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);

    first = cnet_write_queue_external(8u, 0u, &first_free);
    second = cnet_write_queue_external(8u, 0u, &second_free);
    check_true(first != NULL);
    check_true(second != NULL);
    for (size_t index = 0u; index < 8u; ++index) {
      mem_buffer_data(first)[index] = (char)(0x10u + index);
      mem_buffer_data(second)[index] = (char)(0x20u + index);
    }

    slices[0] = mem_slice(first, 1u, 3u);
    slices[1] = mem_slice(first, 5u, 2u);
    slices[2] = mem_slice(second, 2u, 3u);
    check_equal(mem_buffer_ref_count(first), UINT32_C(3));
    check_equal(mem_buffer_ref_count(second), UINT32_C(2));

    check_equal(cnet_write_queue_enqueue_slicev(&queue, connection, slices, 3u, false, &handle),
                SALTS_OK);
    check_equal(mem_buffer_ref_count(first), UINT32_C(4));
    check_equal(mem_buffer_ref_count(second), UINT32_C(3));
    for (size_t index = 0u; index < 3u; ++index) mem_slice_release(&slices[index]);
    check_equal(mem_buffer_ref_count(first), UINT32_C(2));
    check_equal(mem_buffer_ref_count(second), UINT32_C(2));
    mem_buffer_release(first);
    mem_buffer_release(second);
    check_equal(atomic_load_explicit(&first_free.freed, memory_order_acquire), 0);
    check_equal(atomic_load_explicit(&second_free.freed, memory_order_acquire), 0);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_equal(view.size, (size_t)8u);
    check_equal(view.remaining, (size_t)8u);
    check_true(view.vector_write);
    check_equal(cnet_write_queue_build_vector(&queue, &view, view.remaining, spans,
                                              &span_count, &span_bytes),
                SALTS_OK);
    check_equal(span_count, (size_t)3u);
    check_equal(span_bytes, (size_t)8u);
    check_equal(((const unsigned char *)spans[0].data)[0], 0x11u);
    check_equal(spans[0].length, (size_t)3u);
    check_equal(((const unsigned char *)spans[1].data)[0], 0x15u);
    check_equal(spans[1].length, (size_t)2u);
    check_equal(((const unsigned char *)spans[2].data)[0], 0x22u);
    check_equal(spans[2].length, (size_t)3u);

    check_equal(cnet_write_queue_advance(&queue, &view, 4u), SALTS_OK);
    check_equal(view.offset, (size_t)4u);
    check_equal(view.remaining, (size_t)4u);
    check_true(view.vector_write);
    span_count = 0u;
    span_bytes = 0u;
    check_equal(cnet_write_queue_build_vector(&queue, &view, view.remaining, spans,
                                              &span_count, &span_bytes),
                SALTS_OK);
    check_equal(span_count, (size_t)2u);
    check_equal(span_bytes, (size_t)4u);
    check_equal(((const unsigned char *)spans[0].data)[0], 0x16u);
    check_equal(spans[0].length, (size_t)1u);
    check_equal(((const unsigned char *)spans[1].data)[0], 0x22u);
    check_equal(spans[1].length, (size_t)3u);

    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(atomic_load_explicit(&first_free.freed, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&second_free.freed, memory_order_acquire), 1);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("exposes an adjacent retained-vector prefix without changing SG boundaries") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 1u, 16u};
    const cnet_session_handle connection = {1u, 17u};
    cnet_write_queue_free_probe free_probe;
    mem_buffer_t *buffer;
    mem_slice_t slices[3];
    cnet_write_handle handle = {0};
    cnet_write_view view = {0};
    native_io_buffer_span spans[NATIVE_IO_VECTOR_MAX] = {{0}};
    const void *contiguous = NULL;
    size_t span_count = 0u;
    size_t span_bytes = 0u;
    size_t contiguous_bytes = 0u;

    atomic_init(&free_probe.freed, 0);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    buffer = cnet_write_queue_external(8u, 0u, &free_probe);
    check_true(buffer != NULL);
    for (size_t index = 0u; index < 8u; ++index)
      mem_buffer_data(buffer)[index] = (char)(0x40u + index);

    slices[0] = mem_slice(buffer, 0u, 2u);
    slices[1] = mem_slice(buffer, 2u, 3u);
    slices[2] = mem_slice(buffer, 5u, 3u);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(4));

    check_equal(cnet_write_queue_enqueue_slicev(&queue, connection, slices, 3u,
                                                false, &handle),
                SALTS_OK);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(5));
    for (size_t index = 0u; index < 3u; ++index)
      mem_slice_release(&slices[index]);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
    mem_buffer_release(buffer);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 0);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_true(view.vector_write);
    check_equal(view.size, (size_t)8u);

    check_equal(cnet_write_queue_build_vector(&queue, &view, view.remaining,
                                              spans, &span_count, &span_bytes),
                SALTS_OK);
    check_equal(span_count, (size_t)3u);
    check_equal(span_bytes, (size_t)8u);
    check_equal(spans[0].length, (size_t)2u);
    check_equal(spans[1].length, (size_t)3u);
    check_equal(spans[2].length, (size_t)3u);

    check_equal(cnet_write_queue_build_contiguous(&queue, &view, view.remaining,
                                                  &contiguous, &contiguous_bytes),
                SALTS_OK);
    check_true(contiguous != NULL);
    check_equal(contiguous_bytes, (size_t)8u);
    check_equal(((const unsigned char *)contiguous)[0], 0x40u);
    check_equal(((const unsigned char *)contiguous)[7], 0x47u);

    check_equal(cnet_write_queue_advance(&queue, &view, 4u), SALTS_OK);
    contiguous = NULL;
    contiguous_bytes = 0u;
    check_equal(cnet_write_queue_build_contiguous(&queue, &view, view.remaining,
                                                  &contiguous, &contiguous_bytes),
                SALTS_OK);
    check_equal(contiguous_bytes, (size_t)4u);
    check_equal(((const unsigned char *)contiguous)[0], 0x44u);
    check_equal(((const unsigned char *)contiguous)[3], 0x47u);

    check_equal(cnet_write_queue_advance(&queue, &view, contiguous_bytes), SALTS_OK);
    check_equal(view.remaining, (size_t)0u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 1);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("windows a 32-range logical retained vector through two native batches") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 1u, 64u};
    const cnet_session_handle connection = {1u, 16u};
    cnet_write_queue_free_probe free_probe;
    mem_buffer_t *buffer;
    mem_slice_t slices[32];
    cnet_write_handle handle = {0};
    cnet_write_view view = {0};
    native_io_buffer_span spans[NATIVE_IO_VECTOR_MAX] = {{0}};
    size_t span_count = 0u;
    size_t span_bytes = 0u;

    atomic_init(&free_probe.freed, 0);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    buffer = cnet_write_queue_external(32u, 0u, &free_probe);
    check_true(buffer != NULL);
    for (size_t index = 0u; index < 32u; ++index) {
      mem_buffer_data(buffer)[index] = (char)(index + 1u);
      slices[index] = mem_slice(buffer, index, 1u);
      check_equal(slices[index].length, (size_t)1u);
    }
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(33));

    check_equal(cnet_write_queue_enqueue_slicev(&queue, connection, slices, 32u,
                                                false, &handle),
                SALTS_OK);
    /* Thirty-two slice refs plus the caller ref plus one unique queue owner. */
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(34));
    for (size_t index = 0u; index < 32u; ++index)
      mem_slice_release(&slices[index]);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
    mem_buffer_release(buffer);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 0);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_equal(view.size, (size_t)32u);
    check_equal(view.remaining, (size_t)32u);
    check_true(view.vector_write);

    check_equal(cnet_write_queue_build_vector(&queue, &view, view.remaining,
                                              spans, &span_count, &span_bytes),
                SALTS_OK);
    check_equal(span_count, (size_t)NATIVE_IO_VECTOR_MAX);
    check_equal(span_bytes, (size_t)NATIVE_IO_VECTOR_MAX);
    for (size_t index = 0u; index < NATIVE_IO_VECTOR_MAX; ++index) {
      check_equal(spans[index].length, (size_t)1u);
      check_equal(((const unsigned char *)spans[index].data)[0],
                  (unsigned char)(index + 1u));
    }

    check_equal(cnet_write_queue_advance(&queue, &view, span_bytes), SALTS_OK);
    check_equal(view.offset, (size_t)NATIVE_IO_VECTOR_MAX);
    check_equal(view.remaining, (size_t)(32u - NATIVE_IO_VECTOR_MAX));
    span_count = 0u;
    span_bytes = 0u;
    check_equal(cnet_write_queue_build_vector(&queue, &view, view.remaining,
                                              spans, &span_count, &span_bytes),
                SALTS_OK);
    check_equal(span_count, (size_t)(32u - NATIVE_IO_VECTOR_MAX));
    check_equal(span_bytes, (size_t)(32u - NATIVE_IO_VECTOR_MAX));
    for (size_t index = 0u; index < span_count; ++index) {
      check_equal(spans[index].length, (size_t)1u);
      check_equal(((const unsigned char *)spans[index].data)[0],
                  (unsigned char)(NATIVE_IO_VECTOR_MAX + index + 1u));
    }

    check_equal(cnet_write_queue_advance(&queue, &view, span_bytes), SALTS_OK);
    check_equal(view.remaining, (size_t)0u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 1);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("rejects invalid retained vectors without acquiring backing references") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 1u, 16u};
    const cnet_session_handle connection = {1u, 13u};
    cnet_write_queue_free_probe free_probe;
    mem_buffer_t *buffer;
    mem_slice_t segments[2];
    cnet_write_handle handle = {1u, 1u};

    atomic_init(&free_probe.freed, 0);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    buffer = cnet_write_queue_external(8u, 0x33u, &free_probe);
    check_true(buffer != NULL);
    segments[0] = (mem_slice_t){mem_buffer_data(buffer), 2u, buffer};
    segments[1] = (mem_slice_t){mem_buffer_data(buffer) + 8u, 1u, buffer};
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));

    check_equal(cnet_write_queue_enqueue_slicev(&queue, connection, segments, 2u, false, &handle),
                SALTS_EINVAL);
    check_false(cnet_write_handle_valid(handle));
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));
    check_equal(cnet_write_queue_enqueue_slicev(&queue, connection, segments, 0u, false, &handle),
                SALTS_EINVAL);
    check_equal(cnet_write_queue_enqueue_slicev(&queue, connection, segments,
                                                CNET_RETAINED_VECTOR_MAX + 1u,
                                                false, &handle),
                SALTS_EINVAL);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));

    mem_buffer_release(buffer);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 1);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("preserves FIFO across retained contiguous and retained-vector writes") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 4u, 32u};
    const cnet_session_handle connection = {1u, 14u};
    const unsigned char leading[] = {0x01u, 0x02u};
    cnet_write_queue_free_probe retained_free;
    cnet_write_queue_free_probe vector_free;
    mem_buffer_t *leading_buffer = cnet_write_queue_bytes(leading, sizeof(leading));
    mem_buffer_t *retained;
    mem_buffer_t *vector_buffer;
    mem_slice_t vector_slices[2];
    cnet_write_handle handle = {0};
    cnet_write_view view = {0};
    native_io_buffer_span spans[NATIVE_IO_VECTOR_MAX] = {{0}};
    size_t span_count = 0u;
    size_t span_bytes = 0u;

    atomic_init(&retained_free.freed, 0);
    atomic_init(&vector_free.freed, 0);
    check_true(leading_buffer != NULL);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);

    retained = cnet_write_queue_external(2u, 0x22u, &retained_free);
    vector_buffer = cnet_write_queue_external(4u, 0u, &vector_free);
    check_true(retained != NULL);
    check_true(vector_buffer != NULL);
    mem_buffer_data(vector_buffer)[0] = 0x31;
    mem_buffer_data(vector_buffer)[1] = 0x32;
    mem_buffer_data(vector_buffer)[2] = 0x33;
    mem_buffer_data(vector_buffer)[3] = 0x34;
    vector_slices[0] = mem_slice(vector_buffer, 0u, 2u);
    vector_slices[1] = mem_slice(vector_buffer, 2u, 2u);

    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, leading_buffer, false, &handle), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, retained, false, &handle), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_slicev(&queue, connection, vector_slices, 2u, false, &handle), SALTS_OK);
    mem_buffer_release(leading_buffer);
    mem_buffer_release(retained);
    for (size_t index = 0u; index < 2u; ++index) mem_slice_release(&vector_slices[index]);
    mem_buffer_release(vector_buffer);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_false(view.vector_write);
    check_equal(view.size, sizeof(leading));
    check_equal(((const unsigned char *)view.data)[0], 0x01u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_false(view.vector_write);
    check_equal(view.size, (size_t)2u);
    check_equal(((const unsigned char *)view.data)[0], 0x22u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(atomic_load_explicit(&retained_free.freed, memory_order_acquire), 1);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_true(view.vector_write);
    check_equal(cnet_write_queue_build_vector(&queue, &view, view.remaining, spans,
                                              &span_count, &span_bytes), SALTS_OK);
    check_equal(span_count, (size_t)2u);
    check_equal(span_bytes, (size_t)4u);
    check_equal(((const unsigned char *)spans[0].data)[0], 0x31u);
    check_equal(((const unsigned char *)spans[1].data)[0], 0x33u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(atomic_load_explicit(&vector_free.freed, memory_order_acquire), 1);

    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("resumes inside a retained range across the native vector window boundary") {
    enum { RANGE_COUNT = 17u, RANGE_BYTES = 2u };
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 1u, 64u};
    const cnet_session_handle connection = {1u, 18u};
    cnet_write_queue_free_probe free_probe;
    mem_buffer_t *buffer;
    mem_slice_t slices[RANGE_COUNT];
    cnet_write_handle handle = {0};
    cnet_write_view view = {0};
    native_io_buffer_span spans[NATIVE_IO_VECTOR_MAX] = {{0}};
    size_t span_count = 0u;
    size_t span_bytes = 0u;

    atomic_init(&free_probe.freed, 0);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    buffer = cnet_write_queue_external(RANGE_COUNT * RANGE_BYTES, 0u, &free_probe);
    check_true(buffer != NULL);
    for (size_t index = 0u; index < RANGE_COUNT * RANGE_BYTES; ++index)
      mem_buffer_data(buffer)[index] = (char)(index + 1u);
    for (size_t index = 0u; index < RANGE_COUNT; ++index)
      slices[index] = mem_slice(buffer, index * RANGE_BYTES, RANGE_BYTES);

    check_equal(cnet_write_queue_enqueue_slicev(&queue, connection, slices,
                                                RANGE_COUNT, false, &handle),
                SALTS_OK);
    for (size_t index = 0u; index < RANGE_COUNT; ++index)
      mem_slice_release(&slices[index]);
    mem_buffer_release(buffer);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    /*
     * Limit the first native submission to 31 bytes: 15 complete 2-byte ranges
     * plus one byte of range 16. This fills all 16 NativeIO spans while leaving
     * the logical cursor inside range 16.
     */
    check_equal(cnet_write_queue_build_vector(&queue, &view, 31u, spans,
                                              &span_count, &span_bytes),
                SALTS_OK);
    check_equal(span_count, (size_t)NATIVE_IO_VECTOR_MAX);
    check_equal(span_bytes, (size_t)31u);
    check_equal(spans[NATIVE_IO_VECTOR_MAX - 1u].length, (size_t)1u);
    check_equal(((const unsigned char *)
                     spans[NATIVE_IO_VECTOR_MAX - 1u].data)[0],
                (unsigned char)31u);

    check_equal(cnet_write_queue_advance(&queue, &view, span_bytes), SALTS_OK);
    check_equal(view.offset, (size_t)31u);
    check_equal(view.remaining, (size_t)3u);

    span_count = 0u;
    span_bytes = 0u;
    check_equal(cnet_write_queue_build_vector(&queue, &view, view.remaining,
                                              spans, &span_count, &span_bytes),
                SALTS_OK);
    check_equal(span_count, (size_t)2u);
    check_equal(span_bytes, (size_t)3u);
    /* Resume with the second byte of range 16, then the complete range 17. */
    check_equal(spans[0].length, (size_t)1u);
    check_equal(((const unsigned char *)spans[0].data)[0], (unsigned char)32u);
    check_equal(spans[1].length, (size_t)2u);
    check_equal(((const unsigned char *)spans[1].data)[0], (unsigned char)33u);
    check_equal(((const unsigned char *)spans[1].data)[1], (unsigned char)34u);

    check_equal(cnet_write_queue_advance(&queue, &view, span_bytes), SALTS_OK);
    check_equal(view.remaining, (size_t)0u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 1);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("preserves retained FIFO around one multi-window logical write") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 4u, 96u};
    const cnet_session_handle connection = {1u, 17u};
    const unsigned char before = 0xa1u;
    const unsigned char after = 0xb2u;
    cnet_write_queue_free_probe free_probe;
    mem_buffer_t *before_buffer = cnet_write_queue_bytes(&before, 1u);
    mem_buffer_t *after_buffer = cnet_write_queue_bytes(&after, 1u);
    mem_buffer_t *buffer;
    mem_slice_t slices[32];
    cnet_write_handle before_handle = {0};
    cnet_write_handle vector_handle = {0};
    cnet_write_handle after_handle = {0};
    cnet_write_view view = {0};
    native_io_buffer_span spans[NATIVE_IO_VECTOR_MAX] = {{0}};
    size_t span_count = 0u;
    size_t span_bytes = 0u;

    atomic_init(&free_probe.freed, 0);
    check_true(before_buffer != NULL);
    check_true(after_buffer != NULL);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    buffer = cnet_write_queue_external(32u, 0u, &free_probe);
    check_true(buffer != NULL);
    for (size_t index = 0u; index < 32u; ++index) {
      mem_buffer_data(buffer)[index] = (char)(index + 1u);
      slices[index] = mem_slice(buffer, index, 1u);
    }

    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, before_buffer, false, &before_handle), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_slicev(&queue, connection, slices, 32u, false, &vector_handle), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, after_buffer, false, &after_handle), SALTS_OK);
    mem_buffer_release(before_buffer);
    mem_buffer_release(after_buffer);
    for (size_t index = 0u; index < 32u; ++index) mem_slice_release(&slices[index]);
    mem_buffer_release(buffer);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_equal(view.handle.slot, before_handle.slot);
    check_false(view.vector_write);
    check_equal(((const unsigned char *)view.data)[0], before);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_equal(view.handle.slot, vector_handle.slot);
    check_true(view.vector_write);
    check_equal(cnet_write_queue_build_vector(&queue, &view, view.remaining, spans,
                                              &span_count, &span_bytes), SALTS_OK);
    check_equal(span_count, (size_t)NATIVE_IO_VECTOR_MAX);
    check_equal(cnet_write_queue_advance(&queue, &view, span_bytes), SALTS_OK);
    check_equal(view.handle.slot, vector_handle.slot);
    check_true(view.vector_write);
    span_count = 0u;
    span_bytes = 0u;
    check_equal(cnet_write_queue_build_vector(&queue, &view, view.remaining, spans,
                                              &span_count, &span_bytes), SALTS_OK);
    check_equal(span_count, (size_t)(32u - NATIVE_IO_VECTOR_MAX));
    check_equal(cnet_write_queue_advance(&queue, &view, span_bytes), SALTS_OK);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 1);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_equal(view.handle.slot, after_handle.slot);
    check_false(view.vector_write);
    check_equal(((const unsigned char *)view.data)[0], after);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);

    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("releases retained-vector ownership exactly once on tail cancel and discard") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 4u, 32u};
    const cnet_session_handle connection = {1u, 15u};
    const unsigned char head_byte = 0x41u;
    cnet_write_queue_free_probe first_free;
    cnet_write_queue_free_probe second_free;
    mem_buffer_t *head_buffer = cnet_write_queue_bytes(&head_byte, 1u);
    mem_buffer_t *first;
    mem_buffer_t *second;
    mem_slice_t first_slices[2];
    mem_slice_t second_slices[2];
    cnet_write_handle head = {0};
    cnet_write_handle first_handle = {0};
    cnet_write_handle second_handle = {0};
    cnet_write_view view = {0};
    size_t discarded = 0u;

    atomic_init(&first_free.freed, 0);
    atomic_init(&second_free.freed, 0);
    check_true(head_buffer != NULL);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    first = cnet_write_queue_external(4u, 0x51u, &first_free);
    second = cnet_write_queue_external(4u, 0x61u, &second_free);
    check_true(first != NULL);
    check_true(second != NULL);
    first_slices[0] = mem_slice(first, 0u, 2u);
    first_slices[1] = mem_slice(first, 2u, 2u);
    second_slices[0] = mem_slice(second, 0u, 2u);
    second_slices[1] = mem_slice(second, 2u, 2u);

    check_equal(cnet_write_queue_enqueue_buffer(&queue, connection, head_buffer, false, &head), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_slicev(&queue, connection, first_slices, 2u, false, &first_handle), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_slicev(&queue, connection, second_slices, 2u, false, &second_handle), SALTS_OK);
    mem_buffer_release(head_buffer);
    for (size_t index = 0u; index < 2u; ++index) {
      mem_slice_release(&first_slices[index]);
      mem_slice_release(&second_slices[index]);
    }
    mem_buffer_release(first);
    mem_buffer_release(second);

    check_equal(cnet_write_queue_cancel_tail(&queue, connection, second_handle), SALTS_OK);
    check_equal(atomic_load_explicit(&second_free.freed, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&first_free.freed, memory_order_acquire), 0);

    check_equal(cnet_write_queue_discard(&queue, connection, true, &discarded), SALTS_OK);
    check_equal(discarded, (size_t)1u);
    check_equal(atomic_load_explicit(&first_free.freed, memory_order_acquire), 1);

    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_equal(view.handle.slot, head.slot);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

}
