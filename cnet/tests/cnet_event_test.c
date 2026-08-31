#include "cnet_event.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

static cnet_event_queue events;

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
}
