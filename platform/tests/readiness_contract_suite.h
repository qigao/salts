#ifndef TURBO_READINESS_CONTRACT_SUITE_H
#define TURBO_READINESS_CONTRACT_SUITE_H

#include <turbo/readiness.h>

#include <stddef.h>
#include <stdint.h>

typedef struct readiness_contract_fixture readiness_contract_fixture;

typedef struct readiness_contract_factory {
  readiness_contract_fixture *(*create)(turbo_readiness_config config,
                                        turbo_readiness_reactor *reactor,
                                        int *status);
  void (*destroy)(readiness_contract_fixture *fixture);
  int (*emit_resource)(readiness_contract_fixture *fixture,
                       intptr_t native_resource,
                       turbo_readiness_events events);
  int (*emit_token)(readiness_contract_fixture *fixture, uint64_t token,
                    turbo_readiness_events events, int status);
  int (*fail_backend)(readiness_contract_fixture *fixture, int status);
  uint64_t (*token_for_resource)(readiness_contract_fixture *fixture,
                                 intptr_t native_resource);
  void (*fail_next_arm)(readiness_contract_fixture *fixture, int status);
  size_t (*backend_close_calls)(readiness_contract_fixture *fixture);
  size_t (*backend_unarm_calls)(readiness_contract_fixture *fixture);
  size_t (*backend_reentrant_checks)(readiness_contract_fixture *fixture);
} readiness_contract_factory;

const readiness_contract_factory *readiness_contract_factory_get(void);

#endif
