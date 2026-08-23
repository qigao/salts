#include <turbostl/hash_map.h>

#include "turbo_sequence_internal.h"

#include <string.h>

#define TURBO_HASH_EMPTY 0u
#define TURBO_HASH_OCCUPIED 1u
#define TURBO_HASH_TOMBSTONE 2u
#define TURBO_HASH_MIN_BUCKETS 16u

static bool turbo_hash_map_valid(const turbo_hash_map_t *map) {
    return map != NULL && map->initialized && map->key_size != 0u && map->value_size != 0u;
}

static unsigned char *turbo_hash_key_slot(turbo_hash_map_t *map, size_t slot) {
    return map->keys + slot * map->key_stride;
}

static const unsigned char *turbo_hash_key_slot_const(const turbo_hash_map_t *map, size_t slot) {
    return map->keys + slot * map->key_stride;
}

static unsigned char *turbo_hash_value_slot(turbo_hash_map_t *map, size_t slot) {
    return map->values + slot * map->value_stride;
}

static turbo_stl_status turbo_hash_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || right > SIZE_MAX - left)
        return TURBO_STL_CAPACITY_EXCEEDED;
    *out = left + right;
    return TURBO_STL_OK;
}

static turbo_stl_status turbo_hash_mul(size_t left, size_t right, size_t *out) {
    if (out == NULL || (left != 0u && right > SIZE_MAX / left))
        return TURBO_STL_CAPACITY_EXCEEDED;
    *out = left * right;
    return TURBO_STL_OK;
}

static turbo_stl_status turbo_hash_align(size_t value, size_t alignment, size_t *out) {
    size_t padding;
    if (!turbo_sequence_alignment_valid(alignment) || out == NULL)
        return TURBO_STL_INVALID_ARGUMENT;
    padding = alignment - 1u;
    if (value > SIZE_MAX - padding)
        return TURBO_STL_CAPACITY_EXCEEDED;
    *out = (value + padding) & ~padding;
    return TURBO_STL_OK;
}

static size_t turbo_hash_max_alignment(const turbo_hash_map_t *map) {
    size_t alignment = _Alignof(size_t);
    if (map->key_align > alignment) alignment = map->key_align;
    if (map->value_align > alignment) alignment = map->value_align;
    return alignment;
}

static turbo_stl_status turbo_hash_buckets_for_entries(size_t entries, size_t *out_buckets) {
    size_t quotient;
    size_t remainder;
    size_t extra;
    size_t requested;

    if (out_buckets == NULL) return TURBO_STL_INVALID_ARGUMENT;
    if (entries == 0u) { *out_buckets = 0u; return TURBO_STL_OK; }
    quotient = entries / 7u;
    remainder = entries % 7u;
    if (quotient > SIZE_MAX / 3u) return TURBO_STL_CAPACITY_EXCEEDED;
    extra = quotient * 3u + (remainder * 3u + 6u) / 7u;
    if (turbo_hash_add(entries, extra, &requested) != TURBO_STL_OK)
        return TURBO_STL_CAPACITY_EXCEEDED;
    if (requested < TURBO_HASH_MIN_BUCKETS) requested = TURBO_HASH_MIN_BUCKETS;
    *out_buckets = TURBO_HASH_MIN_BUCKETS;
    while (*out_buckets < requested) {
        if (*out_buckets > SIZE_MAX / 2u) return TURBO_STL_CAPACITY_EXCEEDED;
        *out_buckets *= 2u;
    }
    return TURBO_STL_OK;
}

