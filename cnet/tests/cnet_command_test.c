#include "cnet_command.h"
#include "tinytest.h"
#include <turbo/clock.h>
#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

enum { TEST_COMMAND_CAPACITY = 2, TEST_PAYLOAD_CAPACITY = 8 };

static cnet_command_queue queue;

enum {
  TEST_STRESS_PRODUCERS = 4,
  TEST_STRESS_COMMANDS_PER_PRODUCER = 512,
  TEST_STRESS_CLOSE_AFTER = 64,
  TEST_STRESS_TIMEOUT_MS = 5000
};

typedef struct command_stress_payload {
  uint32_t producer;
  uint32_t sequence;
} command_stress_payload;

typedef struct command_stress_producer {
  cnet_command_queue *queue;
  uint32_t producer;
  size_t accepted;
  atomic_int *finished;
  atomic_int *failures;
} command_stress_producer;

static void command_stress_publish(void *user) {
  command_stress_producer *context = (command_stress_producer *)user;
  uint32_t sequence;
  for (sequence = 0u; sequence < TEST_STRESS_COMMANDS_PER_PRODUCER; ++sequence) {
    const command_stress_payload payload = {context->producer, sequence};
    const cnet_command command = {
        CNET_COMMAND_SEND, {context->producer + 1u, 1u}, &payload, sizeof(payload), 0u};
    for (;;) {
      const int status = cnet_command_queue_publish(context->queue, &command);
      if (status == TURBO_OK) {
        ++context->accepted;
        break;
      }
      if (status == TURBO_ENOBUFS) {
        turbo_thread_yield();
        continue;
      }
      if (status == TURBO_ESHUTDOWN) {
        atomic_fetch_add(context->finished, 1);
        return;
      }
      atomic_fetch_add(context->failures, 1);
      atomic_fetch_add(context->finished, 1);
      return;
    }
  }
  atomic_fetch_add(context->finished, 1);
}

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
    it("preserves MPSC producer order and exact accepted count while closing") {
      const cnet_command_queue_config config = {
          .capacity = 16u, .max_payload_bytes = sizeof(command_stress_payload)};
      turbo_thread_t threads[TEST_STRESS_PRODUCERS] = {0};
      command_stress_producer producers[TEST_STRESS_PRODUCERS];
      uint32_t next_sequence[TEST_STRESS_PRODUCERS] = {0};
      atomic_int finished;
      atomic_int failures;
      const uint64_t deadline = turbo_monotonic_ms() + TEST_STRESS_TIMEOUT_MS;
      size_t received = 0u;
      bool closed = false;
      size_t producer;

      atomic_init(&finished, 0);
      atomic_init(&failures, 0);
      check_equal(cnet_command_queue_init(&queue, &config), TURBO_OK);
      for (producer = 0u; producer < TEST_STRESS_PRODUCERS; ++producer) {
        producers[producer] =
            (command_stress_producer){&queue, (uint32_t)producer, 0u, &finished, &failures};
        check_equal(
            turbo_thread_create(&threads[producer], command_stress_publish, &producers[producer]),
            TURBO_OK);
      }

      for (;;) {
        cnet_command_view view = {0};
        const int status = cnet_command_queue_take(&queue, &view);
        if (status == TURBO_OK) {
          const command_stress_payload *payload = (const command_stress_payload *)view.data;
          if (view.size != sizeof(*payload) || payload->producer >= TEST_STRESS_PRODUCERS ||
              payload->sequence != next_sequence[payload->producer])
            atomic_fetch_add(&failures, 1);
          else ++next_sequence[payload->producer];
          ++received;
          check_equal(cnet_command_queue_release(&queue, &view), TURBO_OK);
        } else if (status != TURBO_ETIMEDOUT && status != TURBO_EOF) {
          atomic_fetch_add(&failures, 1);
          break;
        }

        if (!closed && received >= TEST_STRESS_CLOSE_AFTER) {
          int close_status;
          do {
            close_status = cnet_command_queue_close(&queue);
            if (close_status == TURBO_EBUSY) turbo_thread_yield();
          } while (close_status == TURBO_EBUSY);
          check_equal(close_status, TURBO_OK);
          closed = true;
        }
        if (status == TURBO_EOF) break;
        if (turbo_monotonic_ms() >= deadline) {
          atomic_fetch_add(&failures, 1);
          if (!closed) {
            while (cnet_command_queue_close(&queue) == TURBO_EBUSY)
              turbo_thread_yield();
            closed = true;
          }
        }
      }

      for (producer = 0u; producer < TEST_STRESS_PRODUCERS; ++producer)
        check_equal(turbo_thread_join(&threads[producer]), TURBO_OK);
      check_equal(atomic_load(&finished), TEST_STRESS_PRODUCERS);
      check_equal(atomic_load(&failures), 0);
      {
        size_t accepted = 0u;
        for (producer = 0u; producer < TEST_STRESS_PRODUCERS; ++producer) {
          accepted += producers[producer].accepted;
          check_equal(next_sequence[producer], producers[producer].accepted);
        }
        check_equal(received, accepted);
      }
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
