#include "tinytest.h"

#include "../src/readiness_internal.h"

typedef struct state_model_case {
  const char *name;
  salts_readiness_state_view view;
  int valid;
} state_model_case;

static void state_model_callback(void *user,
                                 salts_readiness_events events,
                                 int status) {
  (void)user;
  (void)events;
  (void)status;
}

static salts_readiness_callback_result state_model_continuation(
    void *user, salts_readiness_events events, int status) {
  (void)user;
  (void)events;
  (void)status;
  return (salts_readiness_callback_result){
      SALTS_READINESS_COMPLETE, 0u};
}

spec("Platform readiness state model") {
  it("validates orthogonal lifecycle interest delivery terminal and control facts") {
    static const state_model_case cases[] = {
        {"free",
         {SALTS_READINESS_LIFECYCLE_FREE,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 0u, 0, 0},
         1},
        {"retired",
         {SALTS_READINESS_LIFECYCLE_RETIRED,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 0u, 0, 0},
         1},
        {"registering",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_REGISTER,
          NULL, 0u, 0u, 0u, 0, 0},
         1},
        {"open idle",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 1u, 1, 0},
         1},
        {"arming",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_ARMING,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_ARM,
          state_model_callback, 7u, 0u, 1u, 1, 0},
         1},
        {"armed",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_ARMED,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          state_model_callback, 7u, 0u, 1u, 1, 0},
         1},
        {"normal callback",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_CALLBACK,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          state_model_callback, 0u, 1u, 2u, 1, 0},
         1},
        {"terminal reserved",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_ARMED,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_RESERVED,
          SALTS_READINESS_CONTROL_NONE,
          state_model_callback, 7u, 0u, 1u, 1, 0},
         1},
        {"terminal delivering",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_CALLBACK,
          SALTS_READINESS_TERMINAL_DELIVERING,
          SALTS_READINESS_CONTROL_NONE,
          state_model_callback, 0u, 0u, 1u, 1, 0},
         1},
        {"closing callback",
         {SALTS_READINESS_LIFECYCLE_CLOSING,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_CALLBACK,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_CLOSE,
          state_model_callback, 0u, 1u, 2u, 1, 0},
         1},
        {"orphan cleanup",
         {SALTS_READINESS_LIFECYCLE_CLOSING,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 0u, 1, 1},
         1},
        {"free armed",
         {SALTS_READINESS_LIFECYCLE_FREE,
          SALTS_READINESS_INTEREST_ARMED,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          state_model_callback, 7u, 0u, 0u, 0, 0},
         0},
        {"free native",
         {SALTS_READINESS_LIFECYCLE_FREE,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 0u, 1, 0},
         0},
        {"retired waiter",
         {SALTS_READINESS_LIFECYCLE_RETIRED,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          NULL, 0u, 1u, 0u, 0, 0},
         0},
        {"armed missing callback",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_ARMED,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          NULL, 7u, 0u, 1u, 1, 0},
         0},
        {"armed missing token",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_ARMED,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          state_model_callback, 0u, 0u, 1u, 1, 0},
         0},
        {"callback missing borrow",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_CALLBACK,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 1u, 1, 0},
         0},
        {"reserved without armed interest",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_RESERVED,
          SALTS_READINESS_CONTROL_NONE,
          state_model_callback, 0u, 0u, 1u, 1, 0},
         0},
        {"delivering without callback",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_DELIVERING,
          SALTS_READINESS_CONTROL_NONE,
          state_model_callback, 0u, 0u, 1u, 1, 0},
         0},
        {"arming without arm control",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_ARMING,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          state_model_callback, 7u, 0u, 1u, 1, 0},
         0},
        {"unarming without unarm control",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_UNARMING,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          state_model_callback, 7u, 0u, 1u, 1, 0},
         0},
        {"orphan outside closing lifecycle",
         {SALTS_READINESS_LIFECYCLE_OPEN,
          SALTS_READINESS_INTEREST_IDLE,
          SALTS_READINESS_DELIVERY_IDLE,
          SALTS_READINESS_TERMINAL_NONE,
          SALTS_READINESS_CONTROL_NONE,
          NULL, 0u, 0u, 1u, 1, 1},
         0},
    };

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
      check_equal(salts_readiness_state_model_valid(&cases[index].view),
                  cases[index].valid);
    }
  }

  it("requires exactly one callback form") {
    check_true(salts_readiness_callback_forms_valid(
        state_model_callback, NULL));
    check_true(salts_readiness_callback_forms_valid(
        NULL, state_model_continuation));
    check_false(salts_readiness_callback_forms_valid(NULL, NULL));
    check_false(salts_readiness_callback_forms_valid(
        state_model_callback, state_model_continuation));
  }

  it("keeps a paused old entrant out of a reused handle generation") {
    uintptr_t admission = 0u;

    check_equal(salts_readiness_registration_admission_enter(&admission),
                SALTS_OK);
    check_equal(salts_readiness_registration_admission_entrants(&admission),
                (uint32_t)1u);
    check_equal(salts_readiness_registration_admission_close(&admission),
                SALTS_OK);
    check_equal(salts_readiness_registration_admission_enter(&admission),
                SALTS_EBUSY);
    check_equal(salts_readiness_registration_admission_reset(&admission),
                SALTS_EBUSY);

    salts_readiness_registration_admission_leave(&admission);
    check_equal(salts_readiness_registration_admission_entrants(&admission),
                (uint32_t)0u);
    check_equal(salts_readiness_registration_admission_reset(&admission),
                SALTS_OK);
    check_equal(salts_readiness_registration_admission_enter(&admission),
                SALTS_OK);
    salts_readiness_registration_admission_leave(&admission);
  }

  it("reserves register admission only when no old entrant exists") {
    uintptr_t admission = 0u;

    check_equal(salts_readiness_registration_admission_enter(&admission),
                SALTS_OK);
    check_equal(
        salts_readiness_registration_admission_reserve_register(&admission),
        SALTS_EBUSY);
    salts_readiness_registration_admission_leave(&admission);
    check_equal(
        salts_readiness_registration_admission_reserve_register(&admission),
        SALTS_OK);
    check_equal(salts_readiness_registration_admission_enter(&admission),
                SALTS_EBUSY);
    check_equal(salts_readiness_registration_admission_reset(&admission),
                SALTS_OK);
  }

  it("rejects the largest representable entrant count without closing gate") {
    uintptr_t admission =
        salts_readiness_registration_admission_max_entrants();

    check_true(admission > 0u);
    check_equal(salts_readiness_registration_admission_enter(&admission),
                -EOVERFLOW);
    check_equal(salts_readiness_registration_admission_entrants(&admission),
                (uint32_t)admission);
    check_equal(salts_readiness_registration_admission_close(&admission),
                SALTS_OK);
    check_equal(salts_readiness_registration_admission_enter(&admission),
                SALTS_EBUSY);
  }
}