static turbo_stl_status turbo_hash_map_allocate(turbo_hash_map_t *map, size_t capacity) {
    size_t states_bytes;
    size_t hashes_bytes;
    size_t keys_bytes;
    size_t values_bytes;
    size_t hashes_offset;
    size_t keys_offset;
    size_t values_offset;
    size_t total;
    void *storage;
    turbo_stl_status status;

    status = turbo_hash_mul(capacity, sizeof(uint8_t), &states_bytes);
    if (status != TURBO_STL_OK) return status;
    status = turbo_hash_align(states_bytes, _Alignof(size_t), &hashes_offset);
    if (status != TURBO_STL_OK) return status;
    status = turbo_hash_mul(capacity, sizeof(size_t), &hashes_bytes);
    if (status != TURBO_STL_OK || turbo_hash_add(hashes_offset, hashes_bytes, &total) != TURBO_STL_OK)
        return TURBO_STL_CAPACITY_EXCEEDED;
    status = turbo_hash_align(total, map->key_align, &keys_offset);
    if (status != TURBO_STL_OK) return status;
    status = turbo_hash_mul(capacity, map->key_stride, &keys_bytes);
    if (status != TURBO_STL_OK || turbo_hash_add(keys_offset, keys_bytes, &total) != TURBO_STL_OK)
        return TURBO_STL_CAPACITY_EXCEEDED;
    status = turbo_hash_align(total, map->value_align, &values_offset);
    if (status != TURBO_STL_OK) return status;
    status = turbo_hash_mul(capacity, map->value_stride, &values_bytes);
    if (status != TURBO_STL_OK || turbo_hash_add(values_offset, values_bytes, &total) != TURBO_STL_OK)
        return TURBO_STL_CAPACITY_EXCEEDED;
    status = turbo_sequence_allocate(1u, total, turbo_hash_max_alignment(map), &storage);
    if (status != TURBO_STL_OK) return status;
    memset(storage, TURBO_HASH_EMPTY, states_bytes);
    map->states = (uint8_t *)storage;
    map->hashes = (size_t *)(void *)((unsigned char *)storage + hashes_offset);
    map->keys = (unsigned char *)storage + keys_offset;
    map->values = (unsigned char *)storage + values_offset;
    map->capacity = capacity;
    return TURBO_STL_OK;
}

static void turbo_hash_destroy_value(const cmeta_type_desc *type, void *value) {
    if (type != NULL) type->traits->destroy(value);
}

static turbo_stl_status turbo_hash_prepare(const cmeta_type_desc *type, size_t size,
                                           size_t stride, size_t alignment, const void *source,
                                           void **out_value) {
    turbo_stl_status status;
    if (source == NULL || out_value == NULL) return TURBO_STL_INVALID_ARGUMENT;
    *out_value = NULL;
    status = turbo_sequence_allocate(1u, stride, alignment, out_value);
    if (status != TURBO_STL_OK) return status;
    if (type != NULL && !type->traits->copy_construct(*out_value, source)) {
        turbo_sequence_deallocate(*out_value);
        *out_value = NULL;
        return TURBO_STL_OUT_OF_MEMORY;
    }
    if (type == NULL) memcpy(*out_value, source, size);
    return TURBO_STL_OK;
}

static void turbo_hash_discard(const cmeta_type_desc *type, void *value) {
    if (value == NULL) return;
    turbo_hash_destroy_value(type, value);
    turbo_sequence_deallocate(value);
}

static void turbo_hash_move_construct(const cmeta_type_desc *type, size_t size, void *destination,
                                      void *source) {
    if (type != NULL) type->traits->move_construct(destination, source);
    else memcpy(destination, source, size);
}

static void turbo_hash_move_destroy(const cmeta_type_desc *type, size_t size, void *destination,
                                    void *source) {
    turbo_hash_move_construct(type, size, destination, source);
    turbo_hash_destroy_value(type, source);
}

static size_t turbo_hash_map_hash(const turbo_hash_map_t *map, const void *key) {
    size_t hash = map->key_type != NULL ? (size_t)map->key_type->traits->hash(key)
                                         : map->hash(key, map->key_size, map->ctx);
    return hash == 0u ? 1u : hash;
}

static bool turbo_hash_map_equal(const turbo_hash_map_t *map, const void *left, const void *right) {
    return map->key_type != NULL ? map->key_type->traits->equal(left, right)
                                 : map->equal(left, right, map->key_size, map->ctx);
}

static bool turbo_hash_map_find_slot(const turbo_hash_map_t *map, const void *key, size_t hash,
                                     size_t *out_slot) {
    size_t mask;
    size_t probe;
    if (map->capacity == 0u) return false;
    mask = map->capacity - 1u;
    for (probe = 0u; probe < map->capacity; ++probe) {
        size_t slot = (hash + probe) & mask;
        if (map->states[slot] == TURBO_HASH_EMPTY) return false;
        if (map->states[slot] == TURBO_HASH_OCCUPIED && map->hashes[slot] == hash &&
            turbo_hash_map_equal(map, turbo_hash_key_slot_const(map, slot), key)) {
            *out_slot = slot;
            return true;
        }
    }
    return false;
}

