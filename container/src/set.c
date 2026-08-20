#include <container/set.h>

#include <stdint.h>

int container_set_init(container_set_t *set, size_t key_size, container_hash_fn hash,
                   container_hash_equal_fn equal, void *ctx) {
  if (!set) return TURBO_EINVAL;
  return container_hash_map_init(&set->map, key_size, sizeof(uint8_t), hash, equal, ctx);
}

int container_set_from_array(container_set_t *set, const void *keys, size_t count,
                         size_t key_size, container_hash_fn hash,
                         container_hash_equal_fn equal, void *ctx) {
  const unsigned char *cursor = (const unsigned char *)keys;
  size_t i;
  int rc;

  if (!set || key_size == 0 || (count > 0 && !keys)) return TURBO_EINVAL;
  rc = container_set_init(set, key_size, hash, equal, ctx);
  if (rc != TURBO_OK) return rc;
  rc = container_set_reserve(set, count);
  if (rc != TURBO_OK) {
    container_set_destroy(set);
    return rc;
  }
  for (i = 0; i < count; ++i) {
    rc = container_set_add(set, cursor + i * key_size);
    if (rc != TURBO_OK) {
      container_set_destroy(set);
      return rc;
    }
  }
  return TURBO_OK;
}

void container_set_destroy(container_set_t *set) {
  if (!set) return;
  container_hash_map_destroy(&set->map);
}

void container_set_clear(container_set_t *set) {
  if (!set) return;
  container_hash_map_clear(&set->map);
}

int container_set_reserve(container_set_t *set, size_t min_capacity) {
  if (!set) return TURBO_EINVAL;
  return container_hash_map_reserve(&set->map, min_capacity);
}

int container_set_add(container_set_t *set, const void *key) {
  uint8_t present = 1U;
  if (!set) return TURBO_EINVAL;
  return container_hash_map_put(&set->map, key, &present);
}

bool container_set_contains(const container_set_t *set, const void *key) {
  if (!set) return false;
  return container_hash_map_contains(&set->map, key);
}

int container_set_remove(container_set_t *set, const void *key) {
  if (!set) return TURBO_EINVAL;
  return container_hash_map_remove(&set->map, key, NULL);
}

size_t container_set_size(const container_set_t *set) {
  if (!set) return 0;
  return container_hash_map_size(&set->map);
}

size_t container_set_capacity(const container_set_t *set) {
  if (!set) return 0;
  return container_hash_map_capacity(&set->map);
}

bool container_set_empty(const container_set_t *set) {
  return set == NULL || container_hash_map_empty(&set->map);
}

const void *container_set_key_at(const container_set_t *set, size_t slot) {
  if (!set) return NULL;
  return container_hash_map_key_at(&set->map, slot);
}
