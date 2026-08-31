#include "cnet_callback.h"
#include "tinytest.h"

#include <turbo/clock.h>
#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum { CNET_CALLBACK_TEST_TIMEOUT_MS = 5000 };

typedef struct cnet_callback_test_probe {
  atomic_bool release_first;
  atomic_int first_started;
  atomic_int first_finished;
  atomic_int second_started;
  atomic_int other_finished;
  atomic_int finish_count;
  atomic_int order_error;
  atomic_int observed_second_payload;
  atomic_int release_count;
} cnet_callback_test_probe;

enum { CNET_CALLBACK_STRESS_PRODUCERS = 4, CNET_CALLBACK_STRESS_JOBS_PER_PRODUCER = 100 };

typedef struct cnet_callback_stress_payload {
  int producer;
  int sequence;
} cnet_callback_stress_payload;

typedef struct cnet_callback_stress_probe {
  atomic_int last_sequence[CNET_CALLBACK_STRESS_PRODUCERS];
  atomic_int invoked;
  atomic_int finished;
  atomic_int order_error;
  atomic_int publish_error;
  atomic_int released;
  atomic_int error_producer;
  atomic_int error_expected;
  atomic_int error_actual;
} cnet_callback_stress_probe;

typedef struct cnet_callback_stress_producer {
  cnet_callback_workers *workers;
  cnet_callback_stress_probe *probe;
  cnet_callback_stress_payload payloads[CNET_CALLBACK_STRESS_JOBS_PER_PRODUCER];
  int producer;
} cnet_callback_stress_producer;

typedef struct cnet_callback_handoff_probe {
  cnet_event_queue *events;
  const void *expected_data;
  atomic_int invoked;
  atomic_int pointer_match;
  atomic_int observed_value;
  atomic_int released;
} cnet_callback_handoff_probe;

static bool cnet_callback_test_wait_at_least(atomic_int *value, int expected) {
  const uint64_t deadline = turbo_monotonic_ms() + CNET_CALLBACK_TEST_TIMEOUT_MS;
  while (atomic_load_explicit(value, memory_order_acquire) < expected) {
    if (turbo_monotonic_ms() >= deadline) return false;
    turbo_sleep_ms(1u);
  }
  return true;
}

static void cnet_callback_test_invoke(void *context, const cnet_callback_view *view) {
  cnet_callback_test_probe *probe = (cnet_callback_test_probe *)context;
  const unsigned char value = view->size == 1u ? *(const unsigned char *)view->data : 0u;

  if (view->session.slot == 1u && value == 1u) {
    atomic_store_explicit(&probe->first_started, 1, memory_order_release);
    while (!atomic_load_explicit(&probe->release_first, memory_order_acquire))
      turbo_thread_yield();
    atomic_store_explicit(&probe->first_finished, 1, memory_order_release);
    return;
  }
  if (view->session.slot == 1u && value == 2u) {
    if (atomic_load_explicit(&probe->first_finished, memory_order_acquire) == 0)
      atomic_store_explicit(&probe->order_error, 1, memory_order_release);
    atomic_store_explicit(&probe->observed_second_payload, (int)value, memory_order_release);
    atomic_store_explicit(&probe->second_started, 1, memory_order_release);
    return;
  }
  if (view->session.slot == 2u && value == 3u)
    atomic_store_explicit(&probe->other_finished, 1, memory_order_release);
}

static void cnet_callback_test_finish(void *context, const cnet_callback_view *view) {
  cnet_callback_test_probe *probe = (cnet_callback_test_probe *)context;
  (void)view;
  atomic_fetch_add_explicit(&probe->finish_count, 1, memory_order_acq_rel);
}

static int cnet_callback_test_release(void *context, const cnet_callback_view *view,
                                      uint64_t token) {
  cnet_callback_test_probe *probe = (cnet_callback_test_probe *)context;
  (void)view;
  (void)token;
  atomic_fetch_add_explicit(&probe->release_count, 1, memory_order_acq_rel);
  return TURBO_OK;
}

