#include <turbostl/list.h>

int main(void) {
    turbo_stl_status (*reserve_fn)(turbo_list_t *, size_t) = turbo_list_reserve;
    size_t (*capacity_fn)(const turbo_list_t *) = turbo_list_capacity;
    void *(*at_fn)(turbo_list_t *, size_t) = turbo_list_at;

    return reserve_fn == 0 || capacity_fn == 0 || at_fn == 0;
}
