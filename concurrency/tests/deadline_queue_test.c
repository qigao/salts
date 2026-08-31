#include <turbo/deadline_queue.h>

#include "tinytest.h"

#include <stdint.h>

spec("Concurrency deadline queue") {
  it("orders absolute deadlines and preserves FIFO ties") {
    turbo_deadline_queue queue = {0};
    turbo_deadline_event event = {0};
    turbo_deadline_id first = 0u;
    turbo_deadline_id second = 0u;
    turbo_deadline_id third = 0u;

    check_equal(turbo_deadline_queue_init(&queue, 3u), TURBO_OK);
    check_equal(turbo_deadline_queue_schedule(&queue, 20u, 200u, &first), TURBO_OK);
    check_equal(turbo_deadline_queue_schedule(&queue, 10u, 100u, &second), TURBO_OK);
    check_equal(turbo_deadline_queue_schedule(&queue, 10u, 101u, &third), TURBO_OK);
    check_true(first != 0u && second != 0u && third != 0u);
    check_equal(turbo_deadline_queue_take_ready(&queue, 9u, &event), TURBO_ETIMEDOUT);
    check_equal(event.id, 0u);
    check_equal(turbo_deadline_queue_take_ready(&queue, 10u, &event), TURBO_OK);
    check_equal(event.id, second);
    check_equal(event.token, 100u);
    check_equal(turbo_deadline_queue_take_ready(&queue, 10u, &event), TURBO_OK);
    check_equal(event.id, third);
    check_equal(event.token, 101u);
    check_equal(turbo_deadline_queue_take_ready(&queue, 20u, &event), TURBO_OK);
    check_equal(event.id, first);
    check_equal(event.token, 200u);
    check_equal(turbo_deadline_queue_size(&queue), 0u);
    check_equal(turbo_deadline_queue_destroy(&queue), TURBO_OK);
  }

  it("rejects a stale generation after slot reuse and restores heap order") {
    turbo_deadline_queue queue = {0};
    turbo_deadline_event event = {0};
    turbo_deadline_id first = 0u;
    turbo_deadline_id second = 0u;
    turbo_deadline_id third = 0u;
    turbo_deadline_id replacement = 0u;

    check_equal(turbo_deadline_queue_init(&queue, 3u), TURBO_OK);
    check_equal(turbo_deadline_queue_schedule(&queue, 30u, 3u, &third), TURBO_OK);
    check_equal(turbo_deadline_queue_schedule(&queue, 10u, 1u, &first), TURBO_OK);
    check_equal(turbo_deadline_queue_schedule(&queue, 20u, 2u, &second), TURBO_OK);
    check_equal(turbo_deadline_queue_cancel(&queue, first, &event), TURBO_OK);
    check_equal(event.token, 1u);
    check_equal(turbo_deadline_queue_schedule(&queue, 5u, 4u, &replacement), TURBO_OK);
    check_not_equal(replacement, first);
    check_equal(turbo_deadline_queue_cancel(&queue, second, &event), TURBO_OK);
    check_equal(event.token, 2u);
    check_equal(turbo_deadline_queue_cancel(&queue, first, &event), TURBO_ENOENT);
    check_equal(event.id, 0u);
    check_equal(turbo_deadline_queue_peek(&queue, &event), TURBO_OK);
    check_equal(event.token, 4u);
    check_equal(turbo_deadline_queue_take_ready(&queue, 30u, &event), TURBO_OK);
    check_equal(event.token, 4u);
    check_equal(turbo_deadline_queue_take_ready(&queue, 30u, &event), TURBO_OK);
    check_equal(event.id, third);
    check_equal(turbo_deadline_queue_destroy(&queue), TURBO_OK);
  }

  it("maintains indexed heap invariants across cancellation and slot reuse") {
    enum { CAPACITY = 32, ROUNDS = 128 };
    turbo_deadline_queue queue = {0};
    turbo_deadline_id ids[CAPACITY] = {0};
    size_t round;

    check_equal(turbo_deadline_queue_init(&queue, CAPACITY), TURBO_OK);
    for (round = 0u; round < ROUNDS; ++round) {
      turbo_deadline_event event = {0};
      uint64_t previous_deadline = 0u;
      size_t index;
      size_t taken = 0u;

      for (index = 0u; index < CAPACITY; ++index)
        check_equal(turbo_deadline_queue_schedule(&queue, (uint64_t)((index * 17u) % CAPACITY + 1u),
                                                  (uint64_t)index, &ids[index]),
                    TURBO_OK);
      for (index = 0u; index < CAPACITY; index += 2u) {
        check_equal(turbo_deadline_queue_cancel(&queue, ids[index], &event), TURBO_OK);
        check_equal(event.token, (uint64_t)index);
      }
      while (turbo_deadline_queue_take_ready(&queue, UINT64_MAX, &event) == TURBO_OK) {
        check_true(event.deadline_ms >= previous_deadline);
        check_equal(event.token % 2u, 1u);
        previous_deadline = event.deadline_ms;
        ++taken;
      }
      check_equal(taken, (size_t)(CAPACITY / 2u));
      check_equal(turbo_deadline_queue_size(&queue), 0u);
    }
    check_equal(turbo_deadline_queue_destroy(&queue), TURBO_OK);
  }

  it("fails fast at fixed capacity and invalid boundaries") {
    turbo_deadline_queue queue = {0};
    turbo_deadline_event event = {1u, 2u, 3u};
    turbo_deadline_id id = 99u;

    check_equal(turbo_deadline_queue_init(NULL, 1u), TURBO_EINVAL);
    check_equal(turbo_deadline_queue_init(&queue, 0u), TURBO_EINVAL);
    check_equal(turbo_deadline_queue_init(&queue, SIZE_MAX), TURBO_ERANGE);
    check_equal(turbo_deadline_queue_init(&queue, 1u), TURBO_OK);
    check_equal(turbo_deadline_queue_init(&queue, 1u), TURBO_EALREADY);
    check_equal(turbo_deadline_queue_schedule(&queue, 1u, 7u, &id), TURBO_OK);
    check_equal(turbo_deadline_queue_schedule(&queue, 2u, 8u, &id), TURBO_ENOBUFS);
    check_equal(id, 0u);
    check_equal(turbo_deadline_queue_cancel(&queue, 0u, &event), TURBO_EINVAL);
    check_equal(event.id, 0u);
    check_equal(turbo_deadline_queue_take_ready(&queue, 1u, &event), TURBO_OK);
    check_equal(turbo_deadline_queue_peek(&queue, &event), TURBO_ETIMEDOUT);
    check_equal(event.id, 0u);
    check_equal(turbo_deadline_queue_destroy(&queue), TURBO_OK);
    check_equal(turbo_deadline_queue_destroy(&queue), TURBO_OK);
  }
}
