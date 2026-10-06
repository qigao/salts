#include "cnet_event.h"
#include "tinytest.h"

#include <salts/thread.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static cnet_event_queue events;
enum { CNET_EVENT_TEST_PUBLISHERS = 4 };

typedef struct cnet_event_release_probe {
  cnet_event_queue *queue;
  cnet_event_view *view;
  atomic_bool *start;
  int status;
} cnet_event_release_probe;

typedef struct cnet_event_wait_probe {
  cnet_event_queue *queue;
  cnet_event_view view;
  atomic_bool running;
  atomic_bool entered;
  int status;
} cnet_event_wait_probe;

typedef struct cnet_event_publish_probe {
  cnet_event_queue *queue;
  atomic_bool *start;
  atomic_size_t *attempts;
  atomic_size_t *finished;
  int status;
} cnet_event_publish_probe;

static struct {
  atomic_bool start;
  atomic_size_t attempts;
  atomic_size_t finished;
  cnet_event_publish_probe probes[CNET_EVENT_TEST_PUBLISHERS];
  cmeta_thread_t threads[CNET_EVENT_TEST_PUBLISHERS];
  size_t started;
} publishing;

static void cnet_event_release_worker(void *context) {
  cnet_event_release_probe *probe = (cnet_event_release_probe *)context;
  while (!atomic_load_explicit(probe->start, memory_order_acquire))
    cmeta_thread_yield();
  probe->status = cnet_event_queue_release(probe->queue, probe->view);
}

static int cnet_event_wait_keep_running(void *context) {
  cnet_event_wait_probe *probe = (cnet_event_wait_probe *)context;
  atomic_store_explicit(&probe->entered, true, memory_order_release);
  return atomic_load_explicit(&probe->running, memory_order_acquire);
}

static void cnet_event_wait_worker(void *context) {
  cnet_event_wait_probe *probe = (cnet_event_wait_probe *)context;
  probe->status =
      cnet_event_queue_take_wait(probe->queue, &probe->view, cnet_event_wait_keep_running, probe);
}

static void cnet_event_publish_worker(void *context) {
  cnet_event_publish_probe *probe = (cnet_event_publish_probe *)context;
  const cnet_event event = {CNET_EVENT_STATE,
                            {1u, 1u},
                            CNET_EVENT_STATE_CONNECTED,
                            SALTS_OK,
                            CNET_SESSION_STAGE_NONE,
                            NULL,
                            0u};
  while (!atomic_load_explicit(probe->start, memory_order_acquire))
    cmeta_thread_yield();
  for (;;) {
    const int status = cnet_event_queue_publish(probe->queue, &event);
    atomic_fetch_add_explicit(probe->attempts, 1u, memory_order_release);
    if (status == SALTS_OK || status == SALTS_ENOBUFS) continue;
    probe->status = status == SALTS_ESHUTDOWN || status == SALTS_EINVAL ? SALTS_OK : status;
    atomic_fetch_add_explicit(probe->finished, 1u, memory_order_release);
    return;
  }
}

static void cnet_event_join_publishers(void) {
  while (publishing.started != 0u) {
    const size_t index = publishing.started - 1u;
    check_equal(cmeta_thread_join(&publishing.threads[index]), SALTS_OK);
    cmeta_thread_destroy(&publishing.threads[index]);
    --publishing.started;
  }
}

