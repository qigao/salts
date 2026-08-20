#include <container/hash_map.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define CONTAINER_HASH_EMPTY 0U
#define CONTAINER_HASH_OCCUPIED 1U
#define CONTAINER_HASH_TOMBSTONE 2U
#define CONTAINER_HASH_MIN_CAPACITY 16U

typedef union container_hash_max_align {
  long double long_double_value;
  long long long_long_value;
  void *pointer_value;
} container_hash_max_align_t;

size_t container_hash_bytes(const void *key, size_t key_size, void *ctx) {
  const unsigned char *p = (const unsigned char *)key;
  uint64_t h = UINT64_C(1469598103934665603);
  size_t i;
  (void)ctx;
  for (i = 0; i < key_size; ++i) {
    h ^= (uint64_t)p[i];
    h *= UINT64_C(1099511628211);
  }
  if (h == 0) h = 1;
  return (size_t)h;
}

bool container_hash_key_equal(const void *left, const void *right, size_t key_size, void *ctx) {
  (void)ctx;
  return memcmp(left, right, key_size) == 0;
}

static int container_hash_map_valid(const container_hash_map_t *map) {
  return map != NULL && map->key_size > 0 && map->value_size > 0 && map->hash != NULL &&
         map->equal != NULL;
}

static size_t container_hash_round_pow2(size_t value) {
  size_t n = CONTAINER_HASH_MIN_CAPACITY;
  while (n < value) {
    if (n > SIZE_MAX / 2U) return 0;
    n *= 2U;
  }
  return n;
}

static int container_hash_add_size(size_t left, size_t right, size_t *out) {
  if (right > SIZE_MAX - left) return TURBO_ENOMEM;
  *out = left + right;
  return TURBO_OK;
}

static int container_hash_align_size(size_t value, size_t alignment, size_t *out) {
  size_t remainder = value % alignment;
  return remainder == 0 ? (*out = value, TURBO_OK)
                        : container_hash_add_size(value, alignment - remainder, out);
}

static int container_hash_alloc_arrays(container_hash_map_t *map, size_t capacity) {
  const size_t alignment = _Alignof(container_hash_max_align_t);
  size_t hashes_offset;
  size_t keys_offset;
  size_t values_offset;
  size_t total_size;
  size_t region_size;
  unsigned char *storage;

  if (capacity > SIZE_MAX / map->key_size) return TURBO_ENOMEM;
  if (capacity > SIZE_MAX / map->value_size) return TURBO_ENOMEM;
  if (capacity > SIZE_MAX / sizeof(size_t)) return TURBO_ENOMEM;

  total_size = capacity * sizeof(uint8_t);
  if (container_hash_align_size(total_size, alignment, &hashes_offset) != TURBO_OK)
    return TURBO_ENOMEM;
  region_size = capacity * sizeof(size_t);
  if (container_hash_add_size(hashes_offset, region_size, &total_size) != TURBO_OK)
    return TURBO_ENOMEM;
  if (container_hash_align_size(total_size, alignment, &keys_offset) != TURBO_OK)
    return TURBO_ENOMEM;
  region_size = capacity * map->key_size;
  if (container_hash_add_size(keys_offset, region_size, &total_size) != TURBO_OK)
    return TURBO_ENOMEM;
  if (container_hash_align_size(total_size, alignment, &values_offset) != TURBO_OK)
    return TURBO_ENOMEM;
  region_size = capacity * map->value_size;
  if (container_hash_add_size(values_offset, region_size, &total_size) != TURBO_OK)
    return TURBO_ENOMEM;

  storage = (unsigned char *)malloc(total_size);
  if (!storage) return TURBO_ENOMEM;
  memset(storage, CONTAINER_HASH_EMPTY, capacity * sizeof(uint8_t));
  map->states = storage;
  map->hashes = (size_t *)(storage + hashes_offset);
  map->keys = storage + keys_offset;
  map->values = storage + values_offset;
  map->capacity = capacity;
  return TURBO_OK;
}

static void container_hash_insert_occupied(container_hash_map_t *map, size_t hash, const void *key,
                                       const void *value) {
  size_t mask = map->capacity - 1U;
  size_t i;
  for (i = 0; i < map->capacity; ++i) {
    size_t slot = (hash + i) & mask;
    if (map->states[slot] == CONTAINER_HASH_EMPTY) {
      map->states[slot] = CONTAINER_HASH_OCCUPIED;
      map->hashes[slot] = hash;
      memcpy(map->keys + slot * map->key_size, key, map->key_size);
      memcpy(map->values + slot * map->value_size, value, map->value_size);
      map->size += 1U;
      return;
    }
  }
}

