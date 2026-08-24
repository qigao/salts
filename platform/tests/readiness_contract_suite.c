#define TINYTEST_NO_MAIN
#include "tinytest.h"

#include "readiness_contract_suite.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>

enum {
  CONTRACT_RESOURCE_A = 101,
  CONTRACT_RESOURCE_B = 202,
  CONTRACT_RESOURCE_C = 303,
  CONTRACT_SHORT_WAIT_NS = 20 * 1000 * 1000,
  CONTRACT_LONG_WAIT_NS = 2 * 1000 * 1000 * 1000
};

typedef struct callback_probe {
  turbo_readiness_reactor *reactor;
  turbo_readiness_registration *registration;
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  int calls;
  turbo_readiness_events events;
  int status;
  int stats_status;
  int close_status;
  int unarm_status;
  int entered;
  int released;
  int close_completed;
} callback_probe;

typedef struct close_other_probe {
  callback_probe base;
  turbo_readiness_registration *other;
  int other_close_status;
} close_other_probe;

typedef struct cross_control_probe {
  callback_probe base;
  turbo_readiness_registration *close_other;
  turbo_readiness_registration *unarm_other;
  int close_other_status;
  int unarm_other_status;
  int control_started;
} cross_control_probe;

typedef struct emit_thread_args {
  const readiness_contract_factory *factory;
  readiness_contract_fixture *fixture;
  intptr_t resource;
  int status;
} emit_thread_args;

typedef struct close_thread_args {
  turbo_readiness_registration *registration;
  atomic_int started;
  atomic_int completed;
  int status;
} close_thread_args;

typedef close_thread_args unarm_thread_args;

typedef struct arm_thread_args {
  turbo_readiness_registration *registration;
  turbo_readiness_callback callback;
  void *user;
  int status;
} arm_thread_args;

typedef struct register_thread_args {
  turbo_readiness_reactor *reactor;
  intptr_t resource;
  turbo_readiness_registration registration;
  int status;
} register_thread_args;

typedef struct destroy_thread_args {
  turbo_readiness_reactor *reactor;
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  int entered;
  int released;
  int completed;
  int status;
} destroy_thread_args;

typedef struct terminal_thread_args {
  const readiness_contract_factory *factory;
  readiness_contract_fixture *fixture;
  turbo_readiness_reactor *reactor;
  int terminal_status;
  int result;
} terminal_thread_args;

static callback_probe callback_probe_init(turbo_readiness_reactor *reactor,
                                          turbo_readiness_registration *reg) {
  callback_probe probe = {0};
  probe.reactor = reactor;
  probe.registration = reg;
  turbo_mutex_init(&probe.mutex);
  turbo_cond_init(&probe.changed);
  return probe;
}

static void callback_probe_destroy(callback_probe *probe) {
  turbo_cond_destroy(&probe->changed);
  turbo_mutex_destroy(&probe->mutex);
}

static void record_callback(void *user, turbo_readiness_events events, int status) {
  callback_probe *probe = (callback_probe *)user;
  turbo_readiness_stats stats;
  probe->calls += 1;
  probe->events = events;
  probe->status = status;
  probe->stats_status = turbo_readiness_reactor_stats(probe->reactor, &stats);
}

static void close_from_callback(void *user, turbo_readiness_events events, int status) {
  callback_probe *probe = (callback_probe *)user;
  record_callback(user, events, status);
  probe->close_status = turbo_readiness_close(probe->registration);
}

static void unarm_from_callback(void *user, turbo_readiness_events events, int status) {
  callback_probe *probe = (callback_probe *)user;
  record_callback(user, events, status);
  probe->unarm_status = turbo_readiness_unarm(probe->registration);
}

static void close_other_from_callback(void *user, turbo_readiness_events events, int status) {
  close_other_probe *probe = (close_other_probe *)user;
  record_callback(&probe->base, events, status);
  probe->other_close_status = turbo_readiness_close(probe->other);
}

static void blocking_callback(void *user, turbo_readiness_events events, int status) {
  callback_probe *probe = (callback_probe *)user;
  turbo_mutex_lock(&probe->mutex);
  probe->calls += 1;
  probe->events = events;
  probe->status = status;
  probe->entered = 1;
  turbo_cond_broadcast(&probe->changed);
  while (!probe->released)
    turbo_cond_wait(&probe->changed, &probe->mutex);
  turbo_mutex_unlock(&probe->mutex);
}

static void blocking_close_callback(void *user, turbo_readiness_events events, int status) {
  callback_probe *probe = (callback_probe *)user;
  turbo_mutex_lock(&probe->mutex);
  probe->calls += 1;
  probe->events = events;
  probe->status = status;
  probe->entered = 1;
  turbo_cond_broadcast(&probe->changed);
  while (!probe->released)
    turbo_cond_wait(&probe->changed, &probe->mutex);
  turbo_mutex_unlock(&probe->mutex);

  probe->close_status = turbo_readiness_close(probe->registration);

  turbo_mutex_lock(&probe->mutex);
  probe->close_completed = 1;
  turbo_cond_broadcast(&probe->changed);
  turbo_mutex_unlock(&probe->mutex);
}

static void cross_controls_after_release(void *user, turbo_readiness_events events, int status) {
  cross_control_probe *probe = (cross_control_probe *)user;
  turbo_mutex_lock(&probe->base.mutex);
  probe->base.calls += 1;
  probe->base.events = events;
  probe->base.status = status;
  probe->base.entered = 1;
  turbo_cond_broadcast(&probe->base.changed);
  while (!probe->base.released)
    turbo_cond_wait(&probe->base.changed, &probe->base.mutex);
  turbo_mutex_unlock(&probe->base.mutex);

  probe->close_other_status = turbo_readiness_close(probe->close_other);
  probe->unarm_other_status = turbo_readiness_unarm(probe->unarm_other);

  turbo_mutex_lock(&probe->base.mutex);
  probe->base.close_completed = 1;
  turbo_cond_broadcast(&probe->base.changed);
  turbo_mutex_unlock(&probe->base.mutex);
}

static void close_other_after_release(void *user, turbo_readiness_events events, int status) {
  cross_control_probe *probe = (cross_control_probe *)user;
  turbo_mutex_lock(&probe->base.mutex);
  probe->base.calls += 1;
  probe->base.events = events;
  probe->base.status = status;
  probe->base.entered = 1;
  turbo_cond_broadcast(&probe->base.changed);
  while (!probe->base.released)
    turbo_cond_wait(&probe->base.changed, &probe->base.mutex);
  probe->control_started = 1;
  turbo_cond_broadcast(&probe->base.changed);
  turbo_mutex_unlock(&probe->base.mutex);

  probe->close_other_status = turbo_readiness_close(probe->close_other);

  turbo_mutex_lock(&probe->base.mutex);
  probe->base.close_completed = 1;
  turbo_cond_broadcast(&probe->base.changed);
  turbo_mutex_unlock(&probe->base.mutex);
}