spec("CNet bounded callback events") {
  before_each() { memset(&events, 0, sizeof(events)); }

  after_each() {
    if (publishing.started != 0u) {
      atomic_store_explicit(&publishing.start, true, memory_order_release);
      if (events.impl != NULL) (void)cnet_event_queue_close(&events);
      cnet_event_join_publishers();
    }
    if (events.impl != NULL) {
      int status = cnet_event_queue_close(&events);
      check_true(status == SALTS_OK || status == SALTS_EALREADY);
      check_equal(cnet_event_queue_destroy(&events), SALTS_OK);
    }
  }

  it("bounds copied payload bytes independently of event slots") {
    static const uint8_t six_bytes[] = {1u, 2u, 3u, 4u, 5u, 6u};
    static const uint8_t five_bytes[] = {7u, 8u, 9u, 10u, 11u};
    const cnet_event_queue_config config = {
        .capacity = 8u, .data_capacity = 4u, .max_payload_bytes = 8u, .payload_capacity_bytes = 10u};
    cnet_event event = {CNET_EVENT_RECEIVE,      {1u, 1u}, CNET_EVENT_STATE_NONE, SALTS_OK,
                        CNET_SESSION_STAGE_NONE, six_bytes, sizeof(six_bytes)};
    cnet_event_view view = {0};
    cnet_event_queue_stats stats = {0};

    check_equal(cnet_event_queue_init(&events, &config), SALTS_OK);
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    event.data = five_bytes;
    event.size = sizeof(five_bytes);
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_ENOBUFS);
    check_true(cnet_event_queue_get_stats(&events, &stats));
    check_equal(stats.live_payload_bytes, sizeof(six_bytes));
    check_equal(stats.peak_payload_bytes, sizeof(six_bytes));
    check_equal(stats.rejected_payload_bytes, sizeof(five_bytes));

    check_equal(cnet_event_queue_take(&events, &view), SALTS_OK);
    check_equal(cnet_event_queue_release(&events, &view), SALTS_OK);
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    check_true(cnet_event_queue_get_stats(&events, &stats));
    check_equal(stats.live_payload_bytes, sizeof(five_bytes));
    check_equal(stats.peak_payload_bytes, sizeof(six_bytes));
    check_equal(cnet_event_queue_take(&events, &view), SALTS_OK);
    check_equal(cnet_event_queue_release(&events, &view), SALTS_OK);
  }

  it("retains canonical receive backing without copying DATA") {
    static const unsigned char payload_bytes[] = "canonical-receive";
    const cnet_event_queue_config config = {
        .capacity = 4u, .data_capacity = 2u, .max_payload_bytes = 64u,
        .payload_capacity_bytes = 64u};
    mem_buffer_t *backing = mem_get_buffer(mem_global(), sizeof(payload_bytes) - 1u);
    mem_slice_t retained = {0};
    cnet_event event = {0};
    cnet_event_view view = {0};
    const void *original;

    check_not_null(backing);
    memcpy(mem_buffer_data(backing), payload_bytes, sizeof(payload_bytes) - 1u);
    mem_set_used(backing, sizeof(payload_bytes) - 1u);
    original = mem_buffer_const_data(backing);
    check_equal(mem_buffer_ref_count(backing), UINT32_C(1));

    event = (cnet_event){.kind = CNET_EVENT_RECEIVE,
                         .session = {1u, 1u},
                         .state = CNET_EVENT_STATE_NONE,
                         .status = SALTS_OK,
                         .stage = CNET_SESSION_STAGE_NONE,
                         .data = original,
                         .size = sizeof(payload_bytes) - 1u,
                         .backing = backing};

    check_equal(cnet_event_queue_init(&events, &config), SALTS_OK);
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    check_equal(mem_buffer_ref_count(backing), UINT32_C(2));

    check_equal(cnet_event_queue_take(&events, &view), SALTS_OK);
    check_true(view.data == original);
    check_true(view.backing == backing);
    check_equal(view.size, sizeof(payload_bytes) - 1u);
    check_equal(memcmp(view.data, payload_bytes, view.size), 0);

    retained = mem_slice(view.backing, 0u, view.size);
    check_not_null(retained.buffer);
    check_equal(mem_buffer_ref_count(backing), UINT32_C(3));

    check_equal(cnet_event_queue_release(&events, &view), SALTS_OK);
    check_equal(mem_buffer_ref_count(backing), UINT32_C(2));
    mem_buffer_release(backing);
    backing = NULL;

    check_equal(retained.length, sizeof(payload_bytes) - 1u);
    check_equal(memcmp(retained.data, payload_bytes, retained.length), 0);
    mem_slice_release(&retained);
  }

  it("reserves state headroom when receive data reaches its own limit") {
    static const uint8_t first[] = {1u};
    static const uint8_t second[] = {2u, 3u};
    const cnet_event_queue_config config = {8u, 2u, 8u};
    const cnet_session_handle session = {1u, 1u};
    cnet_event event = {0};
    cnet_event_view view = {0};

    check_equal(cnet_event_queue_init(&events, &config), SALTS_OK);
    event = (cnet_event){CNET_EVENT_RECEIVE,      session, CNET_EVENT_STATE_NONE, SALTS_OK,
                         CNET_SESSION_STAGE_NONE, first,   sizeof(first)};
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    event.data = second;
    event.size = sizeof(second);
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_ENOBUFS);

    event = (cnet_event){CNET_EVENT_STATE,
                         session,
                         CNET_EVENT_STATE_CONNECTED,
                         SALTS_OK,
                         CNET_SESSION_STAGE_NONE,
                         NULL,
                         0u};
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    event.state = CNET_EVENT_STATE_CLOSING;
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    event.state = CNET_EVENT_STATE_TLS_HANDSHAKING;
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    event.state = CNET_EVENT_STATE_CLOSED;
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);

    check_equal(cnet_event_queue_take(&events, &view), SALTS_OK);
    check_equal(view.kind, CNET_EVENT_RECEIVE);
    check_equal(view.data, first, sizeof(first));
    check_equal(cnet_event_queue_release(&events, &view), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &view), SALTS_OK);
    check_equal(view.data, second, sizeof(second));
    check_equal(cnet_event_queue_release(&events, &view), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &view), SALTS_OK);
    check_equal(view.state, CNET_EVENT_STATE_CONNECTED);
    check_equal(cnet_event_queue_release(&events, &view), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &view), SALTS_OK);
    check_equal(view.state, CNET_EVENT_STATE_CLOSING);
    check_equal(cnet_event_queue_release(&events, &view), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &view), SALTS_OK);
    check_equal(view.state, CNET_EVENT_STATE_TLS_HANDSHAKING);
    check_equal(cnet_event_queue_release(&events, &view), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &view), SALTS_OK);
    check_equal(view.state, CNET_EVENT_STATE_CLOSED);
    check_equal(cnet_event_queue_release(&events, &view), SALTS_OK);
  }

  it("closes admission and reports EOF after draining") {
    const cnet_event_queue_config config = {4u, 1u, 4u};
    const cnet_event event = {CNET_EVENT_STATE,
                              {1u, 1u},
                              CNET_EVENT_STATE_FAILED,
                              SALTS_EIO,
                              CNET_SESSION_STAGE_READ,
                              NULL,
                              0u};
    cnet_event_view view = {0};

    check_equal(cnet_event_queue_init(&events, &config), SALTS_OK);
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    check_equal(cnet_event_queue_close(&events), SALTS_OK);
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_ESHUTDOWN);
    check_equal(cnet_event_queue_take(&events, &view), SALTS_OK);
    check_equal(view.status, SALTS_EIO);
    check_equal(view.stage, CNET_SESSION_STAGE_READ);
    check_equal(cnet_event_queue_release(&events, &view), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &view), SALTS_EOF);
  }

  it("allows dispatcher consumers to release borrowed views concurrently") {
    static const uint8_t first[] = {1u};
    static const uint8_t second[] = {2u};
    const cnet_event_queue_config config = {4u, 2u, 4u};
    cnet_event event = {CNET_EVENT_RECEIVE,      {1u, 1u}, CNET_EVENT_STATE_NONE, SALTS_OK,
                        CNET_SESSION_STAGE_NONE, first,    sizeof(first)};
    cnet_event_view views[2] = {0};
    atomic_bool start = false;
    cnet_event_release_probe probes[2] = {{&events, &views[0], &start, SALTS_EIO},
                                          {&events, &views[1], &start, SALTS_EIO}};
    cmeta_thread_t threads[2] = {0};

    check_equal(cnet_event_queue_init(&events, &config), SALTS_OK);
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    event.data = second;
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &views[0]), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &views[1]), SALTS_OK);
    check_equal(cmeta_thread_create(&threads[0], cnet_event_release_worker, &probes[0]), SALTS_OK);
    check_equal(cmeta_thread_create(&threads[1], cnet_event_release_worker, &probes[1]), SALTS_OK);
    atomic_store_explicit(&start, true, memory_order_release);
    check_equal(cmeta_thread_join(&threads[1]), SALTS_OK);
    check_equal(cmeta_thread_join(&threads[0]), SALTS_OK);
    cmeta_thread_destroy(&threads[1]);
    cmeta_thread_destroy(&threads[0]);
    check_equal(probes[0].status, SALTS_OK);
    check_equal(probes[1].status, SALTS_OK);
  }

  it("keeps late MPSC publishers away from destroyed queue storage") {
    enum { MINIMUM_ATTEMPTS = 256 };
    const cnet_event_queue_config config = {1024u, 1u, 1u};
    int close_status;
    size_t index;

    atomic_store_explicit(&publishing.start, false, memory_order_relaxed);
    atomic_store_explicit(&publishing.attempts, 0u, memory_order_relaxed);
    atomic_store_explicit(&publishing.finished, 0u, memory_order_relaxed);
    check_equal(cnet_event_queue_init(&events, &config), SALTS_OK);
    for (index = 0u; index < CNET_EVENT_TEST_PUBLISHERS; ++index) {
      publishing.probes[index] = (cnet_event_publish_probe){
          &events, &publishing.start, &publishing.attempts, &publishing.finished, SALTS_EIO};
      check_equal(cmeta_thread_create(&publishing.threads[index], cnet_event_publish_worker,
                                      &publishing.probes[index]), SALTS_OK);
      ++publishing.started;
    }
    atomic_store_explicit(&publishing.start, true, memory_order_release);
    while (atomic_load_explicit(&publishing.attempts, memory_order_acquire) < MINIMUM_ATTEMPTS &&
           atomic_load_explicit(&publishing.finished, memory_order_acquire) == 0u)
      cmeta_thread_yield();

    close_status = cnet_event_queue_close(&events);
    check_true(close_status == SALTS_OK || close_status == SALTS_EBUSY);
    if (close_status == SALTS_EBUSY) {
      cnet_event_join_publishers();
      check_equal(cnet_event_queue_close(&events), SALTS_OK);
    }

    {
      const uint64_t deadline = cmeta_monotonic_ms() + 5000u;
      for (;;) {
        cnet_event_view view = {0};
        const int status = cnet_event_queue_take(&events, &view);
        if (status == SALTS_EOF) break;
        if (status == SALTS_ETIMEDOUT) {
          check_true(cmeta_monotonic_ms() < deadline);
          cmeta_thread_yield();
          continue;
        }
        check_equal(status, SALTS_OK);
        check_equal(cnet_event_queue_release(&events, &view), SALTS_OK);
      }
    }
    check_equal(cnet_event_queue_destroy(&events), SALTS_OK);

    if (close_status == SALTS_OK) cnet_event_join_publishers();
    for (index = 0u; index < CNET_EVENT_TEST_PUBLISHERS; ++index)
      check_equal(publishing.probes[index].status, SALTS_OK);
  }

  it("blocks without polling and supports an explicit stop wake") {
    const cnet_event_queue_config config = {4u, 1u, 4u};
    const cnet_event event = {CNET_EVENT_STATE,
                              {1u, 1u},
                              CNET_EVENT_STATE_CONNECTED,
                              SALTS_OK,
                              CNET_SESSION_STAGE_NONE,
                              NULL,
                              0u};
    cnet_event_wait_probe probe = {.queue = &events, .status = SALTS_EIO};
    cmeta_thread_t thread = NULL;

    atomic_init(&probe.running, true);
    atomic_init(&probe.entered, false);
    check_equal(cnet_event_queue_init(&events, &config), SALTS_OK);
    check_equal(cmeta_thread_create(&thread, cnet_event_wait_worker, &probe), SALTS_OK);
    while (!atomic_load_explicit(&probe.entered, memory_order_acquire))
      cmeta_thread_yield();
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    check_equal(cmeta_thread_join(&thread), SALTS_OK);
    cmeta_thread_destroy(&thread);
    check_equal(probe.status, SALTS_OK);
    check_equal(probe.view.state, CNET_EVENT_STATE_CONNECTED);
    check_equal(cnet_event_queue_release(&events, &probe.view), SALTS_OK);

    memset(&probe.view, 0, sizeof(probe.view));
    probe.status = SALTS_EIO;
    atomic_store_explicit(&probe.entered, false, memory_order_release);
    check_equal(cmeta_thread_create(&thread, cnet_event_wait_worker, &probe), SALTS_OK);
    while (!atomic_load_explicit(&probe.entered, memory_order_acquire))
      cmeta_thread_yield();
    atomic_store_explicit(&probe.running, false, memory_order_release);
    check_equal(cnet_event_queue_wake(&events), SALTS_OK);
    check_equal(cmeta_thread_join(&thread), SALTS_OK);
    cmeta_thread_destroy(&thread);
    check_equal(probe.status, SALTS_ECANCELED);
    check_equal(probe.view._sequence, 0u);
  }
}
