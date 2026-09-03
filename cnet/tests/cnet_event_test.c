#include "cnet_event.h"
#include "tinytest.h"

#include <salts/thread.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static cnet_event_queue events;

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

static void cnet_event_release_worker(void *context) {
  cnet_event_release_probe *probe = (cnet_event_release_probe *)context;
  while (!atomic_load_explicit(probe->start, memory_order_acquire))
    salts_thread_yield();
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

spec("CNet bounded callback events") {
  before_each() { memset(&events, 0, sizeof(events)); }

  after_each() {
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
    salts_thread_t threads[2] = {0};

    check_equal(cnet_event_queue_init(&events, &config), SALTS_OK);
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    event.data = second;
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &views[0]), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &views[1]), SALTS_OK);
    check_equal(salts_thread_create(&threads[0], cnet_event_release_worker, &probes[0]), SALTS_OK);
    check_equal(salts_thread_create(&threads[1], cnet_event_release_worker, &probes[1]), SALTS_OK);
    atomic_store_explicit(&start, true, memory_order_release);
    check_equal(salts_thread_join(&threads[1]), SALTS_OK);
    check_equal(salts_thread_join(&threads[0]), SALTS_OK);
    salts_thread_destroy(&threads[1]);
    salts_thread_destroy(&threads[0]);
    check_equal(probes[0].status, SALTS_OK);
    check_equal(probes[1].status, SALTS_OK);
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
    salts_thread_t thread = NULL;

    atomic_init(&probe.running, true);
    atomic_init(&probe.entered, false);
    check_equal(cnet_event_queue_init(&events, &config), SALTS_OK);
    check_equal(salts_thread_create(&thread, cnet_event_wait_worker, &probe), SALTS_OK);
    while (!atomic_load_explicit(&probe.entered, memory_order_acquire))
      salts_thread_yield();
    check_equal(cnet_event_queue_publish(&events, &event), SALTS_OK);
    check_equal(salts_thread_join(&thread), SALTS_OK);
    salts_thread_destroy(&thread);
    check_equal(probe.status, SALTS_OK);
    check_equal(probe.view.state, CNET_EVENT_STATE_CONNECTED);
    check_equal(cnet_event_queue_release(&events, &probe.view), SALTS_OK);

    memset(&probe.view, 0, sizeof(probe.view));
    probe.status = SALTS_EIO;
    atomic_store_explicit(&probe.entered, false, memory_order_release);
    check_equal(salts_thread_create(&thread, cnet_event_wait_worker, &probe), SALTS_OK);
    while (!atomic_load_explicit(&probe.entered, memory_order_acquire))
      salts_thread_yield();
    atomic_store_explicit(&probe.running, false, memory_order_release);
    check_equal(cnet_event_queue_wake(&events), SALTS_OK);
    check_equal(salts_thread_join(&thread), SALTS_OK);
    salts_thread_destroy(&thread);
    check_equal(probe.status, SALTS_ECANCELED);
    check_equal(probe.view._sequence, 0u);
  }
}