static size_t turbo_hash_map_insert_slot(const turbo_hash_map_t *map, size_t hash) {
    size_t first_tombstone = SIZE_MAX;
    size_t mask = map->capacity - 1u;
    size_t probe;
    for (probe = 0u; probe < map->capacity; ++probe) {
        size_t slot = (hash + probe) & mask;
        if (map->states[slot] == TURBO_HASH_TOMBSTONE) {
            if (first_tombstone == SIZE_MAX) first_tombstone = slot;
        } else if (map->states[slot] == TURBO_HASH_EMPTY) {
            return first_tombstone == SIZE_MAX ? slot : first_tombstone;
        }
    }
    return first_tombstone;
}

static void turbo_hash_map_rehash_insert(turbo_hash_map_t *map, size_t hash, void *key,
                                         void *value) {
    size_t slot = turbo_hash_map_insert_slot(map, hash);
    turbo_hash_move_construct(map->key_type, map->key_size, turbo_hash_key_slot(map, slot), key);
    turbo_hash_move_construct(map->value_type, map->value_size, turbo_hash_value_slot(map, slot), value);
    map->hashes[slot] = hash;
    map->states[slot] = TURBO_HASH_OCCUPIED;
    ++map->size;
}

static turbo_stl_status turbo_hash_map_rehash(turbo_hash_map_t *map, size_t capacity) {
    turbo_hash_map_t next = *map;
    size_t slot;
    turbo_stl_status status;

    next.states = NULL; next.hashes = NULL; next.keys = NULL; next.values = NULL;
    next.size = 0u; next.capacity = 0u; next.tombstones = 0u;
    status = turbo_hash_map_allocate(&next, capacity);
    if (status != TURBO_STL_OK) return status;
    for (slot = 0u; slot < map->capacity; ++slot) {
        if (map->states[slot] != TURBO_HASH_OCCUPIED) continue;
        turbo_hash_map_rehash_insert(&next, map->hashes[slot], turbo_hash_key_slot(map, slot),
                                     turbo_hash_value_slot(map, slot));
        turbo_hash_destroy_value(map->key_type, turbo_hash_key_slot(map, slot));
        turbo_hash_destroy_value(map->value_type, turbo_hash_value_slot(map, slot));
    }
    turbo_sequence_deallocate(map->states);
    next.generation = map->generation;
    *map = next;
    return TURBO_STL_OK;
}

static turbo_stl_status turbo_hash_map_ensure_insert(turbo_hash_map_t *map) {
    size_t desired;
    size_t buckets;
    turbo_stl_status status;
    if (map->size == SIZE_MAX) return TURBO_STL_CAPACITY_EXCEEDED;
    desired = map->size + 1u;
    status = turbo_hash_buckets_for_entries(desired, &buckets);
    if (status != TURBO_STL_OK) return status;
    if (buckets > map->capacity || (map->tombstones > map->size && map->capacity != 0u)) {
        if (buckets < map->capacity) buckets = map->capacity;
        return turbo_hash_map_rehash(map, buckets);
    }
    return TURBO_STL_OK;
}

static turbo_stl_status turbo_hash_map_initialize(turbo_hash_map_t *map,
                                                  const cmeta_type_desc *key_type,
                                                  const cmeta_type_desc *value_type,
                                                  size_t key_size, size_t key_align,
                                                  size_t value_size, size_t value_align,
                                                  size_t entry_limit, turbo_hash_fn hash,
                                                  turbo_hash_equal_fn equal, void *ctx) {
    size_t key_stride;
    size_t value_stride;
    uint64_t generation;
    turbo_stl_status status;
    if (map == NULL || map->initialized) return TURBO_STL_INVALID_ARGUMENT;
    status = turbo_sequence_stride(key_size, key_align, &key_stride);
    if (status != TURBO_STL_OK) return status;
    status = turbo_sequence_stride(value_size, value_align, &value_stride);
    if (status != TURBO_STL_OK) return status;
    generation = map->generation + UINT64_C(1);
    memset(map, 0, sizeof(*map));
    map->key_size = key_size; map->key_stride = key_stride; map->key_align = key_align;
    map->value_size = value_size; map->value_stride = value_stride; map->value_align = value_align;
    map->entry_limit = entry_limit; map->key_type = key_type; map->value_type = value_type;
    map->hash = hash; map->equal = equal; map->ctx = ctx; map->generation = generation;
    map->initialized = true;
    return TURBO_STL_OK;
}

