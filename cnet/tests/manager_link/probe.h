#ifndef CNET_MANAGER_DSO_PROBE_H
#define CNET_MANAGER_DSO_PROBE_H
#include <cnet/manager.h>
#include <cnet/handoff.h>
#include <cnet/client_pool.h>
#include <cnet/recovery_policy.h>
#include <cnet/managed_dial.h>
#include <cnet/sg_host.h>
#if defined(_WIN32)
# if defined(CNET_DSO_PROBE_BUILD)
#  define CNET_DSO_API __declspec(dllexport)
# else
#  define CNET_DSO_API __declspec(dllimport)
# endif
#else
# define CNET_DSO_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
CNET_DSO_API int cnet_dso_manager_init(cnet_manager *, const cnet_manager_config *);
CNET_DSO_API int cnet_dso_manager_release(cnet_manager *, cnet_managed_connection);
CNET_DSO_API int cnet_dso_handoff_init(cnet_handoff *, const cnet_handoff_config *);
CNET_DSO_API int cnet_dso_handoff_release(cnet_handoff *, cnet_handoff_ticket);
/* These wrappers are executed inside the companion DSO; direct caller API
 * invocations use the same Salts::CNet DSO across both C11 and C++17. */
CNET_DSO_API int cnet_dso_pool_init(cnet_client_pool *, const cnet_pool_config *);
CNET_DSO_API int cnet_dso_pool_terminal(cnet_client_pool *, cnet_pool_connection);
CNET_DSO_API int cnet_dso_reconnect_init(cnet_reconnect_state *,
                                         const cnet_reconnect_config *);
CNET_DSO_API int cnet_dso_reconnect_connected(cnet_reconnect_state *,
                                              cnet_reconnect_ticket, uint64_t);
CNET_DSO_API int cnet_dso_managed_dial_init(cnet_managed_dial *,
                                            const cnet_managed_dial_config *);
CNET_DSO_API int cnet_dso_managed_dial_destroy(cnet_managed_dial *);
CNET_DSO_API int cnet_dso_sg_route_empty(const cnet_sg_host_routes *,
                                         size_t *, size_t *);
#ifdef __cplusplus
}
#endif
#endif
