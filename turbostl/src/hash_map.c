#include <turbostl/hash_map.h>

#include "sequence_internal.h"

#include <string.h>

#define TURBO_HASH_EMPTY 0u
#define TURBO_HASH_OCCUPIED 1u
#define TURBO_HASH_TOMBSTONE 2u
#define TURBO_HASH_MIN_BUCKETS 16u

static bool hash_map_valid(const hash_map_t *map) {
    return map != NULL && map->initialized && map->key_size != 0u && map->value_size != 0u;
}

static unsigned char *hash_key_slot(hash_map_t *map, size_t slot) {
    return map->keys + slot * map->key_stride;
}

static const unsigned char *hash_key_slot_const(const hash_map_t *map, size_t slot) {
    return map->keys + slot * map->key_stride;
}

static unsigned char *hash_value_slot(hash_map_t *map, size_t slot) {
    return map->values + slot * map->value_stride;
}

static stl_status hash_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || right > SIZE_MAX - left)
        return STL_CAPACITY_EXCEEDED;
    *out = left + right;
    return STL_OK;
}

static stl_status hash_mul(size_t left, size_t right, size_t *out) {
    if (out == NULL || (left != 0u && right > SIZE_MAX / left))
        return STL_CAPACITY_EXCEEDED;
    *out = left * right;
    return STL_OK;
}

static stl_status hash_align(size_t value, size_t alignment, size_t *out) {
    size_t padding;
    if (!sequence_alignment_valid(alignment) || out == NULL)
        return STL_INVALID_ARGUMENT;
    padding = alignment - 1u;
    if (value > SIZE_MAX - padding)
        return STL_CAPACITY_EXCEEDED;
    *out = (value + padding) & ~padding;
    return STL_OK;
}

static size_t hash_max_alignment(const hash_map_t *map) {
    size_t alignment = _Alignof(size_t);
    if (map->key_align > alignment) alignment = map->key_align;
    if (map->value_align > alignment) alignment = map->value_align;
    return alignment;
}

static stl_status hash_buckets_for_entries(size_t entries, size_t *out_buckets) {
    size_t quotient;
    size_t remainder;
    size_t extra;
    size_t requested;

    if (out_buckets == NULL) return STL_INVALID_ARGUMENT;
    if (entries == 0u) { *out_buckets = 0u; return STL_OK; }
    quotient = entries / 7u;
    remainder = entries % 7u;
    if (quotient > SIZE_MAX / 3u) return STL_CAPACITY_EXCEEDED;
    extra = quotient * 3u + (remainder * 3u + 6u) / 7u;
    if (hash_add(entries, extra, &requested) != STL_OK)
        return STL_CAPACITY_EXCEEDED;
    if (requested < TURBO_HASH_MIN_BUCKETS) requested = TURBO_HASH_MIN_BUCKETS;
    *out_buckets = TURBO_HASH_MIN_BUCKETS;
    while (*out_buckets < requested) {
        if (*out_buckets > SIZE_MAX / 2u) return STL_CAPACITY_EXCEEDED;
        *out_buckets *= 2u;
    }
    return STL_OK;
}

static stl_status hash_map_allocate(hash_map_t *map, size_t capacity) {
    size_t states_bytes;
    size_t hashes_bytes;
    size_t keys_bytes;
    size_t values_bytes;
    size_t hashes_offset;
    size_t keys_offset;
    size_t values_offset;
    size_t total;
    void *storage;
    stl_status status;

    status = hash_mul(capacity, sizeof(uint8_t), &states_bytes);
    if (status != STL_OK) return status;
    status = hash_align(states_bytes, _Alignof(size_t), &hashes_offset);
    if (status != STL_OK) return status;
    status = hash_mul(capacity, sizeof(size_t), &hashes_bytes);
    if (status != STL_OK || hash_add(hashes_offset, hashes_bytes, &total) != STL_OK)
        return STL_CAPACITY_EXCEEDED;
    status = hash_align(total, map->key_align, &keys_offset);
    if (status != STL_OK) return status;
    status = hash_mul(capacity, map->key_stride, &keys_bytes);
    if (status != STL_OK || hash_add(keys_offset, keys_bytes, &total) != STL_OK)
        return STL_CAPACITY_EXCEEDED;
    status = hash_align(total, map->value_align, &values_offset);
    if (status != STL_OK) return status;
    status = hash_mul(capacity, map->value_stride, &values_bytes);
    if (status != STL_OK || hash_add(values_offset, values_bytes, &total) != STL_OK)
        return STL_CAPACITY_EXCEEDED;
    status = sequence_allocate(1u, total, hash_max_alignment(map), &storage);
    if (status != STL_OK) return status;
    memset(storage, TURBO_HASH_EMPTY, states_bytes);
    map->states = (uint8_t *)storage;
    map->hashes = (size_t *)(void *)((unsigned char *)storage + hashes_offset);
    map->keys = (unsigned char *)storage + keys_offset;
    map->values = (unsigned char *)storage + values_offset;
    map->capacity = capacity;
    return STL_OK;
}

