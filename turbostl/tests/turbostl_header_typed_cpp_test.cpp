#include <rocida/stl/typed.h>
#include "tinytest.hpp"

#include <cstring>
#include <type_traits>

Struct(cpp_construction_payload,
    (TYPE(Vec, int), values),
    (TYPE(Map, int, long), index)
);

static_assert(!std::is_same_v<set_t, hash_set_t>,
              "Set and HashSet must remain independent container types");
static_assert(!std::is_same_v<map_t, btree_t>,
              "Map and BTree must remain independent container types");
static_assert(std::is_same_v<decltype(cpp_construction_payload{}.values), vec_t>,
              "TYPE(Vec,int) must lower to vec_t storage in C++17");
static_assert(std::is_same_v<decltype(cpp_construction_payload{}.index), map_t>,
              "TYPE(Map,int,long) must lower to map_t storage in C++17");

spec("Rocida STL typed C++ public header") {
  it("exposes ordinary self-describing handle types") {
    vec_t vec{};
    list_t list{};
    map_t map{};

    check_true(sizeof(vec) > 0);
    check_true(sizeof(list) > 0);
    check_true(sizeof(map) > 0);
  }

  it("exposes canonical generic metadata without C-only declaration macros") {
    check_true(stl_vec_container_ext.type != nullptr);
    check_true(stl_vec_container_ext.type->constructor ==
               &stl_vec_generic_desc);
    check_equal(stl_vec_container_ext.type->arity, static_cast<size_t>(1));
    check_equal(std::strcmp(stl_vec_generic_desc.stable_id, "turbostl.Vec"), 0);

    check_true(stl_map_container_ext.type != nullptr);
    check_true(stl_map_container_ext.type->constructor ==
               &stl_map_generic_desc);
    check_equal(stl_map_container_ext.type->arity, static_cast<size_t>(2));
    check_equal(std::strcmp(stl_map_generic_desc.stable_id, "turbostl.Map"), 0);
  }

  it("reflects TYPE fields and binds zero handles through C++17") {
    const cmeta_field_desc *values =
        cmeta_struct_find_field(cpp_construction_payload_meta(), "values");
    const cmeta_field_desc *index =
        cmeta_struct_find_field(cpp_construction_payload_meta(), "index");
    cpp_construction_payload payload{};

    check_true(values != nullptr);
    check_true(values->type == &stl_vec_storage_type);
    check_true(values->declared_type != nullptr);
    check_true(values->declared_type->constructor == &stl_vec_generic_desc);
    check_true(cmeta_declared_type_argument(values->declared_type, 0u) ==
               &cmeta_type_int);
    check_equal(cmeta_container_bind_types(
                    &payload.values, values->declared_type),
                CMETA_OK);
    check_true(payload.values.cmeta.descriptor == &stl_vec_container_desc);
    check_true(payload.values.element_type == &cmeta_type_int);

    check_true(index != nullptr);
    check_true(index->type == &stl_map_storage_type);
    check_true(index->declared_type != nullptr);
    check_true(index->declared_type->constructor == &stl_map_generic_desc);
    check_true(cmeta_declared_type_argument(index->declared_type, 0u) ==
               &cmeta_type_int);
    check_true(cmeta_declared_type_argument(index->declared_type, 1u) ==
               &cmeta_type_long);
    check_equal(cmeta_container_bind_types(
                    &payload.index, index->declared_type),
                CMETA_OK);
    check_true(payload.index.cmeta.descriptor == &stl_map_container_desc);
    check_true(payload.index.key_type == &cmeta_type_int);
    check_true(payload.index.value_type == &cmeta_type_long);
  }
}