static void wait_probe_control_started(cross_control_probe *probe) {
  turbo_mutex_lock(&probe->base.mutex);
  while (!probe->control_started)
    turbo_cond_wait(&probe->base.changed, &probe->base.mutex);
  turbo_mutex_unlock(&probe->base.mutex);
}

static void emit_worker(void *user) {
  emit_thread_args *args = (emit_thread_args *)user;
  args->status =
      args->factory->emit_resource(args->fixture, args->resource, TURBO_READINESS_EVENT_READ);
}

static void close_worker(void *user) {
  close_thread_args *args = (close_thread_args *)user;
  atomic_store(&args->started, 1);
  args->status = turbo_readiness_close(args->registration);
  atomic_store(&args->completed, 1);
}

static void unarm_worker(void *user) {
  unarm_thread_args *args = (unarm_thread_args *)user;
  atomic_store(&args->started, 1);
  args->status = turbo_readiness_unarm(args->registration);
  atomic_store(&args->completed, 1);
}

static void arm_worker(void *user) {
  arm_thread_args *args = (arm_thread_args *)user;
  args->status = turbo_readiness_arm(args->registration, TURBO_READINESS_EVENT_READ, args->callback,
                                     args->user);
}

static void register_worker(void *user) {
  register_thread_args *args = (register_thread_args *)user;
  args->status = turbo_readiness_register(args->reactor, args->resource, &args->registration);
}

static void destroy_worker(void *user) {
  destroy_thread_args *args = (destroy_thread_args *)user;
  turbo_mutex_lock(&args->mutex);
  args->entered = 1;
  turbo_cond_broadcast(&args->changed);
  while (!args->released)
    turbo_cond_wait(&args->changed, &args->mutex);
  turbo_mutex_unlock(&args->mutex);

  args->status = turbo_readiness_reactor_destroy(args->reactor);

  turbo_mutex_lock(&args->mutex);
  args->completed = 1;
  turbo_cond_broadcast(&args->changed);
  turbo_mutex_unlock(&args->mutex);
}

static void fatal_worker(void *user) {
  terminal_thread_args *args = (terminal_thread_args *)user;
  args->result = args->factory->fail_backend(args->fixture, args->terminal_status);
}

static void shutdown_worker(void *user) {
  terminal_thread_args *args = (terminal_thread_args *)user;
  args->result = turbo_readiness_reactor_shutdown(args->reactor);
}

static void wait_probe_entered(callback_probe *probe) {
  turbo_mutex_lock(&probe->mutex);
  while (!probe->entered)
    turbo_cond_wait(&probe->changed, &probe->mutex);
  turbo_mutex_unlock(&probe->mutex);
}

static void release_probe(callback_probe *probe) {
  turbo_mutex_lock(&probe->mutex);
  probe->released = 1;
  turbo_cond_broadcast(&probe->changed);
  turbo_mutex_unlock(&probe->mutex);
}

static int wait_probe_close_completed(callback_probe *probe, uint64_t timeout_ns) {
  int status = TURBO_OK;
  turbo_mutex_lock(&probe->mutex);
  while (!probe->close_completed && status == TURBO_OK)
    status = turbo_cond_timedwait(&probe->changed, &probe->mutex, timeout_ns);
  turbo_mutex_unlock(&probe->mutex);
  return status;
}

static readiness_contract_fixture *create_fixture(const readiness_contract_factory *factory,
                                                  size_t capacity, size_t batch_capacity,
                                                  turbo_readiness_reactor *reactor) {
  int status = TURBO_OK;
  turbo_readiness_config config = {capacity, batch_capacity};
  readiness_contract_fixture *fixture = factory->create(config, reactor, &status);
  check_equal(status, TURBO_OK);
  check_not_null(fixture);
  check_not_null(reactor->impl);
  return fixture;
}

static void close_and_destroy_fixture(const readiness_contract_factory *factory,
                                      readiness_contract_fixture *fixture,
                                      turbo_readiness_reactor *reactor) {
  check_equal(turbo_readiness_reactor_shutdown(reactor), TURBO_OK);
  check_equal(turbo_readiness_reactor_destroy(reactor), TURBO_OK);
  check_null(reactor->impl);
  factory->destroy(fixture);
}