static void hash_destroy_value(const cmeta_type_desc *type, void *value) {
    if (type != NULL) type->traits->destroy(value);
}

static stl_status hash_prepare(const cmeta_type_desc *type, size_t size,
                                           size_t stride, size_t alignment, const void *source,
                                           void **out_value) {
    stl_status status;
    if (source == NULL || out_value == NULL) return STL_INVALID_ARGUMENT;
    *out_value = NULL;
    status = sequence_allocate(1u, stride, alignment, out_value);
    if (status != STL_OK) return status;
    if (type != NULL && !type->traits->copy_construct(*out_value, source)) {
        sequence_deallocate(*out_value);
        *out_value = NULL;
        return STL_OUT_OF_MEMORY;
    }
    if (type == NULL) memcpy(*out_value, source, size);
    return STL_OK;
}

static void hash_discard(const cmeta_type_desc *type, void *value) {
    if (value == NULL) return;
    hash_destroy_value(type, value);
    sequence_deallocate(value);
}

static void hash_move_construct(const cmeta_type_desc *type, size_t size, void *destination,
                                      void *source) {
    if (type != NULL) type->traits->move_construct(destination, source);
    else memcpy(destination, source, size);
}

static void hash_move_destroy(const cmeta_type_desc *type, size_t size, void *destination,
                                    void *source) {
    hash_move_construct(type, size, destination, source);
    hash_destroy_value(type, source);
}

static size_t hash_map_hash(const hash_map_t *map, const void *key) {
    size_t hash = map->key_type != NULL ? (size_t)map->key_type->traits->hash(key)
                                         : map->hash(key, map->key_size, map->ctx);
    return hash == 0u ? 1u : hash;
}

static bool hash_map_equal(const hash_map_t *map, const void *left, const void *right) {
    return map->key_type != NULL ? map->key_type->traits->equal(left, right)
                                 : map->equal(left, right, map->key_size, map->ctx);
}

static bool hash_map_find_slot(const hash_map_t *map, const void *key, size_t hash,
                                     size_t *out_slot) {
    size_t mask;
    size_t probe;
    if (map->capacity == 0u) return false;
    mask = map->capacity - 1u;
    for (probe = 0u; probe < map->capacity; ++probe) {
        size_t slot = (hash + probe) & mask;
        if (map->states[slot] == TURBO_HASH_EMPTY) return false;
        if (map->states[slot] == TURBO_HASH_OCCUPIED && map->hashes[slot] == hash &&
            hash_map_equal(map, hash_key_slot_const(map, slot), key)) {
            *out_slot = slot;
            return true;
        }
    }
    return false;
}

