#include "typed_order.h"

#include "tinytest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

spec("generated typed Order C++ owner") {
  it("should expose the C struct API through an owning RAII wrapper") {
    static_assert(!std::is_copy_constructible_v<Orders_typed::OrderOwner>);
    static_assert(!std::is_move_constructible_v<Orders_typed::OrderOwner>);

    const char *json = "{\"header\":{\"seq\":7},\"id\":42,"
                       "\"request_id\":\"01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001\","
                       "\"min_value\":-9223372036854775808,\"max_value\":18446744073709551615,"
                       "\"side\":\"Buy\",\"fills\":[],\"symbol\":\"ABC\",\"payload\":\"raw\"}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = nullptr;
    Orders_typed::OrderOwner order;

    check_int_eq(Orders_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != nullptr) {
      char *serialized = nullptr;
      std::uint8_t *wire = nullptr;
      std::size_t serialized_len = 0;
      std::size_t wire_len = 0;
      Orders_typed::OrderOwner decoded;
      check_int_eq(order.from_json(codec, json, std::strlen(json), &error), DATA_BIND_OK);
      check_uint_eq(order->id, 42u);
      check_uint_eq(order->header.seq, 7u);
      check_str_eq(order->symbol, "ABC");
      check_int_eq(order.to_json(&serialized, &serialized_len, &error), DATA_BIND_OK);
      check_not_null(serialized);
      check_size_gt(serialized_len, 0);
      check_int_eq(order.to_bin(&wire, &wire_len, &error), DATA_BIND_OK);
      check_not_null(wire);
      check_size_gt(wire_len, 0);
      if (wire != nullptr) {
        check_int_eq(decoded.from_bin(codec, wire, wire_len, &error), DATA_BIND_OK);
        check_uint_eq(decoded->id, 42u);
        check_str_eq(decoded->symbol, "ABC");
      }
      tbe_typed_serialized_free(serialized);
      tbe_typed_serialized_free(wire);
    }
    data_bind_free(codec);
  }
}