static int container_hash_map_rehash(container_hash_map_t *map, size_t new_capacity) {
  container_hash_map_t next;
  size_t i;
  int rc;

  memset(&next, 0, sizeof(next));
  next.key_size = map->key_size;
  next.value_size = map->value_size;
  next.hash = map->hash;
  next.equal = map->equal;
  next.ctx = map->ctx;

  rc = container_hash_alloc_arrays(&next, new_capacity);
  if (rc != TURBO_OK) return rc;

  for (i = 0; i < map->capacity; ++i) {
    if (map->states[i] == CONTAINER_HASH_OCCUPIED) {
      container_hash_insert_occupied(&next, map->hashes[i], map->keys + i * map->key_size,
                                 map->values + i * map->value_size);
    }
  }

  free(map->states);
  map->states = next.states;
  map->hashes = next.hashes;
  map->keys = next.keys;
  map->values = next.values;
  map->size = next.size;
  map->capacity = next.capacity;
  map->tombstones = 0;
  return TURBO_OK;
}

static int container_hash_map_ensure_load(container_hash_map_t *map, size_t extra) {
  size_t wanted;
  size_t min_slots;
  size_t capacity;
  size_t used;

  if (extra > SIZE_MAX - map->size) return TURBO_ENOMEM;
  wanted = map->size + extra;
  if (map->size > SIZE_MAX - map->tombstones) return TURBO_ENOMEM;
  if (extra > SIZE_MAX - map->size - map->tombstones) return TURBO_ENOMEM;
  used = map->size + map->tombstones + extra;
  if (map->capacity != 0 && used <= map->capacity - (map->capacity / 10U) * 3U) {
    return TURBO_OK;
  }

  if (wanted > (SIZE_MAX - 1U) / 10U) return TURBO_ENOMEM;
  min_slots = (wanted * 10U) / 7U + 1U;
  capacity = container_hash_round_pow2(min_slots);
  if (capacity == 0) return TURBO_ENOMEM;
  if (map->capacity <= SIZE_MAX / 2U && capacity < map->capacity * 2U && map->tombstones == 0) {
    capacity = map->capacity ? map->capacity * 2U : capacity;
  }
  if (capacity < CONTAINER_HASH_MIN_CAPACITY) capacity = CONTAINER_HASH_MIN_CAPACITY;
  return container_hash_map_rehash(map, capacity);
}

static int container_hash_map_find_slot(const container_hash_map_t *map, const void *key, size_t hash,
                                    size_t *out_slot) {
  size_t mask;
  size_t i;

  if (map->capacity == 0) return 0;
  mask = map->capacity - 1U;
  for (i = 0; i < map->capacity; ++i) {
    size_t slot = (hash + i) & mask;
    uint8_t state = map->states[slot];
    if (state == CONTAINER_HASH_EMPTY) return 0;
    if (state == CONTAINER_HASH_OCCUPIED && map->hashes[slot] == hash &&
        map->equal(map->keys + slot * map->key_size, key, map->key_size, map->ctx)) {
      *out_slot = slot;
      return 1;
    }
  }
  return 0;
}

int container_hash_map_init(container_hash_map_t *map, size_t key_size, size_t value_size,
                        container_hash_fn hash, container_hash_equal_fn equal, void *ctx) {
  if (!map || key_size == 0 || value_size == 0) return TURBO_EINVAL;
  memset(map, 0, sizeof(*map));
  map->key_size = key_size;
  map->value_size = value_size;
  map->hash = hash ? hash : container_hash_bytes;
  map->equal = equal ? equal : container_hash_key_equal;
  map->ctx = ctx;
  return TURBO_OK;
}

int container_hash_map_from_arrays(container_hash_map_t *map, const void *keys, const void *values,
                               size_t count, size_t key_size, size_t value_size,
                               container_hash_fn hash, container_hash_equal_fn equal, void *ctx) {
  const unsigned char *key_cursor = (const unsigned char *)keys;
  const unsigned char *value_cursor = (const unsigned char *)values;
  size_t i;
  int rc;

  if (!map || key_size == 0 || value_size == 0 ||
      (count > 0 && (!keys || !values))) {
    return TURBO_EINVAL;
  }
  rc = container_hash_map_init(map, key_size, value_size, hash, equal, ctx);
  if (rc != TURBO_OK) return rc;
  rc = container_hash_map_reserve(map, count);
  if (rc != TURBO_OK) {
    container_hash_map_destroy(map);
    return rc;
  }
  for (i = 0; i < count; ++i) {
    rc = container_hash_map_put(map, key_cursor + i * key_size,
                            value_cursor + i * value_size);
    if (rc != TURBO_OK) {
      container_hash_map_destroy(map);
      return rc;
    }
  }
  return TURBO_OK;
}

void container_hash_map_destroy(container_hash_map_t *map) {
  if (!map) return;
  free(map->states);
  memset(map, 0, sizeof(*map));
}

void container_hash_map_clear(container_hash_map_t *map) {
  if (!map || !map->states) return;
  memset(map->states, 0, map->capacity * sizeof(uint8_t));
  map->size = 0;
  map->tombstones = 0;
}