static size_t hash_map_insert_slot(const hash_map_t *map, size_t hash) {
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

static void hash_map_rehash_insert(hash_map_t *map, size_t hash, void *key,
                                         void *value) {
    size_t slot = hash_map_insert_slot(map, hash);
    hash_move_construct(map->key_type, map->key_size, hash_key_slot(map, slot), key);
    hash_move_construct(map->value_type, map->value_size, hash_value_slot(map, slot), value);
    map->hashes[slot] = hash;
    map->states[slot] = TURBO_HASH_OCCUPIED;
    ++map->size;
}

static stl_status hash_map_rehash(hash_map_t *map, size_t capacity) {
    hash_map_t next = *map;
    size_t slot;
    stl_status status;

    next.states = NULL; next.hashes = NULL; next.keys = NULL; next.values = NULL;
    next.size = 0u; next.capacity = 0u; next.tombstones = 0u;
    status = hash_map_allocate(&next, capacity);
    if (status != STL_OK) return status;
    for (slot = 0u; slot < map->capacity; ++slot) {
        if (map->states[slot] != TURBO_HASH_OCCUPIED) continue;
        hash_map_rehash_insert(&next, map->hashes[slot], hash_key_slot(map, slot),
                                     hash_value_slot(map, slot));
        hash_destroy_value(map->key_type, hash_key_slot(map, slot));
        hash_destroy_value(map->value_type, hash_value_slot(map, slot));
    }
    sequence_deallocate(map->states);
    next.generation = map->generation;
    *map = next;
    return STL_OK;
}

static stl_status hash_map_ensure_insert(hash_map_t *map) {
    size_t desired;
    size_t buckets;
    stl_status status;
    if (map->size == SIZE_MAX) return STL_CAPACITY_EXCEEDED;
    desired = map->size + 1u;
    status = hash_buckets_for_entries(desired, &buckets);
    if (status != STL_OK) return status;
    if (buckets > map->capacity || (map->tombstones > map->size && map->capacity != 0u)) {
        if (buckets < map->capacity) buckets = map->capacity;
        return hash_map_rehash(map, buckets);
    }
    return STL_OK;
}

static stl_status hash_map_initialize(hash_map_t *map,
                                                  const cmeta_type_desc *key_type,
                                                  const cmeta_type_desc *value_type,
                                                  size_t key_size, size_t key_align,
                                                  size_t value_size, size_t value_align,
                                                  size_t entry_limit, hash_fn hash,
                                                  hash_equal_fn equal, void *ctx) {
    size_t key_stride;
    size_t value_stride;
    uint64_t generation;
    stl_status status;
    if (map == NULL || map->initialized) return STL_INVALID_ARGUMENT;
    status = sequence_stride(key_size, key_align, &key_stride);
    if (status != STL_OK) return status;
    status = sequence_stride(value_size, value_align, &value_stride);
    if (status != STL_OK) return status;
    generation = map->generation + UINT64_C(1);
    memset(map, 0, sizeof(*map));
    map->key_size = key_size; map->key_stride = key_stride; map->key_align = key_align;
    map->value_size = value_size; map->value_stride = value_stride; map->value_align = value_align;
    map->entry_limit = entry_limit; map->key_type = key_type; map->value_type = value_type;
    map->hash = hash; map->equal = equal; map->ctx = ctx; map->generation = generation;
    map->initialized = true;
    return STL_OK;
}

size_t hash_bytes(const void *key, size_t key_size, void *ctx) {
    const unsigned char *bytes = (const unsigned char *)key;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;
    (void)ctx;
    for (index = 0u; index < key_size; ++index) { hash ^= bytes[index]; hash *= UINT64_C(1099511628211); }
    return (size_t)(hash == 0u ? 1u : hash);
}

bool hash_key_equal(const void *left, const void *right, size_t key_size, void *ctx) {
    (void)ctx;
    return memcmp(left, right, key_size) == 0;
}

stl_status hash_map_raw_init(hash_map_t *map, const cmeta_type_desc *key_type,
                                     const cmeta_type_desc *value_type, size_t entry_limit) {
    cmeta_trait_flags key_required = CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COPY |
                                     CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY;
    cmeta_trait_flags value_required = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY;
    if (map == NULL || map->initialized) return STL_INVALID_ARGUMENT;
    if (key_type == NULL || value_type == NULL || key_type->size == 0u || value_type->size == 0u ||
        !sequence_alignment_valid(key_type->align) || !sequence_alignment_valid(value_type->align))
        return STL_INVALID_ARGUMENT;
    if (cmeta_type_require_traits(key_type, key_required) != CMETA_OK ||
        cmeta_type_require_traits(value_type, value_required) != CMETA_OK)
        return STL_TRAIT_MISSING;
    return hash_map_initialize(map, key_type, value_type, key_type->size, key_type->align,
                                     value_type->size, value_type->align, entry_limit, NULL, NULL, NULL);
}

stl_status hash_map_init_bytes(hash_map_t *map, size_t key_size, size_t key_align,
                                           size_t value_size, size_t value_align, size_t entry_limit,
                                           hash_fn hash, hash_equal_fn equal, void *ctx) {
    if (map == NULL || map->initialized || hash == NULL || equal == NULL) return STL_INVALID_ARGUMENT;
    return hash_map_initialize(map, NULL, NULL, key_size, key_align, value_size, value_align,
                                     entry_limit, hash, equal, ctx);
}

stl_status hash_map_raw_from_arrays(hash_map_t *map, const void *keys,
                                            const void *values, size_t count,
                                            const cmeta_type_desc *key_type,
                                            const cmeta_type_desc *value_type, size_t entry_limit) {
    hash_map_t temporary = {0};
    stl_status status;
    size_t index;
    uint64_t generation;
    if (map == NULL) return STL_INVALID_ARGUMENT;
    if (count != 0u && (keys == NULL || values == NULL)) return STL_INVALID_ARGUMENT;
    status = hash_map_raw_init(&temporary, key_type, value_type, entry_limit);
    if (status != STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = hash_map_put(&temporary, (const unsigned char *)keys + index * key_type->size,
                                    (const unsigned char *)values + index * value_type->size);
        if (status != STL_OK) { hash_map_raw_destroy_storage(&temporary); return status; }
    }
    generation = map->generation + UINT64_C(1);
    if (map->initialized) hash_map_raw_destroy_storage(map);
    temporary.generation = generation;
    *map = temporary;
    return STL_OK;
}

stl_status hash_map_from_arrays_bytes(hash_map_t *map, const void *keys,
                                                  const void *values, size_t count, size_t key_size,
                                                  size_t key_align, size_t value_size,
                                                  size_t value_align, size_t entry_limit,
                                                  hash_fn hash, hash_equal_fn equal,
                                                  void *ctx) {
    hash_map_t temporary = {0};
    stl_status status;
    size_t index;
    uint64_t generation;
    if (map == NULL) return STL_INVALID_ARGUMENT;
    if (count != 0u && (keys == NULL || values == NULL)) return STL_INVALID_ARGUMENT;
    status = hash_map_init_bytes(&temporary, key_size, key_align, value_size, value_align,
                                       entry_limit, hash, equal, ctx);
    if (status != STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = hash_map_put(&temporary, (const unsigned char *)keys + index * key_size,
                                    (const unsigned char *)values + index * value_size);
        if (status != STL_OK) { hash_map_raw_destroy_storage(&temporary); return status; }
    }
    generation = map->generation + UINT64_C(1);
    if (map->initialized) hash_map_raw_destroy_storage(map);
    temporary.generation = generation;
    *map = temporary;
    return STL_OK;
}

void hash_map_raw_destroy_storage(hash_map_t *map) {
    size_t slot;
    uint64_t generation;
    if (map == NULL) return;
    generation = map->generation;
    if (map->initialized) {
        for (slot = 0u; slot < map->capacity; ++slot) {
            if (map->states[slot] != TURBO_HASH_OCCUPIED) continue;
            hash_destroy_value(map->key_type, hash_key_slot(map, slot));
            hash_destroy_value(map->value_type, hash_value_slot(map, slot));
        }
        generation += UINT64_C(1);
        sequence_deallocate(map->states);
    }
    memset(map, 0, sizeof(*map));
    map->generation = generation;
}

void hash_map_clear(hash_map_t *map) {
    size_t slot;
    if (!hash_map_valid(map)) return;
    for (slot = 0u; slot < map->capacity; ++slot) {
        if (map->states[slot] != TURBO_HASH_OCCUPIED) continue;
        hash_destroy_value(map->key_type, hash_key_slot(map, slot));
        hash_destroy_value(map->value_type, hash_value_slot(map, slot));
    }
    if (map->size != 0u) {
        memset(map->states, TURBO_HASH_EMPTY, map->capacity * sizeof(*map->states));
        map->size = 0u;
        map->tombstones = 0u;
        ++map->generation;
    }
}

stl_status hash_map_reserve(hash_map_t *map, size_t min_entries) {
    size_t buckets;
    stl_status status;
    if (!hash_map_valid(map)) return STL_INVALID_ARGUMENT;
    if (min_entries > map->entry_limit) return STL_CAPACITY_EXCEEDED;
    status = hash_buckets_for_entries(min_entries, &buckets);
    if (status != STL_OK) return status;
    if (buckets > map->capacity || (map->tombstones > map->size && map->capacity != 0u)) {
        if (buckets < map->capacity) buckets = map->capacity;
        status = hash_map_rehash(map, buckets);
        if (status == STL_OK) ++map->generation;
    }
    return status;
}

stl_status hash_map_put(hash_map_t *map, const void *key, const void *value) {
    size_t hash;
    size_t slot;
    void *prepared_key = NULL;
    void *prepared_value = NULL;
    stl_status status;
    if (!hash_map_valid(map) || key == NULL || value == NULL) return STL_INVALID_ARGUMENT;
    hash = hash_map_hash(map, key);
    if (hash_map_find_slot(map, key, hash, &slot)) {
        status = hash_prepare(map->value_type, map->value_size, map->value_stride,
                                    map->value_align, value, &prepared_value);
        if (status != STL_OK) return status;
        hash_destroy_value(map->value_type, hash_value_slot(map, slot));
        hash_move_destroy(map->value_type, map->value_size, hash_value_slot(map, slot),
                                prepared_value);
        sequence_deallocate(prepared_value);
        ++map->generation;
        return STL_OK;
    }
    if (map->size >= map->entry_limit) return STL_CAPACITY_EXCEEDED;
    status = hash_prepare(map->key_type, map->key_size, map->key_stride, map->key_align,
                                key, &prepared_key);
    if (status != STL_OK) return status;
    status = hash_prepare(map->value_type, map->value_size, map->value_stride,
                                map->value_align, value, &prepared_value);
    if (status != STL_OK) { hash_discard(map->key_type, prepared_key); return status; }
    status = hash_map_ensure_insert(map);
    if (status != STL_OK) {
        hash_discard(map->key_type, prepared_key);
        hash_discard(map->value_type, prepared_value);
        return status;
    }
    slot = hash_map_insert_slot(map, hash);
    hash_move_destroy(map->key_type, map->key_size, hash_key_slot(map, slot), prepared_key);
    hash_move_destroy(map->value_type, map->value_size, hash_value_slot(map, slot), prepared_value);
    sequence_deallocate(prepared_key);
    sequence_deallocate(prepared_value);
    map->hashes[slot] = hash;
    if (map->states[slot] == TURBO_HASH_TOMBSTONE) --map->tombstones;
    map->states[slot] = TURBO_HASH_OCCUPIED;
    ++map->size;
    ++map->generation;
    return STL_OK;
}

void *hash_map_get(hash_map_t *map, const void *key) {
    size_t slot;
    size_t hash;
    if (!hash_map_valid(map) || key == NULL) return NULL;
    hash = hash_map_hash(map, key);
    return hash_map_find_slot(map, key, hash, &slot) ? hash_value_slot(map, slot) : NULL;
}

const void *hash_map_get_const(const hash_map_t *map, const void *key) {
    return hash_map_get((hash_map_t *)map, key);
}

bool hash_map_contains(const hash_map_t *map, const void *key) {
    return hash_map_get_const(map, key) != NULL;
}

stl_status hash_map_remove(hash_map_t *map, const void *key, void *out_value) {
    size_t hash;
    size_t slot;
    if (!hash_map_valid(map) || key == NULL) return STL_INVALID_ARGUMENT;
    hash = hash_map_hash(map, key);
    if (!hash_map_find_slot(map, key, hash, &slot)) return STL_NOT_FOUND;
    if (out_value != NULL)
        hash_move_destroy(map->value_type, map->value_size, out_value,
                                hash_value_slot(map, slot));
    else
        hash_destroy_value(map->value_type, hash_value_slot(map, slot));
    hash_destroy_value(map->key_type, hash_key_slot(map, slot));
    map->states[slot] = TURBO_HASH_TOMBSTONE;
    --map->size;
    ++map->tombstones;
    ++map->generation;
    return STL_OK;
}

size_t hash_map_size(const hash_map_t *map) { return hash_map_valid(map) ? map->size : 0u; }
size_t hash_map_capacity(const hash_map_t *map) { return hash_map_valid(map) ? map->capacity : 0u; }
size_t hash_map_entry_limit(const hash_map_t *map) { return hash_map_valid(map) ? map->entry_limit : 0u; }
uint64_t hash_map_generation(const hash_map_t *map) { return map ? map->generation : UINT64_C(0); }
bool hash_map_empty(const hash_map_t *map) { return hash_map_size(map) == 0u; }

const void *hash_map_key_at(const hash_map_t *map, size_t slot) {
    return hash_map_valid(map) && slot < map->capacity && map->states[slot] == TURBO_HASH_OCCUPIED
        ? hash_key_slot_const(map, slot) : NULL;
}

void *hash_map_value_at(hash_map_t *map, size_t slot) {
    return hash_map_valid(map) && slot < map->capacity && map->states[slot] == TURBO_HASH_OCCUPIED
        ? hash_value_slot(map, slot) : NULL;
}

const void *hash_map_value_at_const(const hash_map_t *map, size_t slot) {
    return hash_map_value_at((hash_map_t *)map, slot);
}
