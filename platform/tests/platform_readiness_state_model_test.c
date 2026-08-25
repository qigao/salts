#include "tinytest.h"

#include "../src/readiness_internal.h"

typedef struct state_model_case {
  const char *name;
  turbo_readiness_state_view view;
  int valid;
} state_model_case;

static void state_model_callback(void *user,
                                 turbo_readiness_events events,
                                 int status) {
  (void)user;
  (void)events;
  (void)status;
}

static turbo_readiness_callback_result state_model_continuation(
    void *user, turbo_readiness_events events, int status) {
  (void)user;
  (void)events;
  (void)status;
  return (turbo_readiness_callback_result){
      TURBO_READINESS_COMPLETE, 0u};
}

spec("Platform readiness state model") {
  it("validates orthogonal lifecycle interest delivery terminal and control facts") {
    static const state_model_case cases[] = {
        {"free",
         {TURBO_READINESS_LIFECYCLE_FREE,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 0u, 0, 0},
         1},
        {"retired",
         {TURBO_READINESS_LIFECYCLE_RETIRED,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 0u, 0, 0},
         1},
        {"registering",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_REGISTER,
          NULL, 0u, 0u, 0u, 0, 0},
         1},
        {"open idle",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 1u, 1, 0},
         1},
        {"arming",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_ARMING,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_ARM,
          state_model_callback, 7u, 0u, 1u, 1, 0},
         1},
        {"armed",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_ARMED,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          state_model_callback, 7u, 0u, 1u, 1, 0},
         1},
        {"normal callback",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_CALLBACK,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          state_model_callback, 0u, 1u, 2u, 1, 0},
         1},
        {"terminal reserved",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_ARMED,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_RESERVED,
          TURBO_READINESS_CONTROL_NONE,
          state_model_callback, 7u, 0u, 1u, 1, 0},
         1},
        {"terminal delivering",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_CALLBACK,
          TURBO_READINESS_TERMINAL_DELIVERING,
          TURBO_READINESS_CONTROL_NONE,
          state_model_callback, 0u, 0u, 1u, 1, 0},
         1},
        {"closing callback",
         {TURBO_READINESS_LIFECYCLE_CLOSING,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_CALLBACK,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_CLOSE,
          state_model_callback, 0u, 1u, 2u, 1, 0},
         1},
        {"orphan cleanup",
         {TURBO_READINESS_LIFECYCLE_CLOSING,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 0u, 1, 1},
         1},
        {"free armed",
         {TURBO_READINESS_LIFECYCLE_FREE,
          TURBO_READINESS_INTEREST_ARMED,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          state_model_callback, 7u, 0u, 0u, 0, 0},
         0},
        {"free native",
         {TURBO_READINESS_LIFECYCLE_FREE,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 0u, 1, 0},
         0},
        {"retired waiter",
         {TURBO_READINESS_LIFECYCLE_RETIRED,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          NULL, 0u, 1u, 0u, 0, 0},
         0},
        {"armed missing callback",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_ARMED,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          NULL, 7u, 0u, 1u, 1, 0},
         0},
        {"armed missing token",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_ARMED,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          state_model_callback, 0u, 0u, 1u, 1, 0},
         0},
        {"callback missing borrow",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_CALLBACK,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 1u, 1, 0},
         0},
        {"reserved without armed interest",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_RESERVED,
          TURBO_READINESS_CONTROL_NONE,
          state_model_callback, 0u, 0u, 1u, 1, 0},
         0},
        {"delivering without callback",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_DELIVERING,
          TURBO_READINESS_CONTROL_NONE,
          state_model_callback, 0u, 0u, 1u, 1, 0},
         0},
        {"arming without arm control",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_ARMING,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          state_model_callback, 7u, 0u, 1u, 1, 0},
         0},
        {"unarming without unarm control",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_UNARMING,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          state_model_callback, 7u, 0u, 1u, 1, 0},
         0},
        {"orphan outside closing lifecycle",
         {TURBO_READINESS_LIFECYCLE_OPEN,
          TURBO_READINESS_INTEREST_IDLE,
          TURBO_READINESS_DELIVERY_IDLE,
          TURBO_READINESS_TERMINAL_NONE,
          TURBO_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 1u, 1, 1},
         0},
    };

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
      check_equal(turbo_readiness_state_model_valid(&cases[index].view),
                  cases[index].valid);
    }
  }

  it("requires exactly one callback form") {
    check_true(turbo_readiness_callback_forms_valid(
        state_model_callback, NULL));
    check_true(turbo_readiness_callback_forms_valid(
        NULL, state_model_continuation));
    check_false(turbo_readiness_callback_forms_valid(NULL, NULL));
    check_false(turbo_readiness_callback_forms_valid(
        state_model_callback, state_model_continuation));
  }

  it("keeps a paused old entrant out of a reused handle generation") {
    uintptr_t admission = 0u;

    check_equal(turbo_readiness_registration_admission_enter(&admission),
                TURBO_OK);
    check_equal(turbo_readiness_registration_admission_entrants(&admission),
                (uint32_t)1u);
    check_equal(turbo_readiness_registration_admission_close(&admission),
                TURBO_OK);
    check_equal(turbo_readiness_registration_admission_enter(&admission),
                TURBO_EBUSY);
    check_equal(turbo_readiness_registration_admission_reset(&admission),
                TURBO_EBUSY);

    turbo_readiness_registration_admission_leave(&admission);
    check_equal(turbo_readiness_registration_admission_entrants(&admission),
                (uint32_t)0u);
    check_equal(turbo_readiness_registration_admission_reset(&admission),
                TURBO_OK);
    check_equal(turbo_readiness_registration_admission_enter(&admission),
                TURBO_OK);
    turbo_readiness_registration_admission_leave(&admission);
  }

  it("reserves register admission only when no old entrant exists") {
    uintptr_t admission = 0u;

    check_equal(turbo_readiness_registration_admission_enter(&admission),
                TURBO_OK);
    check_equal(
        turbo_readiness_registration_admission_reserve_register(&admission),
        TURBO_EBUSY);
    turbo_readiness_registration_admission_leave(&admission);
    check_equal(
        turbo_readiness_registration_admission_reserve_register(&admission),
        TURBO_OK);
    check_equal(turbo_readiness_registration_admission_enter(&admission),
                TURBO_EBUSY);
    check_equal(turbo_readiness_registration_admission_reset(&admission),
                TURBO_OK);
  }

  it("rejects the largest representable entrant count without closing gate") {
    uintptr_t admission =
        turbo_readiness_registration_admission_max_entrants();

    check_true(admission > 0u);
    check_equal(turbo_readiness_registration_admission_enter(&admission),
                -EOVERFLOW);
    check_equal(turbo_readiness_registration_admission_entrants(&admission),
                (uint32_t)admission);
    check_equal(turbo_readiness_registration_admission_close(&admission),
                TURBO_OK);
    check_equal(turbo_readiness_registration_admission_enter(&admission),
                TURBO_EBUSY);
  }
}
