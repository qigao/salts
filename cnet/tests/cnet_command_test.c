#include "cnet_command.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

enum {
  TEST_COMMAND_CAPACITY = 2,
  TEST_COMMAND_SCALE = 4096,
  TEST_COMMAND_CYCLES = 2,
  TEST_PAYLOAD_CAPACITY = 64
};

static cnet_command_queue queue;

static cnet_command make_receive(uint32_t slot, size_t demand) {
  cnet_command command = {0};
  command.kind = CNET_COMMAND_RECEIVE;
  command.connection.slot = slot;
  command.connection.generation = 1u;
  command.argument = demand;
  return command;
}

static cnet_command make_close(uint32_t slot) {
  cnet_command command = {0};
  command.kind = CNET_COMMAND_CLOSE;
  command.connection.slot = slot;
  command.connection.generation = 1u;
  return command;
}

spec("CNet bounded control command queue") {
  before_each() { memset(&queue, 0, sizeof(queue)); }

  after_each() {
    if (queue.impl != NULL) {
      const int status = cnet_command_queue_close(&queue);
      check_true(status == SALTS_OK || status == SALTS_EALREADY);
      check_equal(cnet_command_queue_destroy(&queue), SALTS_OK);
    }
  }

  it("rejects invalid and overflowing queue bounds") {
    const cnet_command_queue_config non_power = {.capacity = 3u, .max_payload_bytes = 8u};
    const cnet_command_queue_config overflow = {
        .capacity = UINT64_C(1) << 63, .max_payload_bytes = SIZE_MAX};
    check_equal(cnet_command_queue_init(&queue, &non_power), SALTS_EINVAL);
    check_equal(cnet_command_queue_init(&queue, &overflow), SALTS_ERANGE);
  }

  it("copies one START_TLS control payload before returning") {
    static const uint8_t expected[] = {1u, 2u, 3u, 4u};
    uint8_t source[] = {1u, 2u, 3u, 4u};
    const cnet_command_queue_config config = {
        .capacity = TEST_COMMAND_CAPACITY, .max_payload_bytes = TEST_PAYLOAD_CAPACITY};
    cnet_command command = {.kind = CNET_COMMAND_START_TLS,
                            .connection = {.slot = 1u, .generation = 1u},
                            .data = source,
                            .size = sizeof(source)};
    cnet_command_view view = {0};
    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
    memset(source, 0, sizeof(source));
    check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
    check_equal(view.kind, CNET_COMMAND_START_TLS);
    check_equal(view.data, expected, sizeof(expected));
    check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
  }

  it("bounds copied control payload bytes independently from command slots") {
    static const uint8_t first[4] = {1u};
    static const uint8_t second[4] = {2u};
    const cnet_command_queue_config config = {
        .capacity = 2u, .max_payload_bytes = 8u, .payload_capacity_bytes = 4u};
    cnet_command a = {.kind = CNET_COMMAND_START_TLS,
                      .connection = {.slot = 1u, .generation = 1u},
                      .data = first,
                      .size = sizeof(first)};
    cnet_command b = {.kind = CNET_COMMAND_START_TLS,
                      .connection = {.slot = 2u, .generation = 1u},
                      .data = second,
                      .size = sizeof(second)};
    cnet_command_queue_stats stats = {0};
    cnet_command_view view = {0};
    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &a), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &b), SALTS_ENOBUFS);
    check_true(cnet_command_queue_get_stats(&queue, &stats));
    check_equal(stats.live_commands, (size_t)1u);
    check_equal(stats.queued_bytes, sizeof(first));
    check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
    check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
  }

  it("cycles a large fixed control FIFO without payload ownership") {
    const cnet_command_queue_config config = {
        .capacity = TEST_COMMAND_SCALE, .max_payload_bytes = TEST_PAYLOAD_CAPACITY};
    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    for (size_t cycle = 0u; cycle < TEST_COMMAND_CYCLES; ++cycle) {
      for (size_t index = 0u; index < TEST_COMMAND_SCALE; ++index) {
        const cnet_command command = make_receive((uint32_t)index + 1u, index + 1u);
        check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
      }
      for (size_t index = 0u; index < TEST_COMMAND_SCALE; ++index) {
        cnet_command_view view = {0};
        check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
        check_equal(view.kind, CNET_COMMAND_RECEIVE);
        check_equal(view.argument, index + 1u);
        check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
      }
    }
  }

  it("rejects malformed control descriptors without consuming capacity") {
    const cnet_command_queue_config config = {.capacity = 2u, .max_payload_bytes = 8u};
    cnet_command bad_receive = make_receive(1u, 0u);
    cnet_command bad_close = make_close(1u);
    cnet_command_queue_stats stats = {0};
    bad_close.data = "x";
    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &bad_receive), SALTS_EINVAL);
    check_equal(cnet_command_queue_publish(&queue, &bad_close), SALTS_EINVAL);
    check_true(cnet_command_queue_get_stats(&queue, &stats));
    check_equal(stats.live_commands, (size_t)0u);
  }

  it("rejects an oversized copied control payload") {
    static const uint8_t payload[9] = {0};
    const cnet_command_queue_config config = {.capacity = 2u, .max_payload_bytes = 8u};
    cnet_command command = {.kind = CNET_COMMAND_CONNECT,
                            .connection = {.slot = 1u, .generation = 1u},
                            .data = payload,
                            .size = sizeof(payload)};
    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &command), SALTS_EMSGSIZE);
  }

  it("rejects publication when every fixed command slot is occupied") {
    const cnet_command_queue_config config = {.capacity = 2u, .max_payload_bytes = 8u};
    cnet_command first = make_receive(1u, 1u);
    cnet_command second = make_receive(2u, 1u);
    cnet_command third = make_receive(3u, 1u);
    cnet_command_view view = {0};
    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &first), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &second), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &third), SALTS_ENOBUFS);
    check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
    check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
    check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
    check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
  }

  it("keeps borrowed tokens generation checked across out-of-order release and reuse") {
    const cnet_command_queue_config config = {.capacity = 2u, .max_payload_bytes = 8u};
    cnet_command_view first_view = {0};
    cnet_command_view second_view = {0};
    cnet_command_view stale = {0};
    cnet_command_view third_view = {0};
    cnet_command first = make_receive(1u, 1u);
    cnet_command second = make_close(2u);
    cnet_command third = make_receive(3u, 2u);
    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &first), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &second), SALTS_OK);
    check_equal(cnet_command_queue_take(&queue, &first_view), SALTS_OK);
    check_equal(cnet_command_queue_take(&queue, &second_view), SALTS_OK);
    stale = second_view;
    check_equal(cnet_command_queue_release(&queue, &second_view), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &third), SALTS_OK);
    check_equal(cnet_command_queue_take(&queue, &third_view), SALTS_OK);
    check_equal(cnet_command_queue_release(&queue, &stale), SALTS_EINVAL);
    check_equal(cnet_command_queue_release(&queue, &third_view), SALTS_OK);
    check_equal(cnet_command_queue_release(&queue, &first_view), SALTS_OK);
  }

  it("closes admission and drains already published controls") {
    const cnet_command_queue_config config = {.capacity = 2u, .max_payload_bytes = 8u};
    cnet_command command = make_receive(1u, 1u);
    cnet_command_view view = {0};
    check_equal(cnet_command_queue_init(&queue, &config), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &command), SALTS_OK);
    check_equal(cnet_command_queue_close(&queue), SALTS_OK);
    check_equal(cnet_command_queue_publish(&queue, &command), SALTS_ESHUTDOWN);
    check_equal(cnet_command_queue_take(&queue, &view), SALTS_OK);
    check_equal(cnet_command_queue_release(&queue, &view), SALTS_OK);
    check_equal(cnet_command_queue_take(&queue, &view), SALTS_EOF);
  }
}
