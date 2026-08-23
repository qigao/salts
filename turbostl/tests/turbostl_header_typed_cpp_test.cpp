#include <cmeta/data.h>
#include <turbostl/typed.h>
#include "tinytest.hpp"

#include <cstring>
#include <type_traits>

static_assert(!std::is_same_v<set_t, hash_set_t>,
              "Set and HashSet must remain independent container types");
static_assert(!std::is_same_v<map_t, btree_t>,
              "Map and BTree must remain independent container types");

spec("TurboSTL typed C++ public header") {
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

  it("exposes canonical semantic categories through the versioned extension") {
    check_true(stl_vec_container_ext.data == &cmeta_data_sequence);
    check_true(stl_list_container_ext.data == &cmeta_data_sequence);
    check_true(stl_set_container_ext.data == &cmeta_data_set);
    check_true(stl_hash_set_container_ext.data == &cmeta_data_set);
    check_true(stl_map_container_ext.data == &cmeta_data_map);
    check_true(stl_hash_map_container_ext.data == &cmeta_data_map);
    check_true(stl_btree_container_ext.data == &cmeta_data_map);
    check_true(stl_bplus_tree_container_ext.data == &cmeta_data_map);
    check_true(stl_heap_container_ext.data == nullptr);
    check_true(stl_multimap_container_ext.data == nullptr);
  }
}
