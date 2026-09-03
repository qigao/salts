#define TINYTEST_NO_MAIN
#include "tinytest.h"

#include "readiness_backend_contract.h"

#include <salts/error_codes.h>
#include <salts/thread.h>

enum {
  BACKEND_CONTRACT_WAIT_NS = 2000000000ULL,
  BACKEND_CONTRACT_QUIESCENCE_YIELDS = 100000
};

typedef struct backend_contract_probe {
  salts_mutex_t mutex;
  salts_cond_t changed;
  size_t calls;
  salts_readiness_events events;
  int status;
} backend_contract_probe;

static void backend_probe_init(backend_contract_probe *probe) {
  probe->mutex = NULL;
  probe->changed = NULL;
  probe->calls = 0;
  probe->events = 0;
  probe->status = SALTS_OK;
  salts_mutex_init(&probe->mutex);
  salts_cond_init(&probe->changed);
}

static void backend_probe_destroy(backend_contract_probe *probe) {
  salts_cond_destroy(&probe->changed);
  salts_mutex_destroy(&probe->mutex);
}

static void backend_record_callback(void *user, salts_readiness_events events, int status) {
  backend_contract_probe *probe = (backend_contract_probe *)user;
  salts_mutex_lock(&probe->mutex);
  probe->calls += 1u;
  probe->events = events;
  probe->status = status;
  salts_cond_broadcast(&probe->changed);
  salts_mutex_unlock(&probe->mutex);
}

static int backend_probe_wait(backend_contract_probe *probe, size_t calls) {
  int status = SALTS_OK;
  salts_mutex_lock(&probe->mutex);
  while (probe->calls < calls && status == SALTS_OK)
    status = salts_cond_timedwait(&probe->changed, &probe->mutex, BACKEND_CONTRACT_WAIT_NS);
  salts_mutex_unlock(&probe->mutex);
  return status;
}

static size_t backend_probe_calls(backend_contract_probe *probe) {
  size_t calls;
  salts_mutex_lock(&probe->mutex);
  calls = probe->calls;
  salts_mutex_unlock(&probe->mutex);
  return calls;
}

static int backend_wait_callbacks_quiescent(salts_readiness_reactor *reactor) {
  for (size_t i = 0; i < BACKEND_CONTRACT_QUIESCENCE_YIELDS; ++i) {
    salts_readiness_stats stats;
    int status = salts_readiness_reactor_stats(reactor, &stats);
    if (status != SALTS_OK) return status;
    if (stats.callbacks_inflight == 0) return SALTS_OK;
    salts_thread_yield();
  }
  return SALTS_ETIMEDOUT;
}

static readiness_backend_contract_fixture *backend_fixture_create(
    const readiness_backend_contract_factory *factory, size_t capacity, size_t batch,
    salts_readiness_reactor *reactor) {
  salts_readiness_config config = {capacity, batch};
  int status = SALTS_OK;
  readiness_backend_contract_fixture *fixture = factory->create(config, reactor, &status);
  check_equal(status, SALTS_OK);
  check_not_null(fixture);
  check_not_null(reactor->impl);
  return fixture;
}

static void backend_fixture_finish(const readiness_backend_contract_factory *factory,
                                   readiness_backend_contract_fixture *fixture,
                                   salts_readiness_reactor *reactor) {
  check_equal(salts_readiness_reactor_shutdown(reactor), SALTS_OK);
  check_equal(salts_readiness_reactor_destroy(reactor), SALTS_OK);
  factory->destroy(fixture);
}

