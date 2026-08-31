#include "cnet_event.h"
#include "tinytest.h"

#include <turbo/thread.h>

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

static void cnet_event_release_worker(void *context) {
  cnet_event_release_probe *probe = (cnet_event_release_probe *)context;
  while (!atomic_load_explicit(probe->start, memory_order_acquire))
    turbo_thread_yield();
  probe->status = cnet_event_queue_release(probe->queue, probe->view);
}

spec("CNet bounded callback events") {
  before_each() { memset(&events, 0, sizeof(events)); }

  after_each() {
    if (events.impl != NULL) {
      int status = cnet_event_queue_close(&events);
      check_true(status == TURBO_OK || status == TURBO_EALREADY);
      check_equal(cnet_event_queue_destroy(&events), TURBO_OK);
    }
  }

  it("reserves state headroom when receive data reaches its own limit") {
    static const uint8_t first[] = {1u};
    static const uint8_t second[] = {2u, 3u};
    const cnet_event_queue_config config = {8u, 2u, 8u};
    const cnet_session_handle session = {1u, 1u};
    cnet_event event = {0};
    cnet_event_view view = {0};

    check_equal(cnet_event_queue_init(&events, &config), TURBO_OK);
    event = (cnet_event){CNET_EVENT_RECEIVE,      session, CNET_EVENT_STATE_NONE, TURBO_OK,
                         CNET_SESSION_STAGE_NONE, first,   sizeof(first)};
    check_equal(cnet_event_queue_publish(&events, &event), TURBO_OK);
    event.data = second;
    event.size = sizeof(second);
    check_equal(cnet_event_queue_publish(&events, &event), TURBO_OK);
    check_equal(cnet_event_queue_publish(&events, &event), TURBO_ENOBUFS);

    event = (cnet_event){CNET_EVENT_STATE,
                         session,
                         CNET_EVENT_STATE_CONNECTED,
                         TURBO_OK,
                         CNET_SESSION_STAGE_NONE,
                         NULL,
                         0u};
    check_equal(cnet_event_queue_publish(&events, &event), TURBO_OK);
    event.state = CNET_EVENT_STATE_CLOSING;
    check_equal(cnet_event_queue_publish(&events, &event), TURBO_OK);
    event.state = CNET_EVENT_STATE_CLOSED;
    check_equal(cnet_event_queue_publish(&events, &event), TURBO_OK);

    check_equal(cnet_event_queue_take(&events, &view), TURBO_OK);
    check_equal(view.kind, CNET_EVENT_RECEIVE);
    check_equal(view.data, first, sizeof(first));
    check_equal(cnet_event_queue_release(&events, &view), TURBO_OK);
    check_equal(cnet_event_queue_take(&events, &view), TURBO_OK);
    check_equal(view.data, second, sizeof(second));
    check_equal(cnet_event_queue_release(&events, &view), TURBO_OK);
    check_equal(cnet_event_queue_take(&events, &view), TURBO_OK);
    check_equal(view.state, CNET_EVENT_STATE_CONNECTED);
    check_equal(cnet_event_queue_release(&events, &view), TURBO_OK);
    check_equal(cnet_event_queue_take(&events, &view), TURBO_OK);
    check_equal(view.state, CNET_EVENT_STATE_CLOSING);
    check_equal(cnet_event_queue_release(&events, &view), TURBO_OK);
    check_equal(cnet_event_queue_take(&events, &view), TURBO_OK);
    check_equal(view.state, CNET_EVENT_STATE_CLOSED);
    check_equal(cnet_event_queue_release(&events, &view), TURBO_OK);
  }

  it("closes admission and reports EOF after draining") {
    const cnet_event_queue_config config = {4u, 1u, 4u};
    const cnet_event event = {CNET_EVENT_STATE,
                              {1u, 1u},
                              CNET_EVENT_STATE_FAILED,
                              TURBO_EIO,
                              CNET_SESSION_STAGE_READ,
                              NULL,
                              0u};
    cnet_event_view view = {0};

    check_equal(cnet_event_queue_init(&events, &config), TURBO_OK);
    check_equal(cnet_event_queue_publish(&events, &event), TURBO_OK);
    check_equal(cnet_event_queue_close(&events), TURBO_OK);
    check_equal(cnet_event_queue_publish(&events, &event), TURBO_ESHUTDOWN);
    check_equal(cnet_event_queue_take(&events, &view), TURBO_OK);
    check_equal(view.status, TURBO_EIO);
    check_equal(view.stage, CNET_SESSION_STAGE_READ);
    check_equal(cnet_event_queue_release(&events, &view), TURBO_OK);
    check_equal(cnet_event_queue_take(&events, &view), TURBO_EOF);
  }

  it("allows callback workers to release borrowed views concurrently") {
    static const uint8_t first[] = {1u};
    static const uint8_t second[] = {2u};
    const cnet_event_queue_config config = {4u, 2u, 4u};
    cnet_event event = {CNET_EVENT_RECEIVE,      {1u, 1u}, CNET_EVENT_STATE_NONE, TURBO_OK,
                        CNET_SESSION_STAGE_NONE, first,    sizeof(first)};
    cnet_event_view views[2] = {0};
    atomic_bool start = false;
    cnet_event_release_probe probes[2] = {{&events, &views[0], &start, TURBO_EIO},
                                          {&events, &views[1], &start, TURBO_EIO}};
    turbo_thread_t threads[2] = {0};

    check_equal(cnet_event_queue_init(&events, &config), TURBO_OK);
    check_equal(cnet_event_queue_publish(&events, &event), TURBO_OK);
    event.data = second;
    check_equal(cnet_event_queue_publish(&events, &event), TURBO_OK);
    check_equal(cnet_event_queue_take(&events, &views[0]), TURBO_OK);
    check_equal(cnet_event_queue_take(&events, &views[1]), TURBO_OK);
    check_equal(turbo_thread_create(&threads[0], cnet_event_release_worker, &probes[0]), TURBO_OK);
    check_equal(turbo_thread_create(&threads[1], cnet_event_release_worker, &probes[1]), TURBO_OK);
    atomic_store_explicit(&start, true, memory_order_release);
    check_equal(turbo_thread_join(&threads[1]), TURBO_OK);
    check_equal(turbo_thread_join(&threads[0]), TURBO_OK);
    turbo_thread_destroy(&threads[1]);
    turbo_thread_destroy(&threads[0]);
    check_equal(probes[0].status, TURBO_OK);
    check_equal(probes[1].status, TURBO_OK);
  }
}
