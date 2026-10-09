#include <cnet/destination_policy.h>
#include <salts/error_codes.h>
#include <tinytest.h>

#include <stddef.h>
#include <stdint.h>

static cnet_destination_hint endpoints[4];

static cnet_destination_selection input(cnet_destination_policy_kind kind) {
  cnet_destination_selection s = {0};
  s.size = sizeof(s);
  s.version = CNET_DESTINATION_POLICY_VERSION;
  s.kind = kind;
  s.endpoints = endpoints;
  s.endpoint_count = 4u;
  s.snapshot_generation = 7u;
  s.expires_at_ms = 1000u;
  s.now_ms = 10u;
  s.key_known = true;
  return s;
}

spec("CNet client destination selection") {
  before_each() {
    for (size_t i = 0u; i < 4u; ++i) {
      endpoints[i].endpoint_id = (i + 1u) * 11u;
      endpoints[i].weight = (uint32_t)(i + 1u);
      endpoints[i].inflight = 9u;
      endpoints[i].eligible = true;
    }
  }

  it("explicit ID is stable across caller sequences and never reroutes") {
    cnet_destination_selection s = input(CNET_DESTINATION_EXPLICIT);
    cnet_destination_result out = {0};
    s.explicit_endpoint_id = 33u;
    check_equal(cnet_destination_choose(&s, &out), SALTS_OK);
    check_equal(out.index, 2u);
    check_equal(out.endpoint_id, 33u);
    check_equal(out.snapshot_generation, 7u);
    endpoints[2].eligible = false;
    check_equal(cnet_destination_choose(&s, &out), SALTS_ENOBUFS);
    check_equal(out.index, SIZE_MAX);
    check_equal(out.endpoint_id, 0u);
    s.explicit_endpoint_id = 99u;
    check_equal(cnet_destination_choose(&s, &out), SALTS_ENOENT);
  }

  it("round robin skips advisory ineligible endpoints without hidden dial") {
    cnet_destination_selection s = input(CNET_DESTINATION_ROUND_ROBIN);
    cnet_destination_result out;
    s.sequence = 5u;
    check_equal(cnet_destination_choose(&s, &out), SALTS_OK);
    check_equal(out.index, 1u);
    endpoints[1].eligible = false;
    check_equal(cnet_destination_choose(&s, &out), SALTS_OK);
    check_equal(out.index, 2u);
    for (size_t i = 0u; i < 4u; ++i) endpoints[i].eligible = false;
    check_equal(cnet_destination_choose(&s, &out), SALTS_ENOBUFS);
    check_equal(out.index, SIZE_MAX);
  }

  it("weighted choice keeps caller-order tickets bounded") {
    cnet_destination_selection s = input(CNET_DESTINATION_WEIGHTED_RR);
    cnet_destination_result out;
    s.sequence = 0u;
    check_equal(cnet_destination_choose(&s, &out), SALTS_OK);
    check_equal(out.endpoint_id, 11u);
    s.sequence = 1u;
    check_equal(cnet_destination_choose(&s, &out), SALTS_OK);
    check_equal(out.endpoint_id, 22u);
    s.sequence = 3u;
    check_equal(cnet_destination_choose(&s, &out), SALTS_OK);
    check_equal(out.endpoint_id, 33u);
    s.sequence = 9u;
    check_equal(cnet_destination_choose(&s, &out), SALTS_OK);
    check_equal(out.endpoint_id, 44u);
    endpoints[3].eligible = false;
    s.sequence = 9u; /* weights 1+2+3 => 9 % 6 = 3 */
    check_equal(cnet_destination_choose(&s, &out), SALTS_OK);
    check_equal(out.endpoint_id, 33u);
  }

  it("least inflight uses rotated tie breaks; strict key stays fixed") {
    cnet_destination_selection s = input(CNET_DESTINATION_LEAST_INFLIGHT);
    cnet_destination_result out;
    endpoints[0].inflight = 10u;
    endpoints[1].inflight = 2u;
    endpoints[2].inflight = 2u;
    s.sequence = 2u;
    check_equal(cnet_destination_choose(&s, &out), SALTS_OK);
    check_equal(out.index, 2u);
    s.kind = CNET_DESTINATION_STRICT_KEY;
    s.key_hash = 2u;
    endpoints[2].eligible = false;
    check_equal(cnet_destination_choose(&s, &out), SALTS_ENOBUFS);
    check_equal(out.index, SIZE_MAX);
    s.key_known = false;
    check_equal(cnet_destination_choose(&s, &out), SALTS_EINVAL);
  }

  it("expired or malformed endpoints fail instead of fallback") {
    cnet_destination_selection s = input(CNET_DESTINATION_ROUND_ROBIN);
    cnet_destination_result out;
    s.now_ms = s.expires_at_ms;
    check_equal(cnet_destination_choose(&s, &out), SALTS_ETIMEDOUT);
    check_equal(out.index, SIZE_MAX);
    s.expires_at_ms = UINT64_MAX;
    check_equal(cnet_destination_choose(&s, &out), SALTS_OK);
    endpoints[2].endpoint_id = endpoints[1].endpoint_id;
    check_equal(cnet_destination_choose(&s, &out), SALTS_EINVAL);
    endpoints[2].endpoint_id = 33u;
    endpoints[2].weight = 0u;
    check_equal(cnet_destination_choose(&s, &out), SALTS_EINVAL);
    endpoints[2].weight = 3u;
    s.snapshot_generation = 0u;
    check_equal(cnet_destination_choose(&s, &out), SALTS_EINVAL);
    s.snapshot_generation = 7u;
    s.version++;
    check_equal(cnet_destination_choose(&s, &out), SALTS_EINVAL);
    s.version = CNET_DESTINATION_POLICY_VERSION;
    s.kind = (cnet_destination_policy_kind)999;
    check_equal(cnet_destination_choose(&s, &out), SALTS_EINVAL);
    check_equal(out.index, SIZE_MAX);
  }
}