spec("Platform readiness backend-neutral contract") {
  const readiness_backend_contract_factory *factory =
      readiness_backend_contract_factory_get();

  it("validates config and enforces registration capacity") {
    salts_readiness_reactor invalid_reactor = {(void *)(uintptr_t)1};
    salts_readiness_config invalid_config = {0, 1};
    int status = SALTS_OK;
    readiness_backend_contract_fixture *invalid =
        factory->create(invalid_config, &invalid_reactor, &status);
    check_null(invalid);
    check_equal(status, SALTS_EINVAL);
    check_null(invalid_reactor.impl);

    salts_readiness_reactor reactor = {0};
    salts_readiness_registration first = {0};
    salts_readiness_registration second = {0};
    salts_readiness_registration rejected = {(void *)(uintptr_t)1, 0u};
    readiness_backend_contract_fixture *fixture =
        backend_fixture_create(factory, 2, 2, &reactor);
    check_equal(salts_readiness_register(&reactor, factory->resource(fixture, 0), &first),
                SALTS_OK);
    check_equal(salts_readiness_register(&reactor, factory->resource(fixture, 1), &second),
                SALTS_OK);
    check_equal(salts_readiness_register(&reactor, factory->resource(fixture, 2), &rejected),
                SALTS_ENOBUFS);
    check_null(rejected.impl);
    check_equal(salts_readiness_close(&first), SALTS_OK);
    check_equal(salts_readiness_close(&second), SALTS_OK);
    backend_fixture_finish(factory, fixture, &reactor);
  }

  it("registers arms delivers and rearms one-shot readiness") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_registration registration = {0};
    backend_contract_probe probe;
    readiness_backend_contract_fixture *fixture =
        backend_fixture_create(factory, 1, 1, &reactor);
    backend_probe_init(&probe);

    check_equal(salts_readiness_register(&reactor, factory->resource(fixture, 0),
                                         &registration),
                SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ,
                                    backend_record_callback, &probe),
                SALTS_OK);
    check_equal(factory->make_readable(fixture, 0), SALTS_OK);
    check_equal(backend_probe_wait(&probe, 1), SALTS_OK);
    check_equal(backend_wait_callbacks_quiescent(&reactor), SALTS_OK);
    check_equal(probe.events & SALTS_READINESS_EVENT_READ, SALTS_READINESS_EVENT_READ);
    check_equal(probe.status, SALTS_OK);
    check_equal(factory->drain_readable(fixture, 0), SALTS_OK);

    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ,
                                    backend_record_callback, &probe),
                SALTS_OK);
    check_equal(factory->make_readable(fixture, 0), SALTS_OK);
    check_equal(backend_probe_wait(&probe, 2), SALTS_OK);
    check_equal(backend_wait_callbacks_quiescent(&reactor), SALTS_OK);
    check_equal(backend_probe_calls(&probe), (size_t)2);

    check_equal(salts_readiness_close(&registration), SALTS_OK);
    backend_probe_destroy(&probe);
    backend_fixture_finish(factory, fixture, &reactor);
  }

  it("unarms closes and re-registers without transferring resource ownership") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_registration registration = {0};
    salts_readiness_registration replacement = {0};
    backend_contract_probe probe;
    readiness_backend_contract_fixture *fixture =
        backend_fixture_create(factory, 1, 1, &reactor);
    intptr_t resource = factory->resource(fixture, 0);
    backend_probe_init(&probe);

    check_equal(salts_readiness_register(&reactor, resource, &registration), SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ,
                                    backend_record_callback, &probe),
                SALTS_OK);
    check_equal(salts_readiness_unarm(&registration), SALTS_OK);
    check_equal(factory->make_readable(fixture, 0), SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ,
                                    backend_record_callback, &probe),
                SALTS_OK);
    check_equal(factory->make_readable(fixture, 0), SALTS_OK);
    check_equal(backend_probe_wait(&probe, 1), SALTS_OK);
    check_equal(backend_probe_calls(&probe), (size_t)1);
    check_equal(factory->drain_readable(fixture, 0), SALTS_OK);
    check_equal(salts_readiness_close(&registration), SALTS_OK);

    check_equal(salts_readiness_register(&reactor, resource, &replacement), SALTS_OK);
    check_equal(salts_readiness_close(&replacement), SALTS_OK);
    backend_probe_destroy(&probe);
    backend_fixture_finish(factory, fixture, &reactor);
  }

  it("delivers one exact terminal callback before shutdown becomes quiescent") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_registration registration = {0};
    backend_contract_probe probe;
    readiness_backend_contract_fixture *fixture =
        backend_fixture_create(factory, 1, 1, &reactor);
    backend_probe_init(&probe);

    check_equal(salts_readiness_register(&reactor, factory->resource(fixture, 0),
                                         &registration),
                SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ,
                                    backend_record_callback, &probe),
                SALTS_OK);
    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(backend_probe_calls(&probe), (size_t)1);
    check_equal(probe.events, (salts_readiness_events)0);
    check_equal(probe.status, SALTS_ESHUTDOWN);
    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    backend_probe_destroy(&probe);
    factory->destroy(fixture);
  }
}
