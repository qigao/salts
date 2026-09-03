#include "salts_cmeta_data.h"
#include "salts_str.h"
#include "salts_vstr.h"
#include "tinytest.hpp"

#include <type_traits>

static_assert(std::is_same_v<decltype(salts_tstr_cmeta_type),
                             const cmeta_type_desc>,
              "tstr metadata is immutable");
static_assert(std::is_same_v<decltype(salts_vstr_cmeta_buffer_ops),
                             const cmeta_data_buffer_ops>,
              "vstr adapter is immutable");
static_assert(std::is_same_v<decltype(salts_int8_cmeta_type),
                             const cmeta_type_desc>,
              "fixed-width metadata is immutable");
static_assert(std::is_same_v<decltype(salts_uint64_cmeta_data),
                             const cmeta_data_desc>,
              "fixed-width data metadata is immutable");
static_assert(std::is_same_v<decltype(salts_uuid_cmeta_buffer_ops),
                             const cmeta_data_buffer_ops>,
              "UUID adapter preserves its declared object type");
static_assert(std::is_same_v<decltype(salts_uuid_cmeta_shape),
                             const cmeta_data_buffer_shape>,
              "UUID shape preserves its declared object type");
static_assert(std::is_same_v<decltype(salts_uuid_cmeta_type),
                             const cmeta_type_desc> &&
                  std::is_same_v<decltype(salts_uuid_cmeta_data),
                                 const cmeta_data_desc>,
              "UUID type and data preserve their declared object types");
static_assert(
    std::is_same_v<decltype(&(salts_uuid_cmeta_buffer_ops)),
                   const cmeta_data_buffer_ops *>,
    "UUID adapter facade preserves the public address type");
static_assert(sizeof(salts_uuid_t) == SALTS_UUID_SIZE,
              "UUID storage has the installed fixed size");

extern "C" const cmeta_data_desc *salts_uuid_cmeta_data_from_peer(void);
extern "C" const cmeta_type_desc *salts_uuid_cmeta_type_from_peer(void);
extern "C" const cmeta_data_buffer_shape *
salts_uuid_cmeta_shape_from_peer(void);
extern "C" const cmeta_data_buffer_ops *
salts_uuid_cmeta_buffer_ops_from_peer(void);

spec("Salts CMeta buffer adapter C++ surface") {
  it("exposes semantically distinct tstr and vstr storage types") {
    check_false(cmeta_type_equal(&salts_tstr_cmeta_type,
                                 &salts_vstr_cmeta_type));
    check_equal(salts_tstr_cmeta_buffer_ops.ownership,
                CMETA_DATA_BUFFER_OWNED);
    check_equal(salts_vstr_cmeta_buffer_ops.ownership,
                CMETA_DATA_BUFFER_BORROWED);
  }
}

spec("Salts fixed-width and UUID CMeta C++ surface") {
  it("compares header-local descriptors semantically") {
    cmeta_type_desc equivalent = salts_int64_cmeta_type;

    check_true(cmeta_type_equal(&salts_int64_cmeta_type, &equivalent));
    check_false(cmeta_type_equal(&salts_int64_cmeta_type,
                                 &salts_uint64_cmeta_type));
    check_equal(salts_uuid_cmeta_buffer_ops.ownership,
                CMETA_DATA_BUFFER_OWNED);
    check_equal(salts_uuid_cmeta_data.storage_type->size,
                sizeof(salts_uuid_t));
  }

  it("validates canonical Core UUID metadata from a C translation unit") {
    const cmeta_data_desc *peer = salts_uuid_cmeta_data_from_peer();

    check_true(peer == &salts_uuid_cmeta_data);
    check_true(salts_uuid_cmeta_type_from_peer() ==
               &salts_uuid_cmeta_type);
    check_true(salts_uuid_cmeta_shape_from_peer() ==
               &salts_uuid_cmeta_shape);
    check_true(salts_uuid_cmeta_buffer_ops_from_peer() ==
               &salts_uuid_cmeta_buffer_ops);
    check_true(salts_uuid_cmeta_data_valid(peer));
  }
}
