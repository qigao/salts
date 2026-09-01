#include "cnet_command.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

enum {
  TEST_COMMAND_CAPACITY = 2,
  TEST_COMMAND_SCALE = 4096,
  TEST_COMMAND_CYCLES = 2,
  TEST_PAYLOAD_CAPACITY = 8
};

static cnet_command_queue queue;

static cnet_command make_send(uint32_t slot, const void *data, size_t size) {
  cnet_command command = {0};
  command.kind = CNET_COMMAND_SEND;
  command.connection.slot = slot;
  command.connection.generation = 1u;
  command.data = data;
  command.size = size;
  return command;
}

spec("CNet bounded command queue") {
  before_each() { memset(&queue, 0, sizeof(queue)); }

  after_each() {
    if (queue.impl != NULL) {
      int status = cnet_command_queue_close(&queue);
      check_true(status == TURBO_OK || status == TURBO_EALREADY);
      check_equal(cnet_command_queue_destroy(&queue), TURBO_OK);
    }
  }

  group("initialization") {
    it("rejects non power of two command capacity") {
      const cnet_command_queue_config config = {.capacity = 3u,
                                                .max_payload_bytes = TEST_PAYLOAD_CAPACITY};

      check_equal(cnet_command_queue_init(&queue, &config), TURBO_EINVAL);
      check_null(queue.impl);
    }

    it("rejects an overflowing resident payload budget") {
      const cnet_command_queue_config config = {.capacity = UINT64_C(1) << 63,
                                                .max_payload_bytes = SIZE_MAX};

      check_equal(cnet_command_queue_init(&queue, &config), TURBO_ERANGE);
      check_null(queue.impl);
    }
  }

  group("publication") {
    it("cycles every slot in a large bounded FIFO") {
      const cnet_command_queue_config config = {.capacity = TEST_COMMAND_SCALE,
                                                .max_payload_bytes = TEST_PAYLOAD_CAPACITY};
      cnet_command_queue_stats stats = {0};

      check_equal(cnet_command_queue_init(&queue, &config), TURBO_OK);
      for (size_t cycle = 0u; cycle < TEST_COMMAND_CYCLES; ++cycle) {
        for (size_t index = 0u; index < TEST_COMMAND_SCALE; ++index) {
          const uint32_t payload = (uint32_t)(cycle * TEST_COMMAND_SCALE + index);
          const cnet_command command = make_send((uint32_t)index + 1u, &payload, sizeof(payload));
          check_equal(cnet_command_queue_publish(&queue, &command), TURBO_OK);
        }
        {
          const uint32_t payload = UINT32_MAX;
          const cnet_command command =
              make_send((uint32_t)TEST_COMMAND_SCALE + 1u, &payload, sizeof(payload));
          check_equal(cnet_command_queue_publish(&queue, &command), TURBO_ENOBUFS);
        }
        for (size_t index = 0u; index < TEST_COMMAND_SCALE; ++index) {
          const uint32_t expected_payload = (uint32_t)(cycle * TEST_COMMAND_SCALE + index);
          cnet_command_view view = {0};
          check_equal(cnet_command_queue_take(&queue, &view), TURBO_OK);
          check_equal(view.kind, CNET_COMMAND_SEND);
          check_equal(view.connection.slot, (uint32_t)index + 1u);
          check_equal(view.connection.generation, UINT32_C(1));
          check_equal(view.size, sizeof(expected_payload));
          check_equal(view.data, &expected_payload, sizeof(expected_payload));
          check_equal(cnet_command_queue_release(&queue, &view), TURBO_OK);
        }
      }
      check_true(cnet_command_queue_get_stats(&queue, &stats));
      check_equal(stats.live_commands, 0u);
      check_equal(stats.peak_commands, (size_t)TEST_COMMAND_SCALE);
      check_equal(stats.queued_bytes, 0u);
      check_equal(stats.peak_queued_bytes, (size_t)TEST_COMMAND_SCALE * sizeof(uint32_t));
      check_equal(stats.rejected_commands, (uint64_t)TEST_COMMAND_CYCLES);
      check_equal(stats.rejected_bytes, (uint64_t)TEST_COMMAND_CYCLES * sizeof(uint32_t));
      check_true(stats.admission_open);
    }

    it("reports bounded live peak byte and rejection counters") {
      static const uint8_t first_payload[1] = {1u};
      static const uint8_t second_payload[2] = {2u, 3u};
      static const uint8_t full_payload[3] = {4u, 5u, 6u};
      static const uint8_t oversize_payload[TEST_PAYLOAD_CAPACITY + 1u] = {0};
      const cnet_command_queue_config config = {.capacity = TEST_COMMAND_CAPACITY,
                                                .max_payload_bytes = TEST_PAYLOAD_CAPACITY};
      cnet_command_queue_stats stats = {0};
      cnet_command_view view = {0};
      cnet_command command;

      check_equal(cnet_command_queue_init(&queue, &config), TURBO_OK);
      command = make_send(1u, first_payload, sizeof(first_payload));
      check_equal(cnet_command_queue_publish(&queue, &command), TURBO_OK);
      command = make_send(2u, second_payload, sizeof(second_payload));
      check_equal(cnet_command_queue_publish(&queue, &command), TURBO_OK);
      command = make_send(3u, full_payload, sizeof(full_payload));
      check_equal(cnet_command_queue_publish(&queue, &command), TURBO_ENOBUFS);
      command = make_send(4u, oversize_payload, sizeof(oversize_payload));
      check_equal(cnet_command_queue_publish(&queue, &command), TURBO_EMSGSIZE);
      check_true(cnet_command_queue_get_stats(&queue, &stats));
      check_equal(stats.live_commands, 2u);
      check_equal(stats.peak_commands, 2u);
      check_equal(stats.queued_bytes, 3u);
      check_equal(stats.peak_queued_bytes, 3u);
      check_equal(stats.rejected_commands, 2u);
      check_equal(stats.rejected_bytes, sizeof(full_payload) + sizeof(oversize_payload));

      check_equal(cnet_command_queue_take(&queue, &view), TURBO_OK);
      check_equal(cnet_command_queue_release(&queue, &view), TURBO_OK);
      check_equal(cnet_command_queue_take(&queue, &view), TURBO_OK);
      check_equal(cnet_command_queue_release(&queue, &view), TURBO_OK);
      check_true(cnet_command_queue_get_stats(&queue, &stats));
      check_equal(stats.live_commands, 0u);
      check_equal(stats.peak_commands, 2u);
      check_equal(stats.queued_bytes, 0u);
      check_equal(stats.peak_queued_bytes, 3u);
    }

    it("copies payload into queue-owned storage") {
      static const uint8_t expected[] = {1u, 2u, 3u, 4u};
      uint8_t source[] = {1u, 2u, 3u, 4u};
      cnet_command_view view = {0};
      cnet_command command;
      const cnet_command_queue_config config = {.capacity = TEST_COMMAND_CAPACITY,
                                                .max_payload_bytes = TEST_PAYLOAD_CAPACITY};

      check_equal(cnet_command_queue_init(&queue, &config), TURBO_OK);
      command = make_send(1u, source, sizeof(source));
      check_equal(cnet_command_queue_publish(&queue, &command), TURBO_OK);
      memset(source, 0, sizeof(source));
      check_equal(cnet_command_queue_take(&queue, &view), TURBO_OK);
      check_equal(view.kind, CNET_COMMAND_SEND);
      check_equal(view.connection.slot, UINT32_C(1));
      check_equal(view.size, sizeof(expected));
      check_equal(view.data, expected, sizeof(expected));
      check_equal(cnet_command_queue_release(&queue, &view), TURBO_OK);
    }

    it("rejects a payload larger than the configured slot") {
      static const uint8_t payload[TEST_PAYLOAD_CAPACITY + 1u] = {0};
      cnet_command command;
      const cnet_command_queue_config config = {.capacity = TEST_COMMAND_CAPACITY,
                                                .max_payload_bytes = TEST_PAYLOAD_CAPACITY};

      check_equal(cnet_command_queue_init(&queue, &config), TURBO_OK);
      command = make_send(1u, payload, sizeof(payload));
      check_equal(cnet_command_queue_publish(&queue, &command), TURBO_EMSGSIZE);
    }

    it("rejects publication when every fixed slot is retained") {
      static const uint8_t payload = 7u;
      cnet_command command;
      const cnet_command_queue_config config = {.capacity = TEST_COMMAND_CAPACITY,
                                                .max_payload_bytes = TEST_PAYLOAD_CAPACITY};

      check_equal(cnet_command_queue_init(&queue, &config), TURBO_OK);
      command = make_send(1u, &payload, sizeof(payload));
      check_equal(cnet_command_queue_publish(&queue, &command), TURBO_OK);
      command.connection.slot = 2u;
      check_equal(cnet_command_queue_publish(&queue, &command), TURBO_OK);
      command.connection.slot = 3u;
      check_equal(cnet_command_queue_publish(&queue, &command), TURBO_ENOBUFS);
      {
        cnet_command_view view = {0};
        check_equal(cnet_command_queue_take(&queue, &view), TURBO_OK);
        check_equal(cnet_command_queue_release(&queue, &view), TURBO_OK);
        check_equal(cnet_command_queue_take(&queue, &view), TURBO_OK);
        check_equal(cnet_command_queue_release(&queue, &view), TURBO_OK);
      }
    }

    it("retains multiple owner views until out of order completions release them") {
      static const uint8_t first_payload = 3u;
      static const uint8_t second_payload = 5u;
      cnet_command first;
      cnet_command second;
      cnet_command_view first_view = {0};
      cnet_command_view second_view = {0};
      const cnet_command_queue_config config = {.capacity = TEST_COMMAND_CAPACITY,
                                                .max_payload_bytes = TEST_PAYLOAD_CAPACITY};

      check_equal(cnet_command_queue_init(&queue, &config), TURBO_OK);
      first = make_send(1u, &first_payload, sizeof(first_payload));
      second = make_send(2u, &second_payload, sizeof(second_payload));
      check_equal(cnet_command_queue_publish(&queue, &first), TURBO_OK);
      check_equal(cnet_command_queue_publish(&queue, &second), TURBO_OK);
      check_equal(cnet_command_queue_take(&queue, &first_view), TURBO_OK);
      check_equal(cnet_command_queue_take(&queue, &second_view), TURBO_OK);
      check_equal(*(const uint8_t *)first_view.data, first_payload);
      check_equal(*(const uint8_t *)second_view.data, second_payload);
      check_equal(cnet_command_queue_release(&queue, &second_view), TURBO_OK);
      check_equal(cnet_command_queue_release(&queue, &first_view), TURBO_OK);
    }

    it("reuses a released slot while an older owner view remains borrowed") {
      static const uint8_t first_payload = 3u;
      static const uint8_t second_payload = 5u;
      static const uint8_t third_payload = 7u;
      cnet_command first;
      cnet_command second;
      cnet_command third;
      cnet_command_view first_view = {0};
      cnet_command_view second_view = {0};
      cnet_command_view third_view = {0};
      const cnet_command_queue_config config = {.capacity = TEST_COMMAND_CAPACITY,
                                                .max_payload_bytes = TEST_PAYLOAD_CAPACITY};

      check_equal(cnet_command_queue_init(&queue, &config), TURBO_OK);
      first = make_send(1u, &first_payload, sizeof(first_payload));
      second = make_send(2u, &second_payload, sizeof(second_payload));
      third = make_send(3u, &third_payload, sizeof(third_payload));
      check_equal(cnet_command_queue_publish(&queue, &first), TURBO_OK);
      check_equal(cnet_command_queue_publish(&queue, &second), TURBO_OK);
      check_equal(cnet_command_queue_take(&queue, &first_view), TURBO_OK);
      check_equal(cnet_command_queue_take(&queue, &second_view), TURBO_OK);
      check_equal(cnet_command_queue_release(&queue, &second_view), TURBO_OK);
      check_equal(cnet_command_queue_publish(&queue, &third), TURBO_OK);
      check_equal(cnet_command_queue_take(&queue, &third_view), TURBO_OK);
      check_equal(*(const uint8_t *)third_view.data, third_payload);
      check_equal(cnet_command_queue_release(&queue, &third_view), TURBO_OK);
      check_equal(cnet_command_queue_release(&queue, &first_view), TURBO_OK);
    }

    it("rejects a stale release token after slot reuse") {
      static const uint8_t first_payload = 3u;
      static const uint8_t second_payload = 5u;
      cnet_command first;
      cnet_command second;
      cnet_command_view first_view = {0};
      cnet_command_view stale_view = {0};
      cnet_command_view second_view = {0};
      const cnet_command_queue_config config = {.capacity = 1u,
                                                .max_payload_bytes = TEST_PAYLOAD_CAPACITY};

      check_equal(cnet_command_queue_init(&queue, &config), TURBO_OK);
      first = make_send(1u, &first_payload, sizeof(first_payload));
      second = make_send(2u, &second_payload, sizeof(second_payload));
      check_equal(cnet_command_queue_publish(&queue, &first), TURBO_OK);
      check_equal(cnet_command_queue_take(&queue, &first_view), TURBO_OK);
      stale_view = first_view;
      check_equal(cnet_command_queue_release(&queue, &first_view), TURBO_OK);
      check_equal(cnet_command_queue_publish(&queue, &second), TURBO_OK);
      check_equal(cnet_command_queue_take(&queue, &second_view), TURBO_OK);
      check_equal(cnet_command_queue_release(&queue, &stale_view), TURBO_EINVAL);
      check_equal(*(const uint8_t *)second_view.data, second_payload);
      check_equal(cnet_command_queue_release(&queue, &second_view), TURBO_OK);
    }
  }

  group("shutdown") {
    it("closes admission and drains already published commands") {
      static const uint8_t payload = 9u;
      cnet_command command;
      cnet_command_view view = {0};
      const cnet_command_queue_config config = {.capacity = TEST_COMMAND_CAPACITY,
                                                .max_payload_bytes = TEST_PAYLOAD_CAPACITY};

      check_equal(cnet_command_queue_init(&queue, &config), TURBO_OK);
      command = make_send(1u, &payload, sizeof(payload));
      check_equal(cnet_command_queue_publish(&queue, &command), TURBO_OK);
      check_equal(cnet_command_queue_close(&queue), TURBO_OK);
      check_equal(cnet_command_queue_publish(&queue, &command), TURBO_ESHUTDOWN);
      check_equal(cnet_command_queue_take(&queue, &view), TURBO_OK);
      check_equal(cnet_command_queue_release(&queue, &view), TURBO_OK);
      check_equal(cnet_command_queue_take(&queue, &view), TURBO_EOF);
    }
  }
}
