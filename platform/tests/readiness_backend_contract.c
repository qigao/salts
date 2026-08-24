#define TINYTEST_NO_MAIN
#include "tinytest.h"

#include "readiness_backend_contract.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

enum {
  BACKEND_CONTRACT_WAIT_NS = 2000000000ULL,
  BACKEND_CONTRACT_QUIESCENCE_YIELDS = 100000
};

typedef struct backend_contract_probe {
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  size_t calls;
  turbo_readiness_events events;
  int status;
} backend_contract_probe;

static void backend_probe_init(backend_contract_probe *probe) {
  probe->mutex = NULL;
  probe->changed = NULL;
  probe->calls = 0;
  probe->events = 0;
  probe->status = TURBO_OK;
  turbo_mutex_init(&probe->mutex);
  turbo_cond_init(&probe->changed);
}

static void backend_probe_destroy(backend_contract_probe *probe) {
  turbo_cond_destroy(&probe->changed);
  turbo_mutex_destroy(&probe->mutex);
}

static void backend_record_callback(void *user, turbo_readiness_events events, int status) {
  backend_contract_probe *probe = (backend_contract_probe *)user;
  turbo_mutex_lock(&probe->mutex);
  probe->calls += 1u;
  probe->events = events;
  probe->status = status;
  turbo_cond_broadcast(&probe->changed);
  turbo_mutex_unlock(&probe->mutex);
}

static int backend_probe_wait(backend_contract_probe *probe, size_t calls) {
  int status = TURBO_OK;
  turbo_mutex_lock(&probe->mutex);
  while (probe->calls < calls && status == TURBO_OK)
    status = turbo_cond_timedwait(&probe->changed, &probe->mutex, BACKEND_CONTRACT_WAIT_NS);
  turbo_mutex_unlock(&probe->mutex);
  return status;
}

static size_t backend_probe_calls(backend_contract_probe *probe) {
  size_t calls;
  turbo_mutex_lock(&probe->mutex);
  calls = probe->calls;
  turbo_mutex_unlock(&probe->mutex);
  return calls;
}

static int backend_wait_callbacks_quiescent(turbo_readiness_reactor *reactor) {
  for (size_t i = 0; i < BACKEND_CONTRACT_QUIESCENCE_YIELDS; ++i) {
    turbo_readiness_stats stats;
    int status = turbo_readiness_reactor_stats(reactor, &stats);
    if (status != TURBO_OK) return status;
    if (stats.callbacks_inflight == 0) return TURBO_OK;
    turbo_thread_yield();
  }
  return TURBO_ETIMEDOUT;
}

static readiness_backend_contract_fixture *backend_fixture_create(
    const readiness_backend_contract_factory *factory, size_t capacity, size_t batch,
    turbo_readiness_reactor *reactor) {
  turbo_readiness_config config = {capacity, batch};
  int status = TURBO_OK;
  readiness_backend_contract_fixture *fixture = factory->create(config, reactor, &status);
  check_equal(status, TURBO_OK);
  check_not_null(fixture);
  check_not_null(reactor->impl);
  return fixture;
}

static void backend_fixture_finish(const readiness_backend_contract_factory *factory,
                                   readiness_backend_contract_fixture *fixture,
                                   turbo_readiness_reactor *reactor) {
  check_equal(turbo_readiness_reactor_shutdown(reactor), TURBO_OK);
  check_equal(turbo_readiness_reactor_destroy(reactor), TURBO_OK);
  factory->destroy(fixture);
}

