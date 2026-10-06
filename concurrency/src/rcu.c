#include <salts/rcu.h>
#include <salts/thread.h>
#include <stdbool.h>
#include <stdlib.h>

enum { SALTS_RCU_EPOCH_COUNT = 2 };
typedef struct salts_rcu_impl {
  salts_rcu *owner;
  salts_mutex_t lock;
  void *current;
  void *retired;
  size_t readers[SALTS_RCU_EPOCH_COUNT];
  size_t max_readers;
  unsigned epoch;
  unsigned retired_epoch;
  bool closed;
} salts_rcu_impl;

static salts_rcu_impl *rcu_impl(const salts_rcu *domain) {
  salts_rcu_impl *impl = domain != NULL ? (salts_rcu_impl *)domain->impl : NULL;
  return impl != NULL && impl->owner == domain ? impl : NULL;
}

int salts_rcu_init(salts_rcu *domain, void *initial, size_t max_readers) {
  salts_rcu_impl *impl;
  if (domain == NULL || max_readers == 0u) return SALTS_EINVAL;
  if (domain->impl != NULL) return SALTS_EALREADY;
  impl = (salts_rcu_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  salts_mutex_init(&impl->lock);
  if (impl->lock == NULL) { free(impl); return SALTS_ENOMEM; }
  impl->owner = domain;
  impl->current = initial;
  impl->max_readers = max_readers;
  domain->impl = impl;
  return SALTS_OK;
}

int salts_rcu_read_lock(salts_rcu *domain, salts_rcu_guard *guard) {
  salts_rcu_impl *impl = rcu_impl(domain);
  int status = SALTS_OK;
  if (impl == NULL || guard == NULL) return SALTS_EINVAL;
  if (guard->owner != NULL || guard->self != NULL) return SALTS_EALREADY;
  salts_mutex_lock(&impl->lock);
  if (impl->closed) status = SALTS_ESHUTDOWN;
  /* Subtraction avoids overflow even when max_readers == SIZE_MAX. */
  else if (impl->readers[0] >= impl->max_readers - impl->readers[1])
    status = SALTS_ENOBUFS;
  else {
    ++impl->readers[impl->epoch];
    guard->owner = domain;
    guard->self = guard;
    guard->value = impl->current;
    guard->epoch = impl->epoch;
  }
  salts_mutex_unlock(&impl->lock);
  return status;
}

const void *salts_rcu_load(const salts_rcu_guard *guard) {
  return guard != NULL && guard->self == guard && guard->owner != NULL
      ? guard->value : NULL;
}

int salts_rcu_read_unlock(salts_rcu_guard *guard) {
  salts_rcu_impl *impl;
  if (guard == NULL || guard->self != guard || guard->owner == NULL ||
      guard->epoch >= SALTS_RCU_EPOCH_COUNT) return SALTS_EINVAL;
  impl = rcu_impl(guard->owner);
  if (impl == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->lock);
  if (impl->readers[guard->epoch] == 0u) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_EPROTO;
  }
  --impl->readers[guard->epoch];
  *guard = (salts_rcu_guard){0};
  salts_mutex_unlock(&impl->lock);
  return SALTS_OK;
}

int salts_rcu_replace(salts_rcu *domain, void *replacement) {
  salts_rcu_impl *impl = rcu_impl(domain);
  int status = SALTS_OK;
  if (impl == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->lock);
  if (impl->closed) status = SALTS_ESHUTDOWN;
  else if (replacement != NULL &&
           (replacement == impl->current || replacement == impl->retired))
    status = SALTS_EINVAL;
  else if (impl->retired != NULL) status = SALTS_EBUSY;
  else {
    impl->retired = impl->current;
    impl->retired_epoch = impl->epoch;
    impl->epoch = (impl->epoch + 1u) % SALTS_RCU_EPOCH_COUNT;
    impl->current = replacement;
  }
  salts_mutex_unlock(&impl->lock);
  return status;
}

int salts_rcu_try_reclaim(salts_rcu *domain, void **out_retired) {
  salts_rcu_impl *impl = rcu_impl(domain);
  int status = SALTS_OK;
  if (out_retired == NULL) return SALTS_EINVAL;
  *out_retired = NULL;
  if (impl == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->lock);
  if (impl->retired == NULL) status = SALTS_ENOENT;
  else if (impl->readers[impl->retired_epoch] != 0u) status = SALTS_EBUSY;
  else { *out_retired = impl->retired; impl->retired = NULL; }
  salts_mutex_unlock(&impl->lock);
  return status;
}

int salts_rcu_close(salts_rcu *domain) {
  salts_rcu_impl *impl = rcu_impl(domain);
  if (impl == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->lock);
  impl->closed = true;
  salts_mutex_unlock(&impl->lock);
  return SALTS_OK;
}

int salts_rcu_destroy(salts_rcu *domain, void **out_current) {
  salts_rcu_impl *impl;
  if (domain == NULL || out_current == NULL) return SALTS_EINVAL;
  *out_current = NULL;
  if (domain->impl == NULL) return SALTS_OK;
  impl = rcu_impl(domain);
  if (impl == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->lock);
  if (!impl->closed || impl->readers[0] != 0u || impl->readers[1] != 0u ||
      impl->retired != NULL) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_EBUSY;
  }
  *out_current = impl->current;
  salts_mutex_unlock(&impl->lock);
  salts_mutex_destroy(&impl->lock);
  free(impl);
  domain->impl = NULL;
  return SALTS_OK;
}
