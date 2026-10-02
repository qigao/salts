#include "cnet_owner_handoff.h"
#include "tinytest.h"

#include <salts/thread.h>
#include <salts_buffer.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct handoff_free_probe {
  atomic_int freed;
} handoff_free_probe;

typedef struct handoff_producer {
  cnet_owner_handoff *handoff;
  cnet_session_handle connection;
  mem_buffer_t *buffer;
  atomic_bool *start;
  size_t count;
  int status;
} handoff_producer;

static void handoff_test_free(void *data, void *user) {
  handoff_free_probe *probe = (handoff_free_probe *)user;
  free(data);
  atomic_fetch_add_explicit(&probe->freed, 1, memory_order_release);
}

static mem_buffer_t *handoff_external(size_t size, unsigned char value,
                                      handoff_free_probe *probe) {
  unsigned char *data = (unsigned char *)malloc(size);
  mem_buffer_t *buffer;
  if (data == NULL) return NULL;
  memset(data, value, size);
  buffer = mem_wrap_external(data, size, handoff_test_free, probe);
  if (buffer == NULL) free(data);
  return buffer;
}

static mem_buffer_t *handoff_bytes(const void *data, size_t size) {
  mem_buffer_t *buffer;
  if (data == NULL || size == 0u) return NULL;
  buffer = mem_get_buffer(mem_global(), size);
  if (buffer == NULL) return NULL;
  memcpy(mem_buffer_data(buffer), data, size);
  mem_set_used(buffer, size);
  return buffer;
}

static void handoff_producer_run(void *user) {
  handoff_producer *producer = (handoff_producer *)user;
  producer->status = SALTS_OK;
  while (!atomic_load_explicit(producer->start, memory_order_acquire))
    salts_thread_yield();
  for (size_t index = 0u; index < producer->count; ++index) {
    const int status = cnet_owner_handoff_publish_buffer(
        producer->handoff, producer->connection, producer->buffer, false);
    if (status != SALTS_OK) {
      producer->status = status;
      return;
    }
  }
}