size_t turbo_hash_bytes(const void *key, size_t key_size, void *ctx) {
    const unsigned char *bytes = (const unsigned char *)key;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;
    (void)ctx;
    for (index = 0u; index < key_size; ++index) { hash ^= bytes[index]; hash *= UINT64_C(1099511628211); }
    return (size_t)(hash == 0u ? 1u : hash);
}

bool turbo_hash_key_equal(const void *left, const void *right, size_t key_size, void *ctx) {
    (void)ctx;
    return memcmp(left, right, key_size) == 0;
}

turbo_stl_status turbo_hash_map_init(turbo_hash_map_t *map, const cmeta_type_desc *key_type,
                                     const cmeta_type_desc *value_type, size_t entry_limit) {
    cmeta_trait_flags key_required = CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COPY |
                                     CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY;
    cmeta_trait_flags value_required = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY;
    if (map == NULL || map->initialized) return TURBO_STL_INVALID_ARGUMENT;
    if (key_type == NULL || value_type == NULL || key_type->size == 0u || value_type->size == 0u ||
        !turbo_sequence_alignment_valid(key_type->align) || !turbo_sequence_alignment_valid(value_type->align))
        return TURBO_STL_INVALID_ARGUMENT;
    if (cmeta_type_require_traits(key_type, key_required) != CMETA_OK ||
        cmeta_type_require_traits(value_type, value_required) != CMETA_OK)
        return TURBO_STL_TRAIT_MISSING;
    return turbo_hash_map_initialize(map, key_type, value_type, key_type->size, key_type->align,
                                     value_type->size, value_type->align, entry_limit, NULL, NULL, NULL);
}

turbo_stl_status turbo_hash_map_init_bytes(turbo_hash_map_t *map, size_t key_size, size_t key_align,
                                           size_t value_size, size_t value_align, size_t entry_limit,
                                           turbo_hash_fn hash, turbo_hash_equal_fn equal, void *ctx) {
    if (map == NULL || map->initialized || hash == NULL || equal == NULL) return TURBO_STL_INVALID_ARGUMENT;
    return turbo_hash_map_initialize(map, NULL, NULL, key_size, key_align, value_size, value_align,
                                     entry_limit, hash, equal, ctx);
}

turbo_stl_status turbo_hash_map_from_arrays(turbo_hash_map_t *map, const void *keys,
                                            const void *values, size_t count,
                                            const cmeta_type_desc *key_type,
                                            const cmeta_type_desc *value_type, size_t entry_limit) {
    turbo_hash_map_t temporary = {0};
    turbo_stl_status status;
    size_t index;
    uint64_t generation;
    if (map == NULL) return TURBO_STL_INVALID_ARGUMENT;
    if (count != 0u && (keys == NULL || values == NULL)) return TURBO_STL_INVALID_ARGUMENT;
    status = turbo_hash_map_init(&temporary, key_type, value_type, entry_limit);
    if (status != TURBO_STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = turbo_hash_map_put(&temporary, (const unsigned char *)keys + index * key_type->size,
                                    (const unsigned char *)values + index * value_type->size);
        if (status != TURBO_STL_OK) { turbo_hash_map_destroy(&temporary); return status; }
    }
    generation = map->generation + UINT64_C(1);
    if (map->initialized) turbo_hash_map_destroy(map);
    temporary.generation = generation;
    *map = temporary;
    return TURBO_STL_OK;
}

turbo_stl_status turbo_hash_map_from_arrays_bytes(turbo_hash_map_t *map, const void *keys,
                                                  const void *values, size_t count, size_t key_size,
                                                  size_t key_align, size_t value_size,
                                                  size_t value_align, size_t entry_limit,
                                                  turbo_hash_fn hash, turbo_hash_equal_fn equal,
                                                  void *ctx) {
    turbo_hash_map_t temporary = {0};
    turbo_stl_status status;
    size_t index;
    uint64_t generation;
    if (map == NULL) return TURBO_STL_INVALID_ARGUMENT;
    if (count != 0u && (keys == NULL || values == NULL)) return TURBO_STL_INVALID_ARGUMENT;
    status = turbo_hash_map_init_bytes(&temporary, key_size, key_align, value_size, value_align,
                                       entry_limit, hash, equal, ctx);
    if (status != TURBO_STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = turbo_hash_map_put(&temporary, (const unsigned char *)keys + index * key_size,
                                    (const unsigned char *)values + index * value_size);
        if (status != TURBO_STL_OK) { turbo_hash_map_destroy(&temporary); return status; }
    }
    generation = map->generation + UINT64_C(1);
    if (map->initialized) turbo_hash_map_destroy(map);
    temporary.generation = generation;
    *map = temporary;
    return TURBO_STL_OK;
}

