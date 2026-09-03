#ifndef CNET_SECURE_KCP_INTERNAL_H
#define CNET_SECURE_KCP_INTERNAL_H

#include <cnet/cnet.h>

bool cnet_secure_kcp_transport_config_valid(const cnet_kcp_config *kcp,
                                            const cnet_kcp_security_config *security);

#endif /* CNET_SECURE_KCP_INTERNAL_H */
