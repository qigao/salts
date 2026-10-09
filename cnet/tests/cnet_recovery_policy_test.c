#include <cnet/recovery_policy.h>
#include <salts/error_codes.h>
#include <tinytest.h>
#include <string.h>

static cnet_reconnect_config config(void) {
  cnet_reconnect_config c = {0};
  c.size = sizeof(c);
  c.version = CNET_RECOVERY_POLICY_VERSION;
  c.max_attempts = 3u;
  c.deadline_ms = 1000u;
  c.initial_backoff_ms = 40u;
  c.maximum_backoff_ms = 160u;
  c.jitter_seed = 123u;
  return c;
}
static cnet_retry_input retry(void) {
  cnet_retry_input p = {0};
  p.size = sizeof(p);
  p.version = CNET_RECOVERY_POLICY_VERSION;
  p.attempts_used = 1u;
  p.max_attempts = 2u;
  p.now_ms = 200u;
  p.deadline_ms = 1000u;
  p.request_body_bytes = 50u;
  p.remaining_retry_byte_budget = 50u;
  p.explicitly_enabled = true;
  p.owned_replayable_body = true;
  p.protocol_proves_not_executed = true;
  return p;
}
spec("CNet bounded recovery without application replay") {
  it("keeps transport CONNECTED separate from protocol READY") {
    cnet_reconnect_state s = {0};
    cnet_reconnect_ticket first = {0}, second = {0};
    cnet_reconnect_snapshot shot;
    uint64_t wait = 77u;
    cnet_reconnect_config conf = config();
    check_equal(cnet_reconnect_init(&s, &conf), SALTS_OK);
    check_equal(cnet_reconnect_begin(&s, 100u, &first, &wait), SALTS_OK);
    check_equal(wait, UINT64_C(0));
    check_equal(cnet_reconnect_connected(&s, first, 110u), SALTS_OK);
    check_equal(cnet_reconnect_begin(&s, 112u, &second, &wait), SALTS_EBUSY);
    check_equal(cnet_reconnect_failed(&s, first, CNET_RECONNECT_TRANSIENT, 120u), SALTS_OK);
    check_equal(cnet_reconnect_get_snapshot(&s, &shot), SALTS_OK);
    check_equal(shot.attempts, (uint32_t)1u);
    check(shot.next_attempt_ms >= 140u && shot.next_attempt_ms <= 160u);
    check_equal(cnet_reconnect_begin(&s, 120u, &second, &wait), SALTS_EBUSY);
    check_equal(wait, shot.next_attempt_ms - UINT64_C(120));
    check_equal(cnet_reconnect_begin(&s, shot.next_attempt_ms, &second, &wait), SALTS_OK);
    check(second.generation != first.generation);
    check_equal(cnet_reconnect_connected(&s, first, 180u), SALTS_ENOENT);
    check_equal(cnet_reconnect_connected(&s, second, 200u), SALTS_OK);
    check_equal(cnet_reconnect_protocol_ready(&s, second, 210u), SALTS_OK);
    check_equal(cnet_reconnect_get_snapshot(&s, &shot), SALTS_OK);
    check(shot.protocol_ready);
    check_equal(shot.attempts, (uint32_t)0u);
    check_equal(cnet_reconnect_lost(&s, second, CNET_RECONNECT_TRANSIENT,
                                    1200u, 3000u), SALTS_OK);
    check_equal(cnet_reconnect_get_snapshot(&s, &shot), SALTS_OK);
    check(shot.next_attempt_ms >= 1220u && shot.next_attempt_ms <= 1240u);
    check_equal(shot.deadline_ms, UINT64_C(3000));
  }
  it("caps exponential backoff and fails closed on auth/security errors") {
    cnet_reconnect_state s = {0};
    cnet_reconnect_ticket t = {0};
    cnet_reconnect_snapshot shot;
    cnet_reconnect_config conf = config();
    uint64_t wait = 0u;
    uint64_t now = 0u;
    check_equal(cnet_reconnect_init(&s, &conf), SALTS_OK);
    for (size_t i = 0u; i < 3u; ++i) {
      check_equal(cnet_reconnect_begin(&s, now, &t, &wait), SALTS_OK);
      check_equal(cnet_reconnect_failed(&s, t, CNET_RECONNECT_TRANSIENT, now), SALTS_OK);
      check_equal(cnet_reconnect_get_snapshot(&s, &shot), SALTS_OK);
      now = shot.next_attempt_ms;
      check(now <= UINT64_C(1000));
    }
    check_equal(cnet_reconnect_begin(&s, now, &t, &wait), SALTS_ENOBUFS);
    memset(&s, 0, sizeof(s));
    check_equal(cnet_reconnect_init(&s, &conf), SALTS_OK);
    check_equal(cnet_reconnect_begin(&s, 10u, &t, &wait), SALTS_OK);
    check_equal(cnet_reconnect_failed(&s, t, CNET_RECONNECT_SECURITY, 20u), SALTS_OK);
    check_equal(cnet_reconnect_begin(&s, 20u, &t, &wait), SALTS_ESHUTDOWN);
  }
  it("rejects deadline expiry and stale attempt generations") {
    cnet_reconnect_state s = {0};
    cnet_reconnect_ticket t = {0};
    cnet_reconnect_config conf = config();
    uint64_t wait = 0u;
    check_equal(cnet_reconnect_init(&s, &conf), SALTS_OK);
    check_equal(cnet_reconnect_begin(&s, 1000u, &t, &wait), SALTS_ETIMEDOUT);
    check_equal(cnet_reconnect_begin(&s, 100u, &t, &wait), SALTS_OK);
    check_equal(cnet_reconnect_connected(&s, t, 1001u), SALTS_ETIMEDOUT);
    check_equal(cnet_reconnect_protocol_ready(&s, t, 1001u), SALTS_EBUSY);
    check_equal(cnet_reconnect_begin(&s, 200u, &t, &wait), SALTS_ESHUTDOWN);
  }
  it("requires positive replay authorization, owned bytes and budgets") {
    cnet_retry_input input = retry();
    cnet_retry_result out;
    check_equal(cnet_retry_evaluate(&input, &out), SALTS_OK);
    check(out.allowed && out.reason == CNET_RETRY_ALLOWED);
    input.protocol_proves_not_executed = false;
    check_equal(cnet_retry_evaluate(&input, &out), SALTS_OK);
    check(!out.allowed && out.reason == CNET_RETRY_UNAUTHORIZED);
    input.application_declares_idempotent = true;
    check_equal(cnet_retry_evaluate(&input, &out), SALTS_OK);
    check(out.allowed);
    input.one_attempt_contract = true;
    check_equal(cnet_retry_evaluate(&input, &out), SALTS_OK);
    check_equal(out.reason, CNET_RETRY_ONE_ATTEMPT);
    input.one_attempt_contract = false;
    input.owned_replayable_body = false;
    check_equal(cnet_retry_evaluate(&input, &out), SALTS_OK);
    check_equal(out.reason, CNET_RETRY_UNREPLAYABLE);
    input.owned_replayable_body = true;
    input.security_failure = true;
    check_equal(cnet_retry_evaluate(&input, &out), SALTS_OK);
    check_equal(out.reason, CNET_RETRY_SECURITY);
    input.security_failure = false;
    input.remaining_retry_byte_budget = 49u;
    check_equal(cnet_retry_evaluate(&input, &out), SALTS_OK);
    check_equal(out.reason, CNET_RETRY_BUDGET);
    input.remaining_retry_byte_budget = 50u;
    input.backoff_not_before_ms = 1000u;
    check_equal(cnet_retry_evaluate(&input, &out), SALTS_OK);
    check_equal(out.reason, CNET_RETRY_DEADLINE);
  }
}