spec("Platform readiness backend-neutral contract") {
  const readiness_backend_contract_factory *factory =
      readiness_backend_contract_factory_get();

  it("validates config and enforces registration capacity") {
    turbo_readiness_reactor invalid_reactor = {(void *)(uintptr_t)1};
    turbo_readiness_config invalid_config = {0, 1};
    int status = TURBO_OK;
    readiness_backend_contract_fixture *invalid =
        factory->create(invalid_config, &invalid_reactor, &status);
    check_null(invalid);
    check_equal(status, TURBO_EINVAL);
    check_null(invalid_reactor.impl);

    turbo_readiness_reactor reactor = {0};
    turbo_readiness_registration first = {0};
    turbo_readiness_registration second = {0};
    turbo_readiness_registration rejected = {(void *)(uintptr_t)1, 0u};
    readiness_backend_contract_fixture *fixture =
        backend_fixture_create(factory, 2, 2, &reactor);
    check_equal(turbo_readiness_register(&reactor, factory->resource(fixture, 0), &first),
                TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, factory->resource(fixture, 1), &second),
                TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, factory->resource(fixture, 2), &rejected),
                TURBO_ENOBUFS);
    check_null(rejected.impl);
    check_equal(turbo_readiness_close(&first), TURBO_OK);
    check_equal(turbo_readiness_close(&second), TURBO_OK);
    backend_fixture_finish(factory, fixture, &reactor);
  }

  it("registers arms delivers and rearms one-shot readiness") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_registration registration = {0};
    backend_contract_probe probe;
    readiness_backend_contract_fixture *fixture =
        backend_fixture_create(factory, 1, 1, &reactor);
    backend_probe_init(&probe);

    check_equal(turbo_readiness_register(&reactor, factory->resource(fixture, 0),
                                         &registration),
                TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                    backend_record_callback, &probe),
                TURBO_OK);
    check_equal(factory->make_readable(fixture, 0), TURBO_OK);
    check_equal(backend_probe_wait(&probe, 1), TURBO_OK);
    check_equal(backend_wait_callbacks_quiescent(&reactor), TURBO_OK);
    check_equal(probe.events & TURBO_READINESS_EVENT_READ, TURBO_READINESS_EVENT_READ);
    check_equal(probe.status, TURBO_OK);
    check_equal(factory->drain_readable(fixture, 0), TURBO_OK);

    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                    backend_record_callback, &probe),
                TURBO_OK);
    check_equal(factory->make_readable(fixture, 0), TURBO_OK);
    check_equal(backend_probe_wait(&probe, 2), TURBO_OK);
    check_equal(backend_wait_callbacks_quiescent(&reactor), TURBO_OK);
    check_equal(backend_probe_calls(&probe), (size_t)2);

    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    backend_probe_destroy(&probe);
    backend_fixture_finish(factory, fixture, &reactor);
  }

  it("unarms closes and re-registers without transferring resource ownership") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_registration registration = {0};
    turbo_readiness_registration replacement = {0};
    backend_contract_probe probe;
    readiness_backend_contract_fixture *fixture =
        backend_fixture_create(factory, 1, 1, &reactor);
    intptr_t resource = factory->resource(fixture, 0);
    backend_probe_init(&probe);

    check_equal(turbo_readiness_register(&reactor, resource, &registration), TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                    backend_record_callback, &probe),
                TURBO_OK);
    check_equal(turbo_readiness_unarm(&registration), TURBO_OK);
    check_equal(factory->make_readable(fixture, 0), TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                    backend_record_callback, &probe),
                TURBO_OK);
    check_equal(factory->make_readable(fixture, 0), TURBO_OK);
    check_equal(backend_probe_wait(&probe, 1), TURBO_OK);
    check_equal(backend_probe_calls(&probe), (size_t)1);
    check_equal(factory->drain_readable(fixture, 0), TURBO_OK);
    check_equal(turbo_readiness_close(&registration), TURBO_OK);

    check_equal(turbo_readiness_register(&reactor, resource, &replacement), TURBO_OK);
    check_equal(turbo_readiness_close(&replacement), TURBO_OK);
    backend_probe_destroy(&probe);
    backend_fixture_finish(factory, fixture, &reactor);
  }

  it("delivers one exact terminal callback before shutdown becomes quiescent") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_registration registration = {0};
    backend_contract_probe probe;
    readiness_backend_contract_fixture *fixture =
        backend_fixture_create(factory, 1, 1, &reactor);
    backend_probe_init(&probe);

    check_equal(turbo_readiness_register(&reactor, factory->resource(fixture, 0),
                                         &registration),
                TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                    backend_record_callback, &probe),
                TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(backend_probe_calls(&probe), (size_t)1);
    check_equal(probe.events, (turbo_readiness_events)0);
    check_equal(probe.status, TURBO_ESHUTDOWN);
    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    backend_probe_destroy(&probe);
    factory->destroy(fixture);
  }
}
