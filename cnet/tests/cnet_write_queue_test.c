#include "cnet_write_queue.h"
#include "tinytest.h"

#include <salts_buffer.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

spec("CNet bounded write ownership queue") {
  it("copies payloads and preserves independent per-connection FIFO order") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {2u, 4u, 16u, 32u};
    const cnet_session_handle first = {1u, 7u};
    const cnet_session_handle second = {2u, 3u};
    unsigned char a[] = {1u, 2u, 3u};
    const unsigned char b[] = {4u, 5u};
    const unsigned char c[] = {8u, 9u};
    cnet_write_handle handle = {0};
    cnet_write_view view = {0};

    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_copy(&queue, first, a, sizeof(a), false, &handle),
                SALTS_OK);
    check_true(cnet_write_handle_valid(handle));
    memset(a, 0u, sizeof(a));
    check_equal(cnet_write_queue_enqueue_copy(&queue, first, b, sizeof(b), true, &handle),
                SALTS_OK);
    check_equal(cnet_write_queue_enqueue_copy(&queue, second, c, sizeof(c), false, &handle),
                SALTS_OK);

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
    check_equal(view.size, sizeof(c));
    check_equal(((const unsigned char *)view.data)[1], 9u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(cnet_write_queue_peek(&queue, first, &view), SALTS_ETIMEDOUT);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("retains external buffers only after successful bounded admission") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 1u, 8u, 8u};
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

  it("bounds copied bytes independently from retained payload ownership") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {2u, 2u, 8u, 8u};
    const cnet_session_handle first = {1u, 1u};
    const cnet_session_handle second = {2u, 1u};
    const unsigned char copied[8] = {0};
    cnet_write_queue_free_probe free_probe;
    mem_buffer_t *retained;
    cnet_write_handle handle = {0};
    cnet_write_view view = {0};
    cnet_write_queue_stats stats = {0};

    atomic_init(&free_probe.freed, 0);
    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_copy(&queue, first, copied, sizeof(copied), false, &handle),
                SALTS_OK);
    check_equal(cnet_write_queue_enqueue_copy(&queue, second, copied, 1u, false, &handle),
                SALTS_ENOBUFS);

    retained = cnet_write_queue_external(4u, 0x7fu, &free_probe);
    check_true(retained != NULL);
    check_equal(cnet_write_queue_enqueue_buffer(&queue, second, retained, false, &handle),
                SALTS_OK);
    mem_buffer_release(retained);
    check_true(cnet_write_queue_get_stats(&queue, &stats));
    check_equal(stats.live_writes, (size_t)2u);
    check_equal(stats.copied_bytes, sizeof(copied));
    check_equal(stats.peak_copied_bytes, sizeof(copied));

    check_equal(cnet_write_queue_peek(&queue, first, &view), SALTS_OK);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(cnet_write_queue_peek(&queue, second, &view), SALTS_OK);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 1);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }

  it("discards queued tails while preserving an active FIFO head") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 4u, 8u, 32u};
    const cnet_session_handle connection = {1u, 9u};
    const unsigned char a = 1u;
    const unsigned char b = 2u;
    const unsigned char d = 3u;
    cnet_write_handle first = {0};
    cnet_write_handle second = {0};
    cnet_write_handle third = {0};
    cnet_write_view view = {0};
    size_t count = 0u;
    size_t discarded = 0u;

    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_copy(&queue, connection, &a, 1u, false, &first), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_copy(&queue, connection, &b, 1u, false, &second), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_copy(&queue, connection, &d, 1u, false, &third), SALTS_OK);
    check_equal(cnet_write_queue_count(&queue, connection, &count), SALTS_OK);
    check_equal(count, (size_t)3u);

    check_equal(cnet_write_queue_discard(&queue, connection, true, &discarded), SALTS_OK);
    check_equal(discarded, (size_t)2u);
    check_equal(cnet_write_queue_count(&queue, connection, &count), SALTS_OK);
    check_equal(count, (size_t)1u);
    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_equal(view.handle.slot, first.slot);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);

    check_equal(cnet_write_queue_enqueue_copy(&queue, connection, &a, 1u, false, &first), SALTS_OK);
    check_equal(cnet_write_queue_enqueue_copy(&queue, connection, &b, 1u, false, &second), SALTS_OK);
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

  it("copies vectors today without claiming native scatter gather") {
    cnet_write_queue queue = {0};
    const cnet_write_queue_config config = {1u, 2u, 16u, 16u};
    const cnet_session_handle connection = {1u, 4u};
    const unsigned char a[] = {1u, 2u};
    const unsigned char b[] = {3u, 4u, 5u};
    const cnet_const_buffer segments[] = {{a, sizeof(a)}, {b, sizeof(b)}};
    cnet_write_handle handle = {0};
    cnet_write_view view = {0};

    check_equal(cnet_write_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_write_queue_enqueuev_copy(&queue, connection, segments, 2u, false, &handle),
                SALTS_OK);
    check_equal(cnet_write_queue_peek(&queue, connection, &view), SALTS_OK);
    check_equal(view.size, (size_t)5u);
    check_equal(((const unsigned char *)view.data)[0], 1u);
    check_equal(((const unsigned char *)view.data)[4], 5u);
    check_equal(cnet_write_queue_settle(&queue, &view), SALTS_OK);
    check_equal(cnet_write_queue_close(&queue), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&queue), SALTS_OK);
  }
}
