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
  CONTRACT_RESOURCE_C = 303
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
  int entered;
  int released;
} callback_probe;

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

static void record_callback(void *user, turbo_readiness_events events,
                            int status) {
  callback_probe *probe = (callback_probe *)user;
  turbo_readiness_stats stats;
  probe->calls += 1;
  probe->events = events;
  probe->status = status;
  probe->stats_status = turbo_readiness_reactor_stats(probe->reactor, &stats);
}

static void close_from_callback(void *user, turbo_readiness_events events,
                                int status) {
  callback_probe *probe = (callback_probe *)user;
  record_callback(user, events, status);
  probe->close_status = turbo_readiness_close(probe->registration);
}

static void blocking_callback(void *user, turbo_readiness_events events,
                              int status) {
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

static void emit_worker(void *user) {
  emit_thread_args *args = (emit_thread_args *)user;
  args->status = args->factory->emit_resource(
      args->fixture, args->resource, TURBO_READINESS_EVENT_READ);
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

static readiness_contract_fixture *create_fixture(
    const readiness_contract_factory *factory, size_t capacity,
    size_t batch_capacity, turbo_readiness_reactor *reactor) {
  int status = TURBO_OK;
  turbo_readiness_config config = {capacity, batch_capacity};
  readiness_contract_fixture *fixture = factory->create(config, reactor, &status);
  check_equal(status, TURBO_OK);
  check_not_null(fixture);
  check_not_null(reactor->impl);
  return fixture;
}

static void close_and_destroy_fixture(
    const readiness_contract_factory *factory,
    readiness_contract_fixture *fixture, turbo_readiness_reactor *reactor) {
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
  }

  group("registration lifecycle") {
    it("registers arms wakes and rearms one-shot callbacks") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 2, 2, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A,
                                           &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      record_callback, &probe), TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A,
                                         TURBO_READINESS_EVENT_READ), TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(probe.events, TURBO_READINESS_EVENT_READ);
      check_equal(probe.status, TURBO_OK);
      check_equal(probe.stats_status, TURBO_OK);

      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_WRITE,
                                      record_callback, &probe), TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A,
                                         TURBO_READINESS_EVENT_WRITE), TURBO_OK);
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

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A,
                                           &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      record_callback, &probe), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      record_callback, &probe), TURBO_EALREADY);
      check_equal(turbo_readiness_unarm(&registration), TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A,
                                         TURBO_READINESS_EVENT_READ), TURBO_OK);
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

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &first),
                  TURBO_OK);
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

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &first),
                  TURBO_OK);
      uint64_t old_token = factory->token_for_resource(fixture, CONTRACT_RESOURCE_A);
      check_not_equal(old_token, (uint64_t)0);
      check_equal(turbo_readiness_arm(&first, TURBO_READINESS_EVENT_READ,
                                      record_callback, &first_probe), TURBO_OK);
      check_equal(factory->emit_token(fixture, old_token,
                                      TURBO_READINESS_EVENT_READ, TURBO_OK), TURBO_OK);
      check_equal(factory->emit_token(fixture, old_token,
                                      TURBO_READINESS_EVENT_READ, TURBO_OK), TURBO_OK);
      check_equal(first_probe.calls, 1);
      check_equal(turbo_readiness_close(&first), TURBO_OK);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &second),
                  TURBO_OK);
      uint64_t new_token = factory->token_for_resource(fixture, CONTRACT_RESOURCE_A);
      check_not_equal(new_token, old_token);
      check_equal(turbo_readiness_arm(&second, TURBO_READINESS_EVENT_READ,
                                      record_callback, &second_probe), TURBO_OK);
      check_equal(factory->emit_token(fixture, old_token,
                                      TURBO_READINESS_EVENT_READ, TURBO_OK), TURBO_OK);
      check_equal(second_probe.calls, 0);
      check_equal(factory->emit_token(fixture, new_token,
                                      TURBO_READINESS_EVENT_READ, TURBO_OK), TURBO_OK);
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

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A,
                                           &registration), TURBO_OK);
      factory->fail_next_arm(fixture, TURBO_EIO);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      record_callback, &probe), TURBO_EIO);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      record_callback, &probe), TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A,
                                         TURBO_READINESS_EVENT_READ), TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
    }
  }

  group("quiescence and callback reentrancy") {
    it("defers callback-close reclamation without deadlocking") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      turbo_readiness_stats stats;
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A,
                                           &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      close_from_callback, &probe), TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A,
                                         TURBO_READINESS_EVENT_READ), TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(probe.close_status, TURBO_EBUSY);
      check_null(registration.impl);
      check_equal(turbo_readiness_reactor_stats(&reactor, &stats), TURBO_OK);
      check_equal(stats.registered_count, (size_t)0);
      check_equal(stats.callbacks_inflight, (size_t)0);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
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

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A,
                                           &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      blocking_callback, &probe), TURBO_OK);
      check_equal(turbo_thread_create(&emit_thread, emit_worker, &emit_args), TURBO_OK);
      wait_probe_entered(&probe);
      check_equal(turbo_thread_create(&close_thread, close_worker, &close_args), TURBO_OK);
      while (!atomic_load(&close_args.started) ||
             factory->backend_close_calls(fixture) == 0)
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

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A,
                                           &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      blocking_callback, &probe), TURBO_OK);
      check_equal(turbo_thread_create(&emit_thread, emit_worker, &emit_args), TURBO_OK);
      wait_probe_entered(&probe);
      check_equal(turbo_thread_create(&unarm_thread, unarm_worker, &unarm_args),
                  TURBO_OK);
      while (!atomic_load(&unarm_args.started) ||
             factory->backend_unarm_calls(fixture) == 0)
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

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A,
                                           &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      record_callback, &probe), TURBO_OK);
      check_equal(factory->emit_resource(fixture, CONTRACT_RESOURCE_A,
                                         TURBO_READINESS_EVENT_READ), TURBO_OK);
      check_equal(probe.stats_status, TURBO_OK);
      check(factory->backend_reentrant_checks(fixture) >= (size_t)2);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      callback_probe_destroy(&probe);
      close_and_destroy_fixture(factory, fixture, &reactor);
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

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A, &first),
                  TURBO_OK);
      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_B, &second),
                  TURBO_OK);
      check_equal(turbo_readiness_arm(&first, TURBO_READINESS_EVENT_READ,
                                      record_callback, &first_probe), TURBO_OK);
      check_equal(turbo_readiness_arm(&second, TURBO_READINESS_EVENT_WRITE,
                                      record_callback, &second_probe), TURBO_OK);

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

    it("delivers shutdown once and reports repeated shutdown") {
      turbo_readiness_reactor reactor = {0};
      turbo_readiness_registration registration = {0};
      readiness_contract_fixture *fixture = create_fixture(factory, 1, 1, &reactor);
      callback_probe probe = callback_probe_init(&reactor, &registration);

      check_equal(turbo_readiness_register(&reactor, CONTRACT_RESOURCE_A,
                                           &registration), TURBO_OK);
      check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                      record_callback, &probe), TURBO_OK);
      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
      check_equal(probe.calls, 1);
      check_equal(probe.status, TURBO_ESHUTDOWN);
      check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_EALREADY);
      check_equal(turbo_readiness_close(&registration), TURBO_OK);
      check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
      callback_probe_destroy(&probe);
      factory->destroy(fixture);
    }
  }
}
