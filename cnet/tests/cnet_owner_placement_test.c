#include <cnet/owner_placement.h>
#include <salts/error_codes.h>
#include <tinytest.h>

#include <stddef.h>
#include <stdint.h>

static cnet_owner_placement_hint owners[4];

static cnet_owner_placement_input input(cnet_owner_placement_kind kind) {
  cnet_owner_placement_input request = {0};
  request.size = sizeof(request);
  request.version = CNET_OWNER_PLACEMENT_VERSION;
  request.kind = kind;
  request.owners = owners;
  request.owner_count = 4u;
  request.key_known = true;
  return request;
}

spec("CNet server fixed-Owner placement decisions") {
  before_each() {
    for (size_t i = 0; i < 4u; ++i) {
      owners[i].eligible = true;
      owners[i].pressure = 10u;
    }
  }

  it("explicit Owner does not move when unavailable") {
    cnet_owner_placement_input request = input(CNET_OWNER_PLACE_EXPLICIT);
    size_t owner = SIZE_MAX;
    request.explicit_owner = 2u;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_OK);
    check_equal(owner, 2u);
    owners[2].eligible = false;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_ENOBUFS);
    check_equal(owner, SIZE_MAX);
    request.explicit_owner = 4u;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_EINVAL);
    check_equal(owner, SIZE_MAX);
  }

  it("round-robin starts from caller ticket and skips only advisory FULL") {
    cnet_owner_placement_input request = input(CNET_OWNER_PLACE_ROUND_ROBIN);
    size_t owner = SIZE_MAX;
    for (size_t i = 0u; i < 4u; ++i) {
      request.sequence = i;
      check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_OK);
      check_equal(owner, i);
    }
    owners[1].eligible = false;
    request.sequence = 1u;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_OK);
    check_equal(owner, 2u);
    for (size_t i = 0u; i < 4u; ++i) owners[i].eligible = false;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_ENOBUFS);
    check_equal(owner, SIZE_MAX);
  }

  it("pressure selection prefers the least score with rotated tie break") {
    cnet_owner_placement_input request = input(CNET_OWNER_PLACE_LOWEST_PRESSURE);
    size_t owner = SIZE_MAX;
    owners[0].pressure = 20u;
    owners[1].pressure = 3u;
    owners[2].pressure = 3u;
    owners[3].pressure = 8u;
    request.sequence = 2u;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_OK);
    check_equal(owner, 2u);
    request.sequence = 3u;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_OK);
    check_equal(owner, 1u);
    owners[1].eligible = false;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_OK);
    check_equal(owner, 2u);
  }

  it("strict key cannot escape its canonical full Owner") {
    cnet_owner_placement_input request = input(CNET_OWNER_PLACE_STRICT_KEY);
    size_t owner = SIZE_MAX;
    request.key_hash = UINT64_MAX; /* maps to one fixed configured Owner */
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_OK);
    check_equal(owner, (size_t)(UINT64_MAX % 4u));
    owners[owner].eligible = false;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_ENOBUFS);
    check_equal(owner, SIZE_MAX);
    request.key_known = false; /* business key not available during accept */
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_EINVAL);
    check_equal(owner, SIZE_MAX);
  }

  it("invalid inputs fail fast without a fallback policy") {
    cnet_owner_placement_input request = input(CNET_OWNER_PLACE_ROUND_ROBIN);
    size_t owner = 99u;
    check_equal(cnet_owner_placement_choose(NULL, &owner), SALTS_EINVAL);
    check_equal(owner, SIZE_MAX);
    check_equal(cnet_owner_placement_choose(&request, NULL), SALTS_EINVAL);
    request.size = 0u;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_EINVAL);
    request.size = sizeof(request);
    request.version++;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_EINVAL);
    request.version = CNET_OWNER_PLACEMENT_VERSION;
    request.owners = NULL;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_EINVAL);
    request.owners = owners;
    request.owner_count = 0u;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_EINVAL);
    request.owner_count = 4u;
    request.kind = (cnet_owner_placement_kind)999;
    check_equal(cnet_owner_placement_choose(&request, &owner), SALTS_EINVAL);
    check_equal(owner, SIZE_MAX);
  }
}