static cnet_callback_job cnet_callback_test_receive_job(uint64_t key, cnet_session_handle session,
                                                        cnet_callback_test_probe *probe,
                                                        const void *data, size_t size) {
  cnet_callback_job job = {0};
  job.serialization_key = key;
  job.invoke = cnet_callback_test_invoke;
  job.finish = cnet_callback_test_finish;
  job.context = probe;
  job.event = (cnet_event){CNET_EVENT_RECEIVE,
                           session,
                           CNET_EVENT_STATE_NONE,
                           TURBO_OK,
                           CNET_SESSION_STAGE_NONE,
                           data,
                           size};
  job.release = cnet_callback_test_release;
  job.release_context = probe;
  return job;
}

static void cnet_callback_stress_invoke(void *context, const cnet_callback_view *view) {
  cnet_callback_stress_probe *probe = (cnet_callback_stress_probe *)context;
  cnet_callback_stress_payload payload = {0};
  int expected;

  if (view->size != sizeof(payload)) {
    atomic_store_explicit(&probe->error_actual, (int)view->size, memory_order_release);
    atomic_store_explicit(&probe->order_error, 1, memory_order_release);
    return;
  }
  memcpy(&payload, view->data, sizeof(payload));
  if (payload.producer < 0 || payload.producer >= CNET_CALLBACK_STRESS_PRODUCERS) {
    atomic_store_explicit(&probe->error_producer, payload.producer, memory_order_release);
    atomic_store_explicit(&probe->order_error, 1, memory_order_release);
    return;
  }
  expected =
      atomic_load_explicit(&probe->last_sequence[payload.producer], memory_order_acquire) + 1;
  if (payload.sequence != expected) {
    atomic_store_explicit(&probe->error_producer, payload.producer, memory_order_release);
    atomic_store_explicit(&probe->error_expected, expected, memory_order_release);
    atomic_store_explicit(&probe->error_actual, payload.sequence, memory_order_release);
    atomic_store_explicit(&probe->order_error, 1, memory_order_release);
  } else
    atomic_store_explicit(&probe->last_sequence[payload.producer], payload.sequence,
                          memory_order_release);
  atomic_fetch_add_explicit(&probe->invoked, 1, memory_order_acq_rel);
}

static void cnet_callback_stress_finish(void *context, const cnet_callback_view *view) {
  cnet_callback_stress_probe *probe = (cnet_callback_stress_probe *)context;
  (void)view;
  atomic_fetch_add_explicit(&probe->finished, 1, memory_order_acq_rel);
}

static int cnet_callback_stress_release(void *context, const cnet_callback_view *view,
                                        uint64_t token) {
  cnet_callback_stress_probe *probe = (cnet_callback_stress_probe *)context;
  (void)view;
  (void)token;
  atomic_fetch_add_explicit(&probe->released, 1, memory_order_acq_rel);
  return TURBO_OK;
}

static void cnet_callback_stress_publish(void *context) {
  cnet_callback_stress_producer *producer = (cnet_callback_stress_producer *)context;
  int sequence;

  for (sequence = 0; sequence < CNET_CALLBACK_STRESS_JOBS_PER_PRODUCER; ++sequence) {
    cnet_callback_stress_payload *payload = &producer->payloads[sequence];
    cnet_callback_job job = {0};
    int status;
    *payload = (cnet_callback_stress_payload){producer->producer, sequence};
    job.serialization_key = 0u;
    job.invoke = cnet_callback_stress_invoke;
    job.finish = cnet_callback_stress_finish;
    job.context = producer->probe;
    job.event = (cnet_event){CNET_EVENT_RECEIVE,      {(uint32_t)producer->producer + 1u, 1u},
                             CNET_EVENT_STATE_NONE,   TURBO_OK,
                             CNET_SESSION_STAGE_NONE, payload,
                             sizeof(*payload)};
    job.release = cnet_callback_stress_release;
    job.release_context = producer->probe;
    do {
      status = cnet_callback_workers_publish(producer->workers, &job);
      if (status == TURBO_ENOBUFS) turbo_thread_yield();
    } while (status == TURBO_ENOBUFS);
    if (status != TURBO_OK) {
      atomic_store_explicit(&producer->probe->publish_error, status, memory_order_release);
      return;
    }
  }
}