spec("Platform readiness contract") {
  const readiness_contract_factory *factory = readiness_contract_factory_get();

  group("bounded initialization") {
    it("rejects zero capacities and leaves the output empty") {
      turbo_readiness_reactor reactor = {(void *)(uintptr_t)1};
      int status = 0;
      turbo_readiness_config config = {0, 1};
      readiness_contract_fixture *fixture = factory->create(config, &reactor, &status);
      check_null(fixture);
      check_equal(status, TURBO_EINVAL);
      check_null(reactor.impl);

      reactor.impl = (void *)(uintptr_t)1;
      config.registration_capacity = 1;
      config.event_batch_capacity = 0;
      fixture = factory->create(config, &reactor, &status);
      check_null(fixture);
      check_equal(status, TURBO_EINVAL);
      check_null(reactor.impl);
    }

    it("rejects arithmetic and token-index overflow") {
      turbo_readiness_reactor reactor = {(void *)(uintptr_t)1};
      int status = 0;
      turbo_readiness_config config = {SIZE_MAX, 1};
      readiness_contract_fixture *fixture = factory->create(config, &reactor, &status);
      check_null(fixture);
      check_equal(status, TURBO_ERANGE);
      check_null(reactor.impl);

      reactor.impl = (void *)(uintptr_t)1;
      config.registration_capacity = (size_t)UINT32_MAX;
      fixture = factory->create(config, &reactor, &status);
      check_null(fixture);
      check_equal(status, TURBO_ERANGE);
      check_null(reactor.impl);
    }

    it("rejects an event batch larger than capacity plus control") {
      turbo_readiness_reactor reactor = {(void *)(uintptr_t)1};
      int status = 0;
      turbo_readiness_config config = {2, 4};
      readiness_contract_fixture *fixture = factory->create(config, &reactor, &status);
      check_null(fixture);
      check_equal(status, TURBO_EINVAL);
      check_null(reactor.impl);
    }

    it("uses the configured registration capacity in the fake backend") {
      enum { LARGE_FAKE_CAPACITY = 12 };
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registrations[LARGE_FAKE_CAPACITY] = {{0}};
      readiness_contract_fixture *fixture =
          create_fixture(factory, LARGE_FAKE_CAPACITY, LARGE_FAKE_CAPACITY, &reactor);

      for (size_t i = 0; i < LARGE_FAKE_CAPACITY; ++i)
        check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A + (intptr_t)i,
                                             &registrations[i]),
                    TURBO_OK);
      for (size_t i = 0; i < LARGE_FAKE_CAPACITY; ++i) {
        if (registrations[i].impl != NULL)
          check_equal(turbo_readiness_close(&registrations[i]), TURBO_OK);
      }
      close_and_destroy_fixture(factory, fixture, &reactor);
    }
  }

  group("registration lifecycle") {
    it("registers arms wakes and rearms one-shot callbacks") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 2, 2, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback, &probe),
          TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A, TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(probe.events, TURBO_READINESS_EVENT_READ);
      check_equal(probe.status, TURBO_OK);
      check_equal(probe.stats_status, TURBO_OK);

      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_WRITE, record_callback, &probe),
          TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A, TURBO_READINESS_EVENT_WRITE),
                  TURBO_OK);
      check_equal(probe.calls, 2);
      check_equal(probe.events, TURBO_READINESS_EVENT_WRITE);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      check_null(registration.impl);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("unarms without delivering and rejects duplicate arm") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback, &probe),
          TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback, &probe),
          TURBO_EALREADY);
      check_equal(turbo_readiness_unarm(&registration), TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A, TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      check_equal(probe.calls, 0);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("rejects full admission without mutating the output handle") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration first = {0};
      turbo_readiness_registration rejected = {(void *)(uintptr_t)1};
      turbo_readiness_stats stats;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &first), TURBO_OK);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_B, &rejected),
                  TURBO_ENOBUFS);
      check_null(rejected.impl);
      check_equal(turbo_readiness_reactor_stats(&reactor, &stats), TURBO_OK);
      check_equal(stats.capacity, (size_t)1);
      check_equal(stats.registered_count, (size_t)1);
      check_equal(stats.rejected_full, (uint64_t)1);

      check_equal(turbo_readiness_close(&first), TURBO_OK);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("suppresses duplicate and stale tokens across resource reuse") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration first = {0};
      turbo_readiness_registration second = {0};
      turbo_readiness_stats stats;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe first_probe = callback_probe_init(&reactor, &first);
      callback_probe second_probe = callback_probe_init(&reactor, &second);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &first), TURBO_OK);
      uint64_t old_token = factory->token_for_resource(fixture, CONTRACT_RESOURCE_A);
      check_not_equal(old_token, (uint64_t)0);
      check_equal(
          turbo_readiness_arm(&first, TURBO_READINESS_EVENT_READ, record_callback, &first_probe),
          TURBO_OK);
      check_equal(factory->emit_token(fixture, old_token, TURBO_READINESS_EVENT_READ, TURBO_OK),
                  TURBO_OK);
      check_equal(factory->emit_token(fixture, old_token, TURBO_READINESS_EVENT_READ, TURBO_OK),
                  TURBO_OK);
      check_equal(first_probe.calls, 1);
      check_equal(turbo_readiness_close(&first), TURBO_OK);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &second), TURBO_OK);
      uint64_t new_token = factory->token_for_resource(fixture, CONTRACT_RESOURCE_A);
      check_not_equal(new_token, old_token);
      check_equal(
          turbo_readiness_arm(&second, TURBO_READINESS_EVENT_READ, record_callback, &second_probe),
          TURBO_OK);
      check_equal(factory->emit_token(fixture, old_token, TURBO_READINESS_EVENT_READ, TURBO_OK),
                  TURBO_OK);
      check_equal(second_probe.calls, 0);
      check_equal(factory->emit_token(fixture, new_token, TURBO_READINESS_EVENT_READ, TURBO_OK),
                  TURBO_OK);
      check_equal(second_probe.calls, 1);

      check_equal(turbo_readiness_reactor_stats(&reactor, &stats), TURBO_OK);
      check_equal(stats.duplicate_events, (uint64_t)1);
      check_equal(stats.stale_events, (uint64_t)1);
      check_equal(turbo_readiness_close(&second), TURBO_OK);
      callback_probe_destroy(&second_probe);
      callback_probe_destroy(&first_probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("propagates an exact backend arm error and remains rearmable") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      factory->fail_next_arm(fixture, TURBO_EIO);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback, &probe),
          TURBO_EIO);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback, &probe),
          TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A, TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("rejects an event copied from an earlier arm generation") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      turbo_readiness_stats stats;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback, &probe),
          TURBO_OK);
      uint64_t token = factory->token_for_resource(fixture, CONTRACT_RESOURCE_A);
      uint64_t stale_arm_token =
          factory->arm_token_for_resource(fixture, CONTRACT_RESOURCE_A);
      check_not_equal(stale_arm_token, (uint64_t)0);
      check_equal(turbo_readiness_unarm(&registration), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback, &probe),
          TURBO_OK);
      check_not_equal(factory->arm_token_for_resource(fixture, CONTRACT_RESOURCE_A),
                      stale_arm_token);

      check_equal(factory->emit_arm_token(fixture, token, stale_arm_token,
                                          TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      check_equal(probe.calls, 0);
      uint64_t current_arm_token =
          factory->arm_token_for_resource(fixture, CONTRACT_RESOURCE_A);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A,
                                         TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(factory->emit_arm_token(fixture, token, current_arm_token,
                                          TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(turbo_readiness_reactor_stats(&reactor, &stats), TURBO_OK);
      check_equal(stats.stale_events, (uint64_t)1);
      check_equal(stats.duplicate_events, (uint64_t)1);

      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }
  }

  group("backend failure ownership") {
    it("releases a failed backend register slot for caller retry") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {(void *)(uintptr_t)1};
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);

      factory->fail_hook(fixture, READINESS_CONTRACT_HOOK_REGISTER, TURBO_EIO, 1);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration),
                  TURBO_EIO);
      check_null(registration.impl);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_not_null(registration.impl);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("restores an armed registration after backend unarm failure") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback, &probe),
          TURBO_OK);
      factory->fail_hook(fixture, READINESS_CONTRACT_HOOK_UNARM, TURBO_EIO, 1);
      check_equal(turbo_readiness_unarm(&registration), TURBO_EIO);
      check_not_null(registration.impl);
      check_equal(turbo_readiness_unarm(&registration), TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A, TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      check_equal(probe.calls, 0);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("retains close ownership after backend close failure") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      factory->fail_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE, TURBO_EIO, 1);
      check_equal(turbo_readiness_close(&registration), TURBO_EIO);
      check_not_null(registration.impl);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      check_null(registration.impl);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("keeps failed shutdown retryable and blocks destroy") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback, &probe),
          TURBO_OK);
      factory->fail_hook(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN, TURBO_EIO, 1);
      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_EIO);
      check_equal(probe.calls, 1);
      check_equal(probe.status, TURBO_ESHUTDOWN);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_EBUSY);
      check_not_null(reactor.impl);
      check_not_null(registration.impl);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      callback_probe_destroy(&probe);
      factory->destroy(fixture);
    }
  }

  group("quiescence and callback reentrancy") {
    it("retains callback-close ownership until a successful retry") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      turbo_readiness_stats stats;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      close_from_callback, &probe),
                  TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A, TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(probe.close_status, TURBO_EBUSY);
      check_not_null(registration.impl);
      check_equal(factory->backend_close_calls(fixture), (size_t)0);
      check_equal(turbo_readiness_reactor_stats(&reactor, &stats), TURBO_OK);
      check_equal(stats.registered_count, (size_t)1);
      check_equal(stats.callbacks_inflight, (size_t)0);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      check_null(registration.impl);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("rejects callback-unarm without waiting for itself") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      unarm_from_callback, &probe),
                  TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A, TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(probe.unarm_status, TURBO_EBUSY);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("returns callback-close before a blocked shutdown hook can join it") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      turbo_thread_t emit_thread = NULL;
      turbo_thread_t shutdown_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);
      emit_thread_args emit_args = {factory, fixture, CONTRACT_RESOURCE_A, TURBO_EIO};
      terminal_thread_args shutdown_args = {factory, fixture, &reactor, TURBO_ESHUTDOWN, TURBO_EIO};
      int close_completed_before_shutdown_release;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      blocking_close_callback, &probe),
                  TURBO_OK);
      check_equal(turbo_thread_create(&emit_thread, emit_worker, &emit_args), TURBO_OK);
      wait_probe_entered(&probe);

      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN);
      check_equal(turbo_thread_create(&shutdown_thread, shutdown_worker, &shutdown_args), TURBO_OK);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      release_probe(&probe);
      close_completed_before_shutdown_release =
          wait_probe_close_completed(&probe, CONTRACT_SHORT_WAIT_NS) == TURBO_OK;
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN);

      check_equal(turbo_thread_join(&emit_thread), TURBO_OK);
      check_equal(turbo_thread_join(&shutdown_thread), TURBO_OK);
      check_equal(close_completed_before_shutdown_release, 1);
      check_equal(probe.close_status, TURBO_EBUSY);
      check_not_null(registration.impl);
      check_equal(factory->backend_close_calls(fixture), (size_t)0);
      check_equal(shutdown_args.result, TURBO_OK);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      check_null(registration.impl);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      callback_probe_destroy(&probe);
      factory->destroy(fixture);
    }

    it("external close waits until an inflight callback returns") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      turbo_thread_t emit_thread = NULL;
      turbo_thread_t close_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);
      emit_thread_args emit_args = {factory, fixture, CONTRACT_RESOURCE_A, TURBO_EIO};
      close_thread_args close_args;
      atomic_init(&close_args.started, 0);
      atomic_init(&close_args.completed, 0);
      close_args.registration = &registration;
      close_args.status = TURBO_EIO;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, blocking_callback, &probe),
          TURBO_OK);
      check_equal(turbo_thread_create(&emit_thread, emit_worker, &emit_args), TURBO_OK);
      wait_probe_entered(&probe);
      check_equal(turbo_thread_create(&close_thread, close_worker, &close_args), TURBO_OK);
      while (!atomic_load(&close_args.started) || factory->backend_close_calls(fixture) == 0)
        turbo_thread_yield();
      check_equal(atomic_load(&close_args.completed), 0);

      release_probe(&probe);
      check_equal(turbo_thread_join(&emit_thread), TURBO_OK);
      check_equal(turbo_thread_join(&close_thread), TURBO_OK);
      check_equal(emit_args.status, TURBO_OK);
      check_equal(close_args.status, TURBO_OK);
      check_equal(atomic_load(&close_args.completed), 1);
      check_null(registration.impl);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("external unarm waits until an inflight callback returns") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      turbo_thread_t emit_thread = NULL;
      turbo_thread_t unarm_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);
      emit_thread_args emit_args = {factory, fixture, CONTRACT_RESOURCE_A, TURBO_EIO};
      unarm_thread_args unarm_args;
      atomic_init(&unarm_args.started, 0);
      atomic_init(&unarm_args.completed, 0);
      unarm_args.registration = &registration;
      unarm_args.status = TURBO_EIO;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, blocking_callback, &probe),
          TURBO_OK);
      check_equal(turbo_thread_create(&emit_thread, emit_worker, &emit_args), TURBO_OK);
      wait_probe_entered(&probe);
      check_equal(turbo_thread_create(&unarm_thread, unarm_worker, &unarm_args), TURBO_OK);
      while (!atomic_load(&unarm_args.started) || factory->backend_unarm_calls(fixture) == 0)
        turbo_thread_yield();
      check_equal(atomic_load(&unarm_args.completed), 0);

      release_probe(&probe);
      check_equal(turbo_thread_join(&emit_thread), TURBO_OK);
      check_equal(turbo_thread_join(&unarm_thread), TURBO_OK);
      check_equal(emit_args.status, TURBO_OK);
      check_equal(unarm_args.status, TURBO_OK);
      check_equal(atomic_load(&unarm_args.completed), 1);
      check_not_null(registration.impl);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("invokes backend hooks and user callbacks outside the reactor lock") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback, &probe),
          TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A, TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      check_equal(probe.stats_status, TURBO_OK);
      check(factory->backend_reentrant_checks(fixture) >= (size_t)2);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }
  }

  group("backend control serialization") {
    it("serializes an arm hook before a close hook on the same slot") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      turbo_thread_t arm_thread = NULL;
      turbo_thread_t close_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);
      arm_thread_args arm_args = {&registration, record_callback, &probe, TURBO_EIO};
      close_thread_args close_args;
      int close_entered_early;
      atomic_init(&close_args.started, 0);
      atomic_init(&close_args.completed, 0);
      close_args.registration = &registration;
      close_args.status = TURBO_EIO;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_ARM);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE);
      check_equal(turbo_thread_create(&arm_thread, arm_worker, &arm_args), TURBO_OK);
      check_equal(
          factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_ARM, 1, CONTRACT_LONG_WAIT_NS),
          TURBO_OK);
      check_equal(turbo_thread_create(&close_thread, close_worker, &close_args), TURBO_OK);
      close_entered_early = factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_CLOSE, 1,
                                                     CONTRACT_SHORT_WAIT_NS) == TURBO_OK;

      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_ARM);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_CLOSE, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE);
      check_equal(turbo_thread_join(&arm_thread), TURBO_OK);
      check_equal(turbo_thread_join(&close_thread), TURBO_OK);

      check_equal(close_entered_early, 0);
      check_equal(arm_args.status, TURBO_OK);
      check_equal(close_args.status, TURBO_OK);
      check_null(registration.impl);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("serializes a firing unarm hook before its rearm hook") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      turbo_thread_t emit_thread = NULL;
      turbo_thread_t unarm_thread = NULL;
      turbo_thread_t arm_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);
      emit_thread_args emit_args = {factory, fixture, CONTRACT_RESOURCE_A, TURBO_EIO};
      unarm_thread_args unarm_args;
      arm_thread_args arm_args = {&registration, record_callback, &probe, TURBO_EIO};
      int arm_entered_early;
      atomic_init(&unarm_args.started, 0);
      atomic_init(&unarm_args.completed, 0);
      unarm_args.registration = &registration;
      unarm_args.status = TURBO_EIO;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, blocking_callback, &probe),
          TURBO_OK);
      check_equal(turbo_thread_create(&emit_thread, emit_worker, &emit_args), TURBO_OK);
      wait_probe_entered(&probe);

      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_UNARM);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_ARM);
      check_equal(turbo_thread_create(&unarm_thread, unarm_worker, &unarm_args), TURBO_OK);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_UNARM, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      release_probe(&probe);
      check_equal(turbo_thread_join(&emit_thread), TURBO_OK);
      check_equal(turbo_thread_create(&arm_thread, arm_worker, &arm_args), TURBO_OK);
      arm_entered_early = factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_ARM, 2,
                                                   CONTRACT_SHORT_WAIT_NS) == TURBO_OK;

      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_UNARM);
      check_equal(
          factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_ARM, 2, CONTRACT_LONG_WAIT_NS),
          TURBO_OK);
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_ARM);
      check_equal(turbo_thread_join(&unarm_thread), TURBO_OK);
      check_equal(turbo_thread_join(&arm_thread), TURBO_OK);

      check_equal(arm_entered_early, 0);
      check_equal(unarm_args.status, TURBO_OK);
      check_equal(arm_args.status, TURBO_OK);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("rolls back register before a concurrent shutdown hook") {
      turbo_readiness_reactor reactor = {0};
      turbo_thread_t register_thread = NULL;
      turbo_thread_t shutdown_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      register_thread_args register_args = {&reactor, CONTRACT_RESOURCE_A, {0}, TURBO_EIO};
      terminal_thread_args shutdown_args = {factory, fixture, &reactor, TURBO_ESHUTDOWN, TURBO_EIO};
      int shutdown_entered_early;
      uint64_t close_sequence;
      uint64_t shutdown_sequence;

      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_REGISTER);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN);
      check_equal(turbo_thread_create(&register_thread, register_worker, &register_args), TURBO_OK);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_REGISTER, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      check_equal(turbo_thread_create(&shutdown_thread, shutdown_worker, &shutdown_args), TURBO_OK);
      shutdown_entered_early = factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN,
                                                        1, CONTRACT_SHORT_WAIT_NS) == TURBO_OK;

      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_REGISTER);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN);
      check_equal(turbo_thread_join(&register_thread), TURBO_OK);
      check_equal(turbo_thread_join(&shutdown_thread), TURBO_OK);
      close_sequence = factory->hook_last_sequence(fixture, READINESS_CONTRACT_HOOK_CLOSE);
      shutdown_sequence = factory->hook_last_sequence(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN);

      check_equal(shutdown_entered_early, 0);
      check_equal(register_args.status, TURBO_ESHUTDOWN);
      check_null(register_args.registration.impl);
      check(close_sequence != 0);
      check(close_sequence < shutdown_sequence);
      check_equal(shutdown_args.result, TURBO_OK);
      if (register_args.registration.impl != NULL)
        check_equal(turbo_readiness_close(&register_args.registration), TURBO_OK);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      factory->destroy(fixture);
    }

    it("keeps failed register rollback reactor-owned for shutdown retry") {
      turbo_readiness_reactor reactor = {0};
      turbo_thread_t register_thread = NULL;
      turbo_thread_t shutdown_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      register_thread_args register_args = {&reactor, CONTRACT_RESOURCE_A, {0}, TURBO_OK};
      terminal_thread_args shutdown_args = {factory, fixture, &reactor, TURBO_ESHUTDOWN, TURBO_OK};

      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_REGISTER);
      factory->fail_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE, TURBO_EIO, 2);
      check_equal(turbo_thread_create(&register_thread, register_worker, &register_args), TURBO_OK);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_REGISTER, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      check_equal(turbo_thread_create(&shutdown_thread, shutdown_worker, &shutdown_args), TURBO_OK);
      check_equal(factory->wait_admission_closed(fixture), TURBO_OK);
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_REGISTER);
      check_equal(turbo_thread_join(&register_thread), TURBO_OK);
      check_equal(turbo_thread_join(&shutdown_thread), TURBO_OK);

      check_equal(register_args.status, TURBO_EIO);
      check_null(register_args.registration.impl);
      check_equal(shutdown_args.result, TURBO_EIO);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_EBUSY);
      check_not_null(reactor.impl);
      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      factory->destroy(fixture);
    }

    it("waits for an already-started close hook before destroy") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      turbo_thread_t close_thread = NULL;
      turbo_thread_t destroy_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      close_thread_args close_args;
      destroy_thread_args destroy_args = {0};
      int destroy_completed_early;
      atomic_init(&close_args.started, 0);
      atomic_init(&close_args.completed, 0);
      close_args.registration = &registration;
      close_args.status = TURBO_EIO;
      destroy_args.reactor = &reactor;
      destroy_args.status = TURBO_EIO;
      turbo_mutex_init(&destroy_args.mutex);
      turbo_cond_init(&destroy_args.changed);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE);
      check_equal(turbo_thread_create(&close_thread, close_worker, &close_args), TURBO_OK);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_CLOSE, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);

      check_equal(turbo_thread_create(&destroy_thread, destroy_worker, &destroy_args), TURBO_OK);
      turbo_mutex_lock(&destroy_args.mutex);
      while (!destroy_args.entered)
        turbo_cond_wait(&destroy_args.changed, &destroy_args.mutex);
      destroy_args.released = 1;
      turbo_cond_broadcast(&destroy_args.changed);
      while (!destroy_args.completed) {
        if (turbo_cond_timedwait(&destroy_args.changed, &destroy_args.mutex,
                                 CONTRACT_SHORT_WAIT_NS) != TURBO_OK)
          break;
      }
      destroy_completed_early = destroy_args.completed;
      turbo_mutex_unlock(&destroy_args.mutex);

      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE);
      check_equal(turbo_thread_join(&close_thread), TURBO_OK);
      check_equal(turbo_thread_join(&destroy_thread), TURBO_OK);
      check_equal(destroy_completed_early, 0);
      check_equal(close_args.status, TURBO_OK);
      check_equal(destroy_args.status, TURBO_OK);
      check_null(reactor.impl);
      turbo_cond_destroy(&destroy_args.changed);
      turbo_mutex_destroy(&destroy_args.mutex);
      factory->destroy(fixture);
    }
  }

  group("terminal control gate") {
    it("makes a callback close waiting on arm fail fast when fatal starts") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration callback_registration = {0};
      turbo_readiness_registration target = {0};
      turbo_thread_t emit_thread = NULL;
      turbo_thread_t arm_thread = NULL;
      turbo_thread_t fatal_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 2, 2, &reactor);
      cross_control_probe closer = {0};
      callback_probe terminal_probe = callback_probe_init(&reactor, &target);
      emit_thread_args emit_args = {factory, fixture, CONTRACT_RESOURCE_A, TURBO_EIO};
      arm_thread_args arm_args = {&target, record_callback, &terminal_probe, TURBO_EIO};
      terminal_thread_args fatal_args = {factory, fixture, &reactor, TURBO_EIO, TURBO_EINVAL};
      closer.base = callback_probe_init(&reactor, &callback_registration);
      closer.close_other = &target;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &callback_registration),
                  TURBO_OK);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_B, &target), TURBO_OK);
      check_equal(turbo_readiness_arm(&callback_registration, TURBO_READINESS_EVENT_READ,
                                      close_other_after_release, &closer),
                  TURBO_OK);
      check_equal(turbo_thread_create(&emit_thread, emit_worker, &emit_args), TURBO_OK);
      wait_probe_entered(&closer.base);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_ARM);
      check_equal(turbo_thread_create(&arm_thread, arm_worker, &arm_args), TURBO_OK);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_ARM, 2,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      release_probe(&closer.base);
      wait_probe_control_started(&closer);
      check_equal(turbo_thread_create(&fatal_thread, fatal_worker, &fatal_args), TURBO_OK);
      check_equal(factory->wait_admission_closed(fixture), TURBO_OK);
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_ARM);
      check_equal(turbo_thread_join(&arm_thread), TURBO_OK);
      check_equal(turbo_thread_join(&emit_thread), TURBO_OK);
      check_equal(turbo_thread_join(&fatal_thread), TURBO_OK);

      check_equal(arm_args.status, TURBO_OK);
      check_equal(closer.close_other_status, TURBO_EBUSY);
      check_not_null(target.impl);
      check_equal(terminal_probe.calls, 1);
      check_equal(terminal_probe.status, TURBO_EIO);
      check_equal(turbo_readiness_close(&callback_registration), TURBO_OK);
      check_equal(turbo_readiness_close(&target), TURBO_OK);
      callback_probe_destroy(&terminal_probe);
      callback_probe_destroy(&closer.base);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("makes a callback close waiting on failed unarm fail fast when shutdown starts") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration callback_registration = {0};
      turbo_readiness_registration target = {0};
      turbo_thread_t emit_thread = NULL;
      turbo_thread_t unarm_thread = NULL;
      turbo_thread_t shutdown_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 2, 2, &reactor);
      cross_control_probe closer = {0};
      callback_probe terminal_probe = callback_probe_init(&reactor, &target);
      emit_thread_args emit_args = {factory, fixture, CONTRACT_RESOURCE_A, TURBO_EIO};
      unarm_thread_args unarm_args;
      terminal_thread_args shutdown_args = {factory, fixture, &reactor, TURBO_ESHUTDOWN,
                                            TURBO_EINVAL};
      atomic_init(&unarm_args.started, 0);
      atomic_init(&unarm_args.completed, 0);
      unarm_args.registration = &target;
      unarm_args.status = TURBO_OK;
      closer.base = callback_probe_init(&reactor, &callback_registration);
      closer.close_other = &target;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &callback_registration),
                  TURBO_OK);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_B, &target), TURBO_OK);
      check_equal(turbo_readiness_arm(&callback_registration, TURBO_READINESS_EVENT_READ,
                                      close_other_after_release, &closer),
                  TURBO_OK);
      check_equal(turbo_readiness_arm(&target, TURBO_READINESS_EVENT_READ, record_callback,
                                      &terminal_probe),
                  TURBO_OK);
      check_equal(turbo_thread_create(&emit_thread, emit_worker, &emit_args), TURBO_OK);
      wait_probe_entered(&closer.base);
      factory->fail_hook(fixture, READINESS_CONTRACT_HOOK_UNARM, TURBO_EIO, 1);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_UNARM);
      check_equal(turbo_thread_create(&unarm_thread, unarm_worker, &unarm_args), TURBO_OK);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_UNARM, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      release_probe(&closer.base);
      wait_probe_control_started(&closer);
      check_equal(turbo_thread_create(&shutdown_thread, shutdown_worker, &shutdown_args), TURBO_OK);
      check_equal(factory->wait_admission_closed(fixture), TURBO_OK);
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_UNARM);
      check_equal(turbo_thread_join(&unarm_thread), TURBO_OK);
      check_equal(turbo_thread_join(&emit_thread), TURBO_OK);
      check_equal(turbo_thread_join(&shutdown_thread), TURBO_OK);

      check_equal(unarm_args.status, TURBO_EIO);
      check_equal(closer.close_other_status, TURBO_EBUSY);
      check_not_null(target.impl);
      check_equal(terminal_probe.calls, 1);
      check_equal(terminal_probe.status, TURBO_ESHUTDOWN);
      check_equal(turbo_readiness_close(&callback_registration), TURBO_OK);
      check_equal(turbo_readiness_close(&target), TURBO_OK);
      callback_probe_destroy(&terminal_probe);
      callback_probe_destroy(&closer.base);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      factory->destroy(fixture);
    }

    it("snapshots an armed slot after its blocked close hook fails during fatal") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      turbo_thread_t close_thread = NULL;
      turbo_thread_t fatal_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);
      close_thread_args close_args;
      terminal_thread_args fatal_args = {factory, fixture, &reactor, TURBO_EIO, TURBO_OK};
      atomic_init(&close_args.started, 0);
      atomic_init(&close_args.completed, 0);
      close_args.registration = &registration;
      close_args.status = TURBO_OK;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback,
                                      &probe),
                  TURBO_OK);
      factory->fail_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE, TURBO_EIO, 1);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE);
      check_equal(turbo_thread_create(&close_thread, close_worker, &close_args), TURBO_OK);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_CLOSE, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      check_equal(turbo_thread_create(&fatal_thread, fatal_worker, &fatal_args), TURBO_OK);
      check_equal(factory->wait_admission_closed(fixture), TURBO_OK);
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE);
      check_equal(turbo_thread_join(&close_thread), TURBO_OK);
      check_equal(turbo_thread_join(&fatal_thread), TURBO_OK);

      check_equal(close_args.status, TURBO_EIO);
      check_not_null(registration.impl);
      check_equal(fatal_args.result, TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(probe.status, TURBO_EIO);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }

    it("snapshots an armed slot after its blocked close hook fails during shutdown") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      turbo_thread_t close_thread = NULL;
      turbo_thread_t shutdown_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);
      close_thread_args close_args;
      terminal_thread_args shutdown_args = {factory, fixture, &reactor, TURBO_ESHUTDOWN, TURBO_OK};
      atomic_init(&close_args.started, 0);
      atomic_init(&close_args.completed, 0);
      close_args.registration = &registration;
      close_args.status = TURBO_OK;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback,
                                      &probe),
                  TURBO_OK);
      factory->fail_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE, TURBO_EIO, 1);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE);
      check_equal(turbo_thread_create(&close_thread, close_worker, &close_args), TURBO_OK);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_CLOSE, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      check_equal(turbo_thread_create(&shutdown_thread, shutdown_worker, &shutdown_args), TURBO_OK);
      check_equal(factory->wait_admission_closed(fixture), TURBO_OK);
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE);
      check_equal(turbo_thread_join(&close_thread), TURBO_OK);
      check_equal(turbo_thread_join(&shutdown_thread), TURBO_OK);

      check_equal(close_args.status, TURBO_EIO);
      check_not_null(registration.impl);
      check_equal(shutdown_args.result, TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(probe.status, TURBO_ESHUTDOWN);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      factory->destroy(fixture);
    }

    it("fails callback cross-slot controls fast while shutdown joins callbacks") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration callback_registration = {0};
      turbo_readiness_registration close_registration = {0};
      turbo_readiness_registration unarm_registration = {0};
      turbo_thread_t emit_thread = NULL;
      turbo_thread_t shutdown_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 3, 3, &reactor);
      cross_control_probe probe = {0};
      callback_probe terminal_probe = callback_probe_init(&reactor, &unarm_registration);
      emit_thread_args emit_args = {factory, fixture, CONTRACT_RESOURCE_A, TURBO_EIO};
      terminal_thread_args shutdown_args = {factory, fixture, &reactor, TURBO_ESHUTDOWN, TURBO_EIO};
      int controls_completed_before_shutdown_release;
      probe.base = callback_probe_init(&reactor, &callback_registration);
      probe.close_other = &close_registration;
      probe.unarm_other = &unarm_registration;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &callback_registration),
                  TURBO_OK);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_B, &close_registration),
                  TURBO_OK);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_C, &unarm_registration),
                  TURBO_OK);
      check_equal(turbo_readiness_arm(&callback_registration, TURBO_READINESS_EVENT_READ,
                                      cross_controls_after_release, &probe),
                  TURBO_OK);
      check_equal(turbo_readiness_arm(&unarm_registration, TURBO_READINESS_EVENT_READ,
                                      record_callback, &terminal_probe),
                  TURBO_OK);
      check_equal(turbo_thread_create(&emit_thread, emit_worker, &emit_args), TURBO_OK);
      wait_probe_entered(&probe.base);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN);
      check_equal(turbo_thread_create(&shutdown_thread, shutdown_worker, &shutdown_args), TURBO_OK);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      release_probe(&probe.base);
      controls_completed_before_shutdown_release =
          wait_probe_close_completed(&probe.base, CONTRACT_SHORT_WAIT_NS) == TURBO_OK;
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN);
      check_equal(turbo_thread_join(&emit_thread), TURBO_OK);
      check_equal(turbo_thread_join(&shutdown_thread), TURBO_OK);

      check_equal(controls_completed_before_shutdown_release, 1);
      check_equal(probe.close_other_status, TURBO_EBUSY);
      check_equal(probe.unarm_other_status, TURBO_EBUSY);
      check_not_null(close_registration.impl);
      check_not_null(unarm_registration.impl);
      check_equal(terminal_probe.calls, 1);
      check_equal(turbo_readiness_close(&callback_registration), TURBO_OK);
      check_equal(turbo_readiness_close(&close_registration), TURBO_OK);
      check_equal(turbo_readiness_close(&unarm_registration), TURBO_OK);
      callback_probe_destroy(&terminal_probe);
      callback_probe_destroy(&probe.base);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      factory->destroy(fixture);
    }

    it("keeps public controls out of orphan retry and backend shutdown") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration other = {0};
      turbo_thread_t register_thread = NULL;
      turbo_thread_t shutdown_thread = NULL;
      turbo_thread_t close_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 2, 2, &reactor);
      register_thread_args register_args = {&reactor, CONTRACT_RESOURCE_A, {0}, TURBO_OK};
      terminal_thread_args shutdown_args = {factory, fixture, &reactor, TURBO_ESHUTDOWN, TURBO_OK};
      close_thread_args close_args;
      int close_entered_during_retry;
      int close_entered_during_shutdown;
      atomic_init(&close_args.started, 0);
      atomic_init(&close_args.completed, 0);
      close_args.registration = &other;
      close_args.status = TURBO_EIO;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_B, &other), TURBO_OK);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_REGISTER);
      factory->fail_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE, TURBO_EIO, 1);
      factory->block_hook_on_call(fixture, READINESS_CONTRACT_HOOK_CLOSE, 2);
      factory->block_hook(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN);
      check_equal(turbo_thread_create(&register_thread, register_worker, &register_args), TURBO_OK);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_REGISTER, 2,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      check_equal(turbo_thread_create(&shutdown_thread, shutdown_worker, &shutdown_args), TURBO_OK);
      check_equal(factory->wait_admission_closed(fixture), TURBO_OK);
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_REGISTER);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_CLOSE, 2,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      check_equal(turbo_thread_create(&close_thread, close_worker, &close_args), TURBO_OK);
      close_entered_during_retry = factory->wait_hook_calls(
          fixture, READINESS_CONTRACT_HOOK_CLOSE, 3, CONTRACT_SHORT_WAIT_NS) == TURBO_OK;
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_CLOSE);
      check_equal(factory->wait_hook_calls(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN, 1,
                                           CONTRACT_LONG_WAIT_NS),
                  TURBO_OK);
      close_entered_during_shutdown = factory->wait_hook_calls(
          fixture, READINESS_CONTRACT_HOOK_CLOSE, 3, CONTRACT_SHORT_WAIT_NS) == TURBO_OK;
      factory->release_hook(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN);
      check_equal(turbo_thread_join(&register_thread), TURBO_OK);
      check_equal(turbo_thread_join(&shutdown_thread), TURBO_OK);
      check_equal(turbo_thread_join(&close_thread), TURBO_OK);

      check_equal(close_entered_during_retry, 0);
      check_equal(close_entered_during_shutdown, 0);
      check_equal(register_args.status, TURBO_EIO);
      check_null(register_args.registration.impl);
      check_equal(shutdown_args.result, TURBO_OK);
      check_equal(close_args.status, TURBO_OK);
      check_null(other.impl);
      check(factory->hook_last_sequence(fixture, READINESS_CONTRACT_HOOK_CLOSE) >
            factory->hook_last_sequence(fixture, READINESS_CONTRACT_HOOK_SHUTDOWN));
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      factory->destroy(fixture);
    }
  }

  group("fatal backend errors and shutdown") {
    it("fans a fatal backend error to every armed slot exactly once") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration first = {0};
      turbo_readiness_registration second = {0};
      turbo_readiness_registration rejected = {(void *)(uintptr_t)1};
      turbo_readiness_stats stats;
      readiness_contract_fixture *fixture = create_fixture(factory, 3, 3, &reactor);
      callback_probe first_probe = callback_probe_init(&reactor, &first);
      callback_probe second_probe = callback_probe_init(&reactor, &second);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &first), TURBO_OK);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_B, &second), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&first, TURBO_READINESS_EVENT_READ, record_callback, &first_probe),
          TURBO_OK);
      check_equal(
          turbo_readiness_arm(&second, TURBO_READINESS_EVENT_WRITE, record_callback, &second_probe),
          TURBO_OK);

      check_equal(factory->fail_backend(fixture, TURBO_EIO), TURBO_OK);
      check_equal(first_probe.calls, 1);
      check_equal(second_probe.calls, 1);
      check_equal(first_probe.status, TURBO_EIO);
      check_equal(second_probe.status, TURBO_EIO);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_C, &rejected),
                  TURBO_ESHUTDOWN);
      check_null(rejected.impl);
      check_equal(turbo_readiness_reactor_stats(&reactor, &stats), TURBO_OK);
      check_equal(stats.backend_errors, (uint64_t)1);
      check_equal(stats.armed_count, (size_t)0);

      check_equal(turbo_readiness_close(&first), TURBO_OK);
      check_equal(turbo_readiness_close(&second), TURBO_OK);
      callback_probe_destroy(&second_probe);
      callback_probe_destroy(&first_probe);
      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      factory->destroy(fixture);
    }

    it("keeps a fatal snapshot when its first callback closes the second") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration first = {0};
      turbo_readiness_registration second = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 2, 2, &reactor);
      close_other_probe first_probe = {0};
      callback_probe second_probe = callback_probe_init(&reactor, &second);
      first_probe.base = callback_probe_init(&reactor, &first);
      first_probe.other = &second;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &first), TURBO_OK);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_B, &second), TURBO_OK);
      check_equal(turbo_readiness_arm(&first, TURBO_READINESS_EVENT_READ, close_other_from_callback,
                                      &first_probe),
                  TURBO_OK);
      check_equal(
          turbo_readiness_arm(&second, TURBO_READINESS_EVENT_READ, record_callback, &second_probe),
          TURBO_OK);

      check_equal(factory->fail_backend(fixture, TURBO_EIO), TURBO_OK);
      check_equal(first_probe.base.calls, 1);
      check_equal(first_probe.other_close_status, TURBO_EBUSY);
      check_equal(second_probe.calls, 1);
      check_equal(second_probe.status, TURBO_EIO);
      check_not_null(second.impl);
      check_equal(factory->backend_close_calls(fixture), (size_t)0);

      check_equal(turbo_readiness_close(&first), TURBO_OK);
      check_equal(turbo_readiness_close(&second), TURBO_OK);
      check_null(second.impl);
      callback_probe_destroy(&second_probe);
      callback_probe_destroy(&first_probe.base);
      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      factory->destroy(fixture);
    }

    it("prevents ordinary dispatch from overtaking a fatal snapshot") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration first = {0};
      turbo_readiness_registration second = {0};
      turbo_thread_t fatal_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 2, 2, &reactor);
      callback_probe first_probe = callback_probe_init(&reactor, &first);
      callback_probe second_probe = callback_probe_init(&reactor, &second);
      terminal_thread_args args = {factory, fixture, &reactor, TURBO_EIO, TURBO_EINVAL};

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &first), TURBO_OK);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_B, &second), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&first, TURBO_READINESS_EVENT_READ, blocking_callback, &first_probe),
          TURBO_OK);
      check_equal(
          turbo_readiness_arm(&second, TURBO_READINESS_EVENT_READ, record_callback, &second_probe),
          TURBO_OK);
      check_equal(turbo_thread_create(&fatal_thread, fatal_worker, &args), TURBO_OK);
      wait_probe_entered(&first_probe);

      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_B, TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      int calls_before_release = second_probe.calls;
      release_probe(&first_probe);
      check_equal(turbo_thread_join(&fatal_thread), TURBO_OK);

      check_equal(calls_before_release, 0);
      check_equal(args.result, TURBO_OK);
      check_equal(second_probe.calls, 1);
      check_equal(second_probe.status, TURBO_EIO);
      check_equal(turbo_readiness_close(&first), TURBO_OK);
      check_equal(turbo_readiness_close(&second), TURBO_OK);
      callback_probe_destroy(&second_probe);
      callback_probe_destroy(&first_probe);
      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      factory->destroy(fixture);
    }

    it("delivers shutdown once and reports repeated shutdown") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &registration), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback, &probe),
          TURBO_OK);
      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(probe.status, TURBO_ESHUTDOWN);
      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_EALREADY);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      callback_probe_destroy(&probe);
      factory->destroy(fixture);
    }

    it("keeps a shutdown snapshot when its first callback closes the second") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration first = {0};
      turbo_readiness_registration second = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 2, 2, &reactor);
      close_other_probe first_probe = {0};
      callback_probe second_probe = callback_probe_init(&reactor, &second);
      first_probe.base = callback_probe_init(&reactor, &first);
      first_probe.other = &second;

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &first), TURBO_OK);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_B, &second), TURBO_OK);
      check_equal(turbo_readiness_arm(&first, TURBO_READINESS_EVENT_READ, close_other_from_callback,
                                      &first_probe),
                  TURBO_OK);
      check_equal(
          turbo_readiness_arm(&second, TURBO_READINESS_EVENT_READ, record_callback, &second_probe),
          TURBO_OK);

      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
      check_equal(first_probe.base.calls, 1);
      check_equal(first_probe.other_close_status, TURBO_EBUSY);
      check_equal(second_probe.calls, 1);
      check_equal(second_probe.status, TURBO_ESHUTDOWN);
      check_not_null(second.impl);
      check_equal(factory->backend_close_calls(fixture), (size_t)0);

      check_equal(turbo_readiness_close(&first), TURBO_OK);
      check_equal(turbo_readiness_close(&second), TURBO_OK);
      check_null(second.impl);
      callback_probe_destroy(&second_probe);
      callback_probe_destroy(&first_probe.base);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      factory->destroy(fixture);
    }

    it("prevents ordinary dispatch from overtaking a shutdown snapshot") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration first = {0};
      turbo_readiness_registration second = {0};
      turbo_thread_t shutdown_thread = NULL;
      readiness_contract_fixture *fixture = create_fixture(factory, 2, 2, &reactor);
      callback_probe first_probe = callback_probe_init(&reactor, &first);
      callback_probe second_probe = callback_probe_init(&reactor, &second);
      terminal_thread_args args = {factory, fixture, &reactor, TURBO_ESHUTDOWN, TURBO_EINVAL};

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &first), TURBO_OK);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_B, &second), TURBO_OK);
      check_equal(
          turbo_readiness_arm(&first, TURBO_READINESS_EVENT_READ, blocking_callback, &first_probe),
          TURBO_OK);
      check_equal(
          turbo_readiness_arm(&second, TURBO_READINESS_EVENT_READ, record_callback, &second_probe),
          TURBO_OK);
      check_equal(turbo_thread_create(&shutdown_thread, shutdown_worker, &args), TURBO_OK);
      wait_probe_entered(&first_probe);

      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_B, TURBO_READINESS_EVENT_READ),
                  TURBO_OK);
      int calls_before_release = second_probe.calls;
      release_probe(&first_probe);
      check_equal(turbo_thread_join(&shutdown_thread), TURBO_OK);

      check_equal(calls_before_release, 0);
      check_equal(args.result, TURBO_OK);
      check_equal(second_probe.calls, 1);
      check_equal(second_probe.status, TURBO_ESHUTDOWN);
      check_equal(turbo_readiness_close(&first), TURBO_OK);
      check_equal(turbo_readiness_close(&second), TURBO_OK);
      callback_probe_destroy(&second_probe);
      callback_probe_destroy(&first_probe);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      factory->destroy(fixture);
    }
  }
}
