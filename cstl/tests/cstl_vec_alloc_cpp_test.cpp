#include <cstl/vec_alloc.h>
#include <cstdlib>
#include <type_traits>

static stl_status allocate(void *, size_t bytes, void **out) {
    *out = std::malloc(bytes);
    return *out != nullptr ? STL_OK : STL_OUT_OF_MEMORY;
}
static void deallocate(void *, void *data, size_t) { std::free(data); }

int main() {
    static_assert(std::is_same<decltype(vec_alloc_view(nullptr)), const vec_t *>::value,
                  "allocator owner exposes a const Vec view");
    const stl_allocator allocator = {nullptr, allocate, deallocate};
    vec_alloc_t *owner = nullptr;
    if (vec_alloc_new_bytes(sizeof(int), alignof(int), 1, &allocator, &owner) != STL_OK) return 1;
    const int value = 42;
    const stl_status status = vec_alloc_push(owner, &value);
    const int result = status == STL_OK && vec_size(vec_alloc_view(owner)) == 1 &&
                       *static_cast<const int *>(vec_at_const(vec_alloc_view(owner), 0)) == value ? 0 : 2;
    vec_alloc_destroy(owner);
    return result;
}