int container_hash_map_reserve(container_hash_map_t *map, size_t min_capacity) {
  size_t min_slots;
  size_t capacity;

  if (!container_hash_map_valid(map)) return TURBO_EINVAL;
  if (min_capacity == 0) return TURBO_OK;
  if (min_capacity > (SIZE_MAX - 1U) / 10U) return TURBO_ENOMEM;
  min_slots = (min_capacity * 10U) / 7U + 1U;
  capacity = container_hash_round_pow2(min_slots);
  if (capacity == 0) return TURBO_ENOMEM;
  if (capacity <= map->capacity) return TURBO_OK;
  return container_hash_map_rehash(map, capacity);
}

int container_hash_map_put(container_hash_map_t *map, const void *key, const void *value) {
  size_t hash;
  size_t mask;
  size_t first_tombstone = SIZE_MAX;
  size_t i;
  int rc;

  if (!container_hash_map_valid(map) || !key || !value) return TURBO_EINVAL;
  rc = container_hash_map_ensure_load(map, 1U);
  if (rc != TURBO_OK) return rc;

  hash = map->hash(key, map->key_size, map->ctx);
  if (hash == 0) hash = 1;
  mask = map->capacity - 1U;

  for (i = 0; i < map->capacity; ++i) {
    size_t slot = (hash + i) & mask;
    uint8_t state = map->states[slot];
    if (state == CONTAINER_HASH_OCCUPIED) {
      if (map->hashes[slot] == hash &&
          map->equal(map->keys + slot * map->key_size, key, map->key_size, map->ctx)) {
        memcpy(map->values + slot * map->value_size, value, map->value_size);
        return TURBO_OK;
      }
    } else if (state == CONTAINER_HASH_TOMBSTONE) {
      if (first_tombstone == SIZE_MAX) first_tombstone = slot;
    } else {
      size_t target = first_tombstone != SIZE_MAX ? first_tombstone : slot;
      if (map->states[target] == CONTAINER_HASH_TOMBSTONE) map->tombstones -= 1U;
      map->states[target] = CONTAINER_HASH_OCCUPIED;
      map->hashes[target] = hash;
      memcpy(map->keys + target * map->key_size, key, map->key_size);
      memcpy(map->values + target * map->value_size, value, map->value_size);
      map->size += 1U;
      return TURBO_OK;
    }
  }

  if (map->capacity > SIZE_MAX / 2U) return TURBO_ENOMEM;
  rc = container_hash_map_rehash(map, map->capacity * 2U);
  if (rc != TURBO_OK) return rc;
  return container_hash_map_put(map, key, value);
}

void *container_hash_map_get(container_hash_map_t *map, const void *key) {
  size_t hash;
  size_t slot = 0;

  if (!container_hash_map_valid(map) || !key) return NULL;
  hash = map->hash(key, map->key_size, map->ctx);
  if (hash == 0) hash = 1;
  if (!container_hash_map_find_slot(map, key, hash, &slot)) return NULL;
  return map->values + slot * map->value_size;
}

const void *container_hash_map_get_const(const container_hash_map_t *map, const void *key) {
  return container_hash_map_get((container_hash_map_t *)map, key);
}

bool container_hash_map_contains(const container_hash_map_t *map, const void *key) {
  return container_hash_map_get_const(map, key) != NULL;
}

int container_hash_map_remove(container_hash_map_t *map, const void *key, void *out_value) {
  size_t hash;
  size_t slot = 0;

  if (!container_hash_map_valid(map) || !key) return TURBO_EINVAL;
  hash = map->hash(key, map->key_size, map->ctx);
  if (hash == 0) hash = 1;
  if (!container_hash_map_find_slot(map, key, hash, &slot)) return TURBO_ENOENT;
  if (out_value) memcpy(out_value, map->values + slot * map->value_size, map->value_size);
  map->states[slot] = CONTAINER_HASH_TOMBSTONE;
  map->size -= 1U;
  map->tombstones += 1U;
  if (map->tombstones > map->size && map->capacity > CONTAINER_HASH_MIN_CAPACITY) {
    (void)container_hash_map_rehash(map, map->capacity);
  }
  return TURBO_OK;
}

size_t container_hash_map_size(const container_hash_map_t *map) {
  if (!map) return 0;
  return map->size;
}

size_t container_hash_map_capacity(const container_hash_map_t *map) {
  if (!map) return 0;
  return map->capacity;
}

bool container_hash_map_empty(const container_hash_map_t *map) {
  return map == NULL || map->size == 0;
}

const void *container_hash_map_key_at(const container_hash_map_t *map, size_t slot) {
  if (!map || slot >= map->capacity || map->states[slot] != CONTAINER_HASH_OCCUPIED) return NULL;
  return map->keys + slot * map->key_size;
}

void *container_hash_map_value_at(container_hash_map_t *map, size_t slot) {
  if (!map || slot >= map->capacity || map->states[slot] != CONTAINER_HASH_OCCUPIED) return NULL;
  return map->values + slot * map->value_size;
}

const void *container_hash_map_value_at_const(const container_hash_map_t *map, size_t slot) {
  return container_hash_map_value_at((container_hash_map_t *)map, slot);
}