void turbo_hash_map_destroy(turbo_hash_map_t *map) {
    size_t slot;
    uint64_t generation;
    if (map == NULL) return;
    generation = map->generation;
    if (map->initialized) {
        for (slot = 0u; slot < map->capacity; ++slot) {
            if (map->states[slot] != TURBO_HASH_OCCUPIED) continue;
            turbo_hash_destroy_value(map->key_type, turbo_hash_key_slot(map, slot));
            turbo_hash_destroy_value(map->value_type, turbo_hash_value_slot(map, slot));
        }
        generation += UINT64_C(1);
        turbo_sequence_deallocate(map->states);
    }
    memset(map, 0, sizeof(*map));
    map->generation = generation;
}

void turbo_hash_map_clear(turbo_hash_map_t *map) {
    size_t slot;
    if (!turbo_hash_map_valid(map)) return;
    for (slot = 0u; slot < map->capacity; ++slot) {
        if (map->states[slot] != TURBO_HASH_OCCUPIED) continue;
        turbo_hash_destroy_value(map->key_type, turbo_hash_key_slot(map, slot));
        turbo_hash_destroy_value(map->value_type, turbo_hash_value_slot(map, slot));
    }
    if (map->size != 0u) {
        memset(map->states, TURBO_HASH_EMPTY, map->capacity * sizeof(*map->states));
        map->size = 0u;
        map->tombstones = 0u;
        ++map->generation;
    }
}

turbo_stl_status turbo_hash_map_reserve(turbo_hash_map_t *map, size_t min_entries) {
    size_t buckets;
    turbo_stl_status status;
    if (!turbo_hash_map_valid(map)) return TURBO_STL_INVALID_ARGUMENT;
    if (min_entries > map->entry_limit) return TURBO_STL_CAPACITY_EXCEEDED;
    status = turbo_hash_buckets_for_entries(min_entries, &buckets);
    if (status != TURBO_STL_OK) return status;
    if (buckets > map->capacity || (map->tombstones > map->size && map->capacity != 0u)) {
        if (buckets < map->capacity) buckets = map->capacity;
        status = turbo_hash_map_rehash(map, buckets);
        if (status == TURBO_STL_OK) ++map->generation;
    }
    return status;
}

turbo_stl_status turbo_hash_map_put(turbo_hash_map_t *map, const void *key, const void *value) {
    size_t hash;
    size_t slot;
    void *prepared_key = NULL;
    void *prepared_value = NULL;
    turbo_stl_status status;
    if (!turbo_hash_map_valid(map) || key == NULL || value == NULL) return TURBO_STL_INVALID_ARGUMENT;
    hash = turbo_hash_map_hash(map, key);
    if (turbo_hash_map_find_slot(map, key, hash, &slot)) {
        status = turbo_hash_prepare(map->value_type, map->value_size, map->value_stride,
                                    map->value_align, value, &prepared_value);
        if (status != TURBO_STL_OK) return status;
        turbo_hash_destroy_value(map->value_type, turbo_hash_value_slot(map, slot));
        turbo_hash_move_destroy(map->value_type, map->value_size, turbo_hash_value_slot(map, slot),
                                prepared_value);
        turbo_sequence_deallocate(prepared_value);
        ++map->generation;
        return TURBO_STL_OK;
    }
    if (map->size >= map->entry_limit) return TURBO_STL_CAPACITY_EXCEEDED;
    status = turbo_hash_prepare(map->key_type, map->key_size, map->key_stride, map->key_align,
                                key, &prepared_key);
    if (status != TURBO_STL_OK) return status;
    status = turbo_hash_prepare(map->value_type, map->value_size, map->value_stride,
                                map->value_align, value, &prepared_value);
    if (status != TURBO_STL_OK) { turbo_hash_discard(map->key_type, prepared_key); return status; }
    status = turbo_hash_map_ensure_insert(map);
    if (status != TURBO_STL_OK) {
        turbo_hash_discard(map->key_type, prepared_key);
        turbo_hash_discard(map->value_type, prepared_value);
        return status;
    }
    slot = turbo_hash_map_insert_slot(map, hash);
    turbo_hash_move_destroy(map->key_type, map->key_size, turbo_hash_key_slot(map, slot), prepared_key);
    turbo_hash_move_destroy(map->value_type, map->value_size, turbo_hash_value_slot(map, slot), prepared_value);
    turbo_sequence_deallocate(prepared_key);
    turbo_sequence_deallocate(prepared_value);
    map->hashes[slot] = hash;
    if (map->states[slot] == TURBO_HASH_TOMBSTONE) --map->tombstones;
    map->states[slot] = TURBO_HASH_OCCUPIED;
    ++map->size;
    ++map->generation;
    return TURBO_STL_OK;
}

