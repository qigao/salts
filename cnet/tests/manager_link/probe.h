#ifndef CNET_MANAGER_DSO_PROBE_H
#define CNET_MANAGER_DSO_PROBE_H
#include <cnet/manager.h>
#include <cnet/handoff.h>
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
#ifdef __cplusplus
}
#endif
#endif
