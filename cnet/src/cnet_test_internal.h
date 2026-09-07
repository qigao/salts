#ifndef CNET_TEST_INTERNAL_H
#define CNET_TEST_INTERNAL_H

#if !defined(CNET_INTERNAL_TESTING)
  #error "cnet_test_internal.h is available only to the private CNet test target"
#endif

#include <cnet/cnet.h>

int cnet_test_datagram_fail_next_drive(cnet_datagram *datagram, int status);
int cnet_test_datagram_set_persistent_drive_failure(cnet_datagram *datagram, int status);
int cnet_test_datagram_process_mixed_batch(size_t *out_callbacks);
int cnet_test_packet_endpoint_fail_next_datagram_drive(cnet_packet_endpoint *endpoint,
                                                       int status);

#endif /* CNET_TEST_INTERNAL_H */
