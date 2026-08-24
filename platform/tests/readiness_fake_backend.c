#include "readiness_fake_backend.h"

#include "../src/readiness_internal.h"

#include <turbo/error_codes.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum { READINESS_FAKE_MAX_REGISTRATIONS = 8 };

typedef struct readiness_fake_record {
  intptr_t native_resource;
  uint64_t token;
  turbo_readiness_events events;
  int active;
} readiness_fake_record;

struct readiness_contract_fixture {
  turbo_readiness_reactor *reactor;
  readiness_fake_record records[READINESS_FAKE_MAX_REGISTRATIONS];
  int next_arm_error;
  atomic_size_t close_calls;
  atomic_size_t unarm_calls;
  atomic_size_t reentrant_checks;
};

static readiness_fake_record *fake_find_resource(
    readiness_contract_fixture *fixture, intptr_t native_resource) {
  for (size_t i = 0; i < READINESS_FAKE_MAX_REGISTRATIONS; ++i) {
    if (fixture->records[i].active &&
        fixture->records[i].native_resource == native_resource)
      return &fixture->records[i];
  }
  return NULL;
}

static readiness_fake_record *fake_find_token(
    readiness_contract_fixture *fixture, uint64_t token) {
  for (size_t i = 0; i < READINESS_FAKE_MAX_REGISTRATIONS; ++i) {
    if (fixture->records[i].active && fixture->records[i].token == token)
      return &fixture->records[i];
  }
  return NULL;
}

static void fake_check_reentrant(readiness_contract_fixture *fixture) {
  turbo_readiness_stats stats;
  if (turbo_readiness_reactor_stats(fixture->reactor, &stats) == TURBO_OK)
    atomic_fetch_add(&fixture->reentrant_checks, 1u);
}

static int fake_register_resource(void *user, intptr_t native_resource,
                                  uint64_t token) {
  readiness_contract_fixture *fixture = (readiness_contract_fixture *)user;
  fake_check_reentrant(fixture);
  if (fake_find_resource(fixture, native_resource) != NULL) return TURBO_EALREADY;
  for (size_t i = 0; i < READINESS_FAKE_MAX_REGISTRATIONS; ++i) {
    if (!fixture->records[i].active) {
      fixture->records[i].native_resource = native_resource;
      fixture->records[i].token = token;
      fixture->records[i].events = 0;
      fixture->records[i].active = 1;
      return TURBO_OK;
    }
  }
  return TURBO_ENOBUFS;
}

static int fake_arm(void *user, uint64_t token,
                    turbo_readiness_events events) {
  readiness_contract_fixture *fixture = (readiness_contract_fixture *)user;
  readiness_fake_record *record;
  fake_check_reentrant(fixture);
  atomic_fetch_add(&fixture->unarm_calls, 1u);
  if (fixture->next_arm_error != TURBO_OK) {
    int status = fixture->next_arm_error;
    fixture->next_arm_error = TURBO_OK;
    return status;
  }
  record = fake_find_token(fixture, token);
  if (record == NULL) return TURBO_EINVAL;
  record->events = events;
  return TURBO_OK;
}

static int fake_unarm(void *user, uint64_t token) {
  readiness_contract_fixture *fixture = (readiness_contract_fixture *)user;
  readiness_fake_record *record;
  fake_check_reentrant(fixture);
  record = fake_find_token(fixture, token);
  if (record == NULL) return TURBO_EINVAL;
  record->events = 0;
  return TURBO_OK;
}

static int fake_close(void *user, uint64_t token) {
  readiness_contract_fixture *fixture = (readiness_contract_fixture *)user;
  readiness_fake_record *record;
  fake_check_reentrant(fixture);
  atomic_fetch_add(&fixture->close_calls, 1u);
  record = fake_find_token(fixture, token);
  if (record == NULL) return TURBO_EINVAL;
  memset(record, 0, sizeof(*record));
  return TURBO_OK;
}

static int fake_shutdown(void *user) {
  readiness_contract_fixture *fixture = (readiness_contract_fixture *)user;
  fake_check_reentrant(fixture);
  return TURBO_OK;
}

static const turbo_readiness_backend_ops fake_backend_ops = {
    fake_register_resource, fake_arm, fake_unarm, fake_close, fake_shutdown};

static readiness_contract_fixture *fake_create(turbo_readiness_config config,
                                               turbo_readiness_reactor *reactor,
                                               int *status) {
  readiness_contract_fixture *fixture;
  if (reactor != NULL) reactor->impl = NULL;
  if (status == NULL) return NULL;
  *status = TURBO_EINVAL;
  if (reactor == NULL) return NULL;

  fixture = (readiness_contract_fixture *)calloc(1, sizeof(*fixture));
  if (fixture == NULL) {
    *status = TURBO_ENOMEM;
    return NULL;
  }
  fixture->reactor = reactor;
  atomic_init(&fixture->close_calls, 0u);
  atomic_init(&fixture->unarm_calls, 0u);
  atomic_init(&fixture->reentrant_checks, 0u);
  *status = turbo_readiness_reactor_init_backend(
      reactor, &config, &fake_backend_ops, fixture);
  if (*status != TURBO_OK) {
    free(fixture);
    return NULL;
  }
  return fixture;
}

static void fake_destroy(readiness_contract_fixture *fixture) { free(fixture); }

static int fake_emit_resource(readiness_contract_fixture *fixture,
                              intptr_t native_resource,
                              turbo_readiness_events events) {
  readiness_fake_record *record = fake_find_resource(fixture, native_resource);
  if (record == NULL || record->events == 0) return TURBO_OK;
  return turbo_readiness_backend_dispatch(fixture->reactor, record->token,
                                          events, TURBO_OK);
}

static int fake_emit_token(readiness_contract_fixture *fixture, uint64_t token,
                           turbo_readiness_events events, int status) {
  return turbo_readiness_backend_dispatch(fixture->reactor, token, events, status);
}

static int fake_fail_backend(readiness_contract_fixture *fixture, int status) {
  return turbo_readiness_backend_fail(fixture->reactor, status);
}

static uint64_t fake_token_for_resource(readiness_contract_fixture *fixture,
                                        intptr_t native_resource) {
  readiness_fake_record *record = fake_find_resource(fixture, native_resource);
  return record != NULL ? record->token : 0;
}

static void fake_fail_next_arm(readiness_contract_fixture *fixture, int status) {
  fixture->next_arm_error = status;
}

static size_t fake_backend_close_calls(readiness_contract_fixture *fixture) {
  return atomic_load(&fixture->close_calls);
}

static size_t fake_backend_unarm_calls(readiness_contract_fixture *fixture) {
  return atomic_load(&fixture->unarm_calls);
}

static size_t fake_backend_reentrant_checks(
    readiness_contract_fixture *fixture) {
  return atomic_load(&fixture->reentrant_checks);
}

const readiness_contract_factory *readiness_contract_factory_get(void) {
  static const readiness_contract_factory factory = {
      fake_create,
      fake_destroy,
      fake_emit_resource,
      fake_emit_token,
      fake_fail_backend,
      fake_token_for_resource,
      fake_fail_next_arm,
      fake_backend_close_calls,
      fake_backend_unarm_calls,
      fake_backend_reentrant_checks};
  return &factory;
}
