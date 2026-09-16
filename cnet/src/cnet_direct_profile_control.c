#include "cnet_client_internal.h"

#if !defined(CNET_INTERNAL_PROFILING) || !defined(CNET_DIRECT_OWNER_EXPERIMENT)
  #error "direct owner control is diagnostic-only"
#endif

int cnet_client_profile_set_owner_io_mode(cnet_client *client, cnet_owner_io_mode mode) {
  if (client == NULL || client->impl == NULL) return SALTS_EINVAL;
  return mode == CNET_OWNER_IO_DIRECT ? SALTS_OK : SALTS_EINVAL;
}
