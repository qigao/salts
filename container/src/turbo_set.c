#include <turbo/container/set.h>

#include <stdint.h>

container_status turbo_set_init(turbo_set_t *set, const cmeta_type_desc *key_type,
                                size_t entry_limit) {
    if (set == NULL) return CONTAINER_INVALID_ARGUMENT;
    return turbo_hash_map_init(&set->map, key_type, &cmeta_type_bool, entry_limit);
}

container_status turbo_set_init_bytes(turbo_set_t *set, size_t key_size, size_t key_align,
                                      size_t entry_limit, turbo_hash_fn hash,
                                      turbo_hash_equal_fn equal, void *ctx) {
    if (set == NULL) return CONTAINER_INVALID_ARGUMENT;
    return turbo_hash_map_init_bytes(&set->map, key_size, key_align, sizeof(uint8_t),
                                     _Alignof(uint8_t), entry_limit, hash, equal, ctx);
}

container_status turbo_set_from_array(turbo_set_t *set, const void *keys, size_t count,
                                      const cmeta_type_desc *key_type, size_t entry_limit) {
    turbo_set_t temporary = {0};
    container_status status;
    size_t index;
    uint64_t generation;
    if (set == NULL || set->map.initialized) return CONTAINER_INVALID_ARGUMENT;
    if (count != 0u && keys == NULL) return CONTAINER_INVALID_ARGUMENT;
    status = turbo_set_init(&temporary, key_type, entry_limit);
    if (status != CONTAINER_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = turbo_set_add(&temporary, (const unsigned char *)keys + index * key_type->size);
        if (status != CONTAINER_OK) { turbo_set_destroy(&temporary); return status; }
    }
    generation = set->map.generation + UINT64_C(1);
    temporary.map.generation = generation;
    *set = temporary;
    return CONTAINER_OK;
}

container_status turbo_set_from_array_bytes(turbo_set_t *set, const void *keys, size_t count,
                                            size_t key_size, size_t key_align, size_t entry_limit,
                                            turbo_hash_fn hash, turbo_hash_equal_fn equal, void *ctx) {
    turbo_set_t temporary = {0};
    container_status status;
    size_t index;
    uint64_t generation;
    if (set == NULL || set->map.initialized) return CONTAINER_INVALID_ARGUMENT;
    if (count != 0u && keys == NULL) return CONTAINER_INVALID_ARGUMENT;
    status = turbo_set_init_bytes(&temporary, key_size, key_align, entry_limit, hash, equal, ctx);
    if (status != CONTAINER_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = turbo_set_add(&temporary, (const unsigned char *)keys + index * key_size);
        if (status != CONTAINER_OK) { turbo_set_destroy(&temporary); return status; }
    }
    generation = set->map.generation + UINT64_C(1);
    temporary.map.generation = generation;
    *set = temporary;
    return CONTAINER_OK;
}

void turbo_set_destroy(turbo_set_t *set) { if (set != NULL) turbo_hash_map_destroy(&set->map); }
void turbo_set_clear(turbo_set_t *set) { if (set != NULL) turbo_hash_map_clear(&set->map); }
container_status turbo_set_reserve(turbo_set_t *set, size_t min_entries) {
    return set == NULL ? CONTAINER_INVALID_ARGUMENT : turbo_hash_map_reserve(&set->map, min_entries);
}

container_status turbo_set_add(turbo_set_t *set, const void *key) {
    uint8_t present = 1u;
    if (set == NULL) return CONTAINER_INVALID_ARGUMENT;
    if (turbo_hash_map_contains(&set->map, key)) return CONTAINER_OK;
    return turbo_hash_map_put(&set->map, key, &present);
}

bool turbo_set_contains(const turbo_set_t *set, const void *key) {
    return set != NULL && turbo_hash_map_contains(&set->map, key);
}
container_status turbo_set_remove(turbo_set_t *set, const void *key) {
    return set == NULL ? CONTAINER_INVALID_ARGUMENT : turbo_hash_map_remove(&set->map, key, NULL);
}
size_t turbo_set_size(const turbo_set_t *set) { return set == NULL ? 0u : turbo_hash_map_size(&set->map); }
size_t turbo_set_capacity(const turbo_set_t *set) { return set == NULL ? 0u : turbo_hash_map_capacity(&set->map); }
size_t turbo_set_entry_limit(const turbo_set_t *set) { return set == NULL ? 0u : turbo_hash_map_entry_limit(&set->map); }
uint64_t turbo_set_generation(const turbo_set_t *set) { return set == NULL ? UINT64_C(0) : turbo_hash_map_generation(&set->map); }
bool turbo_set_empty(const turbo_set_t *set) { return turbo_set_size(set) == 0u; }
const void *turbo_set_key_at(const turbo_set_t *set, size_t slot) { return set == NULL ? NULL : turbo_hash_map_key_at(&set->map, slot); }
