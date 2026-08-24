#include "turbo_cmeta_data.h"
#include "turbo_str.h"
#include "turbo_vstr.h"
#include "tinytest.hpp"

#include <type_traits>

static_assert(std::is_same_v<decltype(turbo_tstr_cmeta_type),
                             const cmeta_type_desc>,
              "tstr metadata is immutable");
static_assert(std::is_same_v<decltype(turbo_vstr_cmeta_buffer_ops),
                             const cmeta_data_buffer_ops>,
              "vstr adapter is immutable");
static_assert(std::is_same_v<decltype(turbo_int8_cmeta_type),
                             const cmeta_type_desc>,
              "fixed-width metadata is immutable");
static_assert(std::is_same_v<decltype(turbo_uint64_cmeta_data),
                             const cmeta_data_desc>,
              "fixed-width data metadata is immutable");
static_assert(std::is_same_v<decltype(turbo_uuid_cmeta_buffer_ops),
                             const cmeta_data_buffer_ops>,
              "UUID adapter is immutable");
static_assert(std::is_same_v<decltype(turbo_uuid_cmeta_adapter),
                             const turbo_uuid_cmeta_adapter_desc>,
              "UUID provenance is immutable");
static_assert(sizeof(turbo_uuid_t) == TURBO_UUID_SIZE,
              "UUID storage has the installed fixed size");

extern "C" const turbo_uuid_cmeta_adapter_desc *
turbo_uuid_cmeta_adapter_from_peer(void);

spec("TurboUtils CMeta buffer adapter C++ surface") {
  it("exposes semantically distinct tstr and vstr storage types") {
    check_false(cmeta_type_equal(&turbo_tstr_cmeta_type,
                                 &turbo_vstr_cmeta_type));
    check_equal(turbo_tstr_cmeta_buffer_ops.ownership,
                CMETA_DATA_BUFFER_OWNED);
    check_equal(turbo_vstr_cmeta_buffer_ops.ownership,
                CMETA_DATA_BUFFER_BORROWED);
  }
}

spec("TurboUtils fixed-width and UUID CMeta C++ surface") {
  it("compares header-local descriptors semantically") {
    cmeta_type_desc equivalent = turbo_int64_cmeta_type;

    check_true(cmeta_type_equal(&turbo_int64_cmeta_type, &equivalent));
    check_false(cmeta_type_equal(&turbo_int64_cmeta_type,
                                 &turbo_uint64_cmeta_type));
    check_equal(turbo_uuid_cmeta_buffer_ops.ownership,
                CMETA_DATA_BUFFER_OWNED);
    check_equal(turbo_uuid_cmeta_data.storage_type->size,
                sizeof(turbo_uuid_t));
  }

  it("validates header-local UUID provenance from a C translation unit") {
    const turbo_uuid_cmeta_adapter_desc *peer =
        turbo_uuid_cmeta_adapter_from_peer();
    turbo_uuid_cmeta_adapter_desc copied = *peer;

    check_true(turbo_uuid_cmeta_adapter_valid(peer, peer->data));
    check_true(turbo_uuid_cmeta_adapter_valid(&copied, copied.data));

    copied.struct_size = TURBO_UUID_CMETA_ADAPTER_PREFIX_SIZE - 1u;
    check_false(turbo_uuid_cmeta_adapter_valid(&copied, copied.data));
  }
}
