#include "tinytest.h"

#include "readiness_fake_backend.h"

#include <turbo/error_codes.h>
#include <turbo/readiness.h>

#include <stdint.h>

enum {
  FAKE_STRESS_CAPACITY = 3,
  FAKE_STRESS_ITERATIONS = 64,
  FAKE_STRESS_RESOURCE_BASE = 20000
};

typedef struct fake_stress_probe {
  size_t calls;
} fake_stress_probe;

static void fake_stress_callback(void *user, turbo_readiness_events events, int status) {
  fake_stress_probe *probe = (fake_stress_probe *)user;
  check_equal(events, TURBO_READINESS_EVENT_READ);
  check_equal(status, TURBO_OK);
  probe->calls += 1u;
}

spec("Platform fake readiness stress") {
  it("keeps fixed capacity quiescent across arm unarm rearm and slot reuse") {
    const readiness_contract_factory *factory = readiness_contract_factory_get();
    const turbo_readiness_config config = {FAKE_STRESS_CAPACITY, 2u};
    turbo_readiness_reactor reactor = {0};
    readiness_contract_fixture *fixture;
    fake_stress_probe probes[FAKE_STRESS_CAPACITY] = {{0}};
    uint64_t expected_duplicate_events = 0u;
    uint64_t expected_stale_events = 0u;
    int init_status = TURBO_EINVAL;

    check_not_null(factory);
    fixture = factory->create(config, &reactor, &init_status);
    check_equal(init_status, TURBO_OK);
    check_not_null(fixture);

    for (size_t iteration = 0; iteration < FAKE_STRESS_ITERATIONS; ++iteration) {
      turbo_readiness_registration registrations[FAKE_STRESS_CAPACITY] = {{0}};
      turbo_readiness_registration rejected = {(void *)(uintptr_t)1u, 0u};
      turbo_readiness_stats stats = {0};
      intptr_t resources[FAKE_STRESS_CAPACITY];

      for (size_t index = 0; index < FAKE_STRESS_CAPACITY; ++index) {
        resources[index] =
            (intptr_t)(FAKE_STRESS_RESOURCE_BASE + iteration * FAKE_STRESS_CAPACITY + index);
        check_equal(turbo_readiness_register(&reactor, resources[index], &registrations[index]),
                    TURBO_OK);
      }
      check_equal(turbo_readiness_register(
                      &reactor,
                      (intptr_t)(FAKE_STRESS_RESOURCE_BASE +
                                 FAKE_STRESS_ITERATIONS * FAKE_STRESS_CAPACITY + iteration),
                      &rejected),
                  TURBO_ENOBUFS);
      check_null(rejected.impl);

      for (size_t index = 0; index < FAKE_STRESS_CAPACITY; ++index) {
        uint64_t registration_token;
        uint64_t cancelled_arm_token;
        size_t calls_before_cancel = probes[index].calls;

        check_equal(turbo_readiness_arm(&registrations[index], TURBO_READINESS_EVENT_READ,
                                        fake_stress_callback, &probes[index]),
                    TURBO_OK);
        check_equal(factory->emit_resource(fixture, resources[index],
                                           TURBO_READINESS_EVENT_READ),
                    TURBO_OK);
        check_equal(probes[index].calls, calls_before_cancel + 1u);

        check_equal(turbo_readiness_arm(&registrations[index], TURBO_READINESS_EVENT_READ,
                                        fake_stress_callback, &probes[index]),
                    TURBO_OK);
        registration_token = factory->token_for_resource(fixture, resources[index]);
        cancelled_arm_token = factory->arm_token_for_resource(fixture, resources[index]);
        check_not_equal(registration_token, (uint64_t)0u);
        check_not_equal(cancelled_arm_token, (uint64_t)0u);
        check_equal(turbo_readiness_unarm(&registrations[index]), TURBO_OK);
        check_equal(factory->emit_arm_token(fixture, registration_token, cancelled_arm_token,
                                            TURBO_READINESS_EVENT_READ),
                    TURBO_OK);
        expected_duplicate_events += 1u;
        check_equal(probes[index].calls, calls_before_cancel + 1u);

        check_equal(turbo_readiness_arm(&registrations[index], TURBO_READINESS_EVENT_READ,
                                        fake_stress_callback, &probes[index]),
                    TURBO_OK);
        check_equal(factory->emit_resource(fixture, resources[index],
                                           TURBO_READINESS_EVENT_READ),
                    TURBO_OK);
        check_equal(probes[index].calls, calls_before_cancel + 2u);
        check_equal(turbo_readiness_close(&registrations[index]), TURBO_OK);
        check_null(registrations[index].impl);
        check_equal(factory->emit_token(fixture, registration_token,
                                        TURBO_READINESS_EVENT_READ, TURBO_OK),
                    TURBO_OK);
        expected_stale_events += 1u;
        check_equal(probes[index].calls, calls_before_cancel + 2u);
      }

      check_equal(turbo_readiness_reactor_stats(&reactor, &stats), TURBO_OK);
      check_equal(stats.registered_count, (size_t)0u);
      check_equal(stats.armed_count, (size_t)0u);
      check_equal(stats.callbacks_inflight, (size_t)0u);
      check_equal(stats.rejected_full, (uint64_t)(iteration + 1u));
      check_equal(stats.duplicate_events, expected_duplicate_events);
      check_equal(stats.stale_events, expected_stale_events);
    }

    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    factory->destroy(fixture);
  }
}
