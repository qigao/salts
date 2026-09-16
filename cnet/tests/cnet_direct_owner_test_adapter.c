#include <cnet/cnet.h>

#include "cnet_client_internal.h"

#ifdef cnet_client_init
  #undef cnet_client_init
#endif

/* The direct-owner test target rewrites calls made by cnet_api_test.c to this
 * adapter. Restore the real public declaration here so the adapter can create
 * the ordinary client before switching the private diagnostic execution mode. */
extern int cnet_client_init(cnet_client *client, const cnet_client_config *config);

int cnet_direct_owner_test_client_init(cnet_client *client, const cnet_client_config *config) {
  int status = cnet_client_init(client, config);
  if (status != SALTS_OK) return status;

  status = cnet_client_profile_set_owner_io_mode(client, CNET_OWNER_IO_DIRECT);
  if (status != SALTS_OK) {
    if (cnet_client_stop(client, 5000u) == SALTS_OK) (void)cnet_client_destroy(client);
  }
  return status;
}
