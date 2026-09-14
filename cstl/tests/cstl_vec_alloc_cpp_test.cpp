#include <cstl/vec_alloc.h>
#include "tinytest.hpp"
#include <cstdlib>
#include <type_traits>

static_assert(std::is_same<decltype(vec_alloc_view(nullptr)), const vec_t *>::value,
              "allocator-bound storage must not expose a mutable ordinary owner");
static stl_status allocate_bytes(void *, size_t bytes, void **out) {
    *out = std::malloc(bytes);
    return *out ? STL_OK : STL_OUT_OF_MEMORY;
}
static void release_bytes(void *, void *data, size_t) { std::free(data); }

suite("CSTL allocator-bound Vec C++ linkage") {
    it("links the C provider and reads the const ordinary Vec view") {
        const stl_allocator allocator = {nullptr, allocate_bytes, release_bytes};
        vec_alloc_t *owner = nullptr;
        const int value = 71;
        check_equal(vec_alloc_new(&cmeta_type_int, 3u, &allocator, &owner), STL_OK);
        check_equal(vec_alloc_push(owner, &value), STL_OK);
        check_equal(vec_size(vec_alloc_view(owner)), size_t(1));
        check_equal(*static_cast<const int *>(vec_at_const(vec_alloc_view(owner), 0u)), value);
        vec_alloc_destroy(owner);
    }
}
