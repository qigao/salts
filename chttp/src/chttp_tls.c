#include "chttp_tls.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct chttp_tls_profile_impl {
  cnet_tls_client client;
  atomic_size_t references;
};

int chttp_tls_http1_alpn_validate(const char *const *protocols, size_t count) {
  static const char protocol[] = "http/1.1";
  size_t size;
  if (count == 0u) return protocols == NULL ? TURBO_OK : TURBO_EINVAL;
  if (protocols == NULL || count != 1u || protocols[0] == NULL) return TURBO_ENOTSUP;
  for (size = 0u; size <= CNET_TLS_ALPN_NAME_MAX_BYTES; ++size)
    if (protocols[0][size] == '\0')
      return size == sizeof(protocol) - 1u && memcmp(protocols[0], protocol, size) == 0
                 ? TURBO_OK
                 : TURBO_ENOTSUP;
  return TURBO_EINVAL;
}

int chttp_tls_profile_init(chttp_tls_profile *profile, const cnet_tls_client_config *config) {
  chttp_tls_profile_impl *impl;
  int status;
  if (profile == NULL || config == NULL) return TURBO_EINVAL;
  if (profile->impl != NULL) return TURBO_EALREADY;
  if (config->size != sizeof(*config)) return TURBO_EINVAL;
  status = chttp_tls_http1_alpn_validate(config->alpn_protocols, config->alpn_protocol_count);
  if (status != TURBO_OK) return status;
  impl = (chttp_tls_profile_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  status = cnet_tls_client_init(&impl->client, config);
  if (status != TURBO_OK) {
    free(impl);
    return status;
  }
  atomic_init(&impl->references, 1u);
  profile->impl = impl;
  return TURBO_OK;
}

void chttp_tls_profile_release(chttp_tls_profile_impl *impl) {
  if (impl == NULL) return;
  if (atomic_fetch_sub_explicit(&impl->references, 1u, memory_order_acq_rel) == 1u) {
    (void)cnet_tls_client_destroy(&impl->client);
    free(impl);
  }
}

int chttp_tls_profile_destroy(chttp_tls_profile *profile) {
  chttp_tls_profile_impl *impl;
  if (profile == NULL) return TURBO_EINVAL;
  impl = (chttp_tls_profile_impl *)profile->impl;
  if (impl == NULL) return TURBO_OK;
  profile->impl = NULL;
  chttp_tls_profile_release(impl);
  return TURBO_OK;
}

int chttp_tls_profile_acquire(const chttp_tls_profile *profile, chttp_tls_profile_impl **out_impl) {
  chttp_tls_profile_impl *impl;
  if (out_impl == NULL) return TURBO_EINVAL;
  *out_impl = NULL;
  if (profile == NULL) return TURBO_OK;
  impl = (chttp_tls_profile_impl *)profile->impl;
  if (impl == NULL) return TURBO_EINVAL;
  (void)atomic_fetch_add_explicit(&impl->references, 1u, memory_order_relaxed);
  *out_impl = impl;
  return TURBO_OK;
}

const cnet_tls_client *chttp_tls_profile_client(const chttp_tls_profile_impl *impl) {
  return impl != NULL ? &impl->client : NULL;
}