static void cnet_callback_handoff_invoke(void *context, const cnet_callback_view *view) {
  cnet_callback_handoff_probe *probe = (cnet_callback_handoff_probe *)context;
  atomic_store_explicit(&probe->pointer_match, view->data == probe->expected_data,
                        memory_order_release);
  if (view->size == 1u)
    atomic_store_explicit(&probe->observed_value, *(const unsigned char *)view->data,
                          memory_order_release);
  atomic_store_explicit(&probe->invoked, 1, memory_order_release);
}

static int cnet_callback_handoff_release(void *context, const cnet_callback_view *view,
                                         uint64_t token) {
  cnet_callback_handoff_probe *probe = (cnet_callback_handoff_probe *)context;
  cnet_event_view release_view = {0};
  int status;
  release_view.kind = view->kind;
  release_view._sequence = token;
  status = cnet_event_queue_release(probe->events, &release_view);
  if (status == TURBO_OK) atomic_fetch_add_explicit(&probe->released, 1, memory_order_acq_rel);
  return status;
}

static int cnet_callback_test_release_error(void *context, const cnet_callback_view *view,
                                            uint64_t token) {
  (void)context;
  (void)view;
  (void)token;
  return TURBO_EIO;
}

spec("CNet callback workers") {
  it("serializes one connection while allowing independent lanes to progress") {
    cnet_callback_workers workers = {0};
    const cnet_callback_workers_config config = {2u, 4u, 8u};
    cnet_callback_test_probe probe = {0};
    unsigned char first = 1u;
    unsigned char second = 2u;
    unsigned char other = 3u;
    cnet_callback_job first_job =
        cnet_callback_test_receive_job(0u, (cnet_session_handle){1u, 1u}, &probe, &first, 1u);
    cnet_callback_job second_job =
        cnet_callback_test_receive_job(0u, (cnet_session_handle){1u, 1u}, &probe, &second, 1u);
    cnet_callback_job other_job =
        cnet_callback_test_receive_job(1u, (cnet_session_handle){2u, 1u}, &probe, &other, 1u);
    bool first_started;
    bool other_finished;
    bool all_finished;
    cnet_callback_workers_config observed_config = {0};

    check_equal(cnet_callback_workers_init(&workers, &config), TURBO_OK);
    check_true(cnet_callback_workers_get_config(&workers, &observed_config));
    check_equal(observed_config.worker_count, config.worker_count);
    check_equal(observed_config.capacity_per_worker, config.capacity_per_worker);
    check_equal(observed_config.max_payload_bytes, config.max_payload_bytes);
    check_equal(cnet_callback_workers_publish(&workers, &first_job), TURBO_OK);
    first_started = cnet_callback_test_wait_at_least(&probe.first_started, 1);
    if (!first_started) atomic_store_explicit(&probe.release_first, true, memory_order_release);
    check_true(first_started);

    check_equal(cnet_callback_workers_publish(&workers, &second_job), TURBO_OK);
    check_equal(cnet_callback_workers_publish(&workers, &other_job), TURBO_OK);
    other_finished = cnet_callback_test_wait_at_least(&probe.other_finished, 1);
    atomic_store_explicit(&probe.release_first, true, memory_order_release);
    all_finished = cnet_callback_test_wait_at_least(&probe.finish_count, 3);

    check_true(other_finished);
    check_true(all_finished);
    check_equal(atomic_load_explicit(&probe.order_error, memory_order_acquire), 0);
    check_equal(atomic_load_explicit(&probe.second_started, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&probe.observed_second_payload, memory_order_acquire), 2);
    check_equal(cnet_callback_workers_destroy(&workers), TURBO_EBUSY);
    check_equal(cnet_callback_workers_stop(&workers, CNET_CALLBACK_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.release_count, memory_order_acquire), 3);
    check_equal(cnet_callback_workers_destroy(&workers), TURBO_OK);
  }

  it("applies bounded backpressure and drains accepted callbacks during stop") {
    cnet_callback_workers workers = {0};
    const cnet_callback_workers_config config = {1u, 2u, 4u};
    cnet_callback_test_probe probe = {0};
    unsigned char first = 1u;
    unsigned char second = 2u;
    unsigned char third = 3u;
    unsigned char oversized[5] = {0};
    cnet_callback_job first_job =
        cnet_callback_test_receive_job(0u, (cnet_session_handle){1u, 1u}, &probe, &first, 1u);
    cnet_callback_job second_job =
        cnet_callback_test_receive_job(0u, (cnet_session_handle){1u, 1u}, &probe, &second, 1u);
    cnet_callback_job third_job =
        cnet_callback_test_receive_job(0u, (cnet_session_handle){1u, 1u}, &probe, &third, 1u);
    cnet_callback_job oversized_job = cnet_callback_test_receive_job(
        0u, (cnet_session_handle){1u, 1u}, &probe, oversized, sizeof(oversized));
    bool first_started;
    bool all_finished;

    check_equal(cnet_callback_workers_init(&workers, &config), TURBO_OK);
    check_equal(cnet_callback_workers_publish(&workers, &oversized_job), TURBO_EMSGSIZE);
    check_equal(cnet_callback_workers_publish(&workers, &first_job), TURBO_OK);
    first_started = cnet_callback_test_wait_at_least(&probe.first_started, 1);
    if (!first_started) atomic_store_explicit(&probe.release_first, true, memory_order_release);
    check_true(first_started);
    check_equal(cnet_callback_workers_publish(&workers, &second_job), TURBO_OK);
    check_equal(cnet_callback_workers_publish(&workers, &third_job), TURBO_ENOBUFS);
    check_equal(cnet_callback_workers_stop(&workers, 0u), TURBO_ETIMEDOUT);
    check_equal(cnet_callback_workers_publish(&workers, &third_job), TURBO_ESHUTDOWN);

    atomic_store_explicit(&probe.release_first, true, memory_order_release);
    all_finished = cnet_callback_test_wait_at_least(&probe.finish_count, 2);
    check_true(all_finished);
    check_equal(cnet_callback_workers_stop(&workers, CNET_CALLBACK_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_callback_workers_stop(&workers, CNET_CALLBACK_TEST_TIMEOUT_MS),
                TURBO_EALREADY);
    check_equal(atomic_load_explicit(&probe.release_count, memory_order_acquire), 2);
    check_equal(cnet_callback_workers_destroy(&workers), TURBO_OK);
  }

  it("preserves every producer order under MPSC contention") {
    cnet_callback_workers workers = {0};
    const cnet_callback_workers_config config = {2u, 128u, 16u};
    cnet_callback_stress_probe probe = {0};
    cnet_callback_stress_producer producers[CNET_CALLBACK_STRESS_PRODUCERS] = {0};
    turbo_thread_t threads[CNET_CALLBACK_STRESS_PRODUCERS] = {0};
    int index;

    for (index = 0; index < CNET_CALLBACK_STRESS_PRODUCERS; ++index)
      atomic_init(&probe.last_sequence[index], -1);
    atomic_init(&probe.error_producer, -1);
    atomic_init(&probe.error_expected, -1);
    atomic_init(&probe.error_actual, -1);
    check_equal(cnet_callback_workers_init(&workers, &config), TURBO_OK);
    for (index = 0; index < CNET_CALLBACK_STRESS_PRODUCERS; ++index) {
      producers[index].workers = &workers;
      producers[index].probe = &probe;
      producers[index].producer = index;
      check_equal(
          turbo_thread_create(&threads[index], cnet_callback_stress_publish, &producers[index]),
          TURBO_OK);
    }
    for (index = 0; index < CNET_CALLBACK_STRESS_PRODUCERS; ++index) {
      check_equal(turbo_thread_join(&threads[index]), TURBO_OK);
      turbo_thread_destroy(&threads[index]);
    }
    check_equal(cnet_callback_workers_stop(&workers, CNET_CALLBACK_TEST_TIMEOUT_MS), TURBO_OK);
    info("first order error: producer=%d expected=%d actual=%d",
         atomic_load_explicit(&probe.error_producer, memory_order_acquire),
         atomic_load_explicit(&probe.error_expected, memory_order_acquire),
         atomic_load_explicit(&probe.error_actual, memory_order_acquire));
    check_equal(atomic_load_explicit(&probe.publish_error, memory_order_acquire), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.order_error, memory_order_acquire), 0);
    check_equal(atomic_load_explicit(&probe.invoked, memory_order_acquire),
                CNET_CALLBACK_STRESS_PRODUCERS * CNET_CALLBACK_STRESS_JOBS_PER_PRODUCER);
    check_equal(atomic_load_explicit(&probe.finished, memory_order_acquire),
                CNET_CALLBACK_STRESS_PRODUCERS * CNET_CALLBACK_STRESS_JOBS_PER_PRODUCER);
    check_equal(atomic_load_explicit(&probe.released, memory_order_acquire),
                CNET_CALLBACK_STRESS_PRODUCERS * CNET_CALLBACK_STRESS_JOBS_PER_PRODUCER);
    check_equal(cnet_callback_workers_destroy(&workers), TURBO_OK);
  }

  it("moves an event queue lease without copying its payload again") {
    cnet_event_queue event_queue = {0};
    cnet_callback_workers workers = {0};
    const cnet_event_queue_config event_config = {4u, 2u, 4u};
    const cnet_callback_workers_config callback_config = {1u, 2u, 4u};
    const unsigned char source = 7u;
    const cnet_event event = {CNET_EVENT_RECEIVE,      {1u, 1u}, CNET_EVENT_STATE_NONE, TURBO_OK,
                              CNET_SESSION_STAGE_NONE, &source,  sizeof(source)};
    cnet_event_view leased = {0};
    cnet_callback_handoff_probe probe = {0};
    cnet_callback_job job = {0};

    check_equal(cnet_event_queue_init(&event_queue, &event_config), TURBO_OK);
    check_equal(cnet_callback_workers_init(&workers, &callback_config), TURBO_OK);
    check_equal(cnet_event_queue_publish(&event_queue, &event), TURBO_OK);
    check_equal(cnet_event_queue_take(&event_queue, &leased), TURBO_OK);
    probe.events = &event_queue;
    probe.expected_data = leased.data;
    job.serialization_key = 0u;
    job.invoke = cnet_callback_handoff_invoke;
    job.context = &probe;
    job.event = (cnet_event){leased.kind,  leased.session, leased.state, leased.status,
                             leased.stage, leased.data,    leased.size};
    job.release = cnet_callback_handoff_release;
    job.release_context = &probe;
    job.release_token = leased._sequence;
    check_equal(cnet_callback_workers_publish(&workers, &job), TURBO_OK);
    check_equal(cnet_callback_workers_stop(&workers, CNET_CALLBACK_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.invoked, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&probe.pointer_match, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&probe.observed_value, memory_order_acquire), 7);
    check_equal(atomic_load_explicit(&probe.released, memory_order_acquire), 1);
    check_equal(cnet_callback_workers_destroy(&workers), TURBO_OK);
    check_equal(cnet_event_queue_close(&event_queue), TURBO_OK);
    check_equal(cnet_event_queue_destroy(&event_queue), TURBO_OK);
  }

  it("reports the first lease release failure after draining callbacks") {
    cnet_callback_workers workers = {0};
    const cnet_callback_workers_config config = {1u, 2u, 4u};
    cnet_callback_test_probe probe = {0};
    unsigned char payload = 2u;
    cnet_callback_job job =
        cnet_callback_test_receive_job(0u, (cnet_session_handle){1u, 1u}, &probe, &payload, 1u);
    job.release = cnet_callback_test_release_error;

    check_equal(cnet_callback_workers_init(&workers, &config), TURBO_OK);
    check_equal(cnet_callback_workers_publish(&workers, &job), TURBO_OK);
    check_equal(cnet_callback_workers_stop(&workers, CNET_CALLBACK_TEST_TIMEOUT_MS), TURBO_EIO);
    check_equal(atomic_load_explicit(&probe.finish_count, memory_order_acquire), 1);
    check_equal(cnet_callback_workers_destroy(&workers), TURBO_OK);
  }

  it("rejects invalid and overflowing resident-memory configurations") {
    cnet_callback_workers workers = {0};
    cnet_callback_workers_config config = {0u, 2u, 8u};

    check_equal(cnet_callback_workers_init(&workers, &config), TURBO_EINVAL);
    config = (cnet_callback_workers_config){1u, 3u, 8u};
    check_equal(cnet_callback_workers_init(&workers, &config), TURBO_EINVAL);
    config = (cnet_callback_workers_config){1u, UINT64_C(1) << 63u, 8u};
    check_equal(cnet_callback_workers_init(&workers, &config), TURBO_ERANGE);
  }
}
