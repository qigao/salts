#include <turbostl/map.h>

int main(void) {
    turbo_stl_status (*reserve_fn)(turbo_map_t *, size_t) = turbo_map_reserve;
    size_t (*capacity_fn)(const turbo_map_t *) = turbo_map_capacity;
    void *(*key_at_fn)(turbo_map_t *, size_t) = turbo_map_key_at;

    return reserve_fn == 0 || capacity_fn == 0 || key_at_fn == 0;
}
