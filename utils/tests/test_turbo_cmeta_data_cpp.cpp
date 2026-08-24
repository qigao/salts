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