spec("CNet private retained owner handoff") {
  it("retains accepted buffers and leaves full rejection ownership unchanged") {
    cnet_owner_handoff handoff = {0};
    const cnet_owner_handoff_config config = {2u, 16u};
    const cnet_session_handle connection = {1u, 1u};
    handoff_free_probe first_free;
    handoff_free_probe second_free;
    handoff_free_probe rejected_free;
    mem_buffer_t *first;
    mem_buffer_t *second;
    mem_buffer_t *rejected;
    cnet_owner_handoff_view view = {0};
    cnet_owner_handoff_stats stats = {0};

    atomic_init(&first_free.freed, 0);
    atomic_init(&second_free.freed, 0);
    atomic_init(&rejected_free.freed, 0);
    first = handoff_external(4u, 0x11u, &first_free);
    second = handoff_external(4u, 0x22u, &second_free);
    rejected = handoff_external(4u, 0x33u, &rejected_free);
    check_not_null(first);
    check_not_null(second);
    check_not_null(rejected);

    check_equal(cnet_owner_handoff_init(&handoff, &config), SALTS_OK);
    check_equal(cnet_owner_handoff_publish_buffer(
                    &handoff, connection, first, false),
                SALTS_OK);
    check_equal(cnet_owner_handoff_publish_buffer(
                    &handoff, connection, second, false),
                SALTS_OK);
    check_equal(mem_buffer_ref_count(first), UINT32_C(2));
    check_equal(mem_buffer_ref_count(second), UINT32_C(2));
    check_equal(cnet_owner_handoff_publish_buffer(
                    &handoff, connection, rejected, false),
                SALTS_ENOBUFS);
    check_equal(mem_buffer_ref_count(rejected), UINT32_C(1));

    check_true(cnet_owner_handoff_get_stats(&handoff, &stats));
    check_equal(stats.live, (size_t)2u);
    check_equal(stats.peak, (size_t)2u);
    check_equal(stats.accepted, UINT64_C(2));
    check_equal(stats.rejected_full, UINT64_C(1));
    check_true(stats.admission_open);

    check_equal(cnet_owner_handoff_take(&handoff, &view), SALTS_OK);
    check_equal(view.backing, first);
    check_equal(cnet_owner_handoff_take(&handoff, &view), SALTS_EBUSY);
    check_equal(cnet_owner_handoff_release(&handoff, &view), SALTS_OK);
    check_equal(mem_buffer_ref_count(first), UINT32_C(1));

    check_equal(cnet_owner_handoff_take(&handoff, &view), SALTS_OK);
    check_equal(view.backing, second);
    check_equal(cnet_owner_handoff_release(&handoff, &view), SALTS_OK);
    check_equal(mem_buffer_ref_count(second), UINT32_C(1));

    mem_buffer_release(first);
    mem_buffer_release(second);
    mem_buffer_release(rejected);
    check_equal(atomic_load_explicit(&first_free.freed, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&second_free.freed, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&rejected_free.freed, memory_order_acquire), 1);

    check_equal(cnet_owner_handoff_close(&handoff), SALTS_OK);
    check_equal(cnet_owner_handoff_take(&handoff, &view), SALTS_EOF);
    check_equal(cnet_owner_handoff_destroy(&handoff), SALTS_OK);
  }

  it("preserves canonical slice metadata through write-queue ownership transfer") {
    cnet_owner_handoff handoff = {0};
    cnet_write_queue writes = {0};
    const cnet_owner_handoff_config handoff_config = {4u, 16u};
    const cnet_write_queue_config write_config = {1u, 2u, 16u};
    const cnet_session_handle connection = {1u, 7u};
    handoff_free_probe free_probe;
    mem_buffer_t *buffer;
    mem_slice_t slice;
    cnet_owner_handoff_view handoff_view = {0};
    cnet_write_handle handle = {0};
    cnet_write_view write_view = {0};
    const char *original;

    atomic_init(&free_probe.freed, 0);
    buffer = handoff_external(8u, 0u, &free_probe);
    check_not_null(buffer);
    for (size_t index = 0u; index < 8u; ++index)
      mem_buffer_data(buffer)[index] = (char)(index + 1u);
    original = mem_buffer_const_data(buffer);
    slice = mem_slice(buffer, 2u, 4u);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));

    check_equal(cnet_owner_handoff_init(&handoff, &handoff_config), SALTS_OK);
    check_equal(cnet_write_queue_init(&writes, &write_config), SALTS_OK);
    check_equal(cnet_owner_handoff_publish_slice(
                    &handoff, connection, &slice, false),
                SALTS_OK);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(3));
    mem_slice_release(&slice);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));

    check_equal(cnet_owner_handoff_take(&handoff, &handoff_view), SALTS_OK);
    check_equal(handoff_view.kind, CNET_OWNER_HANDOFF_SEND_SLICE);
    check_equal(handoff_view.backing, buffer);
    check_equal(handoff_view.offset, (size_t)2u);
    check_equal(handoff_view.size, (size_t)4u);
    check_equal(cnet_owner_handoff_transfer_write(
                    &handoff, &handoff_view, &writes, &handle),
                SALTS_OK);
    check_true(cnet_write_handle_valid(handle));
    check_equal(handoff_view._sequence, UINT64_C(0));
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));

    mem_buffer_release(buffer);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 0);
    check_equal(cnet_write_queue_peek(&writes, connection, &write_view), SALTS_OK);
    check_equal(write_view.data, original + 2);
    check_equal(write_view.size, (size_t)4u);
    check_equal(((const unsigned char *)write_view.data)[0], 3u);
    check_equal(((const unsigned char *)write_view.data)[3], 6u);
    check_equal(cnet_write_queue_settle(&writes, &write_view), SALTS_OK);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 1);

    check_equal(cnet_write_queue_close(&writes), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&writes), SALTS_OK);
    check_equal(cnet_owner_handoff_close(&handoff), SALTS_OK);
    check_equal(cnet_owner_handoff_destroy(&handoff), SALTS_OK);
  }

  it("keeps the borrowed descriptor live when the owner write FIFO is full") {
    cnet_owner_handoff handoff = {0};
    cnet_write_queue writes = {0};
    const cnet_owner_handoff_config handoff_config = {2u, 16u};
    const cnet_write_queue_config write_config = {1u, 1u, 16u};
    const cnet_session_handle connection = {1u, 2u};
    const unsigned char first_bytes[] = {1u, 2u};
    const unsigned char second_bytes[] = {3u, 4u};
    mem_buffer_t *first = handoff_bytes(first_bytes, sizeof(first_bytes));
    mem_buffer_t *second = handoff_bytes(second_bytes, sizeof(second_bytes));
    cnet_write_handle first_handle = {0};
    cnet_write_handle second_handle = {0};
    cnet_write_view write_view = {0};
    cnet_owner_handoff_view handoff_view = {0};

    check_not_null(first);
    check_not_null(second);
    check_equal(cnet_owner_handoff_init(&handoff, &handoff_config), SALTS_OK);
    check_equal(cnet_write_queue_init(&writes, &write_config), SALTS_OK);

    check_equal(cnet_write_queue_enqueue_buffer(
                    &writes, connection, first, false, &first_handle),
                SALTS_OK);
    check_equal(cnet_owner_handoff_publish_buffer(
                    &handoff, connection, second, false),
                SALTS_OK);
    check_equal(mem_buffer_ref_count(second), UINT32_C(2));
    check_equal(cnet_owner_handoff_take(&handoff, &handoff_view), SALTS_OK);

    check_equal(cnet_owner_handoff_transfer_write(
                    &handoff, &handoff_view, &writes, &second_handle),
                SALTS_ENOBUFS);
    check_equal(handoff_view.kind, CNET_OWNER_HANDOFF_SEND_BUFFER);
    check_true(handoff_view._sequence != 0u);
    check_equal(mem_buffer_ref_count(second), UINT32_C(2));

    check_equal(cnet_write_queue_peek(&writes, connection, &write_view), SALTS_OK);
    check_equal(cnet_write_queue_settle(&writes, &write_view), SALTS_OK);
    check_equal(cnet_owner_handoff_transfer_write(
                    &handoff, &handoff_view, &writes, &second_handle),
                SALTS_OK);
    check_true(cnet_write_handle_valid(second_handle));
    check_equal(mem_buffer_ref_count(second), UINT32_C(2));

    mem_buffer_release(first);
    mem_buffer_release(second);
    check_equal(cnet_write_queue_peek(&writes, connection, &write_view), SALTS_OK);
    check_equal(write_view.size, sizeof(second_bytes));
    check_equal(cnet_write_queue_settle(&writes, &write_view), SALTS_OK);
    check_equal(cnet_write_queue_close(&writes), SALTS_OK);
    check_equal(cnet_write_queue_destroy(&writes), SALTS_OK);
    check_equal(cnet_owner_handoff_close(&handoff), SALTS_OK);
    check_equal(cnet_owner_handoff_destroy(&handoff), SALTS_OK);
  }

  it("carries receive demand without payload ownership") {
    cnet_owner_handoff handoff = {0};
    const cnet_owner_handoff_config config = {2u, 64u};
    const cnet_session_handle connection = {1u, 3u};
    cnet_owner_handoff_view view = {0};

    check_equal(cnet_owner_handoff_init(&handoff, &config), SALTS_OK);
    check_equal(cnet_owner_handoff_publish_receive(
                    &handoff, connection, 17u),
                SALTS_OK);
    check_equal(cnet_owner_handoff_take(&handoff, &view), SALTS_OK);
    check_equal(view.kind, CNET_OWNER_HANDOFF_RECEIVE);
    check_equal(view.connection.slot, connection.slot);
    check_equal(view.connection.generation, connection.generation);
    check_null(view.backing);
    check_equal(view.demand, (size_t)17u);
    check_equal(cnet_owner_handoff_release(&handoff, &view), SALTS_OK);
    check_equal(cnet_owner_handoff_close(&handoff), SALTS_OK);
    check_equal(cnet_owner_handoff_destroy(&handoff), SALTS_OK);
  }

  it("closes admission without leaking a rejected retain") {
    cnet_owner_handoff handoff = {0};
    const cnet_owner_handoff_config config = {2u, 16u};
    const cnet_session_handle connection = {1u, 4u};
    const unsigned char bytes[] = {5u};
    mem_buffer_t *buffer = handoff_bytes(bytes, sizeof(bytes));
    cnet_owner_handoff_stats stats = {0};

    check_not_null(buffer);
    check_equal(cnet_owner_handoff_init(&handoff, &config), SALTS_OK);
    check_equal(cnet_owner_handoff_close(&handoff), SALTS_OK);
    check_equal(cnet_owner_handoff_publish_buffer(
                    &handoff, connection, buffer, false),
                SALTS_ESHUTDOWN);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));
    check_true(cnet_owner_handoff_get_stats(&handoff, &stats));
    check_equal(stats.accepted, UINT64_C(0));
    check_equal(stats.rejected_closed, UINT64_C(1));
    check_false(stats.admission_open);
    mem_buffer_release(buffer);
    check_equal(cnet_owner_handoff_destroy(&handoff), SALTS_OK);
  }

  it("accepts concurrent MPSC retained producers with exact refcount recovery") {
    enum { PRODUCERS = 4, PER_PRODUCER = 8, TOTAL = PRODUCERS * PER_PRODUCER };
    cnet_owner_handoff handoff = {0};
    const cnet_owner_handoff_config config = {64u, 16u};
    const cnet_session_handle connection = {1u, 5u};
    const unsigned char bytes[] = {9u, 8u, 7u, 6u};
    mem_buffer_t *buffer = handoff_bytes(bytes, sizeof(bytes));
    atomic_bool start = false;
    handoff_producer producers[PRODUCERS];
    salts_thread_t threads[PRODUCERS] = {0};
    cnet_owner_handoff_view view = {0};
    cnet_owner_handoff_stats stats = {0};

    check_not_null(buffer);
    check_equal(cnet_owner_handoff_init(&handoff, &config), SALTS_OK);
    for (size_t index = 0u; index < PRODUCERS; ++index) {
      producers[index] = (handoff_producer){
          &handoff, connection, buffer, &start, PER_PRODUCER, SALTS_EIO};
      check_equal(salts_thread_create(
                      &threads[index], handoff_producer_run, &producers[index]),
                  SALTS_OK);
    }

    atomic_store_explicit(&start, true, memory_order_release);
    for (size_t index = 0u; index < PRODUCERS; ++index) {
      check_equal(salts_thread_join(&threads[index]), SALTS_OK);
      salts_thread_destroy(&threads[index]);
      check_equal(producers[index].status, SALTS_OK);
    }

    check_equal(mem_buffer_ref_count(buffer), (uint32_t)(TOTAL + 1u));
    check_true(cnet_owner_handoff_get_stats(&handoff, &stats));
    check_equal(stats.live, (size_t)TOTAL);
    check_equal(stats.accepted, (uint64_t)TOTAL);
    check_equal(stats.rejected_full, UINT64_C(0));

    for (size_t index = 0u; index < TOTAL; ++index) {
      check_equal(cnet_owner_handoff_take(&handoff, &view), SALTS_OK);
      check_equal(view.backing, buffer);
      check_equal(cnet_owner_handoff_release(&handoff, &view), SALTS_OK);
    }
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));
    mem_buffer_release(buffer);

    check_equal(cnet_owner_handoff_close(&handoff), SALTS_OK);
    check_equal(cnet_owner_handoff_destroy(&handoff), SALTS_OK);
  }
}