void *turbo_hash_map_get(turbo_hash_map_t *map, const void *key) {
    size_t slot;
    size_t hash;
    if (!turbo_hash_map_valid(map) || key == NULL) return NULL;
    hash = turbo_hash_map_hash(map, key);
    return turbo_hash_map_find_slot(map, key, hash, &slot) ? turbo_hash_value_slot(map, slot) : NULL;
}

const void *turbo_hash_map_get_const(const turbo_hash_map_t *map, const void *key) {
    return turbo_hash_map_get((turbo_hash_map_t *)map, key);
}

bool turbo_hash_map_contains(const turbo_hash_map_t *map, const void *key) {
    return turbo_hash_map_get_const(map, key) != NULL;
}

turbo_stl_status turbo_hash_map_remove(turbo_hash_map_t *map, const void *key, void *out_value) {
    size_t hash;
    size_t slot;
    if (!turbo_hash_map_valid(map) || key == NULL) return TURBO_STL_INVALID_ARGUMENT;
    hash = turbo_hash_map_hash(map, key);
    if (!turbo_hash_map_find_slot(map, key, hash, &slot)) return TURBO_STL_NOT_FOUND;
    if (out_value != NULL)
        turbo_hash_move_destroy(map->value_type, map->value_size, out_value,
                                turbo_hash_value_slot(map, slot));
    else
        turbo_hash_destroy_value(map->value_type, turbo_hash_value_slot(map, slot));
    turbo_hash_destroy_value(map->key_type, turbo_hash_key_slot(map, slot));
    map->states[slot] = TURBO_HASH_TOMBSTONE;
    --map->size;
    ++map->tombstones;
    ++map->generation;
    return TURBO_STL_OK;
}

size_t turbo_hash_map_size(const turbo_hash_map_t *map) { return turbo_hash_map_valid(map) ? map->size : 0u; }
size_t turbo_hash_map_capacity(const turbo_hash_map_t *map) { return turbo_hash_map_valid(map) ? map->capacity : 0u; }
size_t turbo_hash_map_entry_limit(const turbo_hash_map_t *map) { return turbo_hash_map_valid(map) ? map->entry_limit : 0u; }
uint64_t turbo_hash_map_generation(const turbo_hash_map_t *map) { return map ? map->generation : UINT64_C(0); }
bool turbo_hash_map_empty(const turbo_hash_map_t *map) { return turbo_hash_map_size(map) == 0u; }

const void *turbo_hash_map_key_at(const turbo_hash_map_t *map, size_t slot) {
    return turbo_hash_map_valid(map) && slot < map->capacity && map->states[slot] == TURBO_HASH_OCCUPIED
        ? turbo_hash_key_slot_const(map, slot) : NULL;
}

void *turbo_hash_map_value_at(turbo_hash_map_t *map, size_t slot) {
    return turbo_hash_map_valid(map) && slot < map->capacity && map->states[slot] == TURBO_HASH_OCCUPIED
        ? turbo_hash_value_slot(map, slot) : NULL;
}

const void *turbo_hash_map_value_at_const(const turbo_hash_map_t *map, size_t slot) {
    return turbo_hash_map_value_at((turbo_hash_map_t *)map, slot);
}
