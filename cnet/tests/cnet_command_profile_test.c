#include "cnet_command.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

static cnet_command_queue queue;

spec("CNet command queue diagnostic profile") {
  before_each() { memset(&queue, 0, sizeof(queue)); }

  after_each() {
    if (queue.impl != NULL) {
      int status = cnet_command_queue_close(&queue);
      check_true(status == SALTS_OK || status == SALTS_EALREADY);
      check_equal(cnet_command_queue_destroy(&queue), SALTS_OK);
    }
  }

  it("separates payload copy from publication control") {
    static const uint8_t payload[4096] = {1u};
    const cnet_command_queue_config config = {
        .capacity = 2u,
        .max_payload_bytes = sizeof(payload),
    };
    const cnet_command command = {
        .kind = CNET_COMMAND_SEND,
        .connection = {.slot = 1u, .generation = 1u},
        .data = payload,
        .size = sizeof(payload),
    };
    cnet_command_queue_profile profile = {0};
    cnet_command_view view = {0};

    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_command_queue_profile_begin(&queue), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
    check_equal(cnet_command_queue_profile_take(&queue, &profile), SALTS_OK);

    check_equal(profile.publish_calls, UINT64_C(1));
    check_equal(profile.payload_copy_calls, UINT64_C(1));
    check_true(profile.publish_ns >= profile.payload_copy_ns);
    check_true(profile.payload_copy_ns > UINT64_C(0));

    check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
    check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
  }
}
